"""Trade-level scenario + exposure review: aligned path fans and DB EE/PFE."""

from __future__ import annotations

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib.figure import Figure
from matplotlib.gridspec import GridSpec

from numeraire_viz.db import read_sql
from numeraire_viz.exposure_paths import (
    load_leg_exposure_paths,
    load_trade_leg_exposure_eod,
    resolve_leg_exposure_export,
)
from numeraire_viz.scenario_paths import (
    _spot0_by_underlying,
    load_scenario_paths,
    resolve_scenario_export,
)


def load_trade_underlyings(
    trade_id: str,
    *,
    db_path: str | Path | None = None,
) -> list[str]:
    """Distinct ``underlying_id`` values for legs booked on ``trade_id``."""
    df = read_sql(
        """
        SELECT DISTINCT p.underlying_id
        FROM trade_legs tl
        INNER JOIN products p ON p.product_id = tl.product_id
        WHERE tl.trade_id = ?
        ORDER BY p.underlying_id
        """,
        (trade_id,),
        db_path=db_path,
    )
    if df.empty:
        raise ValueError(f"trade_id={trade_id!r} not found in trade_legs / products")
    return df["underlying_id"].tolist()


def select_plot_paths(df: pd.DataFrame, *, max_paths: int = 50) -> list[int]:
    """First ``max_paths`` path ids (sorted) — same set for scenario and exposure panels."""
    paths = sorted(df["path"].unique())
    if max_paths is not None:
        paths = paths[: max(1, max_paths)]
    return [int(p) for p in paths]


def aggregate_trade_sum_leg_exposure_paths(
    df: pd.DataFrame,
    trade_id: str,
) -> pd.DataFrame:
    """
    Sum per-leg exposure along each path: Σ_leg max(0, PV_leg).

    Matches the ``exposure`` column in ``_leg_exposure.csv``.
    """
    slab = df[df["trade_id"] == trade_id]
    if slab.empty:
        raise ValueError(f"trade_id={trade_id!r} not in leg exposure CSV")
    return (
        slab.groupby(
            ["trade_id", "path", "step", "pillar_id", "year_fraction"],
            as_index=False,
        )["exposure"]
        .sum()
        .rename(columns={"exposure": "exposure_value"})
        .sort_values(["path", "step"])
        .reset_index(drop=True)
    )


def aggregate_trade_net_exposure_paths(
    df: pd.DataFrame,
    trade_id: str,
) -> pd.DataFrame:
    """
    Net trade exposure per path: max(0, Σ_leg PV_leg).

    Differs from ``aggregate_trade_sum_leg_exposure_paths`` when the trade has
    multiple legs whose combined PV can be negative while individual legs are positive.
    """
    slab = df[df["trade_id"] == trade_id]
    if slab.empty:
        raise ValueError(f"trade_id={trade_id!r} not in leg exposure CSV")
    grouped = (
        slab.groupby(
            ["trade_id", "path", "step", "pillar_id", "year_fraction"],
            as_index=False,
        )["pv_total"]
        .sum()
        .rename(columns={"pv_total": "pv_net"})
    )
    grouped["exposure_value"] = grouped["pv_net"].clip(lower=0.0)
    return grouped.sort_values(["path", "step"]).reset_index(drop=True)


def aggregate_trade_exposure_eod_profile(
    db_df: pd.DataFrame,
    trade_id: str,
) -> pd.DataFrame:
    """
    Trade-level EE / PFE profile by summing persisted leg rows from ``trade_leg_exposure_eod``.

    For multi-leg trades this is a presentation rollup (PFE is not additive in general).
    """
    slab = db_df[db_df["trade_id"] == trade_id]
    if slab.empty:
        raise ValueError(f"trade_id={trade_id!r} not in trade_leg_exposure_eod")
    return (
        slab.groupby(["pillar_id", "grid_step", "year_fraction"], as_index=False)
        .agg(
            ee=("ee", "sum"),
            pfe_95=("pfe_95", "sum"),
            pfe_97=("pfe_97", "sum"),
        )
        .sort_values("year_fraction")
        .reset_index(drop=True)
    )


def _path_color_map(paths: list[int]) -> dict[int, tuple[float, ...]]:
    cmap = plt.get_cmap("tab10")
    return {path_id: cmap(i % 10) for i, path_id in enumerate(paths)}


def _draw_path_fan(
    ax: plt.Axes,
    df: pd.DataFrame,
    *,
    paths: list[int],
    x_col: str,
    y_col: str,
    path_colors: dict[int, tuple[float, ...]],
) -> None:
    for path_id in paths:
        grp = df[df["path"] == path_id].sort_values(x_col)
        ax.plot(
            grp[x_col],
            grp[y_col],
            color=path_colors[path_id],
            alpha=0.85,
            linewidth=1.1,
        )


def _overlay_db_exposure_profile(
    ax: plt.Axes,
    profile: pd.DataFrame,
    *,
    legend_prefix: str = "DB Σ legs",
) -> None:
    ax.plot(
        profile["year_fraction"],
        profile["ee"],
        color="black",
        linewidth=2.2,
        linestyle="-",
        label=f"{legend_prefix} — EE (mean)",
        zorder=5,
    )
    ax.plot(
        profile["year_fraction"],
        profile["pfe_95"],
        color="crimson",
        linewidth=1.8,
        linestyle="--",
        label=f"{legend_prefix} — PFE 95%",
        zorder=5,
    )
    ax.plot(
        profile["year_fraction"],
        profile["pfe_97"],
        color="darkorange",
        linewidth=1.8,
        linestyle="--",
        label=f"{legend_prefix} — PFE 97%",
        zorder=5,
    )


def plot_trade_scenario_and_exposure(
    trade_id: str,
    *,
    scope_key: str,
    valuation_as_of: str,
    max_paths: int = 50,
    use_year_fraction: bool = True,
    exports_dir: str | Path | None = None,
    db_path: str | Path | None = None,
    figsize: tuple[float, float] | None = None,
    title: str | None = None,
) -> Figure:
    """
    Combined trade review figure:

    - Top: GBM spot fans for each underlying in the trade (same ``path`` ids).
    - Middle: two exposure fans on the same paths — Σ leg exposure vs net trade exposure.
    - Bottom: EE / PFE 95% / PFE 97% from ``trade_leg_exposure_eod`` (summed over legs).
    """
    scenarios = load_scenario_paths(
        scope_key=scope_key,
        valuation_as_of=valuation_as_of,
        exports_dir=exports_dir,
    )
    leg_exposure = load_leg_exposure_paths(
        scope_key=scope_key,
        valuation_as_of=valuation_as_of,
        exports_dir=exports_dir,
    )
    db_rows = load_trade_leg_exposure_eod(
        as_of=valuation_as_of,
        scope_key=scope_key,
        trade_id=trade_id,
        db_path=str(db_path) if db_path is not None else None,
    )
    underlyings = load_trade_underlyings(trade_id, db_path=db_path)

    paths = select_plot_paths(leg_exposure[leg_exposure["trade_id"] == trade_id], max_paths=max_paths)
    path_colors = _path_color_map(paths)

    sum_leg = aggregate_trade_sum_leg_exposure_paths(leg_exposure, trade_id)
    sum_leg = sum_leg[sum_leg["path"].isin(paths)]
    net_trade = aggregate_trade_net_exposure_paths(leg_exposure, trade_id)
    net_trade = net_trade[net_trade["path"].isin(paths)]
    db_profile = aggregate_trade_exposure_eod_profile(db_rows, trade_id)

    x_col = "year_fraction" if use_year_fraction else "step"
    x_label = "Year fraction from valuation" if use_year_fraction else "Grid step"

    n_under = len(underlyings)
    under_ncol = min(2, n_under)
    under_nrow = int(np.ceil(n_under / under_ncol))

    stack_exposure = under_ncol < 2
    n_rows = under_nrow + (3 if stack_exposure else 2)
    if figsize is None:
        width = 6.5 * max(under_ncol, 1)
        height = 3.6 * under_nrow + (11.5 if stack_exposure else 9.5)
        figsize = (width, height)

    height_ratios = [1.0] * under_nrow + ([1.15, 1.15, 0.95] if stack_exposure else [1.15, 0.95])
    fig = plt.figure(figsize=figsize, layout="constrained")
    gs = GridSpec(
        n_rows,
        max(under_ncol, 1),
        figure=fig,
        height_ratios=height_ratios,
        hspace=0.45,
        wspace=0.28,
    )

    scenario_df = scenarios[scenarios["underlying_id"].isin(underlyings)]
    spot0_map = _spot0_by_underlying(scenario_df)

    for idx, uid in enumerate(underlyings):
        row = idx // under_ncol
        col = idx % under_ncol
        ax = fig.add_subplot(gs[row, col])
        slab = scenario_df[scenario_df["underlying_id"] == uid]
        slab = slab[slab["path"].isin(paths)]
        _draw_path_fan(
            ax,
            slab,
            paths=paths,
            x_col=x_col,
            y_col="value",
            path_colors=path_colors,
        )
        spot0 = float(spot0_map[uid])
        ax.axhline(spot0, color="black", linestyle=":", linewidth=0.9, alpha=0.65)
        ax.set_title(f"Scenarios — {uid}  (t0={spot0:.4g})")
        ax.set_xlabel(x_label)
        ax.set_ylabel("Simulated spot")
        ax.grid(True, alpha=0.25)

    exposure_row = under_nrow
    ax_sum = fig.add_subplot(gs[exposure_row, 0])
    _draw_path_fan(
        ax_sum,
        sum_leg,
        paths=paths,
        x_col=x_col,
        y_col="exposure_value",
        path_colors=path_colors,
    )
    _overlay_db_exposure_profile(ax_sum, db_profile)
    ax_sum.set_title("Realization — Σ leg exposure  [max(0, PV) per leg, summed]")
    ax_sum.set_xlabel(x_label)
    ax_sum.set_ylabel("Exposure")
    ax_sum.grid(True, alpha=0.25)
    ax_sum.legend(loc="best", fontsize=8)

    if stack_exposure:
        ax_net = fig.add_subplot(gs[exposure_row + 1, 0])
        profile_row = exposure_row + 2
    else:
        ax_net = fig.add_subplot(gs[exposure_row, 1])
        profile_row = exposure_row + 1
    _draw_path_fan(
        ax_net,
        net_trade,
        paths=paths,
        x_col=x_col,
        y_col="exposure_value",
        path_colors=path_colors,
    )
    _overlay_db_exposure_profile(ax_net, db_profile)
    ax_net.set_title("Realization — net trade exposure  [max(0, Σ leg PV)]")
    ax_net.set_xlabel(x_label)
    ax_net.set_ylabel("Exposure")
    ax_net.grid(True, alpha=0.25)
    ax_net.legend(loc="best", fontsize=8)

    ax_prof = fig.add_subplot(gs[profile_row, :])
    ax_prof.plot(db_profile["year_fraction"], db_profile["ee"], label="EE (mean)", linewidth=2.2)
    ax_prof.plot(db_profile["year_fraction"], db_profile["pfe_95"], label="PFE 95%", linewidth=1.8)
    ax_prof.plot(db_profile["year_fraction"], db_profile["pfe_97"], label="PFE 97%", linewidth=1.8)
    ax_prof.set_xlabel("Year fraction from valuation")
    ax_prof.set_ylabel("Exposure")
    ax_prof.set_title(
        f"Trade exposure profile (DB, Σ legs) — {trade_id}  "
        f"[{scope_key}, as_of={valuation_as_of}, paths 0–{paths[-1]}]"
    )
    ax_prof.legend()
    ax_prof.grid(True, alpha=0.3)

    fig.suptitle(
        title
        or (
            f"Trade scenario & exposure review — {trade_id}  "
            f"({len(paths)} paths, same path ids in all panels)"
        ),
        fontsize=13,
        y=1.01,
    )
    return fig
