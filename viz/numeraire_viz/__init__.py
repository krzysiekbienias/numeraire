"""Numeraire++ visualization: SQLite market data → matplotlib."""

from numeraire_viz.db import default_db_path, resolve_db_path
from numeraire_viz.vol_surface import (
    list_vol_surface_dates,
    load_vol_surface,
    plot_term_structure_atm,
    plot_vol_smiles,
    plot_vol_surface_3d,
    plot_vol_surface_3d_mesh,
)

__all__ = [
    "default_db_path",
    "resolve_db_path",
    "list_vol_surface_dates",
    "load_vol_surface",
    "plot_vol_smiles",
    "plot_vol_surface_3d",
    "plot_vol_surface_3d_mesh",
    "plot_term_structure_atm",
]
