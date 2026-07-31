# CCR exposure demo — interview script & slide deck

Living **talk track** for presenting the Monte Carlo exposure pipeline: multifactor GBM → path pricing → EE/PFE → SQLite + CSV audit + Python review.

For module design see [`architecture.md`](architecture.md). For build/run see [`README.md`](../README.md) and [`viz/README.md`](../viz/README.md).

---

## How to use this doc

| Section | Purpose |
| -------- | -------- |
| [Slide deck](#slide-deck) | Copy-paste or print as 8–10 slides (A4 / PDF) |
| [Demo flow](#demo-flow-15-20-min) | Step-by-step live walkthrough |
| [Talking points](#talking-points) | PL + EN phrases for interviews |
| [Q&A](#qa-why-not-x) | Honest answers from the actual codebase |

**Audience:** technical interview, internal risk/quant review, or architecture walkthrough.

**Duration:** 2–3 min (elevator) · 15–20 min (full demo) · 5 min (slides only).

---

## Slide deck

### Slide 1 — Title

**CCR exposure batch (Numeraire++)**

- Expected Exposure (EE) and Potential Future Exposure (PFE 95 / 97)
- Per **leg × pillar** on an exposure time grid
- Multifactor historical GBM + path-wise analytic repricing

---

### Slide 2 — Business problem

**What CCR needs**

- For each live leg and each future pillar: distribution of positive exposure along simulated market paths
- Official numbers: **EE** (mean) and **PFE** (high quantiles)
- Reproducible inputs: calibration, MC seed, sticky market data @ `as_of`

**Exposure definition (per leg, per path, per pillar)**

\[
\text{exposure} = \max(0,\ \text{PV}_{\text{leg}})
\]

---

### Slide 3 — End-to-end pipeline

```text
Historical calibration (vol + correlation)
        ↓
Evolve multifactor GBM  →  ScenarioBuffer  (spots)
        ↓
Path pricing (analytic)   →  LegPathPvBuffer (PV per leg)
        ↓
Exposure metrics        →  EE, PFE 95, PFE 97.5 (DB column `pfe_975`)
        ↓
Persist                 →  trade_leg_exposure_eod (SQLite)
Optional audit          →  exports/*_all_factors.csv, *_leg_exposure.csv
Review                  →  Python viz (same path ids)
```

**Entry point:** `dev_main --simulate --as-of YYYY-MM-DD --book BOOK_1 --price-paths`

---

### Slide 4 — Memory design (MC-native, CCR-first)

Designed **from day one** for thousands of paths × factors × pillars — not retrofitted from CSV or ORM rows.

| Choice | Why |
| ------ | --- |
| **Struct-of-Arrays (SoA)** | For each `(factor, step)` all path values are **contiguous** — sequential access in GBM evolution and path statistics |
| **Flat 3D → 1D index** | `offset = ((factor × steps) + step) × stride + path` |
| **Padded stride (64 B)** | Each path slab starts cache-line aligned |
| **`std::pmr::monotonic_buffer_resource`** | One arena allocation per run; bulk free on teardown |
| **`std::span` slab views** | Non-owning views on contiguous path segments — no tensor copies in hot APIs |

**Key types:** `ScenarioBuffer` (spots), `LegPathPvBuffer` (leg PV).

---

### Slide 5 — Market data on paths

**Sticky @ valuation `as_of` (FO-consistent)**

- **Spots** — from simulated GBM paths (`ScenarioBuffer`)
- **IV** — from `vol_surface_eod` (moneyness uses simulated spot on the slice)
- **Rates** — from `discount_curve_eod` via `RiskFreeRateForTenor`

**Path pricing:** `ScenarioSliceMarketData` implements `IMarketData` for one `(step, path)` slice; `PricePortfolioAlongPaths` loops step → path → leg.

---

### Slide 6 — Data roles (separation of concerns)

| Layer | What | Role |
| ----- | ---- | ---- |
| **In-memory SoA** | `ScenarioBuffer`, `LegPathPvBuffer` | Compute |
| **SQLite** | `trade_leg_exposure_eod` | Official EE / PFE (full MC, e.g. 1000 paths) |
| **CSV exports** | `*_all_factors.csv`, `*_leg_exposure.csv` | Audit, regression, Python viz |
| **Python** | `plot_trade_scenario_and_exposure` | Human review — **same path ids** in every panel |

The engine is **not** designed around pandas layout; CSV is an export contract.

---

### Slide 7 — Demo: one trade review

**Question answered:** *“For this trade, what scenarios did we run, what did pricing do on those exact paths, and what are the official EE/PFE?”*

1. Pick `trade_id` (e.g. `TRD_10001`)
2. **Top:** spot fans for trade underlyings — first **50 path ids** from export
3. **Middle:** two exposure fans on the **same 50 paths**
   - Σ leg exposure: \(\sum \max(0, PV_{\text{leg}})\)
   - Net trade exposure: \(\max(0, \sum PV_{\text{leg}})\)
4. **Overlay:** EE / PFE from **DB** (summed over legs)
5. **Bottom:** EE / PFE profile from DB only

Fan = subset for visuals; DB = full MC population.

---

### Slide 8 — Verified scale (example run)

**BOOK_1 @ 2026-06-01** (after `--simulate --price-paths`)

| Artifact | Scale |
| -------- | ----- |
| `BOOK_1_2026-06-01_all_factors.csv` | 1000 paths × 5 factors × 14 steps |
| `BOOK_1_2026-06-01_leg_exposure.csv` | 1000 paths × 11 legs × 14 steps |
| `trade_leg_exposure_eod` | 11 legs × 14 pillars = **154 rows** |

Line-count sanity check: `wc -l exports/BOOK_1_2026-06-01_*.csv`

---

### Slide 9 — Honest limits & next steps

**What we optimize for today**

- Correctness and auditability of CCR batch
- Memory layout suited to MC (SoA, arena, spans)

**Known simplifications**

- Path pricing loop is **step × path × leg** with analytic pricer per slice — correctness-first, not fully batched
- PFE in DB is **per leg**; trade-level PFE rollup in Python sums leg quantiles (not a true net-trade quantile)
- Many single-leg books: Σ leg vs net trade exposure coincide

**Natural extensions**

- Reorder / batch pricing loop; optional OpenMP over paths
- Trade-level PFE from aggregated path exposure in C++
- Richer vol surface coverage per equity underlying

---

### Slide 10 — One-liner

**PL:**  
*Silnik exposure MC od początku był pod CCR: tensor ścieżek SoA w monotonic arena, repricing na siatce filarów, oficjalne EE/PFE w SQLite, CSV tylko do audytu i review.*

**EN:**  
*We built the exposure Monte Carlo layer CCR-first: struct-of-arrays path storage in a monotonic arena, path-wise repricing on the exposure grid, official EE/PFE in SQLite, and CSV only for audit and visualization.*

---

## Demo flow (15–20 min)

### Prerequisites

```bash
# Build
./scripts/build.sh

# Repo .env: NUMERAIRE_PERSIST_EXPOSURE=1, dump paths, DB vol/rate sources
# See .env.example § path-wise leg exposure
```

### Step 0 — Context (30 s)

> BOOK_1, as_of 2026-06-01, multifactor calibration (e.g. AAPL, GOOGL, MSFT, NDX, NVDA), 1000 paths, weekly exposure grid (~14 pillars), LIVE legs only.

### Step 1 — Run batch (if exports not present)

```bash
./build/dev_main --simulate --as-of 2026-06-01 --book BOOK_1 --price-paths
```

With `NUMERAIRE_PERSIST_EXPOSURE=1` in `.env`, EE/PFE land in `trade_leg_exposure_eod`.

### Step 2 — Sanity check artifacts (1 min)

```bash
wc -l exports/BOOK_1_2026-06-01_all_factors.csv
wc -l exports/BOOK_1_2026-06-01_leg_exposure.csv

sqlite3 db.sqlite3 "
  SELECT COUNT(*) FROM trade_leg_exposure_eod
  WHERE as_of='2026-06-01' AND scope_key='BOOK_1';
"
```

### Step 3 — Official EE/PFE in DB (1 min)

```bash
sqlite3 -header -column db.sqlite3 "
  SELECT leg_id, pillar_id, year_fraction,
         ROUND(ee, 2) AS ee, ROUND(pfe_95, 2) AS pfe_95
  FROM trade_leg_exposure_eod
  WHERE trade_id='TRD_10001' AND as_of='2026-06-01'
  ORDER BY grid_step
  LIMIT 8;
"
```

If EE/PFE are zero: negative PV → `max(0, PV) = 0` — mention the definition explicitly.

### Step 4 — Jupyter trade review (5–8 min)

```bash
source .venv-viz/bin/activate
cd viz && jupyter lab --allow-root notebooks/gbm_calibration_scenarios.ipynb
```

In notebook (set `VALUATION_AS_OF = "2026-06-01"`):

```python
from numeraire_viz import plot_trade_scenario_and_exposure

plot_trade_scenario_and_exposure(
    "TRD_10001",
    scope_key="BOOK_1",
    valuation_as_of="2026-06-01",
    max_paths=50,
)
```

**Say while showing:**

- Same path ids (0…49) in scenario and exposure panels
- DB lines = official stats from full MC
- Two exposure panels differ only when multi-leg netting matters

### Step 5 — Under the hood (3–5 min)

Open briefly (do not read entire files):

| File | What to point at |
| ---- | ---------------- |
| [`include/numeraire/simulation/scenario_buffer.hpp`](../include/numeraire/simulation/scenario_buffer.hpp) | SoA layout, flat `Index()`, `Slab()` → `std::span`, monotonic arena |
| [`src/simulation/gbm_evolution.cpp`](../src/simulation/gbm_evolution.cpp) | `EvolveMultiFactorGbm`, slab iteration |
| [`src/simulation/path_pricer.cpp`](../src/simulation/path_pricer.cpp) | `PricePortfolioAlongPaths` — step × path × leg |
| [`src/simulation/exposure_metrics.cpp`](../src/simulation/exposure_metrics.cpp) | `ComputeLegExposureMetrics` — mean + quantile |
| [`src/simulation/historical_gbm_simulate.cpp`](../src/simulation/historical_gbm_simulate.cpp) | CLI wiring: simulate, dump, persist |

---

## Talking points

### Elevator pitch (PL, ~45 s)

> Potrzebowaliśmy EOD profilu ekspozycji CCR: EE i PFE na siatce filarów dla każdej nogi. Pipeline to kalibracja historyczna, multifactor GBM na tysiącach ścieżek, wycena analityczna na każdym węźle siatki, potem statystyki i zapis do SQLite. Bufory MC zaprojektowaliśmy od razu pod ten workload — SoA, jeden tensor w monotonic arena, widoki przez span — a CSV i Python służą tylko audytowi i review, nie są źródłem prawdy.

### Elevator pitch (EN, ~45 s)

> We needed an EOD CCR exposure profile: EE and PFE on a time grid per leg. The pipeline calibrates historical vol and correlation, evolves a multifactor GBM across thousands of paths, reprices analytically on each grid node, then aggregates and persists to SQLite. The Monte Carlo buffers were designed for that workload from day one — struct-of-arrays, a single tensor in a monotonic arena, span-based slab views — while CSV and Python are audit and review layers, not the system of record.

### Memory design (interview sound bite)

> *“It’s a CCR batch tensor problem: many paths, few factors, many pillars. We store paths contiguously per (factor, step), map 3D logic to one flat block with cache-line padded stride, allocate once per run with `std::pmr::monotonic_buffer_resource`, and pass `std::span` slabs into the GBM kernel so we never copy the scenario cube.”*

### Data separation (interview sound bite)

> *“SQLite holds official EE/PFE from the full simulation. CSV holds raw paths for audit. Python aligns path ids for charts but doesn’t redefine the metrics.”*

---

## Q&A — “why not X?”

| Question | Answer |
| -------- | ------ |
| Why SoA? | Sequential access over paths in GBM evolution and when computing per-pillar statistics |
| Why monotonic PMR arena? | One MC run ≈ one lifetime; single bulk allocation, no heap churn in the hot loop |
| Why not store all paths in SQLite? | Volume and latency; DB stores aggregates, CSV stores raw paths for audit |
| Why per-leg PFE in DB? | CCR reporting granularity at leg level; trade rollup is a presentation concern |
| Σ leg PFE vs true trade PFE? | Summing leg quantiles ≠ quantile of net trade exposure; documented in viz; future C++ trade-level metric possible |
| L1 cache tuning? | We align to cache lines and design for locality; no explicit L1 blocking or profiling in v1 |
| GBM vol vs pricing vol? | GBM drives spot evolution only; path pricing IV/rates come from DB @ `as_of` |

---

## Code map (quick reference)

```text
simulation/
  scenario_buffer.hpp / .cpp     — spot SoA tensor
  leg_path_pv_buffer.hpp / .cpp  — leg PV SoA tensor
  gbm_evolution.cpp              — evolve GBM into ScenarioBuffer
  path_pricer.cpp                — PricePortfolioAlongPaths
  exposure_metrics.cpp           — EE, PFE 95/97
  scenario_dump.cpp              — CSV: all_factors
  leg_exposure_dump.cpp            — CSV: leg_exposure
  historical_gbm_simulate.cpp    — orchestration + CLI

database/
  sqlite_trade_leg_exposure_repository.*  — trade_leg_exposure_eod

viz/
  trade_exposure_review.py       — plot_trade_scenario_and_exposure
```

---

## Checklist before presenting

- [ ] `db.sqlite3` has calibration + trades for chosen `as_of`
- [ ] `exports/BOOK_*_{as_of}_*.csv` exist (or run simulate first)
- [ ] `trade_leg_exposure_eod` populated (`NUMERAIRE_PERSIST_EXPOSURE=1`)
- [ ] `.venv-viz` installed (`pip install -e "viz/[notebook]"`)
- [ ] Pick a `trade_id` with known underlyings in calibration set
- [ ] Notebook `VALUATION_AS_OF` matches export filenames
