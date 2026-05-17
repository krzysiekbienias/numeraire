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
| [`scripts/`](scripts/) | `setup_macos.sh`, `build.sh`, `test.sh`, `format.sh`, `clean.sh`, [`import_trade_bundle.py`](scripts/import_trade_bundle.py) |
| [`trades/incoming/`](trades/incoming/) | Draft trade bundle JSON for import (only [`trade_bundle.sample.json`](trades/incoming/trade_bundle.sample.json) tracked; other `*.json` ignored) |
| [`configs/`](configs/) | JSON defaults loaded by [`utils::Config`](include/numeraire/utils/config.hpp) |
| [`docs/`](docs/) | [`architecture.md`](docs/architecture.md), [`development.md`](docs/development.md) (sprints / stages), [`coding_style.md`](docs/coding_style.md) |

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
| **Price trades** (SQLite + env quotes) | `<trade_id>`, `--all`, `--trades-json`, or env `NUMERAIRE_DEV_TRADE_ID` | Reads `trades` + catalog; NPV via Black–Scholes |

**Dispatch:** ingest handlers are checked **in order**: `--fetch-index-eod-daily` → `--fetch-eod-daily` → `--fetch-option-contracts`. The first matching branch runs and exits. If argv matches none of those, `dev_main` opens the SQLite repo and runs **pricing**.

Always run from the **repository root** so `.env` and `configs/default.json` resolve. Full flags for ingest are printed on failure or via `./build/dev_main --help`.

#### Pricing trades from SQLite

The binary loads `.env` (via [`EnvLoader`](include/numeraire/utils/env_loader.hpp)),
opens the DB at [`NUMERAIRE_DB_PATH`](.env.example) (default [`db.sqlite3`](.gitignore)),
applies [`sql/schema_v1.sql`](sql/schema_v1.sql) if tables are missing, then
loads a **trade header + all legs** from SQLite and runs **analytic Black–Scholes**
per leg, summing book NPV (`sign × quantity × unit NPV`) using synthetic quotes
from the `NUMERAIRE_DEV_*` variables in `.env` (quotes are **not** yet pulled from `equity_daily_eod`; see [`docs/architecture.md`](docs/architecture.md)).

- **Which trades**: `dev_main <trade_id>` — one id; `dev_main --all` — every row in
  `trades` (sorted); `dev_main --trades-json path.json` — list as
  `["TRD_1",...]` or `{"trade_ids":[...]}` (see
  [`trades/incoming/pricing_batch.sample.json`](trades/incoming/pricing_batch.sample.json)).
  With no arguments, `NUMERAIRE_DEV_TRADE_ID` from [.env](.env.example) if set.
- **Market**: `NUMERAIRE_DEV_SPOT`, `NUMERAIRE_DEV_RATE`, `NUMERAIRE_DEV_VOL`,
  `NUMERAIRE_DEV_DIV_YIELD` — default spot/dividend are applied per underlying
  found on the trade’s legs.

Examples:

```bash
./build/dev_main TRD_001
./build/dev_main --all
./build/dev_main --trades-json trades/incoming/pricing_batch.sample.json
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

1. **Pricing / models** — more instruments and engines via `ModelType` / `PricingEngineType`; wire persisted quotes into `IMarketData` (today: Polygon bars in SQLite; `dev_main` NPV still uses env/static snapshot).
2. **Later** — optional Python bindings, service layer / web tier if needed.

---

## License

(To be chosen.)
