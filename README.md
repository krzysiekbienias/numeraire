# Numeraire++

Modular C++20 framework for derivative pricing: clear separation between
instruments, pricing engines, models and market data, with QuantLib-backed
schedule generation where it belongs.

Stage 1 is being built sprint-by-sprint. This README reflects the following
**in order**:

| Sprint | Scope |
|--------|--------|
| **0** | Layout, CMake modules, tooling |
| **1** | `numeraire_utils`: logging + shared exception hierarchy |
| **2** | `.env` via [`EnvLoader`](include/numeraire/utils/env_loader.hpp) + `configs/default.json` via [`Config`](include/numeraire/utils/config.hpp) |
| **3** | [`enums`](include/numeraire/enums/enums.hpp) + [`quantlib_bridge`](include/numeraire/utils/quantlib_bridge.hpp) (domain types ↔ QuantLib) |
| **3.5** | [`ModelType`](include/numeraire/enums/model_type.hpp) vs [`PricingEngineType`](include/numeraire/enums/pricing_engine_type.hpp) — model family vs numerical pricing engine |
| **4** | [`schedule`](include/numeraire/schedule/schedule_generator.hpp): lightweight [`Schedule`](include/numeraire/schedule/schedule.hpp) POD + `ScheduleGenerator` (QuantLib used internally to build dates) |

---

## Repository layout

| Path | Purpose |
|------|---------|
| [`include/numeraire/`](include/numeraire/) | Public headers: [`enums/`](include/numeraire/enums/), [`utils/`](include/numeraire/utils/), [`schedule/`](include/numeraire/schedule/), … |
| [`src/`](src/) | Implementation TUs per module ([`src/utils/`](src/utils/), [`src/schedule/`](src/schedule/), …) |
| [`app/`](app/) | `app` (CLI placeholder) and `dev_main` (sandbox) |
| [`unit_tests/`](unit_tests/) | GoogleTest sources (`test_*.cpp`, including per-module dirs) |
| [`integration_tests/`](integration_tests/) | Placeholder for I/O-heavy tests (DB, Polygon, cache) |
| [`cmake/`](cmake/) | `NumeraireCompileOptions.cmake`, `NumeraireDependencies.cmake` |
| [`scripts/`](scripts/) | `setup_macos.sh`, `build.sh`, `test.sh`, `format.sh`, `clean.sh` |
| [`configs/`](configs/) | JSON defaults loaded by [`utils::Config`](include/numeraire/utils/config.hpp) |
| [`docs/`](docs/) | Architecture and coding style |

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

## Logging (Sprint 1)

Call `Logger::Init()` once near process entry (see [`app/main.cpp`](app/main.cpp);
there we use `using numeraire::utils::Logger` to keep call sites short). Default
level is **info** unless `NUMERAIRE_LOG_LEVEL` is set (`trace`, `debug`, `info`,
`warn`, `error`, `critical`, case-insensitive). Log with the static helpers
`Logger::NumInfo`, `Logger::NumWarn`, … from
[`include/numeraire/utils/logger.hpp`](include/numeraire/utils/logger.hpp)
(`fmt::format_string`-checked format strings).

---

## Configuration and environment (Sprint 2)

- **`.env`** — optional dotenv-style file at the repo root. [`EnvLoader`](include/numeraire/utils/env_loader.hpp)
  parses `KEY=value` lines (`#` comments; trim). [`EnvLoader::Get`](include/numeraire/utils/env_loader.hpp)
  returns a non-empty **`std::getenv(key)`** value when present, otherwise the value from the file
  (real process environment wins over `.env`). Call **`ApplyToEnvironment()`** before `Logger::Init()` if you
  want `NUMERAIRE_LOG_LEVEL` and similar to be picked up from `.env` on POSIX (no-op on Windows).
- **`configs/default.json`** — committed defaults. [`Config::Load`](include/numeraire/utils/config.hpp)
  reads the file; [`RequireString`](include/numeraire/utils/config.hpp) / `RequireAt` walk dotted paths
  such as `logging.level`. Missing file or invalid JSON throws `numeraire::ConfigError`.

---

## Enums and QuantLib bridge (Sprint 3)

Domain `enum class` types (calendars, day counts, currencies, option style, …)
live under [`include/numeraire/enums/`](include/numeraire/enums/) (umbrella
[`enums.hpp`](include/numeraire/enums/enums.hpp)). [`quantlib_bridge`](include/numeraire/utils/quantlib_bridge.hpp)
maps them to QuantLib calendars, frequencies, conventions, and related types for schedule generation and future pricers.

---

## Model vs pricing engine (Sprint 3.5)

We split **what world you assume** from **how you price in that world**:

- **`ModelType`** ([`model_type.hpp`](include/numeraire/enums/model_type.hpp)) — model / dynamics family (e.g. `kBlackScholes`, `kHeston`). Framework taxonomy; QuantLib mapping stays per concrete engine later.
- **`PricingEngineType`** ([`pricing_engine_type.hpp`](include/numeraire/enums/pricing_engine_type.hpp)) — numerical engine (e.g. `kAnalytic`, `kMonteCarlo`, `kBinomialTree`, `kFiniteDifference`).

Factories and config can combine a model with an engine when the pair is valid.

---

## Schedules and generation (Sprint 4)

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

1. **Sprints 5–6** — `core`: interfaces (`IProduct`, `IPricer`, `IModel`, …), `PricingEngine`, `PricingResult`, factories (see [`docs/architecture.md`](docs/architecture.md)).
2. **Later** — SQLite trade repository (after schema review), Polygon market
   data, concrete pricers wired to `ModelType` / `PricingEngineType`, optional Python bindings
   and a web tier.

---

## License

(To be chosen — not set in Sprint 0.)
