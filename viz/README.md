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

Launch:

```bash
cd viz && jupyter lab notebooks/vol_surface_eod.ipynb
```

## Package API (`numeraire_viz`)

```python
from numeraire_viz import load_vol_surface, plot_vol_smiles, plot_vol_surface_3d, plot_vol_surface_3d_mesh

df, header = load_vol_surface(as_of="2026-05-15", underlying_id="NDX")
plot_vol_smiles(df, spot=header["spot_used"])
plot_vol_surface_3d(df, spot=header["spot_used"])
plot_vol_surface_3d_mesh(df, spot=header["spot_used"])  # interpolated mesh (viz only)
```

Future modules (spot history, trade MTM, etc.) can live alongside `vol_surface.py` under `numeraire_viz/`.
