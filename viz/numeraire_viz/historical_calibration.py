"""Historical GBM calibration plots (vol, correlation) from SQLite."""

from __future__ import annotations

from typing import Literal

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib.figure import Figure

from numeraire_viz.db import read_sql, resolve_db_path

LatestMode = Literal["latest_on_or_before", "exact"]


_CALIBRATION_LIST_SQL = """
SELECT calibration_id, scope_key, as_of, num_factors, calculated_at
FROM historical_calibration
WHERE calibration_scope = 'book'
  AND scope_key = ?
ORDER BY as_of DESC
"""

_CALIBRATION_HEADER_SQL = """
SELECT
    calibration_id,
    calibration_method,
    calibration_scope,
    scope_key,
    as_of,
    history_start,
    history_end,
    lookback_calendar_days,
    num_factors,
    num_return_observations,
    calculated_at
FROM historical_calibration
WHERE calibration_scope = 'book'
  AND scope_key = ?
  AND as_of <= ?
ORDER BY as_of DESC
LIMIT 1
"""

_CALIBRATION_HEADER_EXACT_SQL = """
SELECT
    calibration_id,
    calibration_method,
    calibration_scope,
    scope_key,
    as_of,
    history_start,
    history_end,
    lookback_calendar_days,
    num_factors,
    num_return_observations,
    calculated_at
FROM historical_calibration
WHERE calibration_scope = 'book'
  AND scope_key = ?
  AND as_of = ?
LIMIT 1
"""

_FACTORS_SQL = """
SELECT factor_index, underlying_id, spot_as_of, volatility
FROM historical_calibration_factor
WHERE calibration_id = ?
ORDER BY factor_index
"""

_CORRELATION_SQL = """
SELECT factor_i, factor_j, rho
FROM historical_calibration_correlation
WHERE calibration_id = ?
ORDER BY factor_i, factor_j
"""


def list_calibration_dates(
    scope_key: str = "BOOK_1",
    *,
    db_path: str | None = None,
) -> list[str]:
    df = read_sql(_CALIBRATION_LIST_SQL, (scope_key,), db_path=db_path)
    return df["as_of"].astype(str).tolist()


def load_historical_calibration(
    scope_key: str = "BOOK_1",
    as_of: str | None = None,
    *,
    valuation_as_of: str | None = None,
    mode: LatestMode = "latest_on_or_before",
    db_path: str | None = None,
) -> tuple[pd.Series, pd.DataFrame, pd.DataFrame]:
    """
    Load one book calibration snapshot.

    Parameters
    ----------
    as_of
        Exact calibration date (used when ``mode='exact'`` or when set without valuation_as_of).
    valuation_as_of
        When set with ``mode='latest_on_or_before'``, picks the newest calibration with
        ``calibration.as_of <= valuation_as_of`` (same rule as C++ simulate).
    """
    if mode == "exact":
        if not as_of:
            raise ValueError("as_of is required when mode='exact'.")
        header_df = read_sql(_CALIBRATION_HEADER_EXACT_SQL, (scope_key, as_of), db_path=db_path)
    else:
        ref = valuation_as_of or as_of
        if not ref:
            raise ValueError("Provide valuation_as_of or as_of for latest_on_or_before lookup.")
        header_df = read_sql(_CALIBRATION_HEADER_SQL, (scope_key, ref), db_path=db_path)

    if header_df.empty:
        path = resolve_db_path(db_path)
        raise ValueError(
            f"No historical_calibration for scope_key={scope_key!r} "
            f"(as_of={as_of!r}, valuation_as_of={valuation_as_of!r}, mode={mode!r}) in {path}"
        )

    header = header_df.iloc[0]
    calibration_id = int(header["calibration_id"])

    factors = read_sql(_FACTORS_SQL, (calibration_id,), db_path=db_path)
    correlations = read_sql(_CORRELATION_SQL, (calibration_id,), db_path=db_path)
    return header, factors.reset_index(drop=True), correlations.reset_index(drop=True)


def correlation_matrix_from_sparse(
    factors: pd.DataFrame,
    correlations: pd.DataFrame,
) -> tuple[np.ndarray, list[str]]:
    """Reconstruct symmetric correlation matrix with underlying labels."""
    labels = factors.sort_values("factor_index")["underlying_id"].astype(str).tolist()
    n = len(labels)
    if n == 0:
        raise ValueError("factors must not be empty.")

    matrix = np.eye(n, dtype=float)
    index_by_label = {label: idx for idx, label in enumerate(labels)}
    factor_index_to_label = {
        int(row.factor_index): str(row.underlying_id) for row in factors.itertuples(index=False)
    }

    for row in correlations.itertuples(index=False):
        i = int(row.factor_i)
        j = int(row.factor_j)
        rho = float(row.rho)
        if i == j:
            matrix[i, j] = 1.0
            continue
        li = factor_index_to_label.get(i)
        lj = factor_index_to_label.get(j)
        if li is None or lj is None:
            raise ValueError(f"correlation references unknown factor index ({i}, {j}).")
        ii = index_by_label[li]
        jj = index_by_label[lj]
        matrix[ii, jj] = rho
        matrix[jj, ii] = rho

    return matrix, labels


def plot_correlation_heatmap(
    matrix: np.ndarray,
    labels: list[str],
    *,
    title: str | None = None,
    vmin: float = -1.0,
    vmax: float = 1.0,
    annotate: bool = True,
    figsize: tuple[float, float] = (8, 6.5),
) -> Figure:
    """Symmetric correlation heatmap with underlying ids on both axes."""
    n = len(labels)
    if matrix.shape != (n, n):
        raise ValueError("matrix shape must match labels length.")

    fig, ax = plt.subplots(figsize=figsize)
    im = ax.imshow(matrix, cmap="RdBu_r", vmin=vmin, vmax=vmax, aspect="equal")

    ax.set_xticks(range(n))
    ax.set_yticks(range(n))
    ax.set_xticklabels(labels, rotation=45, ha="right")
    ax.set_yticklabels(labels)
    ax.set_xlabel("Underlying")
    ax.set_ylabel("Underlying")

    if annotate:
        for i in range(n):
            for j in range(n):
                ax.text(
                    j,
                    i,
                    f"{matrix[i, j]:.2f}",
                    ha="center",
                    va="center",
                    color="white" if abs(matrix[i, j]) > 0.55 else "black",
                    fontsize=9,
                )

    fig.colorbar(im, ax=ax, shrink=0.85, label="correlation ρ")
    ax.set_title(title or "Historical GBM correlation matrix")
    fig.tight_layout()
    return fig


def plot_factor_volatility_bars(
    factors: pd.DataFrame,
    *,
    title: str | None = None,
    figsize: tuple[float, float] = (9, 4.5),
) -> Figure:
    """Annualized historical vol per book factor."""
    if factors.empty:
        raise ValueError("factors must not be empty.")

    df = factors.sort_values("factor_index").copy()
    fig, ax = plt.subplots(figsize=figsize)
    bars = ax.bar(df["underlying_id"], df["volatility"], color="steelblue", alpha=0.85)
    ax.bar_label(bars, labels=[f"{v:.1%}" for v in df["volatility"]], fontsize=8)

    ax.set_ylabel("Annualized volatility")
    ax.set_xlabel("Underlying")
    ax.set_title(title or "Historical GBM factor volatilities")
    ax.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()
    return fig


def plot_calibration_overview(
    header: pd.Series,
    factors: pd.DataFrame,
    matrix: np.ndarray,
    labels: list[str],
    *,
    figsize: tuple[float, float] = (14, 5.5),
) -> Figure:
    """Side-by-side vol bars + correlation heatmap for one snapshot."""
    fig, axes = plt.subplots(1, 2, figsize=figsize)

    df = factors.sort_values("factor_index")
    bars = axes[0].bar(df["underlying_id"], df["volatility"], color="steelblue", alpha=0.85)
    axes[0].bar_label(bars, labels=[f"{v:.1%}" for v in df["volatility"]], fontsize=8)
    axes[0].set_ylabel("Annualized volatility")
    axes[0].set_xlabel("Underlying")
    axes[0].set_title("Factor volatilities")
    axes[0].grid(True, axis="y", alpha=0.3)

    n = len(labels)
    im = axes[1].imshow(matrix, cmap="RdBu_r", vmin=-1.0, vmax=1.0, aspect="equal")
    axes[1].set_xticks(range(n))
    axes[1].set_yticks(range(n))
    axes[1].set_xticklabels(labels, rotation=45, ha="right")
    axes[1].set_yticklabels(labels)
    for i in range(n):
        for j in range(n):
            axes[1].text(
                j,
                i,
                f"{matrix[i, j]:.2f}",
                ha="center",
                va="center",
                color="white" if abs(matrix[i, j]) > 0.55 else "black",
                fontsize=8,
            )
    fig.colorbar(im, ax=axes[1], shrink=0.9, label="correlation ρ")
    axes[1].set_title("Correlation matrix")

    fig.suptitle(
        f"{header['scope_key']} calibration as_of={header['as_of']} "
        f"(history {header['history_start']} → {header['history_end']})",
        fontsize=13,
    )
    fig.tight_layout()
    return fig
