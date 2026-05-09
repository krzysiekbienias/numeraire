# Integration tests

This directory hosts integration tests that exercise components which cannot
be tested in pure isolation. Examples:

- **SQLite trade repository** (Stage 2+) — real database round-trips against
  the on-disk schema imported from the previous project.
- **Polygon.io market data provider** (Stage 3+) — real HTTP calls, gated by
  the `POLYGON_API_KEY` environment variable.
- **Market environment cache** (Stage 3+) — file system + DB interactions for
  the cache layer used by repeatable simulations.

Sprint 0 ships only this placeholder so the CMake structure is wired up and
ready to compile real tests as soon as the corresponding modules land.

## Adding a test later

1. Drop a `test_*.cpp` file anywhere under `integration_tests/` (root or a
   per-feature subdirectory of your choice).
2. Re-run CMake configure (`scripts/build.sh`); the file is picked up
   automatically via `CONFIGURE_DEPENDS`.
3. Run the resulting `integration_test_environment` binary directly, or via
   `ctest --output-on-failure -L integration` once we add labels.
