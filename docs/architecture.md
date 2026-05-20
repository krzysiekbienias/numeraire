# Numeraire++ — architecture

Living document. Updated as the codebase evolves. **Sprint/stage history:** `[development.md](development.md)`.

## Vision

Numeraire++is a modular C++ derivative pricing framework. The primary goals
are:

- **Composable** — products, pricers, models and market data are independent
abstractions. You build a trade and pick a pricer per call.
- **Testable end-to-end** — unit tests where possible (everything pure),
integration tests where I/O is involved (DB, market data providers).
- **QuantLib-grounded** — schedule generation uses QuantLib internally; UTs
cross-check our outputs against raw QuantLib as the ground truth.
- **Hybrid coupling** — public API speaks our domain types
(`numeraire::CalendarType`, `Schedule`, `OptionType`); QuantLib lives
inside the schedule module and inside individual pricers as an
implementation detail. Bridge helpers in
`include/numeraire/utils/quantlib_bridge.hpp` (Sprint 1+).

---

## Module dependency graph (target)

```mermaid
flowchart TB
    enums[enums<br/>OptionType, Currency,<br/>CalendarType, ...]
    utils[utils<br/>Logger, Config, EnvLoader,<br/>Exception, time, string,<br/>quantlib_bridge]
    schedule[schedule<br/>ScheduleGenerator,<br/>Schedule POD]
    core[core<br/>IProduct, IPricer, IModel,<br/>IMarketData, PricingEngine,<br/>PricingResult, Factories]
    database[database<br/>ITradeRepository,<br/>TradeDto,<br/>SqliteTradeRepository]
    marketData[market_data<br/>IMarketDataProvider,<br/>MarketSnapshot,<br/>StaticMarketDataProvider]
    app[app<br/>main / dev_main]
    tests[unit_tests<br/>integration_tests]

    enums --> utils
    enums --> schedule
    enums --> core
    enums --> database
    enums --> marketData

    utils --> schedule
    utils --> core
    utils --> database
    utils --> marketData

    schedule --> core
    core --> database
    core --> marketData

    core --> app
    schedule --> app
    utils --> app

    core --> tests
    schedule --> tests
    utils --> tests
    database --> tests
    marketData --> tests

    spdlog([spdlog + fmt]) -.-> utils
    nlohmann([nlohmann_json]) -.-> utils
    quantlib([QuantLib]) -.-> schedule
    quantlib -.-> tests
    gtest([GoogleTest]) -.-> tests
```



Rules:

- `enums` and `utils` are leaf modules — depended on, never depending.
- `core` defines abstractions only; no concrete pricer/product lives here.
- `database` and `market_data` know `core` (they implement / consume its
interfaces); `core` does not know them.
- QuantLib is visible only in `schedule` (and tests, as a benchmark). `core`
must never link QuantLib directly.

---

## Pricing flow (target)

```mermaid
sequenceDiagram
    participant User as Client code
    participant Engine as PricingEngine
    participant Product as IProduct
    participant Pricer as IPricer
    participant Model as IModel
    participant Mkt as IMarketData

    User->>User: build IProduct (own or load via ITradeRepository)
    User->>User: build IMarketData (own or via IMarketDataProvider)
    User->>User: pick IPricer + IModel (ModelType)
    User->>Engine: Price(product, pricer, market)
    Engine->>Pricer: Price(product, market)
    Pricer->>Model: parameters / setup
    Pricer->>Mkt: ValuationDate, spot, vol, rates, dividends
    Note over Pricer,Mkt: T = year fraction(ValuationDate, ExpiryDate)
    Pricer->>Product: type, exercise, payoff, schedule
    Pricer-->>Engine: PricingResult{npv, greeks?, diagnostics}
    Engine-->>User: PricingResult
```



---

## Build system

- Top-level `[CMakeLists.txt](../CMakeLists.txt)` is intentionally short. It
declares the project, options, includes the two helper modules, then
delegates to per-module subdirectories (gated by `if(EXISTS)` so empty
modules don't break the build).
- `[cmake/NumeraireCompileOptions.cmake](../cmake/NumeraireCompileOptions.cmake)`
— language standard, warnings, debug/release flags, OS-specific tweaks.
Single source of truth for "how we compile".
- `[cmake/NumeraireDependencies.cmake](../cmake/NumeraireDependencies.cmake)`
— every `find_package` lives here. Single source of truth for "what we
use". Per-module CMakeLists then link only the deps they actually need.

The first real library is `**numeraire_utils**` (`[src/utils/CMakeLists.txt](../src/utils/CMakeLists.txt)`):
spdlog-backed `[Logger](../include/numeraire/utils/logger.hpp)`, log-level
parsing, `[EnvLoader](../include/numeraire/utils/env_loader.hpp)` (dotenv-style
`.env` + optional `ApplyToEnvironment` / POSIX `setenv`), `[Config](../include/numeraire/utils/config.hpp)`
(JSON defaults via nlohmann_json), and the `[exception](../include/numeraire/utils/exception.hpp)`
types. The `[enums](../include/numeraire/enums/enums.hpp)` module holds domain `enum class` types;
`[quantlib_bridge](../include/numeraire/utils/quantlib_bridge.hpp)` maps them to QuantLib calendars,
frequencies, day counters, conventions, currencies, and option/exercise enums. Executables link
`numeraire_utils` directly (which **PUBLIC**-ly depends on `numeraire_enums` and QuantLib).

The `**numeraire_schedule`** library (`[src/schedule/CMakeLists.txt](../src/schedule/CMakeLists.txt)`) wraps
QuantLib schedule generation: `[Schedule](../include/numeraire/schedule/schedule.hpp)` (date list + day-count),
`[ScheduleConfig](../include/numeraire/schedule/schedule_config.hpp)`, `[ScheduleGenerator](../include/numeraire/schedule/schedule_generator.hpp)`
(builder + `Generate` / `YearFraction` / `IsBusinessDay` / `Adjust`), plus `[ScheduleToQuantLib](../include/numeraire/schedule/schedule_quantlib.hpp)` /
`ScheduleFromQuantLib`. It links `**numeraire_utils**` for `quantlib_bridge` only (QuantLib stays out of `core`).

### `ScheduleGenerator` dependencies

`[ScheduleGenerator](../include/numeraire/schedule/schedule_generator.hpp)` is a thin façade: it owns a
`[ScheduleConfig](../include/numeraire/schedule/schedule_config.hpp)` (domain enums only in the public header) and,
in the implementation TU, maps that config to QuantLib via `[quantlib_bridge](../include/numeraire/utils/quantlib_bridge.hpp)`
and `[ScheduleFromQuantLib](../include/numeraire/schedule/schedule_quantlib.hpp)`. Call sites receive a lightweight
`[Schedule](../include/numeraire/schedule/schedule.hpp)` (dates + `DayCount` tag), not a `QuantLib::Schedule`.

**Types and composition** (solid = header-level ownership / API; dashed = implementation detail in `.cpp`):

```mermaid
flowchart TB
    subgraph api["Public API (schedule headers)"]
        SG[ScheduleGenerator]
        Builder[ScheduleGenerator.Builder]
        SC[ScheduleConfig]
        Sch[Schedule]
        D[Date]
    end

    subgraph enums["enums"]
        CT[CalendarType]
        FR[Frequency]
        CN[DateConvention]
        RG[DateGenerationRule]
        DC[DayCount]
    end

    subgraph impl["Implementation .cpp only"]
        QB[quantlib_bridge]
        SQL[schedule_quantlib]
        QL[QuantLib]
        EX[ScheduleError]
    end

    SG --> SC
    Builder --> SC
    Builder -->|Build| SG
    SG -->|Generate| Sch
    SG --> D
    Sch --> D
    Sch --> DC

    SC --> CT
    SC --> FR
    SC --> CN
    SC --> RG
    SC --> DC

    SG -.-> QB
    SG -.-> SQL
    SG -.-> EX
    SQL -.-> QL
    QB -.-> QL
    D -.->|ToQuantLibDate / FromQuantLibDate| QL
```



`**Generate(start, end)**` (conceptual sequence):

```mermaid
sequenceDiagram
    participant Client
    participant SG as ScheduleGenerator
    participant SC as ScheduleConfig
    participant QB as quantlib_bridge
    participant QLS as QuantLib Schedule
    participant SF as ScheduleFromQuantLib
    participant Out as Schedule

    Client->>SG: Generate(start, end)
    SG->>SG: validate dates, frequency
    SG->>SC: read calendar, conventions, rule, frequency, day_count
    SG->>QB: ToQuantLib(...)
    SG->>QLS: construct schedule
    SG->>SF: ScheduleFromQuantLib(ql_schedule, day_count)
    SF->>Out: dates + DayCount
    SG-->>Client: Schedule
```



**Pricing takeaway:** keep stochastic models and numerical engines out of `ScheduleGenerator`; pricers consume
`Schedule` / `YearFraction` / `Adjust` alongside `IModel` and market data. Note: `[date.hpp](../include/numeraire/schedule/date.hpp)`
includes QuantLib’s date type for conversions at the schedule module boundary.

### Trade row → `ScheduleConfig` (database first, then JSON fallback)

Schedule fields may live on the trade row (e.g. optional `schedule_*` columns on `wiener_tradebook`). **One place**
must own the merge rule: **use DB strings when present; otherwise fall back to** `[configs/default.json](../configs/default.json)`
under `schedule` (`default_calendar`, `default_frequency`, …), read through `[Config](../include/numeraire/utils/config.hpp)`.

- **Persistence** — SQLite (or any repository) returns a small DTO with `std::optional<std::string>` per column; it does **not**
implement fallback logic.
- **Config** — supplies default strings from JSON; it does **not** know about SQL.
- `**ScheduleConfigResolver` (target)** — lives in the `**schedule`** module; parses optional DB strings to enums and fills gaps
from `Config`. String→enum parsing stays in the resolver’s `.cpp` (or private helpers), not in thin `enum` headers.

**Component view** (single “driver” for merge):

```mermaid
flowchart LR
    subgraph persistence["Persistence"]
        DB[(Trade table e.g. wiener_tradebook)]
        JSON[configs/default.json]
    end

    subgraph access["Thin adapters — no merge rule"]
        Repo[TradeRepository / SQLite]
        CFG[Config Load]
    end

    subgraph schedule_mod["schedule — single merge driver"]
        DRV[ScheduleConfigResolver ResolveScheduleConfig]
    end

    subgraph consumer["Consumer"]
        GEN[ScheduleGenerator]
    end

    DB --> Repo
    JSON --> CFG
    Repo -->|TradeScheduleOverrides DTO optional strings| DRV
    CFG -->|schedule.default_* strings| DRV
    DRV -->|ScheduleConfig| GEN
```



**Sequence** (conceptual):

```mermaid
sequenceDiagram
    participant App as App / pricer
    participant Repo as TradeRepository
    participant DB as SQLite trade row
    participant CFG as Config default.json
    participant RES as ScheduleConfigResolver
    participant GEN as ScheduleGenerator

    App->>Repo: GetTrade(trade_id)
    Repo->>DB: SELECT schedule_* ...
    DB-->>Repo: row NULL or strings
    Repo-->>App: TradeScheduleOverrides DTO

    App->>CFG: Config already loaded
    App->>RES: Resolve(overrides, config)
    Note over RES: per field: DB value then parse else Config default
    RES-->>App: ScheduleConfig

    App->>GEN: generator with resolved ScheduleConfig Generate(start, end)
```



**Minimal types** (contract sketch):

```mermaid
classDiagram
    class TradeScheduleOverrides {
        <<DTO from DB>>
        +optional string calendar
        +optional string frequency
        +optional string convention
        +optional string rule
        +optional string day_count
    }

    class Config {
        +Root json
        +RequireString path
    }

    class ScheduleConfig {
        +CalendarType calendar_type
        +Frequency frequency
        +DateConvention convention
        +DateGenerationRule rule
        +DayCount day_count
    }

    class ScheduleConfigResolver {
        +Resolve overrides config ScheduleConfig
    }

    ScheduleConfigResolver ..> TradeScheduleOverrides : reads
    ScheduleConfigResolver ..> Config : fallback default_*
    ScheduleConfigResolver ..> ScheduleConfig : builds
```



---

## SQLite schema & Polygon market-data pipeline *(current stage)*

> **Note:** This subsection describes the **persisted catalog, trades, and market-data tables plus HTTP ingest from Polygon.io** as implemented **today**. It will evolve as DB-backed `IMarketData` and richer surfaces ship. See `[sql/schema_v1.sql](../sql/schema_v1.sql)` for the authoritative DDL.

### Tables at a glance

- **Booking / catalog:** `products`, `products_equity`, `trades`, `trade_legs` — loaded via [`import_trade_bundle.py`](../scripts/import_trade_bundle.py) (or fixtures); **`trade_legs.execution_price`** and **`commission`** are placeholders until booking (see § *Booking price*).
- **Market history / reference:** `equity_daily_eod`, `index_daily_eod`, `option_contract` — populated by optional Polygon REST jobs run via `[dev_main](../app/dev_main.cpp)`.
- **EOD MTM:** `trade_leg_mtm_eod` (current mark per `leg_id` + `as_of` + `pricing_engine`) and `trade_leg_mtm_eod_archive` (append-only history per `batch_run_id`) — written by **MTM** pricing mode in `[dev_main](../app/dev_main.cpp)` (`--as-of`; does not update `execution_price`).

There are **no foreign keys** from the Polygon-fed tables into `products` / `trades`. Business joins are by convention (e.g. `products.underlying_id` aligned with `equity_daily_eod.ticker`).

### Logical ER diagram

```mermaid
erDiagram
    products ||--|| products_equity : product_id
    trades ||--o{ trade_legs : trade_id
    products ||--o{ trade_legs : product_id

    equity_daily_eod {
        int id PK
        text ticker
        text as_of
        real close
        text source
        text ingested_at
    }

    index_daily_eod {
        int id PK
        text ticker
        text as_of
        real close
        text source
    }

    option_contract {
        int id PK
        text option_ticker
        text listing_as_of
        text underlying_ticker
        real strike_price
        text contract_type
        text source
    }

    products {
        text product_id PK
        text underlying_id
        text asset_kind
    }

    products_equity {
        text product_id PK
        text instrument_type
        text option_type
        real strike
    }

    trades {
        text trade_id PK
        text portfolio_id
        text strategy_type
        text trade_date
        text booking_timestamp
        text status
    }

    trade_legs {
        text leg_id PK
        text trade_id FK
        text product_id FK
        text direction
        real quantity
        real execution_price
        real commission
    }
```



### Polygon.io → SQLite ingest

Bulk endpoints target the same SQLite file configured under `database.path` (see `[configs/default.json](../configs/default.json)`). Credentials and throttle use environment variables (`POLYGON_API_KEY`, optional `POLYGON_BASE_URL`, `NUMERAIRE_POLYGON_SLEEP_SEC_AFTER_CALL`), documented alongside the ingest code under `[src/market_data_providers/](../src/market_data_providers/)`.

```mermaid
flowchart LR
    subgraph Polygon["Polygon REST"]
        A1["v2/aggs daily"]
        A2["v3/reference/options/contracts"]
    end

    subgraph DB["SQLite schema_v1"]
        EOD[equity_daily_eod]
        IDX[index_daily_eod]
        OPT[option_contract]
        TR[trades + trade_legs]
        CAT[products + products_equity]
    end

    A1 -->|listed equity tickers| EOD
    A1 -->|index tickers e.g. I:SPX| IDX
    IDX -->|strike band uses index close| OPT
    A2 --> OPT

    TR --> CAT
```



**Ordering detail:** loading `option_contract` uses an **index level** already present in `index_daily_eod` (e.g. close for strike-window logic). In practice, **ingest index daily EOD before option contract reference** for a given valuation date window.

### `IMarketData` and valuation date *(shipped)*

`[IMarketData](../include/numeraire/core/imarket_data.hpp)` is the read-only market view passed into pricers. Besides spot, rate, dividend yield, and implied vol, it exposes:

- `**ValuationDate()`** — calendar **as-of** for which inputs are valid (EOD session). Documented convention: pricers compute time to expiry from `**ValuationDate()`** to the product’s `**ExpiryDate()**` using **Act/365 Fixed** unless a concrete provider says otherwise.

`[MarketSnapshot](../include/numeraire/market_data/market_snapshot.hpp)` carries `valuation_date` (set from `--as-of` / `NUMERAIRE_DEV_AS_OF` in `dev_main`). `[StaticMarketDataProvider](../include/numeraire/market_data/static_market_data_provider.hpp)` wraps that snapshot into an `IMarketData` handle **without I/O** — callers build the snapshot elsewhere (env, SQLite lookup in `dev_main`, unit tests).

`[AnalyticBlackScholesEquityPricer](../include/numeraire/pricers/analytic_black_scholes_equity_pricer.hpp)` uses `schedule::Act365FixedYearFraction(market.ValuationDate(), product.ExpiryDate())` for T and for the vol slice passed to `ImpliedVolatility`. `**TradeDate()`** on the product is booking metadata, not the pricing time axis.

Unit tests benchmark NPV/greeks against `QuantLib::BlackCalculator` with the same T convention; see `[unit_tests/pricers/test_analytic_black_scholes_equity_pricer.cpp](../unit_tests/pricers/test_analytic_black_scholes_equity_pricer.cpp)`.

### Trade lifecycle: import → booking → MTM

Three **separate** steps touch the book. They reuse the same pricer stack (`ProductFactory`, `PricingEngine`, `AnalyticBlackScholesEquityPricer`) but differ in **valuation date**, **what gets persisted**, and **whether `trade_legs` is updated**.

| Step | Tool | Valuation date (`IMarketData::ValuationDate()`) | Writes |
|------|------|--------------------------------------------------|--------|
| **1. Structural book** | [`import_trade_bundle.py`](../scripts/import_trade_bundle.py) | — | `products`, `products_equity`, `trades`, `trade_legs`; `execution_price` null → **0**; `commission` from JSON or **0** |
| **2. Booking price** | `dev_main --price-booking` *(planned)* | **`trades.trade_date`** per trade | `trade_legs.execution_price` (= model **pv_unit** at trade date); optional `trades.booking_timestamp` |
| **3. EOD MTM** | `dev_main --as-of` *(shipped)* | **CLI `--as-of`** / `NUMERAIRE_DEV_AS_OF` | `trade_leg_mtm_eod` + archive only |

**`Product::TradeDate()`** is set from `trades.trade_date` when the catalog bundle is built ([`ProductFactory`](../src/products/product_factory.cpp)); it is **metadata** on the instrument. **Time to expiry** for both booking and MTM uses **`ValuationDate()` → expiry** (Act/365 Fixed), not `TradeDate()`.

```mermaid
flowchart LR
    JSON[import_trade_bundle.py] --> BOOK[(trades + trade_legs)]
    EOD[Polygon / equity_daily_eod] --> BOOK
    BOOK --> PB["dev_main --price-booking planned"]
    PB -->|UPDATE execution_price| BOOK
    BOOK --> MTM["dev_main --as-of shipped"]
    MTM --> MTM_TBL[(trade_leg_mtm_eod)]
```

**Variant B (dev book):** MTM on `trade_date` the same day as booking is allowed after step 2; re-running MTM on later `--as-of` dates does not require re-booking.

### Booking price (`--price-booking`) *(planned)*

> **Status:** Spec + schema + **[`SqliteTradeLegBookingRepository`](../include/numeraire/database/sqlite_trade_leg_booking_repository.hpp)** (DB writes) shipped; **`dev_main --price-booking`** CLI not implemented yet. See [`development.md`](development.md) § *Booking price*.

When shipped, booking answers: *“What was the model premium per share at trade date?”* That value is stored as **`trade_legs.execution_price`** and is **not** mixed with commission.

**Conventions (v1):**

| Field | Rule |
|-------|------|
| **`execution_price`** | Per-share option premium from the booking run: **`pv_unit`** from `PricingResult::Npv()` (same scale as MTM `pv_unit`). **Convention A:** commission is **not** included in `execution_price`. |
| **`commission`** | Set at **import** (`commission` or `commission_per_contract × quantity` in JSON). Booking **does not** recalculate commission in v1. |
| **Valuation date** | `MarketSnapshot::valuation_date` = `ParseIsoDate(trades.trade_date)`. Reject trades with empty/invalid `trade_date`. |
| **Market inputs** | Same env as MTM: `NUMERAIRE_DEV_SPOT_SOURCE`, `NUMERAIRE_DEV_RATE`, `NUMERAIRE_DEV_VOL`, `NUMERAIRE_DEV_DIV_YIELD`. With **`SPOT_SOURCE=db`**, spot is `equity_daily_eod.close` on **`trade_date`** (ingest required for that session). |
| **Greeks / MTM tables** | Booking run **does not** write `trade_leg_mtm_eod` or greeks to the book row. |
| **`booking_timestamp`** | Optional: set to `datetime('now')` on `trades` when booking completes; otherwise leave as imported. |
| **Re-run** | TBD in implementation: overwrite `execution_price` with log vs fail if already &gt; 0. Document the chosen rule in the PR. |
| **CLI mutual exclusion** | `--price-booking` and `--as-of` must not be combined in one process invocation. |

**Planned argv** (mirror MTM trade selection):

```text
dev_main --price-booking <trade_id>
dev_main --price-booking --all
dev_main --price-booking --trades-json <path>
```

**Downstream (later tickets):** `pnl_inception` on MTM rows will use `pv_total`, `execution_price`, and `commission` via [`LegPvTotal`](../include/numeraire/database/leg_pv.hpp)-style cost basis; booking must run before inception PnL is meaningful.

### From book + market snapshot to NPV and MTM *(today’s `dev_main --as-of` path)*

Pricing follows the same sequence as **[Pricing flow (target)](#pricing-flow-target)** earlier in this document: `SqliteTradeRepository` assembles a `TradeCatalogBundle` per `trade_id`; `ProductFactory` and `PricingEngine` price each leg.

**Valuation date** — Required for pricing mode: `**--as-of YYYY-MM-DD*`* or `**NUMERAIRE_DEV_AS_OF**`. Parsed into `MarketSnapshot::valuation_date` and served via `IMarketData::ValuationDate()`. Missing or invalid date → `ValidationError` at startup (no “timeless” run).

For market inputs, `**dev_main` builds a `MarketSnapshot**` and `**StaticMarketDataProvider**`:

- **Spot** — `**NUMERAIRE_DEV_SPOT_SOURCE=env`** (default): `NUMERAIRE_DEV_SPOT` for every underlying referenced by booked legs.
- **Spot — DB** — `**NUMERAIRE_DEV_SPOT_SOURCE=db`**: `**equity_daily_eod.close**` for each underlying on `**ValuationDate**` (requires an ingested daily bar; ticker match is case-insensitive; `adjusted` via `**NUMERAIRE_DEV_SPOT_ADJUSTED**`, default `1`).
- **Rate / vol / dividends** — still from env: `**NUMERAIRE_DEV_RATE`**, `**NUMERAIRE_DEV_VOL**`, `**NUMERAIRE_DEV_DIV_YIELD**`.

**MTM persistence** — After each leg is priced, `dev_main` upserts `**trade_leg_mtm_eod`** and appends `**trade_leg_mtm_eod_archive**` with inputs used (`underlying_spot`, rates, vol), outputs (`pv_unit`, greeks), `**years_to_maturity**` (same T as the pricer), `pricing_engine`, and a shared `**batch_run_id**` for the run.

**EOD MTM — unit vs position columns** — Pricer output is **per one share** (one unit of underlying). Booked leg size comes from `trade_legs` (`direction`, `quantity` in contracts) and `products_equity.contract_size` (shares per listed contract, e.g. 100 for US equity options). Let s = +1 for LONG and -1 for SHORT (`[PositionSign](../include/numeraire/database/leg_pv.hpp)`). Position multiplier:

M = sign(direction) × quantity × contract_size


| Column                                                                 | Meaning                                                                                                                                            |
| ---------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| `pv_unit`                                                              | Model NPV per share from the pricer                                                                                                                |
| `pv_total`                                                             | M \times \text{pvunit} — `[LegPvTotal](../include/numeraire/database/leg_pv.hpp)`                                                                  |
| `delta`, `gamma`, `vega`, `theta`, `rho`                               | Model greek **per share** (same scale as `pv_unit`; from `[PricingGreeks](../include/numeraire/core/pricing_result.hpp)`)                          |
| `delta_total`, `gamma_total`, `vega_total`, `theta_total`, `rho_total` | M \times the corresponding unit greek — same linear scaling as `pv_total` (helper beside `[LegPvTotal](../include/numeraire/database/leg_pv.hpp)`) |


`quantity` and `contract_size` must be strictly positive before pricing; direction is only on `trade_legs.direction`, not on the sign of `quantity`.

**Greek conventions** (`[AnalyticBlackScholesEquityPricer](../src/pricers/analytic_black_scholes_equity_pricer.cpp)`, benchmarked vs `QuantLib::BlackCalculator` in unit tests): sensitivities are w.r.t. **spot S**, **absolute volatility \sigma**, and **rate r** on the same T as NPV. `vega` is \partial V / \partial \sigma (not “per 1% vol”). `theta` is time decay **per calendar year** (not per day). Position totals inherit these definitions; they are not re-normalized at persist time.

A fully SQLite-backed `IMarketDataProvider` (vol/rate surfaces from DB, no env shim) remains future work.

```mermaid
flowchart LR
    subgraph Persisted["SQLite"]
        TR[trades + trade_legs + catalog]
        MD[equity_daily_eod index_daily_eod option_contract]
        MTM[trade_leg_mtm_eod + archive]
    end

    ASOF["--as-of / NUMERAIRE_DEV_AS_OF"] --> SNAP[MarketSnapshot valuation_date]
    TR -->|GetCatalogForTrade| PR[PricingEngine + AnalyticBlackScholesEquityPricer]
    ENV[ENV NUMERAIRE_DEV_* rate vol q] --> SNAP
    SNAP --> SMP[StaticMarketDataProvider]
    SMP -->|IMarketData ValuationDate spot vol| PR
    MD -->|equity_daily_eod close SOURCE=db| SNAP
    PR --> NPV[Book NPV per trade log]
    PR --> MTM
```

*(Booking path — planned — uses `trade_date` instead of `ASOF` and updates `trade_legs.execution_price` instead of `MTM`.)*



---

## Delivery status

Sprint checklist, Stage 2 pricing tracks, SQLite/Polygon milestones, and gaps are maintained in `**[development.md](development.md)**` so this file stays focused on structure and diagrams.
