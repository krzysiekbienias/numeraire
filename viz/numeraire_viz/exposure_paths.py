"""Leg exposure path plots and EE/PFE profiles from CSV exports or SQLite."""

from __future__ import annotations

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib.figure import Figure

from numeraire_viz.db import repo_root, resolve_db_path

_EXPORTS_DIR_NAME = "exports"
_EXPOSURE_GLOB = "*_leg_exposure.csv"


def default_exports_dir() -> Path:
    return repo_root() / _EXPORTS_DIR_NAME


def list_leg_exposure_exports(exports_dir: str | Path | None = None) -> list[Path]:
    root = Path(exports_dir) if exports_dir is not None else default_exports_dir()
    if not root.is_dir():
        return []
    return sorted(root.glob(_EXPOSURE_GLOB))


def resolve_leg_exposure_export(
    scope_key: str,
    valuation_as_of: str,
    *,
    exports_dir: str | Path | None = None,
) -> Path:
    """
    Default export path from ``dev_main --simulate --price-paths``:
    ``exports/{scope_key}_{valuation_as_of}_leg_exposure.csv``.
    """
    root = Path(exports_dir) if exports_dir is not None else default_exports_dir()
    path = root / f"{scope_key}_{valuation_as_of}_leg_exposure.csv"
    if not path.is_file():
        available = [p.name for p in list_leg_exposure_exports(root)]
        raise FileNotFoundError(
            f"Leg exposure export not found: {path}. "
            f"Available in {root}: {available or '(none)'}"
        )
    return path


def load_leg_exposure_paths(
    csv_path: str | Path | None = None,
    *,
    scope_key: str | None = None,
    valuation_as_of: str | None = None,
    exports_dir: str | Path | None = None,
) -> pd.DataFrame:
    """
    Load long-form leg exposure CSV from ``dev_main --simulate --price-paths``.

    Columns: path, leg_id, trade_id, pillar_id, step, year_fraction, pv_total, exposure.
    """
    if csv_path is None:
        if not scope_key or not valuation_as_of:
            raise ValueError("Provide csv_path or (scope_key, valuation_as_of).")
        csv_path = resolve_leg_exposure_export(scope_key, valuation_as_of, exports_dir=exports_dir)

    path = Path(csv_path)
    if not path.is_file():
        raise FileNotFoundError(f"Leg exposure CSV not found: {path}")

    df = pd.read_csv(path)
    expected = {
        "path",
        "leg_id",
        "trade_id",
        "pillar_id",
        "step",
        "year_fraction",
        "pv_total",
        "exposure",
    }
    missing = expected - set(df.columns)
    if missing:
        raise ValueError(f"Leg exposure CSV missing columns: {sorted(missing)}")

    return df.sort_values(["leg_id", "path", "step"]).reset_index(drop=True)


def load_trade_leg_exposure_eod(
    *,
    as_of: str,
    scope_key: str | None = None,
    leg_id: str | None = None,
    trade_id: str | None = None,
    db_path: str | None = None,
) -> pd.DataFrame:
    """Load persisted EE / PFE profiles from ``trade_leg_exposure_eod``."""
    import sqlite3

    db = resolve_db_path(db_path)
    clauses = ["as_of = ?"]
    params: list[str] = [as_of]
    if scope_key:
        clauses.append("scope_key = ?")
        params.append(scope_key)
    if leg_id:
        clauses.append("leg_id = ?")
        params.append(leg_id)
    if trade_id:
        clauses.append("trade_id = ?")
        params.append(trade_id)

    sql = f"""
        SELECT leg_id, trade_id, pillar_id, grid_step, year_fraction, exposure_date,
               ee, pfe_95, pfe_97, scope_key, batch_run_id, pricing_engine, remarks
        FROM trade_leg_exposure_eod
        WHERE {' AND '.join(clauses)}
        ORDER BY leg_id, grid_step
    """
    with sqlite3.connect(db) as conn:
        return pd.read_sql_query(sql, conn, params=params)


def plot_leg_exposure_paths(
    df: pd.DataFrame,
    *,
    leg_id: str | None = None,
    trade_id: str | None = None,
    max_paths: int | None = 50,
    use_year_fraction: bool = True,
    title: str | None = None,
    figsize: tuple[float, float] = (10, 5.5),
) -> Figure:
    """Fan chart of simulated leg exposure (max(0, pv_total)) along the exposure grid."""
    if df.empty:
        raise ValueError("df must not be empty")

    x_col = "year_fraction" if use_year_fraction else "step"
    x_label = "Year fraction from valuation" if use_year_fraction else "Grid step"

    if leg_id:
        slab = df[df["leg_id"] == leg_id].copy()
    elif trade_id:
        slab = df[df["trade_id"] == trade_id].copy()
    else:
        first_leg = df["leg_id"].iloc[0]
        slab = df[df["leg_id"] == first_leg].copy()

    if slab.empty:
        raise ValueError("No rows after leg/trade filter")

    uid = slab["leg_id"].iloc[0]
    paths = sorted(slab["path"].unique())
    if max_paths is not None:
        paths = paths[: max(1, max_paths)]
    slab = slab[slab["path"].isin(paths)]

    fig, ax = plt.subplots(figsize=figsize)
    cmap = plt.get_cmap("tab10")
    for i, path_id in enumerate(paths):
        grp = slab[slab["path"] == path_id].sort_values(x_col)
        ax.plot(
            grp[x_col],
            grp["exposure"],
            color=cmap(i % 10),
            alpha=0.85,
            linewidth=1.2,
        )

    ax.set_xlabel(x_label)
    ax.set_ylabel("Leg exposure")
    ax.set_title(title or f"Leg exposure paths — {uid}")
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    return fig


def plot_exposure_profile(
    df: pd.DataFrame,
    *,
    leg_id: str | None = None,
    figsize: tuple[float, float] = (10, 5.5),
    title: str | None = None,
) -> Figure:
    """Plot EE and PFE quantiles vs year fraction from ``trade_leg_exposure_eod``."""
    if df.empty:
        raise ValueError("df must not be empty")

    slab = df.copy()
    if leg_id:
        slab = slab[slab["leg_id"] == leg_id]
    if slab.empty:
        raise ValueError(f"leg_id={leg_id!r} not in data")

    slab = slab.sort_values("year_fraction")
    uid = slab["leg_id"].iloc[0]

    fig, ax = plt.subplots(figsize=figsize)
    ax.plot(slab["year_fraction"], slab["ee"], label="EE (mean)", linewidth=2.0)
    ax.plot(slab["year_fraction"], slab["pfe_95"], label="PFE 95%", linewidth=1.5)
    ax.plot(slab["year_fraction"], slab["pfe_97"], label="PFE 97%", linewidth=1.5)
    ax.set_xlabel("Year fraction from valuation")
    ax.set_ylabel("Exposure")
    ax.set_title(title or f"Exposure profile — {uid}")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    return fig


def aggregate_trade_exposure_paths(df: pd.DataFrame) -> pd.DataFrame:
    """
    Sum leg exposures per (trade_id, path, step): Σ max(0, PV_leg).

    For net trade exposure ``max(0, Σ PV_leg)`` see
    ``trade_exposure_review.aggregate_trade_net_exposure_paths``.
    """
    grouped = (
        df.groupby(["trade_id", "path", "step", "pillar_id", "year_fraction"], as_index=False)["exposure"]
        .sum()
        .rename(columns={"exposure": "trade_exposure"})
    )
    return grouped
