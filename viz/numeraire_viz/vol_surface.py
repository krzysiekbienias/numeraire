"""EOD implied-volatility surface plots from vol_surface_eod tables."""

from __future__ import annotations

from typing import Literal

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib.figure import Figure
from scipy.interpolate import griddata

from numeraire_viz.db import read_sql, resolve_db_path

QualityFilter = Literal["ok", "all"]
ContractView = Literal["call", "put", "both", "mid"]


_SURFACE_SQL = """
SELECT
    h.surface_id,
    h.underlying_id,
    h.as_of,
    h.surface_kind,
    h.spot_used,
    h.risk_free_rate,
    h.dividend_yield,
    h.model,
    h.price_source,
    h.point_count,
    p.expiration_date,
    p.years_to_maturity,
    p.strike,
    p.contract_type,
    p.implied_vol,
    p.input_price,
    p.quality,
    p.source_option_ticker
FROM vol_surface_eod h
INNER JOIN vol_surface_point_eod p ON p.surface_id = h.surface_id
WHERE h.underlying_id = ?
  AND h.as_of = ?
  AND h.surface_kind = ?
"""


def list_vol_surface_dates(
    underlying_id: str = "NDX",
    surface_kind: str = "implied_bs_eod",
    *,
    db_path: str | None = None,
) -> list[str]:
    df = read_sql(
        """
        SELECT as_of FROM vol_surface_eod
        WHERE underlying_id = ? AND surface_kind = ?
        ORDER BY as_of
        """,
        (underlying_id, surface_kind),
        db_path=db_path,
    )
    return df["as_of"].astype(str).tolist()


def load_vol_surface(
    as_of: str,
    underlying_id: str = "NDX",
    surface_kind: str = "implied_bs_eod",
    *,
    quality: QualityFilter = "ok",
    contract_view: ContractView = "both",
    db_path: str | None = None,
) -> tuple[pd.DataFrame, pd.Series]:
    """
  Load surface points joined with header metadata.

  Returns (points_df, header_series) where header_series is one row from vol_surface_eod.
    """
    raw = read_sql(
        _SURFACE_SQL,
        (underlying_id, as_of, surface_kind),
        db_path=db_path,
    )
    if raw.empty:
        path = resolve_db_path(db_path)
        raise ValueError(
            f"No vol surface for underlying={underlying_id!r} as_of={as_of!r} "
            f"kind={surface_kind!r} in {path}"
        )

    header_cols = [
        "surface_id",
        "underlying_id",
        "as_of",
        "surface_kind",
        "spot_used",
        "risk_free_rate",
        "dividend_yield",
        "model",
        "price_source",
        "point_count",
    ]
    header = raw.iloc[0][header_cols]

    points = raw.drop(columns=header_cols).copy()
    if quality == "ok":
        points = points[points["quality"] == "ok"].copy()

    points = _apply_contract_view(points, contract_view)
    points["moneyness"] = points["strike"] / float(header["spot_used"])
    points["log_moneyness"] = np.log(points["moneyness"].clip(lower=1e-12))
    return points.reset_index(drop=True), header


def _apply_contract_view(df: pd.DataFrame, contract_view: ContractView) -> pd.DataFrame:
    if contract_view == "both":
        return df
    if contract_view in ("call", "put"):
        return df[df["contract_type"] == contract_view].copy()

    # mid: average call/put IV at (expiration_date, strike)
    grouped = (
        df.groupby(["expiration_date", "years_to_maturity", "strike"], as_index=False)
        .agg(
            implied_vol=("implied_vol", "mean"),
            input_price=("input_price", "mean"),
            quality=("quality", "first"),
            source_option_ticker=("source_option_ticker", "first"),
        )
    )
    grouped["contract_type"] = "mid"
    return grouped


def plot_vol_smiles(
    points: pd.DataFrame,
    *,
    spot: float | None = None,
    title: str | None = None,
    max_expiries: int = 12,
    figsize: tuple[float, float] = (12, 8),
) -> Figure:
    """One subplot per expiration: strike (or moneyness) vs implied vol."""
    if points.empty:
        raise ValueError("No points to plot")

    df = points.sort_values(["expiration_date", "strike", "contract_type"])
    expiries = df["expiration_date"].drop_duplicates().tolist()
    if len(expiries) > max_expiries:
        expiries = expiries[:max_expiries]

    n = len(expiries)
    ncols = min(3, n)
    nrows = int(np.ceil(n / ncols))
    fig, axes = plt.subplots(nrows, ncols, figsize=figsize, squeeze=False)
    axes_flat = axes.ravel()

    styles = {"call": {"marker": "o", "linestyle": "-"}, "put": {"marker": "s", "linestyle": "--"}, "mid": {"marker": "^", "linestyle": "-"}}

    for ax, exp in zip(axes_flat, expiries, strict=False):
        slab = df[df["expiration_date"] == exp]
        for ctype, grp in slab.groupby("contract_type"):
            sty = styles.get(ctype, {"marker": ".", "linestyle": "-"})
            ax.plot(grp["strike"], grp["implied_vol"], label=ctype, **sty)
        if spot is not None:
            ax.axvline(spot, color="gray", linewidth=0.8, linestyle=":", alpha=0.7)
        tau = slab["years_to_maturity"].iloc[0]
        ax.set_title(f"{exp}  (T≈{tau:.3f}y)")
        ax.set_xlabel("Strike")
        ax.set_ylabel("Implied vol")
        ax.grid(True, alpha=0.3)
        ax.legend(loc="best", fontsize=8)

    for ax in axes_flat[len(expiries) :]:
        ax.set_visible(False)

    fig.suptitle(title or "Volatility smiles (EOD)", fontsize=14)
    fig.tight_layout()
    return fig


def plot_term_structure_atm(
    points: pd.DataFrame,
    *,
    spot: float,
    atm_band: float = 0.02,
    title: str | None = None,
    figsize: tuple[float, float] = (10, 5),
) -> Figure:
    """ATM-ish term structure: years_to_maturity vs implied vol."""
    if points.empty:
        raise ValueError("No points to plot")

    df = points.copy()
    df["dist"] = (df["strike"] / spot - 1.0).abs()
    near = df[df["dist"] <= atm_band]
    if near.empty:
        near = df.loc[df.groupby("expiration_date")["dist"].idxmin()]

    fig, ax = plt.subplots(figsize=figsize)
    for ctype, grp in near.groupby("contract_type"):
        g = grp.sort_values("years_to_maturity")
        ax.plot(g["years_to_maturity"], g["implied_vol"], marker="o", label=ctype)

    ax.set_xlabel("Years to maturity")
    ax.set_ylabel("Implied vol")
    ax.set_title(title or f"Term structure (|K/S-1| ≤ {atm_band:.1%})")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    return fig


def _filter_surface_points(
    points: pd.DataFrame,
    contract_type: str | None,
) -> pd.DataFrame:
    if points.empty:
        raise ValueError("No points to plot")
    df = points.copy()
    if contract_type is not None:
        df = df[df["contract_type"] == contract_type]
    if df.empty:
        raise ValueError(f"No rows for contract_type={contract_type!r}")
    return df


def _dedupe_xy_mean_z(
    x: np.ndarray,
    y: np.ndarray,
    z: np.ndarray,
    *,
    decimals: int = 8,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Collapse duplicate (x, y) slices (e.g. after contract_view='both') for meshing."""
    xr = np.round(x.astype(float), decimals)
    yr = np.round(y.astype(float), decimals)
    keys: dict[tuple[float, float], list[float]] = {}
    for xi, yi, zi in zip(xr, yr, z, strict=True):
        keys.setdefault((float(xi), float(yi)), []).append(float(zi))
    xs = np.array([k[0] for k in keys])
    ys = np.array([k[1] for k in keys])
    zs = np.array([float(np.mean(v)) for v in keys.values()])
    return xs, ys, zs


def plot_vol_surface_3d(
    points: pd.DataFrame,
    *,
    spot: float | None = None,
    use_log_moneyness: bool = True,
    contract_type: str | None = "call",
    title: str | None = None,
    figsize: tuple[float, float] = (11, 7),
) -> Figure:
    """3D scatter: tenor × strike (or log-moneyness) × implied vol."""
    df = _filter_surface_points(points, contract_type)
    x_col = "log_moneyness" if use_log_moneyness else "strike"
    x_label = "ln(K/S)" if use_log_moneyness else "Strike"

    fig = plt.figure(figsize=figsize)
    ax = fig.add_subplot(111, projection="3d")
    sc = ax.scatter(
        df[x_col],
        df["years_to_maturity"],
        df["implied_vol"],
        c=df["implied_vol"],
        cmap="viridis",
        s=12,
        alpha=0.85,
    )
    fig.colorbar(sc, ax=ax, shrink=0.6, label="implied vol")
    ax.set_xlabel(x_label)
    ax.set_ylabel("Years to maturity")
    ax.set_zlabel("Implied vol")
    if spot is not None and not use_log_moneyness:
        ax.axvline(spot, color="gray", linestyle=":", alpha=0.5)
    ax.set_title(title or f"Vol surface 3D scatter ({contract_type or 'all'})")
    fig.tight_layout()
    return fig


def plot_vol_surface_3d_mesh(
    points: pd.DataFrame,
    *,
    spot: float | None = None,
    use_log_moneyness: bool = True,
    contract_type: str | None = "call",
    title: str | None = None,
    figsize: tuple[float, float] = (11, 7),
    grid_resolution: int = 50,
    interp_method: Literal["linear", "nearest", "cubic"] = "linear",
    show_scatter: bool = True,
) -> Figure:
    """
    Interpolated 3D surface (visualization only).

    Uses ``scipy.interpolate.griddata`` on a regular grid in (ln(K/S), T).
    Values outside the convex hull of market points are masked (linear/cubic).
    """
    if grid_resolution < 4:
        raise ValueError("grid_resolution must be >= 4")

    df = _filter_surface_points(points, contract_type)
    x_col = "log_moneyness" if use_log_moneyness else "strike"
    x_label = "ln(K/S)" if use_log_moneyness else "Strike"

    x, y, z = _dedupe_xy_mean_z(
        df[x_col].to_numpy(),
        df["years_to_maturity"].to_numpy(),
        df["implied_vol"].to_numpy(),
    )
    if len(x) < 4:
        raise ValueError("Need at least 4 distinct (moneyness, tenor) points for a mesh")

    pad_x = 0.05 * max(float(np.ptp(x)), 1e-6)
    pad_y = 0.05 * max(float(np.ptp(y)), 1e-6)
    xi = np.linspace(float(x.min()) - pad_x, float(x.max()) + pad_x, grid_resolution)
    yi = np.linspace(float(y.min()) - pad_y, float(y.max()) + pad_y, grid_resolution)
    xi_grid, yi_grid = np.meshgrid(xi, yi)

    zi_grid = griddata((x, y), z, (xi_grid, yi_grid), method=interp_method)
    if interp_method == "cubic" and np.isnan(zi_grid).all():
        zi_grid = griddata((x, y), z, (xi_grid, yi_grid), method="linear")

    zi_masked = np.ma.masked_invalid(zi_grid)

    fig = plt.figure(figsize=figsize)
    ax = fig.add_subplot(111, projection="3d")
    surf = ax.plot_surface(
        xi_grid,
        yi_grid,
        zi_masked,
        cmap="viridis",
        linewidth=0,
        antialiased=True,
        alpha=0.92,
    )
    fig.colorbar(surf, ax=ax, shrink=0.6, label="implied vol (interpolated)")

    if show_scatter:
        ax.scatter(x, y, z, c="black", s=8, alpha=0.35, depthshade=True)

    ax.set_xlabel(x_label)
    ax.set_ylabel("Years to maturity")
    ax.set_zlabel("Implied vol")
    if spot is not None and not use_log_moneyness:
        ax.axvline(spot, color="gray", linestyle=":", alpha=0.5)
    ax.set_title(title or f"Vol surface 3D mesh ({contract_type or 'all'})")
    fig.tight_layout()
    return fig
