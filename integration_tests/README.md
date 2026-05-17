# Integration tests

This directory hosts integration tests that exercise components which cannot
be tested in pure isolation. Examples:

- **SQLite trade repository** — round-trips against [`sql/schema_v1.sql`](../sql/schema_v1.sql) / [`SqliteTradeRepository`](../include/numeraire/database/sqlite_trade_repository.hpp); heavy paths today live mostly in unit tests + [`dev_main`](../README.md).
- **Polygon HTTP ingest** — real outbound REST calls (same stack as [`dev_main --fetch-*`](../README.md)), gated by `POLYGON_API_KEY`; candidate for `integration_tests/` once we add non-placeholder binaries here.
- **Market environment cache** (future) — file system + DB interactions for repeatable simulations.

Progress vs sprint labels is summarized in **[`docs/development.md`](../docs/development.md)**.

The CMake wiring below shipped early so adding `test_*.cpp` stays friction-free once tests land:

## Adding a test later

1. Drop a `test_*.cpp` file anywhere under `integration_tests/` (root or a
   per-feature subdirectory of your choice).
2. Re-run CMake configure (`scripts/build.sh`); the file is picked up
   automatically via `CONFIGURE_DEPENDS`.
3. Run the resulting `integration_test_environment` binary directly, or via
   `ctest --output-on-failure -L integration` once we add labels.
