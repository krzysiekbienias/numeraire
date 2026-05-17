# Numeraire++ — development progress

Living document: **what shipped when**, stage naming, and gaps. For module design and diagrams see [`architecture.md`](architecture.md); for build/run instructions see [`README.md`](../README.md).

---

## Stage vs sprint

- **Sprint** — numbered chunk of work (`0`, `1`, …).
- **Stage** — broader theme. Boundaries are pragmatic: persistence and Polygon ingest landed **alongside** core pricing rather than in a separate “Stage 3” release branch, so this file records **actual repo state** rather than an old checklist only.

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
| **8** | `market_data`: snapshots + providers | **Partially shipped** — [`MarketSnapshot`](../include/numeraire/market_data/market_snapshot.hpp), [`IMarketDataProvider`](../include/numeraire/market_data/imarket_data_provider.hpp), [`StaticMarketDataProvider`](../include/numeraire/market_data/static_market_data_provider.hpp). **Also shipped:** Polygon-compatible **REST ingest** (HTTP client + upserts), not only “quotes inside pricing”: [`src/market_data_providers/`](../src/market_data_providers/) → tables `equity_daily_eod`, `index_daily_eod`, `option_contract`. Wired from [`dev_main`](../app/dev_main.cpp) (`--fetch-*` flags). **Still open:** read persisted bars into `IMarketData` for valuation (today `dev_main` uses env-driven static quotes for NPV). |
| **9** | Polish, CI, tightening | **Ongoing** — incremental |

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
| SQLite table layout + Polygon → DB pipeline + pricing seam | [`architecture.md`](architecture.md) § *SQLite schema & Polygon market-data pipeline* |
| `dev_main` flags and examples | [`README.md`](../README.md) § *dev_main* |
