# SQL reference

## Schema

- **`schema_v1.sql`** — full DDL (run on empty DB or via `dev_main` bootstrap).

## Seeds

- **`seed_market_data_prep_scope.sql`** — scope rows for `daily_market_prep.sh` (GOOGL, NVDA, AAPL, MSFT, NDX).

## Cron (two jobs)

```bash
./scripts/daily_market_prep.sh   # all ingest (scope + book underlyings not in scope)
./scripts/daily_book_mtm.sh      # LIVE trades MTM (spot/vol from DB; no booking)
```

```bash
sqlite3 "${NUMERAIRE_DB_PATH:-db.sqlite3}" < sql/schema_v1.sql
sqlite3 "${NUMERAIRE_DB_PATH:-db.sqlite3}" < sql/seed_market_data_prep_scope.sql
```

### Per-ticker pipeline flags (`market_data_prep_scope`)

Each column is **0 or 1** per `scope_id` — not all-or-nothing.

| Flag | `dev_main` step |
|------|-----------------|
| `ingest_index_eod` | `--fetch-index-eod-daily` (`provider_symbol`, e.g. `I:NDX`) |
| `ingest_equity_eod` | `--fetch-eod-daily` (`provider_symbol`, e.g. `AAPL`) |
| `ingest_option_contracts` | `--fetch-option-contracts` (`option_underlying_id`) |
| `build_option_universe` | `--build-option-universe` |
| `fetch_option_daily_price_eod` | `--fetch-option-daily-price-eod` |
| `build_vol_surface_eod` | `--build-vol-surface-eod` |

**Vol surface today** uses **`index_daily_eod`** close for spot (`BuildVolSurfaceEod`); the shipped path is **NDX + `I:NDX`**. Turning on `build_vol_surface_eod` for a single stock requires a future builder change (equity close).

**Deactivate** a name without deleting: `UPDATE market_data_prep_scope SET is_active = 0 WHERE scope_id = 'MSFT_EOD';`

**Date window:** cron `as_of` must satisfy `ingest_from_date <= as_of` and (`ingest_to_date` IS NULL OR `ingest_to_date >= as_of`).

**Book catch-up:** after scope rows, `daily_market_prep.sh` fetches equity EOD for any `trade_legs` underlying not covered by an active `ingest_equity_eod=1` scope row (`NUMERAIRE_PREP_SKIP_BOOK_EQUITY=1` to disable).

## Par yield curve (FRED)

| Table | Role |
|-------|------|
| `par_curve_eod` | One par-yield snapshot per `(curve_id, as_of)` — header (currency, day_count, source) |
| `par_curve_point_eod` | Pillars: `tenor`, `quoted_rate` (decimal par quote), `fred_series_id`, `instrument_type` |

Default `curve_id`: **`USD_TREASURY_PAR_FRED`**. Link from prep scope via `discount_curve_id` + `discount_curve_source` (`SQLITE` / `FRED` when wired).

**Existing DB** (par tables missing, or legacy `discount_curve_*` to remove):

```bash
sqlite3 "${NUMERAIRE_DB_PATH:-db.sqlite3}" < sql/apply_par_curve_fred.sql
```

Legacy tables only (no par tables yet):

```bash
sqlite3 "${NUMERAIRE_DB_PATH:-db.sqlite3}" < sql/drop_legacy_discount_curve.sql
```

**Ingest** (after `FRED_API_KEY` in `.env`):

```bash
python3 scripts/fetch_fred_treasury_par_yields.py --as-of 2026-05-27
```

Bootstrap discount factors from quoted pillars is a follow-up (C++ or script); v1 only stores market par quotes.
