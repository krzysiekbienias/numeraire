"""Numeraire++ visualization: SQLite market data → matplotlib."""

from numeraire_viz.db import default_db_path, resolve_db_path
from numeraire_viz.discount_curve import (
    list_discount_curve_dates,
    list_par_curve_dates,
    load_discount_curve,
    plot_curve_overview,
    plot_discount_factors,
    plot_yield_curve,
)
from numeraire_viz.vol_surface import (
    list_vol_surface_dates,
    load_vol_surface,
    plot_term_structure_atm,
    plot_vol_smiles,
    plot_vol_surface_3d,
    plot_vol_surface_3d_mesh,
)
from numeraire_viz.historical_calibration import (
    correlation_matrix_from_sparse,
    list_calibration_dates,
    load_historical_calibration,
    plot_calibration_overview,
    plot_correlation_heatmap,
    plot_factor_volatility_bars,
)
from numeraire_viz.scenario_paths import (
    default_exports_dir,
    list_scenario_exports,
    load_scenario_paths,
    plot_scenario_paths,
    plot_scenario_paths_grid,
    plot_terminal_distribution,
    resolve_scenario_export,
)

__all__ = [
    "default_db_path",
    "resolve_db_path",
    "list_par_curve_dates",
    "list_discount_curve_dates",
    "load_discount_curve",
    "plot_yield_curve",
    "plot_discount_factors",
    "plot_curve_overview",
    "list_vol_surface_dates",
    "load_vol_surface",
    "plot_vol_smiles",
    "plot_vol_surface_3d",
    "plot_vol_surface_3d_mesh",
    "plot_term_structure_atm",
    "list_calibration_dates",
    "load_historical_calibration",
    "correlation_matrix_from_sparse",
    "plot_correlation_heatmap",
    "plot_factor_volatility_bars",
    "plot_calibration_overview",
    "default_exports_dir",
    "list_scenario_exports",
    "resolve_scenario_export",
    "load_scenario_paths",
    "plot_scenario_paths",
    "plot_scenario_paths_grid",
    "plot_terminal_distribution",
]
