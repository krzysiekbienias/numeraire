"""USD Treasury par-yield → bootstrapped zero / discount-factor plots from SQLite."""

from __future__ import annotations

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib.figure import Figure

from numeraire_viz.db import read_sql, resolve_db_path

_DEFAULT_CURVE_ID = "USD_TREASURY_PAR_FRED"

_CURVE_SQL = """
SELECT
    d.curve_id,
    d.as_of,
    d.source_par_curve_id,
    d.source_par_as_of,
    d.currency,
    d.day_count,
    d.interpolation_method,
    d.bootstrap_engine,
    p.curve_kind AS par_curve_kind,
    p.source AS par_source,
    pt.tenor,
    pt.time_years,
    pq.quoted_rate,
    pq.instrument_type,
    pq.fred_series_id,
    pt.zero_rate,
    pt.discount_factor
FROM discount_curve_eod d
INNER JOIN discount_curve_point_eod pt
    ON pt.curve_id = d.curve_id AND pt.as_of = d.as_of
LEFT JOIN par_curve_eod p
    ON p.curve_id = d.source_par_curve_id AND p.as_of = d.source_par_as_of
LEFT JOIN par_curve_point_eod pq
    ON pq.curve_id = d.source_par_curve_id
   AND pq.as_of = d.source_par_as_of
   AND pq.tenor = pt.tenor
WHERE d.curve_id = ?
  AND d.as_of = ?
ORDER BY pt.time_years, pt.tenor
"""


def list_discount_curve_dates(
    curve_id: str = _DEFAULT_CURVE_ID,
    *,
    db_path: str | None = None,
) -> list[str]:
    """Available ``as_of`` dates with bootstrapped discount curves."""
    df = read_sql(
        """
        SELECT as_of FROM discount_curve_eod
        WHERE curve_id = ?
        ORDER BY as_of
        """,
        (curve_id,),
        db_path=db_path,
    )
    return df["as_of"].astype(str).tolist()


def list_par_curve_dates(
    curve_id: str = _DEFAULT_CURVE_ID,
    *,
    db_path: str | None = None,
) -> list[str]:
    """Available ``as_of`` dates with FRED par-yield snapshots."""
    df = read_sql(
        """
        SELECT as_of FROM par_curve_eod
        WHERE curve_id = ?
        ORDER BY as_of
        """,
        (curve_id,),
        db_path=db_path,
    )
    return df["as_of"].astype(str).tolist()


def load_discount_curve(
    as_of: str,
    curve_id: str = _DEFAULT_CURVE_ID,
    *,
    db_path: str | None = None,
) -> tuple[pd.DataFrame, pd.Series, pd.Series]:
    """
    Load joined par quotes + bootstrapped zero rates and discount factors.

    Returns ``(points_df, discount_header, par_header)``.
    ``points_df`` columns: tenor, time_years, quoted_rate, zero_rate, discount_factor, …
    """
    raw = read_sql(_CURVE_SQL, (curve_id, as_of), db_path=db_path)
    if raw.empty:
        path = resolve_db_path(db_path)
        raise ValueError(
            f"No discount curve for curve_id={curve_id!r} as_of={as_of!r} in {path}. "
            "Run fetch_fred_treasury_par_yields.py and dev_main --build-discount-curve-eod."
        )

    header_cols = [
        "curve_id",
        "as_of",
        "source_par_curve_id",
        "source_par_as_of",
        "currency",
        "day_count",
        "interpolation_method",
        "bootstrap_engine",
        "par_curve_kind",
        "par_source",
    ]
    discount_header = raw.iloc[0][header_cols].copy()
    discount_header.name = "discount_curve_eod"

    par_header = pd.Series(
        {
            "curve_id": discount_header["source_par_curve_id"],
            "as_of": discount_header["source_par_as_of"],
            "curve_kind": discount_header["par_curve_kind"],
            "source": discount_header["par_source"],
            "currency": discount_header["currency"],
            "day_count": discount_header["day_count"],
        },
        name="par_curve_eod",
    )

    point_cols = [
        "tenor",
        "time_years",
        "quoted_rate",
        "instrument_type",
        "fred_series_id",
        "zero_rate",
        "discount_factor",
    ]
    points = raw[point_cols].copy()
    points["quoted_rate_pct"] = points["quoted_rate"] * 100.0
    points["zero_rate_pct"] = points["zero_rate"] * 100.0
    return points.reset_index(drop=True), discount_header, par_header


def _tenor_tick_labels(points: pd.DataFrame) -> tuple[np.ndarray, list[str]]:
    t = points["time_years"].to_numpy(dtype=float)
    labels = points["tenor"].astype(str).tolist()
    return t, labels


def plot_yield_curve(
    points: pd.DataFrame,
    *,
    discount_header: pd.Series | None = None,
    par_header: pd.Series | None = None,
    show_interpolated_zero: bool = True,
    title: str | None = None,
    figsize: tuple[float, float] = (11, 5),
) -> Figure:
    """Par yield vs bootstrapped zero coupon rate (% annualized) vs maturity."""
    if points.empty:
        raise ValueError("No points to plot")

    df = points.sort_values("time_years").reset_index(drop=True)
    t, labels = _tenor_tick_labels(df)

    fig, ax = plt.subplots(figsize=figsize)
    ax.plot(
        t,
        df["quoted_rate_pct"],
        marker="o",
        linestyle="--",
        linewidth=1.5,
        label="Par yield (FRED quote)",
        color="#2563eb",
    )
    ax.plot(
        t,
        df["zero_rate_pct"],
        marker="s",
        linestyle="-",
        linewidth=2.0,
        label="Zero rate (bootstrapped)",
        color="#dc2626",
    )

    if show_interpolated_zero and len(t) >= 2:
        t_dense = np.linspace(float(t.min()), float(t.max()), 200)
        z_dense = np.interp(t_dense, t, df["zero_rate_pct"].to_numpy(dtype=float))
        ax.plot(t_dense, z_dense, linestyle=":", color="#dc2626", alpha=0.45, label="Zero (linear interp)")

    ax.set_xticks(t)
    ax.set_xticklabels(labels)
    ax.set_xlabel("Tenor (pillar label at pillar time)")
    ax.set_ylabel("Rate (%)")
    ax.grid(True, alpha=0.3)
    ax.legend(loc="best")

    if title:
        ax.set_title(title)
    elif discount_header is not None:
        ax.set_title(
            f"{discount_header['curve_id']} @ {discount_header['as_of']} "
            f"(par input {discount_header['source_par_as_of']})"
        )
    else:
        ax.set_title("Par yield vs zero coupon rate")

    fig.tight_layout()
    return fig


def plot_discount_factors(
    points: pd.DataFrame,
    *,
    discount_header: pd.Series | None = None,
    title: str | None = None,
    figsize: tuple[float, float] = (11, 5),
) -> Figure:
    """Bootstrapped discount factors vs pillar maturity (years)."""
    if points.empty:
        raise ValueError("No points to plot")

    df = points.sort_values("time_years").reset_index(drop=True)
    t, labels = _tenor_tick_labels(df)

    fig, ax = plt.subplots(figsize=figsize)
    ax.plot(t, df["discount_factor"], marker="D", linestyle="-", linewidth=2.0, color="#059669", label="DF(T)")
    ax.set_xticks(t)
    ax.set_xticklabels(labels)
    ax.set_xlabel("Tenor (pillar label at pillar time)")
    ax.set_ylabel("Discount factor DF(T)")
    ax.set_ylim(0.0, 1.02)
    ax.grid(True, alpha=0.3)
    ax.legend(loc="best")

    if title:
        ax.set_title(title)
    elif discount_header is not None:
        ax.set_title(
            f"Discount factors — {discount_header['curve_id']} @ {discount_header['as_of']} "
            f"({discount_header['bootstrap_engine']})"
        )
    else:
        ax.set_title("Discount factors")

    fig.tight_layout()
    return fig


def plot_curve_overview(
    points: pd.DataFrame,
    *,
    discount_header: pd.Series | None = None,
    par_header: pd.Series | None = None,
    show_interpolated_zero: bool = True,
    title: str | None = None,
    figsize: tuple[float, float] = (11, 9),
) -> Figure:
    """Two-panel overview: rates (par + zero) and discount factors."""
    if points.empty:
        raise ValueError("No points to plot")

    fig = plt.figure(figsize=figsize)
    gs = fig.add_gridspec(2, 1, height_ratios=[1.1, 1.0], hspace=0.28)

    ax_rates = fig.add_subplot(gs[0, 0])
    ax_df = fig.add_subplot(gs[1, 0], sharex=ax_rates)

    df = points.sort_values("time_years").reset_index(drop=True)
    t, labels = _tenor_tick_labels(df)

    ax_rates.plot(t, df["quoted_rate_pct"], "o--", color="#2563eb", label="Par yield")
    ax_rates.plot(t, df["zero_rate_pct"], "s-", color="#dc2626", linewidth=2, label="Zero rate")
    if show_interpolated_zero and len(t) >= 2:
        t_dense = np.linspace(float(t.min()), float(t.max()), 200)
        z_dense = np.interp(t_dense, t, df["zero_rate_pct"].to_numpy(dtype=float))
        ax_rates.plot(t_dense, z_dense, ":", color="#dc2626", alpha=0.45, label="Zero (linear interp)")

    ax_rates.set_ylabel("Rate (%)")
    ax_rates.grid(True, alpha=0.3)
    ax_rates.legend(loc="best")

    ax_df.plot(t, df["discount_factor"], "D-", color="#059669", linewidth=2, label="DF(T)")
    ax_df.set_xticks(t)
    ax_df.set_xticklabels(labels)
    ax_df.set_xlabel("Tenor (pillar label at pillar time)")
    ax_df.set_ylabel("Discount factor")
    ax_df.set_ylim(0.0, 1.02)
    ax_df.grid(True, alpha=0.3)
    ax_df.legend(loc="best")

    if title:
        fig.suptitle(title, fontsize=14, y=1.01)
    elif discount_header is not None:
        fig.suptitle(
            f"{discount_header['curve_id']} @ {discount_header['as_of']} — "
            f"par {discount_header['source_par_as_of']} → "
            f"{discount_header['bootstrap_engine']}",
            fontsize=14,
            y=1.01,
        )
    else:
        fig.suptitle("Discount curve overview", fontsize=14, y=1.01)

    fig.subplots_adjust(hspace=0.28, top=0.92)
    return fig
