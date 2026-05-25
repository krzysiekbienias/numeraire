# Numeraire++

Modular C++20 framework for derivative pricing: clear separation between
instruments, pricing engines, models and market data, with QuantLib-backed
schedule generation where it belongs.

This repository is also deployed on **Hetzner** (Linux): we run two server-side
environments, **development** and **production**, so feature work can be
validated before promoting config, binaries, and data to the live tier. Paths,
service names, and credentials are environment-specific and are **not**
checked into this repo (use `.env` locally and your orchestration secrets on
the host).

Sprint history and what is actually shipped (including SQLite schema and Polygon ingest) live in **[`docs/development.md`](docs/development.md)** so this README stays oriented toward setup and daily use.

---

## Repository layout

| Path | Purpose |
|------|---------|
| [`include/numeraire/`](include/numeraire/) | Public headers: [`enums/`](include/numeraire/enums/), [`utils/`](include/numeraire/utils/), [`schedule/`](include/numeraire/schedule/), … |
| [`src/`](src/) | Implementation TUs per module ([`src/utils/`](src/utils/), [`src/schedule/`](src/schedule/), …) |
| [`app/`](app/) | `app` (CLI placeholder) and `dev_main` (pricing + Polygon ingest modes) |
| [`unit_tests/`](unit_tests/) | GoogleTest sources (`test_*.cpp`, including per-module dirs) |
| [`integration_tests/`](integration_tests/) | Placeholder for I/O-heavy tests (DB, Polygon, cache) |
| [`cmake/`](cmake/) | `NumeraireCompileOptions.cmake`, `NumeraireDependencies.cmake` |
| [`scripts/`](scripts/) | `setup_macos.sh`, `build.sh`, `test.sh`, `format.sh`, `clean.sh`, [`import_trade_bundle.py`](scripts/import_trade_bundle.py), [`daily_dev_eod.sh`](scripts/daily_dev_eod.sh) (Hetzner cron) |
| [`trades/incoming/`](trades/incoming/) | Draft trade bundle JSON for import (only [`trade_bundle.sample.json`](trades/incoming/trade_bundle.sample.json) tracked; other `*.json` ignored) |
| [`configs/`](configs/) | JSON defaults loaded by [`utils::Config`](include/numeraire/utils/config.hpp) |
| [`docs/`](docs/) | [`architecture.md`](docs/architecture.md), [`development.md`](docs/development.md) (sprints / stages), [`mathematical_background.md`](docs/mathematical_background.md), [`coding_style.md`](docs/coding_style.md) |

---

## Requirements (macOS, Homebrew)

Install once:

```bash
./scripts/setup_macos.sh
```

That pulls **CMake**, **Ninja**, **QuantLib**, **spdlog**, **fmt**,
**nlohmann-json**, **SQLite**, **GoogleTest**, and **LLVM** (for
`clang-format` / `clang-tidy`).

Add LLVM to your `PATH` so tools resolve consistently (example for Apple
silicon Homebrew):

```bash
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
```

Copy environment template:

```bash
cp .env.example .env
```

---

## Configure and build

```bash
./scripts/build.sh          # Debug, output in ./build/
./scripts/build.sh Release  # Release
```

Or manually:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

Binaries (with default options): `build/app`, `build/dev_main`,
`build/test_environment`.

Run `app` / `dev_main` from the **repository root** so relative paths resolve
(`.env`, `configs/default.json`). [`scripts/test.sh`](scripts/test.sh) already
`cd`s to the root before invoking `test_environment`.

### `dev_main`: pricing + Polygon ingest

[`dev_main`](app/dev_main.cpp) is one executable with **four capabilities**:

| Mode | argv pattern | Writes / reads |
|------|----------------|----------------|
| **Index daily EOD** (Polygon) | `--fetch-index-eod-daily …` | Upserts [`index_daily_eod`](sql/schema_v1.sql) |
| **Equity daily EOD** (Polygon) | `--fetch-eod-daily …` | Upserts [`equity_daily_eod`](sql/schema_v1.sql) |
| **Option contracts reference** (Polygon) | `--fetch-option-contracts …` | Upserts [`option_contract`](sql/schema_v1.sql) |
| **Price trades** (SQLite + env quotes) | `--as-of` + `<trade_id>`, `--all`, `--trades-json`, or env `NUMERAIRE_DEV_TRADE_ID` | Reads `trades` + catalog; NPV via analytic composite pricer; optional MTM rows in `trade_leg_mtm_eod` |

**Dispatch:** ingest handlers are checked **in order**: `--fetch-index-eod-daily` → `--fetch-eod-daily` → `--fetch-option-contracts`. The first matching branch runs and exits. If argv matches none of those, `dev_main` opens the SQLite repo and runs **pricing**.

Always run from the **repository root** so `.env` and `configs/default.json` resolve. Full flags for ingest are printed on failure or via `./build/dev_main --help`.

#### Pricing trades from SQLite

The binary loads `.env` (via [`EnvLoader`](include/numeraire/utils/env_loader.hpp)),
opens the DB at [`NUMERAIRE_DB_PATH`](.env.example) (default [`db.sqlite3`](.gitignore)),
applies [`sql/schema_v1.sql`](sql/schema_v1.sql) if tables are missing, then
loads a **trade header + all legs** from SQLite and runs **analytic closed-form pricing**
per leg (composite pricer: Black–Scholes for options/binaries, dedicated forward engine for equity forwards), summing book NPV (`sign × quantity × unit NPV`) using quotes from
`NUMERAIRE_DEV_*` (and optionally **`equity_daily_eod`** for spot). See
[`docs/architecture.md`](docs/architecture.md) for `IMarketData::ValuationDate()` and MTM persistence.

- **Valuation date (required)** — Every pricing run needs calendar **as-of**
  **`--as-of YYYY-MM-DD`** or **`NUMERAIRE_DEV_AS_OF`** (often in `.env`). Without a
  valid ISO date, `dev_main` exits with an error. The date is stored on
  [`MarketSnapshot::valuation_date`](include/numeraire/market_data/market_snapshot.hpp)
  and exposed as [`IMarketData::ValuationDate()`](include/numeraire/core/imarket_data.hpp).
  **Time to expiry** (`years_to_maturity` in MTM, and \(\tau\) in all analytic pricers)
  uses **Act/365 Fixed from valuation date to product expiry**, not from trade date.
  [`PricerFactory`](include/numeraire/pricers/pricer_factory.hpp) returns
  [`AnalyticCompositePricer`](include/numeraire/pricers/analytic_composite_pricer.hpp), which routes to
  [`AnalyticBlackScholesEquityPricer`](include/numeraire/pricers/analytic_black_scholes_equity_pricer.hpp) or
  [`AnalyticForwardPricer`](include/numeraire/pricers/analytic_forward_pricer.hpp) by product type — see
  [`docs/architecture.md`](docs/architecture.md) § *Analytic pricer layer*.
- **Which trades** — `dev_main [--as-of DATE] …`: **`--as-of`** may appear in any
  position alongside `<trade_id>`, `--all`, or `--trades-json`
  (e.g. `dev_main --as-of 2026-05-01 TRD_001`).
- **Synthetic vs DB spot** — **`NUMERAIRE_DEV_SPOT_SOURCE=env`** (default):
  **`NUMERAIRE_DEV_SPOT`** per underlying. **`NUMERAIRE_DEV_SPOT_SOURCE=db`**:
  **`equity_daily_eod.close`** on the valuation date (`timespan='1d'`;
  **`NUMERAIRE_DEV_SPOT_ADJUSTED`**, default `1`). **`NUMERAIRE_DEV_RATE`**,
  **`NUMERAIRE_DEV_VOL`**, **`NUMERAIRE_DEV_DIV_YIELD`** remain env-driven.
- **MTM** — When a valuation date is set, one row per leg is written to
  **`trade_leg_mtm_eod`** (official) plus **`trade_leg_mtm_eod_archive`** (append-only
  per `batch_run_id`). **`--as-of` date range batching** is not built into `dev_main`;
  use a shell loop — see [`docs/development.md`](docs/development.md) § *Batch `--as-of` reruns*.

Examples:

```bash
# Requires --as-of or NUMERAIRE_DEV_AS_OF (e.g. from .env)
./build/dev_main --as-of 2026-05-15 TRD_001
NUMERAIRE_DEV_SPOT_SOURCE=db ./build/dev_main --as-of 2026-05-15 TRD_001
./build/dev_main --as-of 2026-05-15 --all
./build/dev_main --trades-json trades/incoming/pricing_batch.sample.json --as-of 2026-05-15
```

Check `years_to_maturity` moves with `as_of`:

```bash
sqlite3 db.sqlite3 "
SELECT as_of, leg_id, years_to_maturity, underlying_spot, pv_unit
FROM trade_leg_mtm_eod WHERE trade_id = 'TRD_10001' ORDER BY as_of;
"
```

You need matching rows in `trades`, `trade_legs`, `products`, and `products_equity`. The
inserts in [`unit_tests/database/test_sqlite_trade_repository.cpp`](unit_tests/database/test_sqlite_trade_repository.cpp)
(`TRD_001` / `P_AAPL_001`) are a convenient reference when seeding `db.sqlite3`.

To load a **full bundle** (product + `products_equity` + trade header + legs) from JSON on a
host where only the DB is available, use Python 3 stdlib only:

```bash
python3 scripts/import_trade_bundle.py trades/incoming/trade_bundle.sample.json
```

Uses `NUMERAIRE_DB_PATH` when set (same as `dev_main`), or `--db path/to/db.sqlite3`.
Tables must already exist (`sql/schema_v1.sql`; `dev_main` applies it when missing).

#### Polygon ingest (optional HTTP jobs)

Requires **`POLYGON_API_KEY`** in `.env`. Optional: **`POLYGON_BASE_URL`** (default `https://api.polygon.io`), **`NUMERAIRE_POLYGON_SLEEP_SEC_AFTER_CALL`** (throttle between requests). Equity ingest accepts **`--raw`** for unadjusted bars (`adjusted=false`). Implementation lives under [`src/market_data_providers/`](src/market_data_providers/).

**Suggested order for option-chain ingest:** load **index** daily bars first (`index_daily_eod`), then **`--fetch-option-contracts`**, which reads index **close** from SQLite for the strike window.

```bash
# Listed equities → equity_daily_eod (repeat --ticker for more symbols)
./build/dev_main --fetch-eod-daily --from 2025-01-02 --to 2025-01-31 --ticker AAPL

# Indices → index_daily_eod (defaults to I:SPX if you omit --ticker)
./build/dev_main --fetch-index-eod-daily --from 2025-01-02 --to 2025-01-31 --ticker I:NDX

# Options reference → option_contract (--underlying / --index-ticker / --strike-band per --help)
./build/dev_main --fetch-option-contracts --from 2025-01-02 --to 2025-01-02 --underlying NDX
```

**Backfill many equities from `universe_instrument`** (one `dev_main` call, all active Polygon equity symbols) — see [`docs/development.md`](docs/development.md) § *Polygon EOD backfill (`universe_instrument`)*.

---

## Tests

```bash
./scripts/test.sh                      # all unit tests
./scripts/test.sh LoggerTest         # suite filter: LoggerTest.*
./scripts/test.sh LoggerTest.ReInitIsSafe  # single test
```

Some I/O-heavy unit tests (e.g. [`unit_tests/utils/test_env_loader.cpp`](unit_tests/utils/test_env_loader.cpp),
[`unit_tests/utils/test_config.cpp`](unit_tests/utils/test_config.cpp)) create small `.env` / JSON files under the
**system temp directory** (`std::filesystem::temp_directory_path()`, e.g. macOS `$TMPDIR`) in uniquely named
subfolders. They are **not removed** when the test binary exits; look for prefixes like
`numeraire_env_loader_ut_` or `numeraire_config_ut_` under `$TMPDIR` if you need to inspect leftovers.
From a terminal (macOS/Linux), run `echo "$TMPDIR"` to see the base path, then e.g.
`find "$TMPDIR" -maxdepth 3 -type d -name 'numeraire_*_ut_*'` to list those folders.

Integration tests compile only after you add `integration_tests/test_*.cpp`.

---

## Logging

Call `Logger::Init()` once near process entry (see [`app/main.cpp`](app/main.cpp);
there we use `using numeraire::utils::Logger` to keep call sites short). Default
level is **info** unless `NUMERAIRE_LOG_LEVEL` is set (`trace`, `debug`, `info`,
`warn`, `error`, `critical`, case-insensitive). Log with the static helpers
`Logger::NumInfo`, `Logger::NumWarn`, … from
[`include/numeraire/utils/logger.hpp`](include/numeraire/utils/logger.hpp)
(`fmt::format_string`-checked format strings).

---

## Configuration and environment

- **`.env`** — optional dotenv-style file at the repo root. [`EnvLoader`](include/numeraire/utils/env_loader.hpp)
  parses `KEY=value` lines (`#` comments; trim). [`EnvLoader::Get`](include/numeraire/utils/env_loader.hpp)
  returns a non-empty **`std::getenv(key)`** value when present, otherwise the value from the file
  (real process environment wins over `.env`). Call **`ApplyToEnvironment()`** before `Logger::Init()` if you
  want `NUMERAIRE_LOG_LEVEL` and similar to be picked up from `.env` on POSIX (no-op on Windows).
- **`configs/default.json`** — committed defaults. [`Config::Load`](include/numeraire/utils/config.hpp)
  reads the file; [`RequireString`](include/numeraire/utils/config.hpp) / `RequireAt` walk dotted paths
  such as `logging.level`. Missing file or invalid JSON throws `numeraire::ConfigError`.

---

## Enums and QuantLib bridge

Domain `enum class` types (calendars, day counts, currencies, option style, …)
live under [`include/numeraire/enums/`](include/numeraire/enums/) (umbrella
[`enums.hpp`](include/numeraire/enums/enums.hpp)). [`quantlib_bridge`](include/numeraire/utils/quantlib_bridge.hpp)
maps them to QuantLib calendars, frequencies, conventions, and related types for schedule generation and future pricers.

---

## Model vs pricing engine

We split **what world you assume** from **how you price in that world**:

- **`ModelType`** ([`model_type.hpp`](include/numeraire/enums/model_type.hpp)) — model / dynamics family (e.g. `kBlackScholes`, `kHeston`). Framework taxonomy; QuantLib mapping stays per concrete engine later.
- **`PricingEngineType`** ([`pricing_engine_type.hpp`](include/numeraire/enums/pricing_engine_type.hpp)) — numerical engine (e.g. `kAnalytic`, `kMonteCarlo`, `kBinomialTree`, `kFiniteDifference`).

Factories and config can combine a model with an engine when the pair is valid.

---

## Schedules and generation

The `schedule` module separates **data** from **generation logic**:

- **`numeraire::schedule::Schedule`** — small POD (`std::vector<Date>` plus `DayCount`). Pass this across modules; it does not depend on QuantLib.
- **`numeraire::schedule::ScheduleGenerator`** — uses QuantLib internally with a [`ScheduleConfig`](include/numeraire/schedule/schedule_config.hpp) (calendar, generation rule, frequency, …) and returns the lightweight `Schedule`.
- **Bridge** ([`schedule_quantlib.hpp`](include/numeraire/schedule/schedule_quantlib.hpp)) — `ScheduleToQuantLib` / `ScheduleFromQuantLib` when a pricer needs a full `QuantLib::Schedule`.

---

## Code style

- **Formatting**: [`.clang-format`](.clang-format) — Google-based, 4-space
  indent, 120 column limit. Run `./scripts/format.sh` (or `./scripts/format.sh
  --check` in CI).
- **Naming / static analysis**: [`.clang-tidy`](.clang-tidy) — Google naming
  via `readability-identifier-naming`, plus curated bug/perf/modernize checks.
  Editors: use **clangd** with `--clang-tidy` (see
  [`.vscode/settings.json`](.vscode/settings.json)).

Full rules: [`docs/coding_style.md`](docs/coding_style.md).

Architecture and module graph: [`docs/architecture.md`](docs/architecture.md).

---

## Visual Studio Code / Cursor

Shared launch configs: [`.vscode/launch.json`](.vscode/launch.json) — debug
`app`, `dev_main`, or `test_environment` under LLDB.

Recommended extensions are listed in [`.vscode/extensions.json`](.vscode/extensions.json).

Point **clangd** at `compile_commands.json` generated in `build/` (the
settings file already passes `--compile-commands-dir=${workspaceFolder}/build`).

---

## Roadmap (high level)

See **[`docs/development.md`](docs/development.md)** for sprint/table status (what is done vs pending).

1. **Pricing / models** — more instruments and engines via `ModelType` / `PricingEngineType`; richer DB-backed quotes on `IMarketData` (today: `ValuationDate` + static snapshot in `dev_main`; spot-from-DB optional; rate/vol still env).
2. **Later** — optional Python bindings, service layer / web tier if needed.

---

## License

(To be chosen.)
