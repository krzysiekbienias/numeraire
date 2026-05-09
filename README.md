# Numeraire++

Modular C++20 framework for derivative pricing: clear separation between
instruments, pricing engines, models and market data, with QuantLib-backed
schedule generation where it belongs.

Stage 1 is being built sprint-by-sprint. This README reflects **Sprint 0**
(layout, CMake modules, tooling), **Sprint 1** (`numeraire_utils`: logging and
the shared exception hierarchy), **Sprint 2** (`.env` via `EnvLoader` +
`configs/default.json` via `Config`), and **Sprint 3** ([`enums`](include/numeraire/enums/enums.hpp)
plus [`quantlib_bridge`](include/numeraire/utils/quantlib_bridge.hpp) to QuantLib).

---

## Repository layout

| Path | Purpose |
|------|---------|
| [`include/numeraire/`](include/numeraire/) | Public headers: [`enums/`](include/numeraire/enums/), [`utils/`](include/numeraire/utils/), … |
| [`src/`](src/) | Implementation translation units (per-module, e.g. [`src/utils/`](src/utils/)) |
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

## Logging (Sprint 1)

Call `Logger::Init()` once near process entry (see [`app/main.cpp`](app/main.cpp);
there we use `using numeraire::utils::Logger` to keep call sites short). Default
level is **info** unless `NUMERAIRE_LOG_LEVEL` is set (`trace`, `debug`, `info`,
`warn`, `error`, `critical`, case-insensitive). Log with the static helpers
`Logger::NumInfo`, `Logger::NumWarn`, … from
[`include/numeraire/utils/logger.hpp`](include/numeraire/utils/logger.hpp)
(`fmt::format_string`-checked format strings).

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

1. **Sprint 1+** — `utils` (logging, exceptions, env, config), then enums,
   QuantLib bridge, schedule wrapper, core interfaces and factories.
2. **Later** — SQLite trade repository (after schema review), Polygon market
   data, concrete pricers (analytical / MC / tree), optional Python bindings
   and a web tier.

---

## License

(To be chosen — not set in Sprint 0.)
