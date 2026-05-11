# Numeraire++ — architecture

Living document. Updated as each sprint ships.

## Vision

Numeraire++ is a modular C++ derivative pricing framework. The primary goals
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
    database[database<br/>ITradeRepository,<br/>TradeDto,<br/>InMemoryTradeRepository]
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
    Pricer->>Mkt: spot, vol, rates, dividends
    Pricer->>Product: type, exercise, payoff, schedule
    Pricer-->>Engine: PricingResult{npv, greeks?, diagnostics}
    Engine-->>User: PricingResult
```

---

## Build system

- Top-level [`CMakeLists.txt`](../CMakeLists.txt) is intentionally short. It
  declares the project, options, includes the two helper modules, then
  delegates to per-module subdirectories (gated by `if(EXISTS)` so empty
  modules don't break the build).
- [`cmake/NumeraireCompileOptions.cmake`](../cmake/NumeraireCompileOptions.cmake)
  — language standard, warnings, debug/release flags, OS-specific tweaks.
  Single source of truth for "how we compile".
- [`cmake/NumeraireDependencies.cmake`](../cmake/NumeraireDependencies.cmake)
  — every `find_package` lives here. Single source of truth for "what we
  use". Per-module CMakeLists then link only the deps they actually need.

The first real library is **`numeraire_utils`** ([`src/utils/CMakeLists.txt`](../src/utils/CMakeLists.txt)):
spdlog-backed [`Logger`](../include/numeraire/utils/logger.hpp), log-level
parsing, [`EnvLoader`](../include/numeraire/utils/env_loader.hpp) (dotenv-style
`.env` + optional `ApplyToEnvironment` / POSIX `setenv`), [`Config`](../include/numeraire/utils/config.hpp)
(JSON defaults via nlohmann_json), and the [`exception`](../include/numeraire/utils/exception.hpp)
types. The [`enums`](../include/numeraire/enums/enums.hpp) module holds domain `enum class` types;
[`quantlib_bridge`](../include/numeraire/utils/quantlib_bridge.hpp) maps them to QuantLib calendars,
frequencies, day counters, conventions, currencies, and option/exercise enums. Executables link
`numeraire_utils` directly (which **PUBLIC**-ly depends on `numeraire_enums` and QuantLib).

The **`numeraire_schedule`** library ([`src/schedule/CMakeLists.txt`](../src/schedule/CMakeLists.txt)) wraps
QuantLib schedule generation: [`Schedule`](../include/numeraire/schedule/schedule.hpp) (date list + day-count),
[`ScheduleConfig`](../include/numeraire/schedule/schedule_config.hpp), [`ScheduleGenerator`](../include/numeraire/schedule/schedule_generator.hpp)
(builder + `Generate` / `YearFraction` / `IsBusinessDay` / `Adjust`), plus [`ScheduleToQuantLib`](../include/numeraire/schedule/schedule_quantlib.hpp) /
`ScheduleFromQuantLib`. It links **`numeraire_utils`** for `quantlib_bridge` only (QuantLib stays out of `core`).

### `ScheduleGenerator` dependencies

[`ScheduleGenerator`](../include/numeraire/schedule/schedule_generator.hpp) is a thin façade: it owns a
[`ScheduleConfig`](../include/numeraire/schedule/schedule_config.hpp) (domain enums only in the public header) and,
in the implementation TU, maps that config to QuantLib via [`quantlib_bridge`](../include/numeraire/utils/quantlib_bridge.hpp)
and [`ScheduleFromQuantLib`](../include/numeraire/schedule/schedule_quantlib.hpp). Call sites receive a lightweight
[`Schedule`](../include/numeraire/schedule/schedule.hpp) (dates + `DayCount` tag), not a `QuantLib::Schedule`.

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

**`Generate(start, end)`** (conceptual sequence):

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
`Schedule` / `YearFraction` / `Adjust` alongside `IModel` and market data. Note: [`date.hpp`](../include/numeraire/schedule/date.hpp)
includes QuantLib’s date type for conversions at the schedule module boundary.

### Trade row → `ScheduleConfig` (database first, then JSON fallback)

Schedule fields may live on the trade row (e.g. optional `schedule_*` columns on `wiener_tradebook`). **One place**
must own the merge rule: **use DB strings when present; otherwise fall back to** [`configs/default.json`](../configs/default.json)
under `schedule` (`default_calendar`, `default_frequency`, …), read through [`Config`](../include/numeraire/utils/config.hpp).

- **Persistence** — SQLite (or any repository) returns a small DTO with `std::optional<std::string>` per column; it does **not**
  implement fallback logic.
- **Config** — supplies default strings from JSON; it does **not** know about SQL.
- **`ScheduleConfigResolver` (target)** — lives in the **`schedule`** module; parses optional DB strings to enums and fills gaps
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

## Sprint plan (Stage 1)

| Sprint | Deliverables |
|--------|--------------|
| 0 | Build system + layout + style enforcement |
| 1 | `utils`: Logger (spdlog facade), Exception hierarchy (done) |
| 2 | `utils`: EnvLoader, Config (nlohmann_json wrapper) — **done** |
| 3 | `enums` + `utils/quantlib_bridge` — **done** |
| 3.5 | `enums`: [`ModelType`](../include/numeraire/enums/model_type.hpp) vs [`PricingEngineType`](../include/numeraire/enums/pricing_engine_type.hpp) (model / dynamics family vs numerical pricing engine) — **done** |
| 4 | `schedule`: Schedule POD + ScheduleGenerator (QuantLib internally), UT vs raw QuantLib benchmark — **done** |
| 5 | `core`: interfaces (IProduct/IPricer/IModel/IMarketData), PricingEngine, PricingResult |
| 6 | `core`: factories (ProductFactory, PricerFactory) |
| 7 | `database`: TradeDto, ITradeRepository, InMemoryTradeRepository (SQLite waits for schema) |
| 8 | `market_data`: MarketSnapshot, IMarketDataProvider, StaticMarketDataProvider (Polygon waits) |
| 9 | Polish: CI, tightening checks as needed |
