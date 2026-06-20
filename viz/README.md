# Numeraire++ visualization (`viz/`)

Python package and Jupyter notebooks: **read from SQLite** (`NUMERAIRE_DB_PATH` or repo `db.sqlite3`) and plot market analytics.

Operational scripts stay in [`../scripts/`](../scripts/). This folder is for **exploration and charts** only.

## Setup

From the repository root:

```bash
python3 -m venv .venv-viz
source .venv-viz/bin/activate
pip install -e "viz/[notebook]"
python -m ipykernel install --user --name numeraire-viz --display-name "Numeraire++ viz"
```

Database path (same as C++ / `.env`):

- Default: `<repo>/db.sqlite3` (resolved from package location, **not** Jupyter `cwd`).
- `NUMERAIRE_DB_PATH=db.sqlite3` in the environment or repo `.env` is interpreted **relative to repo root**.
- Absolute paths are used as-is.

```bash
# optional override
export NUMERAIRE_DB_PATH=/opt/numeraire/dev/db.sqlite3
```

## Notebooks

| Notebook | Description |
| -------- | ----------- |
| [`notebooks/vol_surface_eod.ipynb`](notebooks/vol_surface_eod.ipynb) | EOD implied-vol surface from `vol_surface_eod` / `vol_surface_point_eod` |
| [`notebooks/discount_curve_eod.ipynb`](notebooks/discount_curve_eod.ipynb) | FRED par yields, bootstrapped zero rates, discount factors |
| [`notebooks/gbm_calibration_scenarios.ipynb`](notebooks/gbm_calibration_scenarios.ipynb) | Historical GBM calibration (vol + correlation heatmap) and MC scenario paths from `exports/` |

Launch:

```bash
cd viz && jupyter lab notebooks/vol_surface_eod.ipynb
```

On environments where the shell runs as **root** (e.g. some dev containers), Jupyter refuses to start unless you pass:

```bash
cd viz && jupyter lab --allow-root notebooks/gbm_calibration_scenarios.ipynb
```

Or set once in the venv: `export JUPYTER_ALLOW_ROOT=1`.

## Package API (`numeraire_viz`)

```python
from numeraire_viz import load_vol_surface, plot_vol_smiles, plot_vol_surface_3d, plot_vol_surface_3d_mesh

df, header = load_vol_surface(as_of="2026-05-15", underlying_id="NDX")
plot_vol_smiles(df, spot=header["spot_used"])
plot_vol_surface_3d(df, spot=header["spot_used"])
plot_vol_surface_3d_mesh(df, spot=header["spot_used"])  # interpolated mesh (viz only)
```

**Discount curve (FRED par → bootstrap):**

```python
from numeraire_viz import load_discount_curve, plot_curve_overview, plot_yield_curve, plot_discount_factors

points, disc_hdr, par_hdr = load_discount_curve(as_of="2026-05-27", curve_id="USD_TREASURY_PAR_FRED")
plot_curve_overview(points, discount_header=disc_hdr, par_header=par_hdr)
plot_yield_curve(points, discount_header=disc_hdr)
plot_discount_factors(points, discount_header=disc_hdr)
```

**Historical GBM calibration + scenario paths:**

```python
from numeraire_viz import (
    load_historical_calibration,
    correlation_matrix_from_sparse,
    plot_calibration_overview,
    load_scenario_paths,
    plot_scenario_paths_grid,
)

header, factors, corr = load_historical_calibration("BOOK_1", valuation_as_of="2026-06-15")
matrix, labels = correlation_matrix_from_sparse(factors, corr)
plot_calibration_overview(header, factors, matrix, labels)

scenarios = load_scenario_paths(scope_key="BOOK_1", valuation_as_of="2026-06-15")
plot_scenario_paths_grid(scenarios, max_paths=10)
```

Future modules (spot history, trade MTM, etc.) can live alongside `vol_surface.py` / `discount_curve.py` under `numeraire_viz/`.
