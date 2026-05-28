-- Seed `market_data_prep_scope` for daily_market_prep (idempotent: OR REPLACE).
--
-- Each row toggles pipeline steps independently (0/1) — NOT all-or-nothing.
-- Apply after schema:  sqlite3 db.sqlite3 < sql/schema_v1.sql
--                      sqlite3 db.sqlite3 < sql/seed_market_data_prep_scope.sql
--
-- Flag dependency cheat-sheet (today's dev_main behaviour):
--   build_vol_surface_eod  → needs fetch_option_daily_price_eod + option quotes in DB
--   fetch_option_daily_price_eod → needs option_universe_eod (build_option_universe)
--   build_option_universe → needs option_contract + index/equity spot for grid
--   build_vol_surface_eod (NDX) → needs index_daily_eod on provider_symbol (I:NDX)
--   Single-name vol from equity spot is NOT wired yet (index close only in builder).
PRAGMA foreign_keys = ON;
INSERT OR REPLACE INTO market_data_prep_scope (
        scope_id,
        instrument_id,
        provider_symbol,
        asset_class,
        ingest_from_date,
        ingest_to_date,
        is_active,
        ingest_index_eod,
        ingest_equity_eod,
        ingest_option_contracts,
        build_option_universe,
        fetch_option_daily_price_eod,
        build_vol_surface_eod,
        option_underlying_id,
        option_grid_config_name,
        notes
    )
VALUES -- Full NDX option pipeline + implied-vol surface (production path for index vol).
    (
        'NDX_EOD',
        'NDX',
        'I:NDX',
        'INDEX',
        '2026-01-02',
        NULL,
        1,
        1,
        0,
        1,
        1,
        1,
        1,
        'NDX',
        'default_index_option_universe',
        'Index spot (I:NDX) + parametric option universe + vol surface'
    ),
    -- Listed names: equity daily bars only by default (flip option_* flags per name as needed).
    (
        'AAPL_EOD',
        'AAPL',
        'AAPL',
        'EQUITY',
        '2026-01-02',
        NULL,
        1,
        0,
        1,
        0,
        0,
        0,
        0,
        NULL,
        'default_index_option_universe',
        'Equity EOD only; enable ingest_option_contracts=1 for chain catalog'
    ),
    (
        'MSFT_EOD',
        'MSFT',
        'MSFT',
        'EQUITY',
        '2026-01-02',
        NULL,
        1,
        0,
        1,
        0,
        0,
        0,
        0,
        NULL,
        'default_index_option_universe',
        'Equity EOD only'
    ),
    (
        'GOOGL_EOD',
        'GOOGL',
        'GOOGL',
        'EQUITY',
        '2026-01-02',
        NULL,
        1,
        0,
        1,
        0,
        0,
        0,
        0,
        NULL,
        'default_index_option_universe',
        'Equity EOD only'
    ),
    (
        'NVDA_EOD',
        'NVDA',
        'NVDA',
        'EQUITY',
        '2026-01-02',
        NULL,
        1,
        0,
        1,
        0,
        0,
        0,
        0,
        NULL,
        'default_index_option_universe',
        'Equity EOD only'
    );
-- Example: option catalog ONLY for one name (no prices, no surface):
--   UPDATE market_data_prep_scope SET
--     ingest_equity_eod = 0,
--     ingest_option_contracts = 1,
--     build_option_universe = 0,
--     fetch_option_daily_price_eod = 0,
--     build_vol_surface_eod = 0,
--     option_underlying_id = 'NVDA'
--   WHERE scope_id = 'NVDA_EOD';
--
-- Example: contracts + universe + prices + surface for NDX (already on in NDX_EOD row).
-- To disable a step for NDX only:
--   UPDATE market_data_prep_scope SET fetch_option_daily_price_eod = 0 WHERE scope_id = 'NDX_EOD';
