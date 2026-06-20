"""Monte Carlo scenario path plots from multifactor GBM CSV exports."""

from __future__ import annotations

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib.figure import Figure

from numeraire_viz.db import repo_root

_EXPORTS_DIR_NAME = "exports"
_SCENARIO_GLOB = "*_all_factors.csv"


def default_exports_dir() -> Path:
    return repo_root() / _EXPORTS_DIR_NAME


def list_scenario_exports(exports_dir: str | Path | None = None) -> list[Path]:
    root = Path(exports_dir) if exports_dir is not None else default_exports_dir()
    if not root.is_dir():
        return []
    return sorted(root.glob(_SCENARIO_GLOB))


def resolve_scenario_export(
    scope_key: str,
    valuation_as_of: str,
    *,
    exports_dir: str | Path | None = None,
) -> Path:
    """
    Default export path from ``dev_main --simulate``:
    ``exports/{scope_key}_{valuation_as_of}_all_factors.csv``.
    """
    root = Path(exports_dir) if exports_dir is not None else default_exports_dir()
    path = root / f"{scope_key}_{valuation_as_of}_all_factors.csv"
    if not path.is_file():
        available = [p.name for p in list_scenario_exports(root)]
        raise FileNotFoundError(
            f"Scenario export not found: {path}. "
            f"Available in {root}: {available or '(none)'}"
        )
    return path


def load_scenario_paths(
    csv_path: str | Path | None = None,
    *,
    scope_key: str | None = None,
    valuation_as_of: str | None = None,
    exports_dir: str | Path | None = None,
) -> pd.DataFrame:
    """
    Load long-form multifactor scenario CSV.

    Columns: path, factor, underlying_id, step, year_fraction, value.
    """
    if csv_path is None:
        if not scope_key or not valuation_as_of:
            raise ValueError("Provide csv_path or (scope_key, valuation_as_of).")
        csv_path = resolve_scenario_export(scope_key, valuation_as_of, exports_dir=exports_dir)

    path = Path(csv_path)
    if not path.is_file():
        raise FileNotFoundError(f"Scenario CSV not found: {path}")

    df = pd.read_csv(path)
    expected = {"path", "factor", "underlying_id", "step", "year_fraction", "value"}
    missing = expected - set(df.columns)
    if missing:
        raise ValueError(f"Scenario CSV missing columns: {sorted(missing)}")

    df = df.sort_values(["underlying_id", "path", "step"]).reset_index(drop=True)
    return df


def _spot0_by_underlying(df: pd.DataFrame) -> pd.Series:
    t0 = df[df["step"] == 0].drop_duplicates(subset=["underlying_id", "path"])
    return t0.groupby("underlying_id")["value"].first()


def plot_scenario_paths(
    df: pd.DataFrame,
    *,
    underlying_id: str | None = None,
    max_paths: int | None = None,
    use_year_fraction: bool = True,
    title: str | None = None,
    figsize: tuple[float, float] = (10, 5.5),
) -> Figure:
    """Fan chart of simulated spots for one underlying (or the first in the frame)."""
    if df.empty:
        raise ValueError("df must not be empty")

    x_col = "year_fraction" if use_year_fraction else "step"
    x_label = "Year fraction from valuation" if use_year_fraction else "Grid step"

    underlyings = df["underlying_id"].drop_duplicates().tolist()
    uid = underlying_id or underlyings[0]
    if uid not in underlyings:
        raise ValueError(f"underlying_id={uid!r} not in data: {underlyings}")

    slab = df[df["underlying_id"] == uid].copy()
    paths = sorted(slab["path"].unique())
    if max_paths is not None:
        paths = paths[: max(1, max_paths)]
    slab = slab[slab["path"].isin(paths)]

    spot0 = float(_spot0_by_underlying(slab)[uid])

    fig, ax = plt.subplots(figsize=figsize)
    cmap = plt.get_cmap("tab10")
    for i, path_id in enumerate(paths):
        grp = slab[slab["path"] == path_id].sort_values(x_col)
        ax.plot(
            grp[x_col],
            grp["value"],
            color=cmap(i % 10),
            alpha=0.85,
            linewidth=1.2,
            label=f"path {path_id}" if len(paths) <= 8 else None,
        )

    ax.axhline(spot0, color="black", linestyle="--", linewidth=1.0, alpha=0.7, label=f"t0 spot={spot0:.2g}")
    ax.set_xlabel(x_label)
    ax.set_ylabel("Simulated spot")
    ax.set_title(title or f"GBM scenario paths — {uid}")
    ax.grid(True, alpha=0.3)
    if len(paths) <= 8:
        ax.legend(loc="best", fontsize=8)
    fig.tight_layout()
    return fig


def plot_scenario_paths_grid(
    df: pd.DataFrame,
    *,
    max_paths: int | None = None,
    use_year_fraction: bool = True,
    ncol: int = 2,
    figsize_per_axis: tuple[float, float] = (5.5, 4.0),
    title: str | None = None,
) -> Figure:
    """One fan chart per underlying in the export."""
    if df.empty:
        raise ValueError("df must not be empty")

    x_col = "year_fraction" if use_year_fraction else "step"
    x_label = "Year fraction" if use_year_fraction else "Step"
    underlyings = df["underlying_id"].drop_duplicates().tolist()
    n = len(underlyings)
    ncols = min(ncol, n)
    nrows = int(np.ceil(n / ncols))

    fig, axes = plt.subplots(
        nrows,
        ncols,
        figsize=(figsize_per_axis[0] * ncols, figsize_per_axis[1] * nrows),
        squeeze=False,
    )
    axes_flat = axes.ravel()
    cmap = plt.get_cmap("tab10")
    spot0_map = _spot0_by_underlying(df)

    for ax, uid in zip(axes_flat, underlyings, strict=False):
        slab = df[df["underlying_id"] == uid]
        paths = sorted(slab["path"].unique())
        if max_paths is not None:
            paths = paths[: max(1, max_paths)]
        for i, path_id in enumerate(paths):
            grp = slab[slab["path"] == path_id].sort_values(x_col)
            ax.plot(grp[x_col], grp["value"], color=cmap(i % 10), alpha=0.8, linewidth=1.0)

        spot0 = float(spot0_map[uid])
        ax.axhline(spot0, color="black", linestyle="--", linewidth=0.9, alpha=0.65)
        ax.set_title(f"{uid}  (t0={spot0:.4g})")
        ax.set_xlabel(x_label)
        ax.set_ylabel("Spot")
        ax.grid(True, alpha=0.25)

    for ax in axes_flat[len(underlyings) :]:
        ax.set_visible(False)

    fig.suptitle(title or "Multifactor GBM scenario paths", fontsize=13)
    fig.tight_layout()
    return fig


def plot_terminal_distribution(
    df: pd.DataFrame,
    *,
    underlying_id: str | None = None,
    figsize: tuple[float, float] = (9, 4.5),
    bins: int = 20,
    title: str | None = None,
) -> Figure:
    """Histogram of terminal (last step) simulated spots for one underlying."""
    if df.empty:
        raise ValueError("df must not be empty")

    underlyings = df["underlying_id"].drop_duplicates().tolist()
    uid = underlying_id or underlyings[0]
    slab = df[df["underlying_id"] == uid]
    max_step = slab["step"].max()
    terminal = slab[slab["step"] == max_step]["value"].to_numpy()
    spot0 = float(_spot0_by_underlying(slab)[uid])

    fig, ax = plt.subplots(figsize=figsize)
    ax.hist(terminal, bins=bins, color="steelblue", alpha=0.85, edgecolor="white")
    ax.axvline(spot0, color="black", linestyle="--", linewidth=1.0, label=f"t0 spot={spot0:.4g}")
    ax.axvline(float(np.mean(terminal)), color="crimson", linestyle="-", linewidth=1.0, label="mean terminal")
    ax.set_xlabel("Terminal spot")
    ax.set_ylabel("Path count")
    ax.set_title(title or f"Terminal distribution — {uid} (step={max_step})")
    ax.legend()
    ax.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()
    return fig
