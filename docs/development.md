# Numeraire++ — development progress

Living document: **what shipped when**, stage naming, and gaps. For module design and diagrams see [`architecture.md`](architecture.md); for build/run instructions see [`README.md`](../README.md).

---

## Stage vs sprint

- **Sprint** — numbered chunk of work (`0`, `1`, …).
- **Stage** — broader theme. Boundaries are pragmatic: persistence and Polygon ingest landed **alongside** core pricing rather than in a separate “Stage 3” release branch, so this file records **actual repo state** rather than an old checklist only.

---

## Definition of Done — per fix (acceptance criteria)

Two levels — do not mix them:

| Level | What it means | Where it lives |
|--------|----------------|----------------|
| **Team / release DoD** | Every PR: tests, review, CI green, no secrets committed, … | Contributing norms, CI config — not repeated here. |
| **Fix / story DoD** | This change is complete when *these* observable checks pass. | **Issue** (*Acceptance criteria*) and/or **PR** (*Test plan* / *How to verify*). |

For a bounded bugfix or small feature, treat the fix-level DoD as **acceptance criteria** in agile terms: a short checklist the author and reviewer can execute after the code change.

**Where to write it**

- **Default:** issue + PR description (copy the checklist into the PR). No separate `docs/acceptance/…` file unless the scenario becomes a long-lived regression reference or multi-week epic.
- **Repo docs:** add or extend a rule here only when the behaviour is a **lasting product convention** (e.g. “EOD MTM: `years_to_maturity` = Act365(`as_of`, expiry)”). One-off verification steps stay in the ticket/PR.
- **EOD MTM position scaling** — `pv_unit` and `delta`…`rho` are **per share**; `pv_total` and `delta_total`…`rho_total` are **position** values: `sign(direction) × quantity × contract_size × <unit value>`. Same \(M\) as [`LegPvTotal`](../include/numeraire/database/leg_pv.hpp); greek totals use the same helper pattern ([`architecture.md`](architecture.md) § *From book + market snapshot to NPV and MTM*). `quantity` and `contract_size` are validated `> 0` before pricing.
- **Booking `execution_price`** — After import, legs may have `execution_price = 0`. **Booking** (`dev_main --price-booking`, planned) sets `execution_price` to model **`pv_unit`** on **`trades.trade_date`** (per-share premium; **no** commission inside the price — convention A). **Commission** stays whatever import wrote. **MTM** (`--as-of`) does not update `execution_price`. Full lifecycle table: [`architecture.md`](architecture.md) § *Trade lifecycle: import → booking → MTM*.

**What a good fix-level DoD contains**

1. **Inputs** — trade id, `as_of` values, env vars held constant between runs (`NUMERAIRE_DEV_SPOT_SOURCE`, spot/rate/vol, …). See [`README.md`](../README.md) § *dev_main*.
2. **Observable outcomes** — relations or tolerances (not only “looks correct”). Example patterns:
   - Two valuation dates: `T(as_of₁) − T(as_of₂) ≈ Act365(as_of₁, as_of₂)`; PV may move with `T` if spot/r/vol are fixed.
   - `as_of` on expiry: `years_to_maturity = 0`, `pv_unit` = intrinsic (same idea as `ZeroTimeIsIntrinsic` in pricer unit tests).
   - Same `as_of` run twice: one row in `trade_leg_mtm_eod` per `(leg_id, as_of, pricing_engine)`; identical `years_to_maturity` and `pv_unit` (archive may get a second `batch_run_id` — that is not a failure).
3. **How to verify** — command(s) plus **SQL** against `db.sqlite3` after `dev_main … --as-of YYYY-MM-DD`, or debugger at the valuation-date → `T` boundary when wiring is unclear.

**Automation**

- **Phase 0 (spec):** write the checklist before coding; use DB fixtures only to discover `expiry`, `strike`, leg ids.
- **After merge:** mirror the same cases in **unit/integration tests** with an in-memory SQLite seed (see [`unit_tests/database/`](../unit_tests/database/)), not dependence on a developer’s local `TRD_*` row. CI should not require a private trade id.

### Batch `--as-of` reruns (weekdays)

`dev_main` accepts **one** valuation date per run (`--as-of` or `NUMERAIRE_DEV_AS_OF`). For a date range, loop from the **repository root** after `cmake --build build`. Use `NUMERAIRE_DEV_SPOT_SOURCE=db` only when `equity_daily_eod` has a bar for each day you price (weekends and holidays usually do not).

```bash
d=2026-05-01
end=2026-05-15
while [[ "$d" < "$end" || "$d" == "$end" ]]; do
  dow=$(date -d "$d" +%u)   # 1=Mon … 7=Sun (GNU date)
  if [[ "$dow" -le 5 ]]; then
    ./build/dev_main --as-of "$d" TRD_10001 || break
  fi
  d=$(date -I -d "$d + 1 day")
done
```

- **`|| break`** — stop the whole batch on the first failed run (missing EOD, bad trade id, …). Use **`|| continue`** to skip bad days and keep going.
- Adjust `d`, `end`, and the trade id; override env per run if needed (`NUMERAIRE_DEV_SPOT_SOURCE=db`, rate/vol from `.env`).

Quick check in SQLite after the loop:

```bash
sqlite3 db.sqlite3 "
SELECT as_of, leg_id, years_to_maturity, underlying_spot, pv_unit
FROM trade_leg_mtm_eod
WHERE trade_id = 'TRD_10001'
  AND as_of BETWEEN '2026-05-01' AND '2026-05-15'
ORDER BY as_of, leg_id;
"
```

To drive dates only from rows that exist in the market table (no weekend gaps), see [`README.md`](../README.md) § *dev_main*; a future `--as-of-from` / `--as-of-to` on `dev_main` itself is not implemented yet.

---

## Booking price (`--price-booking`) *(planned)*

**Goal:** Close the gap between structural import and MTM/PnL by persisting a model **trade-date premium** on each leg.

| Item | Detail |
|------|--------|
| **Status** | **Shipped** — `dev_main --price-booking`, DB writes, status rules, MTM gates (see checklist). |
| **Spec** | [`architecture.md`](architecture.md) § *Booking price (`--price-booking`)* and § *Trade lifecycle* |
| **Import today** | [`import_trade_bundle.py`](../scripts/import_trade_bundle.py) — `execution_price` null → `0`; commission from JSON |
| **MTM today** | `dev_main --as-of` — reads legs; **ignores** `execution_price` / `commission` for pricing |

### Operator workflow (target)

```bash
# 1 — book structure (already shipped)
python3 scripts/import_trade_bundle.py trades/incoming/my_trade.json --db db.sqlite3

# 2 — EOD on trade_date for each underlying (when SPOT_SOURCE=db)
./build/dev_main --fetch-eod-daily --from 2026-06-01 --to 2026-06-01 --ticker AAPL

# 3 — booking (trade must be PENDING in DB)
./build/dev_main --price-booking TRD_10004

# 4 — MTM (shipped; as_of >= trade_date)
NUMERAIRE_DEV_SPOT_SOURCE=db ./build/dev_main --as-of 2026-06-01 TRD_10004
```

### Fix-level acceptance criteria (for the implementation PR)

1. **Given** a **PENDING** trade with valid `trade_date`, legs with `execution_price = 0`, and market data available on `trade_date` (env spot or `equity_daily_eod` when `NUMERAIRE_DEV_SPOT_SOURCE=db`).
2. **When** `dev_main --price-booking <trade_id>` runs with fixed `NUMERAIRE_DEV_RATE` / `VOL` / `DIV_YIELD`.
3. **Then** for each leg: `trade_legs.execution_price` equals the **pv_unit** that the same pricer would produce if `ValuationDate = trade_date` (within float tolerance vs a unit test seed).
4. **And** `trade_leg_mtm_eod` row count unchanged by the booking run.
5. **And** `commission` columns unchanged.
6. **And** combining `--price-booking` with `--as-of` in one argv fails fast with `ValidationError`.
7. **And** when all booked `execution_price > 0`, `trades.status` becomes **`LIVE`**; MTM on that trade then succeeds with `--as-of` ≥ `trade_date`.

**SQL verify after booking:**

```bash
sqlite3 db.sqlite3 "
SELECT t.trade_id, t.trade_date, l.leg_id, l.execution_price, l.commission
FROM trades t
JOIN trade_legs l ON l.trade_id = t.trade_id
WHERE t.trade_id = 'TRD_10004';
"
```

### Implementation checklist (code)

| # | Deliverable | Status |
|---|-------------|--------|
| 1 | `UPDATE trade_legs SET execution_price = ?` (+ optional `trades.booking_timestamp`) — [`SqliteTradeLegBookingRepository`](../include/numeraire/database/sqlite_trade_leg_booking_repository.hpp) | **Shipped** |
| 2 | Booking pricer path (`ValuationDate = trade_date`) in `dev_main` | **Shipped** |
| 3 | `dev_main --price-booking` + mutual exclusion with `--as-of` | **Shipped** |
| 4 | Rules + repo UT (`trade_booking_rules`, booking repository) | **Shipped** |
| 5 | [`README.md`](../README.md) § *dev_main* — booking row in capabilities table | Planned |

**Explicitly out of scope for booking v1:** `pnl_daily` / `pnl_inception`, IV from Polygon, MC engine, commission recalculation, Hetzner cron.

---

## Stage 1 — foundation (historical)

Early milestones (kernel modules before trade persistence and HTTP ingest):

| Sprint | Scope |
|--------|--------|
| **0** | Layout, CMake modules, tooling |
| **1** | `numeraire_utils`: logging + shared exception hierarchy |
| **2** | `.env` via [`EnvLoader`](../include/numeraire/utils/env_loader.hpp) + [`configs/default.json`](../configs/default.json) via [`Config`](../include/numeraire/utils/config.hpp) |
| **3** | [`enums`](../include/numeraire/enums/enums.hpp) + [`quantlib_bridge`](../include/numeraire/utils/quantlib_bridge.hpp) |
| **3.5** | [`ModelType`](../include/numeraire/enums/model_type.hpp) vs [`PricingEngineType`](../include/numeraire/enums/pricing_engine_type.hpp) |
| **4** | [`schedule`](../include/numeraire/schedule/schedule_generator.hpp): [`Schedule`](../include/numeraire/schedule/schedule.hpp) POD + `ScheduleGenerator` (QuantLib internal) |

---

## Stage 2 — core pricing + SQLite book + Polygon REST ingest

Original sprint rows **5–9** were summarized next to the architecture doc; below is how they map to **today’s tree**.

| Sprint | Original intent | Status in repo |
|--------|-----------------|----------------|
| **5** | `core`: `IProduct` / `IPricer` / `IModel` / `IMarketData`, `PricingEngine`, `PricingResult` | **Shipped** — [`pricing_engine.hpp`](../include/numeraire/core/pricing_engine.hpp), related interfaces under [`include/numeraire/core/`](../include/numeraire/core/) |
| **6** | Factories (`ProductFactory`, `PricerFactory`) | **Shipped** — [`products/`](../include/numeraire/products/), [`pricers/`](../include/numeraire/pricers/) |
| **7** | Trade persistence (`TradeDto`, `ITradeRepository`, …) | **Shipped as SQLite-first** — reference DDL [`sql/schema_v1.sql`](../sql/schema_v1.sql); [`SqliteTradeRepository`](../include/numeraire/database/sqlite_trade_repository.hpp); bootstrap from [`sqlite_schema`](../include/numeraire/database/sqlite_schema.hpp); bundle import [`scripts/import_trade_bundle.py`](../scripts/import_trade_bundle.py). *(An in-memory repository was discussed early on; the runnable path is SQLite.)* |
| **8** | `market_data`: snapshots + providers | **Partially shipped** — [`MarketSnapshot`](../include/numeraire/market_data/market_snapshot.hpp), [`StaticMarketDataProvider`](../include/numeraire/market_data/static_market_data_provider.hpp). **Persisted Polygon daily bars**: [`market_data_providers`](../src/market_data_providers/) → `equity_daily_eod` / `index_daily_eod` / `option_contract`, [`dev_main`](../app/dev_main.cpp) ingest flags. **Spot-on-date for pricing**: `NUMERAIRE_DEV_SPOT_SOURCE=db` reads `equity_daily_eod.close` into the snapshot (**`NUMERAIRE_DEV_RATE` / `VOL` still env**). **Still open:** dedicated `SqliteMarketDataProvider` implementing `IMarketData` beyond this dev shim, vol/rate surfaces from DB. |
| **9** | Polish, CI, tightening | **Ongoing** — incremental |
| **—** | **Booking price** (`--price-booking`) | **Shipped** — see § *Booking price* above |

### SQLite — reference DDL (book, market, catalog, MTM placeholder)

[`sql/schema_v1.sql`](../sql/schema_v1.sql) is the single bootstrap DDL (idempotent `IF NOT EXISTS`). Besides booking + Polygon-fed market tables, it includes:

- **`catalog_instrument_type`** — optional reference codes aligned with `products_equity.instrument_type` (seed / UI). Index **`idx_catalog_instrument_type_family`** on `family` (SQLite requires index names not to collide with table names in the same schema).
- **`trades` / `trade_legs`** — structural book from import; **`execution_price`** (per-share premium at booking) and **`commission`** (trade cost, separate). Booking fill: planned `dev_main --price-booking`; see [`architecture.md`](architecture.md) § *Booking price*.
- **`trade_leg_mtm_eod`** — per-leg **EOD mark-to-model** row (inputs used, PV, greeks, `years_to_maturity`, `pricing_engine`, `as_of`). **Written by `dev_main --as-of`** (plus append-only **`trade_leg_mtm_eod_archive`**). `years_to_maturity` = Act/365(`ValuationDate`, expiry) — see [`architecture.md`](architecture.md) § *IMarketData and valuation date*. PV/greek columns: unit (`pv_unit`, `delta`, …) vs position (`pv_total`, `delta_total`, …) — scaling in [`architecture.md`](architecture.md) § *EOD MTM — unit vs position columns*. Columns **`pnl_daily`** / **`pnl_inception`** exist in DDL but are **not populated** yet.

Separator lines in the SQL file are **`--` comments** so the script parses cleanly when applied wholesale.

### REST API note

There is **no public HTTP server** in this repo. “REST” here means **outbound** calls to **Polygon.io** (or compatible base URL) from `dev_main` / [`market_data_providers`](../src/market_data_providers/), persisting responses into SQLite — batch ETL style, not an inbound API.

### Pricing tracks (living goals)

These tracks remain the north star for pricer work:

| Track | Goal |
|--------|------|
| **Closed-form pricers** | NPV/greeks from explicit formulas in-library; QuantLib not the maths source inside pricer TUs. |
| **QuantLib in tests** | Benchmark parity (e.g. `BlackCalculator`) on conventions. |
| **Time axis** | Shared calendar/year-fraction helpers in `schedule`; pricers consume domain dates/fractions. |
| **Orchestration** | `PricingEngine`, factories, persistence + market expansion — see [`architecture.md`](architecture.md) module graph. |

**Shipped under these tracks (non-exhaustive):**

- [`PricingEngine`](../include/numeraire/core/pricing_engine.hpp)
- [`AnalyticBlackScholesEquityPricer`](../include/numeraire/pricers/analytic_black_scholes_equity_pricer.hpp) with UT vs `QuantLib::BlackCalculator`
- SQLite-backed catalog + trades + multi-mode [`dev_main`](../README.md#dev_main-pricing--polygon-ingest) (price book vs Polygon ingest)

---

## What we intentionally document elsewhere

| Topic | Where |
|--------|--------|
| Fix-level Definition of Done / acceptance checklists | This file § *Definition of Done — per fix* |
| Import → booking → MTM lifecycle + booking conventions | [`architecture.md`](architecture.md) § *Trade lifecycle* / *Booking price* |
| Booking delivery checklist + operator workflow | This file § *Booking price (`--price-booking`)* |
| SQLite table layout + Polygon → DB pipeline + pricing seam | [`architecture.md`](architecture.md) § *SQLite schema & Polygon market-data pipeline* |
| `dev_main` flags and examples | [`README.md`](../README.md) § *dev_main* |
