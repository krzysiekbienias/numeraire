# Numeraire++

Modular C++20 framework for derivative pricing: clear separation between
instruments, pricing engines, models and market data, with QuantLib-backed
schedule generation where it belongs.

Stage 1 is being built sprint-by-sprint. This README reflects **Sprint 0**
(build layout, CMake modules, tooling, placeholders for future libraries).

---

## Repository layout

| Path | Purpose |
|------|---------|
| [`include/numeraire/`](include/numeraire/) | Public headers (one module subtree per slice) |
| [`src/`](src/) | Implementation translation units (+ temporary `core_lib.cpp` anchor) |
| [`app/`](app/) | `app` (CLI placeholder) and `dev_main` (sandbox) |
| [`unit_tests/`](unit_tests/) | GoogleTest sources (`test_*.cpp`, including per-module dirs) |
| [`integration_tests/`](integration_tests/) | Placeholder for I/O-heavy tests (DB, Polygon, cache) |
| [`cmake/`](cmake/) | `NumeraireCompileOptions.cmake`, `NumeraireDependencies.cmake` |
| [`scripts/`](scripts/) | `setup_macos.sh`, `build.sh`, `test.sh`, `format.sh`, `clean.sh` |
| [`configs/`](configs/) | JSON defaults (wired up when `utils::Config` lands) |
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

---

## Tests

```bash
./scripts/test.sh                # all unit tests
./scripts/test.sh Smoke          # suite filter: Smoke.*
./scripts/test.sh Smoke.ItRuns # single test
```

Integration tests compile only after you add `integration_tests/test_*.cpp`.

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
