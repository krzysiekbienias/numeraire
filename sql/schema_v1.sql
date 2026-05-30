-- Numeraire++ — reference SQLite schema (v1, trade legs + product catalog)
--
-- Purpose:
--   - Single place documenting table/column names the C++ repository layer expects
--   - Bootstrapping empty DBs; unit tests apply this to :memory: + INSERT fixtures
--
-- Application should use: PRAGMA foreign_keys = ON; after open.
-- Safe to run repeatedly: CREATE TABLE / INDEX IF NOT EXISTS (no DROP).
PRAGMA foreign_keys = ON;
CREATE TABLE IF NOT EXISTS products (
    product_id TEXT PRIMARY KEY,
    asset_kind TEXT NOT NULL,
    underlying_id TEXT NOT NULL,
    expiry_date TEXT,
    settlement TEXT NOT NULL DEFAULT 'CASH',
    currency TEXT NOT NULL DEFAULT 'USD',
    contract_size REAL NOT NULL DEFAULT 100.0,
    day_count TEXT NOT NULL DEFAULT 'Actual365Fixed',
    calendar TEXT NOT NULL DEFAULT 'UnitedStates'
);
CREATE TABLE IF NOT EXISTS products_equity (
    product_id TEXT PRIMARY KEY,
    instrument_type TEXT NOT NULL,
    option_type TEXT CHECK (
        option_type IS NULL
        OR option_type IN ('call', 'put')
    ),
    strike REAL,
    exercise_style TEXT DEFAULT 'european',
    structured_params TEXT NOT NULL DEFAULT '{}',
    FOREIGN KEY (product_id) REFERENCES products (product_id) ON DELETE CASCADE
);
CREATE TABLE IF NOT EXISTS trades (
    trade_id TEXT PRIMARY KEY,
    portfolio_id TEXT NOT NULL,
    strategy_type TEXT NOT NULL,
    booking_timestamp TEXT,
    trade_date TEXT,
    updated_at TEXT,
    status TEXT NOT NULL DEFAULT 'LIVE'
);
CREATE TABLE IF NOT EXISTS trade_legs (
    leg_id TEXT PRIMARY KEY,
    trade_id TEXT NOT NULL,
    product_id TEXT NOT NULL,
    direction TEXT NOT NULL CHECK (direction IN ('LONG', 'SHORT')),
    quantity REAL NOT NULL,
    execution_price REAL NOT NULL,
    commission REAL NOT NULL DEFAULT 0,
    FOREIGN KEY (trade_id) REFERENCES trades (trade_id) ON DELETE CASCADE,
    FOREIGN KEY (product_id) REFERENCES products (product_id)
);
CREATE INDEX IF NOT EXISTS idx_trade_legs_trade_id ON trade_legs (trade_id);
CREATE INDEX IF NOT EXISTS idx_trade_legs_product_id ON trade_legs (product_id);
-- ---------------------------------------------------------------------------
-- Controlled market-data / pricing universe (reference metadata, not trade products).
--
-- `instrument_id` — canonical key in Numeraire (align with `products.underlying_id` when possible).
-- `provider_symbol` — symbol passed to the ingest API (Polygon ticker, `I:NDX`, `C:EURUSD`, …).
-- `data_vendor` — primary vendor for this instrument's market data (not everything is Polygon).
-- Ingest scope: `is_active` + per-pipeline flags (`ingest_equity_eod`, `ingest_index_eod`, …).
-- No FK to `equity_daily_eod` / `products` — joins by convention on ticker / underlying_id.
CREATE TABLE IF NOT EXISTS universe_instrument (
    instrument_id TEXT PRIMARY KEY,
    provider_symbol TEXT NOT NULL,
    display_name TEXT,
    asset_class TEXT NOT NULL CHECK (
        asset_class IN (
            'EQUITY',
            'INDEX',
            'FX',
            'COMMODITY',
            'RATE',
            'BOND',
            'OTHER'
        )
    ),
    sector TEXT,
    industry TEXT,
    quote_currency TEXT NOT NULL DEFAULT 'USD',
    session_calendar TEXT NOT NULL DEFAULT 'America/New_York',
    country TEXT,
    data_vendor TEXT NOT NULL DEFAULT 'POLYGON',
    is_active INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0, 1)),
    ingest_equity_eod INTEGER NOT NULL DEFAULT 0 CHECK (ingest_equity_eod IN (0, 1)),
    ingest_index_eod INTEGER NOT NULL DEFAULT 0 CHECK (ingest_index_eod IN (0, 1)),
    ingest_priority INTEGER NOT NULL DEFAULT 100,
    notes TEXT,
    created_at TEXT,
    updated_at TEXT,
    UNIQUE (provider_symbol, asset_class)
);
CREATE INDEX IF NOT EXISTS idx_universe_instrument_active_equity ON universe_instrument (is_active, ingest_equity_eod);
CREATE INDEX IF NOT EXISTS idx_universe_instrument_data_vendor ON universe_instrument (data_vendor, is_active);
-- ---------------------------------------------------------------------------
-- Explicit scope for `daily_market_prep.sh` (not derived from `trade_legs`).
--
-- `instrument_id` — canonical key (`vol_surface_eod.underlying_id`, `option_contract.underlying_ticker`).
-- `provider_symbol` — Polygon / vendor symbol (`AAPL`, `I:NDX`, …).
-- Cron session `as_of` must satisfy ingest_from_date <= as_of and (ingest_to_date IS NULL OR ingest_to_date >= as_of).
--
-- Quotation / curves: `discount_curve_id` → `par_curve_eod` / bootstrapped `discount_curve_eod`
-- (pricing via `NUMERAIRE_DEV_RATE_SOURCE=db`). NULL columns fall back to env
-- (`NUMERAIRE_DEV_RATE`, `NUMERAIRE_DEV_DIV_YIELD`). `forward_curve_*` reserved for future use.
CREATE TABLE IF NOT EXISTS market_data_prep_scope (
    scope_id TEXT PRIMARY KEY,
    instrument_id TEXT NOT NULL,
    provider_symbol TEXT NOT NULL,
    asset_class TEXT NOT NULL CHECK (
        asset_class IN (
            'EQUITY',
            'INDEX',
            'FX',
            'COMMODITY',
            'RATE',
            'BOND',
            'OTHER'
        )
    ),
    ingest_from_date TEXT NOT NULL,
    ingest_to_date TEXT,
    is_active INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0, 1)),
    ingest_index_eod INTEGER NOT NULL DEFAULT 0 CHECK (ingest_index_eod IN (0, 1)),
    ingest_equity_eod INTEGER NOT NULL DEFAULT 0 CHECK (ingest_equity_eod IN (0, 1)),
    ingest_option_contracts INTEGER NOT NULL DEFAULT 0 CHECK (ingest_option_contracts IN (0, 1)),
    build_option_universe INTEGER NOT NULL DEFAULT 0 CHECK (build_option_universe IN (0, 1)),
    fetch_option_daily_price_eod INTEGER NOT NULL DEFAULT 0 CHECK (fetch_option_daily_price_eod IN (0, 1)),
    build_vol_surface_eod INTEGER NOT NULL DEFAULT 0 CHECK (build_vol_surface_eod IN (0, 1)),
    option_underlying_id TEXT,
    option_grid_config_name TEXT NOT NULL DEFAULT 'default_index_option_universe',
    surface_kind TEXT NOT NULL DEFAULT 'implied_bs_eod',
    quote_currency TEXT NOT NULL DEFAULT 'USD',
    session_calendar TEXT NOT NULL DEFAULT 'America/New_York',
    data_vendor TEXT NOT NULL DEFAULT 'POLYGON',
    equity_eod_adjusted INTEGER NOT NULL DEFAULT 1 CHECK (equity_eod_adjusted IN (0, 1)),
    ingest_priority INTEGER NOT NULL DEFAULT 100,
    discount_curve_id TEXT,
    discount_curve_source TEXT CHECK (
        discount_curve_source IS NULL
        OR discount_curve_source IN (
            'ENV',
            'SQLITE',
            'FLAT_OVERRIDE',
            'VENDOR',
            'FRED'
        )
    ),
    forward_curve_id TEXT,
    forward_curve_source TEXT CHECK (
        forward_curve_source IS NULL
        OR forward_curve_source IN ('ENV', 'SQLITE', 'FLAT_OVERRIDE', 'VENDOR')
    ),
    risk_free_rate_override REAL,
    dividend_yield_override REAL,
    valuation_curve_set TEXT,
    notes TEXT,
    last_prep_as_of TEXT,
    last_prep_at TEXT,
    last_prep_status TEXT,
    created_at TEXT,
    updated_at TEXT,
    CHECK (
        ingest_to_date IS NULL
        OR ingest_to_date >= ingest_from_date
    ),
    CHECK (
        option_underlying_id IS NOT NULL
        OR (
            ingest_option_contracts = 0
            AND build_option_universe = 0
            AND fetch_option_daily_price_eod = 0
            AND build_vol_surface_eod = 0
        )
    ),
    CHECK (
        risk_free_rate_override IS NULL
        OR discount_curve_id IS NULL
        OR discount_curve_source = 'FLAT_OVERRIDE'
    )
);
CREATE INDEX IF NOT EXISTS idx_market_data_prep_scope_active ON market_data_prep_scope (
    is_active,
    ingest_from_date,
    ingest_to_date
);
CREATE INDEX IF NOT EXISTS idx_market_data_prep_scope_instrument ON market_data_prep_scope (instrument_id);
-- End-of-day OHLCV for listed equities (e.g. Polygon `v2/aggs` `1/day`, adjusted flag).
--
-- Time semantics:
--   `as_of` — valuation / session calendar date this EOD bar is for (`YYYY-MM-DD`),
--       interpreted in `session_calendar` (US listed: trading day in ET).
--   `provider_timestamp_utc_ms` — raw Unix ms (`t`) from the provider bar; audit trail only.
--   `ingested_at` — when this row was written to SQLite (UTC ISO-8601 recommended).
CREATE TABLE IF NOT EXISTS equity_daily_eod (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ticker TEXT NOT NULL,
    as_of TEXT NOT NULL,
    session_calendar TEXT NOT NULL DEFAULT 'America/New_York',
    open REAL NOT NULL,
    high REAL NOT NULL,
    low REAL NOT NULL,
    close REAL NOT NULL,
    currency TEXT NOT NULL DEFAULT 'USD',
    volume REAL,
    vwap REAL,
    trade_count INTEGER,
    source TEXT NOT NULL,
    timespan TEXT NOT NULL DEFAULT '1d',
    adjusted INTEGER NOT NULL CHECK (adjusted IN (0, 1)) DEFAULT 1,
    provider_timestamp_utc_ms INTEGER,
    ingested_at TEXT NOT NULL,
    UNIQUE (ticker, as_of, timespan, adjusted)
);
CREATE INDEX IF NOT EXISTS idx_equity_daily_eod_ticker_as_of ON equity_daily_eod (ticker, as_of);
-- End-of-day OHLC for benchmark / cash indices (e.g. Massive/Polygon `v2/aggs` `1/day`, ticker `I:NDX`).
--
-- Diff vs `equity_daily_eod`:
--   Provider bars are often OHLC + `t` only — `volume` / `vwap` / `trade_count` typically NULL.
--   `adjusted` is retained for schema parity with equity ingest; for pure index levels it is not
--   “split-adjusted” like stocks — store `1` unless the provider exposes an explicit adjusted series.
--
-- Time semantics: same as `equity_daily_eod` (`as_of`, `session_calendar`, `provider_timestamp_utc_ms`, `ingested_at`).
CREATE TABLE IF NOT EXISTS index_daily_eod (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ticker TEXT NOT NULL,
    as_of TEXT NOT NULL,
    session_calendar TEXT NOT NULL DEFAULT 'America/New_York',
    open REAL NOT NULL,
    high REAL NOT NULL,
    low REAL NOT NULL,
    close REAL NOT NULL,
    currency TEXT NOT NULL DEFAULT 'USD',
    volume REAL,
    vwap REAL,
    trade_count INTEGER,
    source TEXT NOT NULL,
    timespan TEXT NOT NULL DEFAULT '1d',
    adjusted INTEGER NOT NULL CHECK (adjusted IN (0, 1)) DEFAULT 1,
    provider_timestamp_utc_ms INTEGER,
    ingested_at TEXT NOT NULL,
    UNIQUE (ticker, as_of, timespan, adjusted)
);
CREATE INDEX IF NOT EXISTS idx_index_daily_eod_ticker_as_of ON index_daily_eod (ticker, as_of);
-- Option contract definitions from provider reference (e.g. Polygon `v3/reference/options/contracts`).
--
-- `listing_as_of` — parametr `as_of` w zapytaniu: dzień kalendarzowy snapshotu łańcucha / katalogu
--     kontraktów (jakie kontrakty istniały w reference tego dnia). To nie jest dzień sesji OHLC opcji;
--     ten będzie w przyszłej tabeli barów opcji jako `as_of`.
CREATE TABLE IF NOT EXISTS option_contract (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    option_ticker TEXT NOT NULL,
    listing_as_of TEXT NOT NULL,
    underlying_ticker TEXT NOT NULL,
    expiration_date TEXT NOT NULL,
    strike_price REAL NOT NULL,
    contract_type TEXT NOT NULL CHECK (contract_type IN ('call', 'put', 'other')),
    exercise_style TEXT NOT NULL,
    shares_per_contract INTEGER NOT NULL DEFAULT 100,
    primary_exchange TEXT,
    cfi TEXT,
    currency TEXT NOT NULL DEFAULT 'USD',
    source TEXT NOT NULL,
    ingested_at TEXT NOT NULL,
    UNIQUE (option_ticker, listing_as_of)
);
CREATE INDEX IF NOT EXISTS idx_option_contract_underlying_listing ON option_contract (underlying_ticker, listing_as_of);
CREATE INDEX IF NOT EXISTS idx_option_contract_expiry ON option_contract (
    underlying_ticker,
    listing_as_of,
    expiration_date
);
-- Parametric subset of `option_contract` for EOD option price ingest (built from grid JSON).
--
-- One row per selected `option_ticker` per (`listing_as_of`, `underlying_ticker`, `grid_config_name`).
-- Grid coordinates (`pillar_id`, `otm_percent`, `contract_type`) record why the contract was chosen.
CREATE TABLE IF NOT EXISTS option_universe_eod (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    listing_as_of TEXT NOT NULL,
    underlying_ticker TEXT NOT NULL,
    grid_config_name TEXT NOT NULL,
    option_ticker TEXT NOT NULL,
    pillar_id TEXT NOT NULL,
    target_dte_days INTEGER NOT NULL,
    expiration_date TEXT NOT NULL,
    otm_percent REAL NOT NULL,
    contract_type TEXT NOT NULL CHECK (contract_type IN ('call', 'put')),
    strike_price REAL NOT NULL,
    target_strike REAL NOT NULL,
    spot_used REAL NOT NULL,
    strike_gap_pct REAL NOT NULL,
    built_at TEXT NOT NULL,
    UNIQUE (
        listing_as_of,
        underlying_ticker,
        grid_config_name,
        option_ticker
    ),
    UNIQUE (
        listing_as_of,
        underlying_ticker,
        grid_config_name,
        pillar_id,
        otm_percent,
        contract_type
    )
);
CREATE INDEX IF NOT EXISTS idx_option_universe_listing_underlying ON option_universe_eod (
    listing_as_of,
    underlying_ticker,
    grid_config_name
);
-- ---------------------------------------------------------------------------
-- Reference codes for `products_equity.instrument_type` (optional seed / UI).
CREATE TABLE IF NOT EXISTS catalog_instrument_type (
    code TEXT NOT NULL PRIMARY KEY,
    family TEXT NOT NULL,
    maps_to_instrument_type TEXT NOT NULL,
    description_en TEXT NOT NULL,
    example_product_id TEXT NOT NULL,
    sort_order INTEGER NOT NULL DEFAULT 0,
    is_active INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0, 1)),
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS idx_catalog_instrument_type_family ON catalog_instrument_type (family);
-- ---------------------------------------------------------------------------
-- EOD mark-to-model per trade leg (batch C++).
-- Multiple rows per (leg_id, as_of) allowed when pricing_engine differs (e.g. analytic vs MC).
CREATE TABLE IF NOT EXISTS trade_leg_mtm_eod (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    as_of TEXT NOT NULL,
    -- valuation session date YYYY-MM-DD
    session_calendar TEXT NOT NULL DEFAULT 'America/New_York',
    trade_id TEXT NOT NULL,
    -- denormalized; convenience for trade-level rollups
    leg_id TEXT NOT NULL,
    -- Market snapshot used by the batch (audit / reproducibility)
    underlying_spot REAL NOT NULL,
    risk_free_rate REAL NOT NULL,
    dividend_yield REAL NOT NULL DEFAULT 0,
    implied_vol_used REAL NOT NULL,
    years_to_maturity REAL NOT NULL,
    -- Valuation outputs
    numeraire_currency TEXT NOT NULL DEFAULT 'USD',
    pv_unit REAL NOT NULL,
    pv_total REAL NOT NULL,
    pnl_daily REAL,
    pnl_inception REAL,
    -- Greeks (convention: same scale as pv_unit — document in app/docs)
    delta REAL NOT NULL,
    delta_total REAL NOT NULL,
    gamma REAL NOT NULL,
    gamma_total REAL NOT NULL,
    vega REAL NOT NULL,
    vega_total REAL NOT NULL,
    theta REAL NOT NULL,
    theta_total REAL NOT NULL,
    rho REAL NOT NULL,
    rho_total REAL NOT NULL,
    pricing_engine TEXT NOT NULL,
    -- e.g. analytic_black_scholes, monte_carlo_gbm_v1
    batch_run_id TEXT,
    calculated_at TEXT NOT NULL DEFAULT (datetime('now')),
    remarks TEXT NOT NULL DEFAULT '',
    FOREIGN KEY (trade_id) REFERENCES trades (trade_id) ON DELETE CASCADE,
    FOREIGN KEY (leg_id) REFERENCES trade_legs (leg_id) ON DELETE CASCADE,
    UNIQUE (leg_id, as_of, pricing_engine)
);
CREATE INDEX IF NOT EXISTS idx_trade_leg_mtm_eod_leg_asof_engine ON trade_leg_mtm_eod (leg_id, as_of, pricing_engine);
CREATE INDEX IF NOT EXISTS idx_trade_leg_mtm_eod_asof ON trade_leg_mtm_eod (as_of);
CREATE INDEX IF NOT EXISTS idx_trade_leg_mtm_eod_trade_asof ON trade_leg_mtm_eod (trade_id, as_of);
CREATE INDEX IF NOT EXISTS idx_trade_leg_mtm_eod_engine ON trade_leg_mtm_eod (pricing_engine);
-- ---------------------------------------------------------------------------
-- Append-only archive of every MTM leg valuation produced by a batch run.
-- Official current mark remains in trade_leg_mtm_eod (INSERT OR REPLACE).
-- Compare runs: filter or join on batch_run_id (and leg_id / as_of / pricing_engine).
CREATE TABLE IF NOT EXISTS trade_leg_mtm_eod_archive (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    batch_run_id TEXT NOT NULL,
    calculated_at TEXT NOT NULL,
    as_of TEXT NOT NULL,
    session_calendar TEXT NOT NULL DEFAULT 'America/New_York',
    trade_id TEXT NOT NULL,
    leg_id TEXT NOT NULL,
    underlying_spot REAL NOT NULL,
    risk_free_rate REAL NOT NULL,
    dividend_yield REAL NOT NULL DEFAULT 0,
    implied_vol_used REAL NOT NULL,
    years_to_maturity REAL NOT NULL,
    numeraire_currency TEXT NOT NULL DEFAULT 'USD',
    pv_unit REAL NOT NULL,
    pv_total REAL NOT NULL,
    pnl_daily REAL,
    pnl_inception REAL,
    delta REAL NOT NULL,
    delta_total REAL NOT NULL,
    gamma REAL NOT NULL,
    gamma_total REAL NOT NULL,
    vega REAL NOT NULL,
    vega_total REAL NOT NULL,
    theta REAL NOT NULL,
    theta_total REAL NOT NULL,
    rho REAL NOT NULL,
    rho_total REAL NOT NULL,
    pricing_engine TEXT NOT NULL,
    remarks TEXT NOT NULL DEFAULT '',
    FOREIGN KEY (trade_id) REFERENCES trades (trade_id) ON DELETE CASCADE,
    FOREIGN KEY (leg_id) REFERENCES trade_legs (leg_id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_mtm_archive_batch_run ON trade_leg_mtm_eod_archive (batch_run_id);
CREATE INDEX IF NOT EXISTS idx_mtm_archive_leg_asof_engine_batch ON trade_leg_mtm_eod_archive (leg_id, as_of, pricing_engine, batch_run_id);
CREATE INDEX IF NOT EXISTS idx_mtm_archive_trade_asof ON trade_leg_mtm_eod_archive (trade_id, as_of);
-- -------------------------------------------------------
-- End-of-day OHLC for listed options (e.g. Polygon `v2/aggs` `1/day` on `O:NDXP…`).
--
-- Scope: market **prices** only. Implied vol is computed in-app from close + spot + rates;
-- vendor IV (Polygon snapshot) is out of scope here (future benchmark only).
--
-- Join to catalog: `option_ticker` ↔ `option_contract.option_ticker` (by convention; no FK).
--
-- Time semantics:
--   `as_of` — valuation / session calendar date this EOD bar is for (`YYYY-MM-DD`),
--       US listed options: trading day in ET (`session_calendar`).
--   NOT the same as `option_contract.listing_as_of` (reference snapshot date).
--   `provider_timestamp_utc_ms` — raw Unix ms (`t`) from the provider bar; audit only.
--   `ingested_at` — when this row was written to SQLite (UTC ISO-8601 recommended).
--
-- Ingest: one HTTP request per `option_ticker` and date range; upsert one row per trading day
-- in `results[]`. Missing bar = no row (illiquid / no trades that session).
CREATE TABLE IF NOT EXISTS option_daily_price_eod (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    option_ticker TEXT NOT NULL,
    as_of TEXT NOT NULL,
    session_calendar TEXT NOT NULL DEFAULT 'America/New_York',
    open REAL NOT NULL,
    high REAL NOT NULL,
    low REAL NOT NULL,
    close REAL NOT NULL,
    currency TEXT NOT NULL DEFAULT 'USD',
    volume REAL,
    vwap REAL,
    trade_count INTEGER,
    source TEXT NOT NULL,
    timespan TEXT NOT NULL DEFAULT '1d',
    adjusted INTEGER NOT NULL CHECK (adjusted IN (0, 1)) DEFAULT 1,
    provider_timestamp_utc_ms INTEGER,
    ingested_at TEXT NOT NULL,
    UNIQUE (option_ticker, as_of, timespan, adjusted)
);
CREATE INDEX IF NOT EXISTS idx_option_daily_price_eod_ticker_as_of ON option_daily_price_eod (option_ticker, as_of);
CREATE INDEX IF NOT EXISTS idx_option_daily_price_eod_as_of ON option_daily_price_eod (as_of);
-- ---------------------------------------------------------------------------
-- EOD par-yield curves (FRED Treasury DGS* — second data provider).
--
-- `curve_id` — canonical name referenced by `market_data_prep_scope.discount_curve_id`
--     (e.g. USD_TREASURY_PAR_FRED). Ingest: `fetch_fred_treasury_par_yields.py`; bootstrap:
--     `dev_main --build-discount-curve-eod` (see `daily_market_prep.sh`).
--
-- `quoted_rate` — market par/simple quote, annualized decimal (0.0525 = 5.25%), from FRED % / 100.
--     **Not** a bootstrapped zero rate; see `numeraire::quant::BootstrapDiscountCurve`.
-- `tenor` — pillar label (1M, 3M, …); `tenor_days` optional calendar-day hint for bootstrap.
-- `instrument_type` — curve pillar product: deposit | fra | futures | swap | other (FRED DGS* → deposit/swap).
--
-- Time semantics: `as_of` = valuation session date (YYYY-MM-DD, US calendar for DGS*).
CREATE TABLE IF NOT EXISTS par_curve_eod (
    curve_id TEXT NOT NULL,
    as_of TEXT NOT NULL,
    currency TEXT NOT NULL DEFAULT 'USD',
    curve_kind TEXT NOT NULL DEFAULT 'treasury_par_fred',
    source TEXT NOT NULL DEFAULT 'FRED',
    day_count TEXT NOT NULL DEFAULT 'Actual365Fixed',
    session_calendar TEXT NOT NULL DEFAULT 'America/New_York',
    notes TEXT,
    ingested_at TEXT NOT NULL DEFAULT (datetime('now')),
    PRIMARY KEY (curve_id, as_of)
);
CREATE INDEX IF NOT EXISTS idx_par_curve_eod_as_of ON par_curve_eod (as_of);
CREATE TABLE IF NOT EXISTS par_curve_point_eod (
    curve_id TEXT NOT NULL,
    as_of TEXT NOT NULL,
    tenor TEXT NOT NULL,
    tenor_days INTEGER,
    instrument_type TEXT NOT NULL CHECK (
        instrument_type IN ('deposit', 'fra', 'futures', 'swap', 'other')
    ),
    fred_series_id TEXT NOT NULL,
    quoted_rate REAL NOT NULL CHECK (quoted_rate >= 0.0),
    quote_style TEXT NOT NULL DEFAULT 'annualized_decimal',
    PRIMARY KEY (curve_id, as_of, tenor),
    FOREIGN KEY (curve_id, as_of) REFERENCES par_curve_eod (curve_id, as_of) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_par_curve_point_curve_as_of ON par_curve_point_eod (curve_id, as_of);
-- ---------------------------------------------------------------------------
-- EOD bootstrapped discount curves (zero rates + discount factors at pillars).
--
-- Built from `par_curve_*` quotes via `BootstrapDiscountCurve` (deposit/swap bootstrap,
-- linear zero-rate interpolation between pillars during solve; runtime DF uses same convention).
--
-- `curve_id` — canonical name for pricing (`market_data_prep_scope.discount_curve_id`).
-- `source_par_curve_id` / `source_par_as_of` — input par-yield snapshot used for bootstrap.
-- `zero_rate` / `discount_factor` — solved at pillar times only (no dense coupon grid stored).
CREATE TABLE IF NOT EXISTS discount_curve_eod (
    curve_id TEXT NOT NULL,
    as_of TEXT NOT NULL,
    source_par_curve_id TEXT NOT NULL,
    source_par_as_of TEXT NOT NULL,
    currency TEXT NOT NULL DEFAULT 'USD',
    day_count TEXT NOT NULL DEFAULT 'Actual365Fixed',
    session_calendar TEXT NOT NULL DEFAULT 'America/New_York',
    interpolation_method TEXT NOT NULL DEFAULT 'linear_zero_rate',
    bootstrap_engine TEXT NOT NULL DEFAULT 'deposit_swap_v1',
    batch_run_id TEXT,
    ingested_at TEXT NOT NULL DEFAULT (datetime('now')),
    PRIMARY KEY (curve_id, as_of),
    FOREIGN KEY (source_par_curve_id, source_par_as_of) REFERENCES par_curve_eod (curve_id, as_of)
);
CREATE INDEX IF NOT EXISTS idx_discount_curve_eod_as_of ON discount_curve_eod (as_of);
CREATE INDEX IF NOT EXISTS idx_discount_curve_eod_source_par ON discount_curve_eod (source_par_curve_id, source_par_as_of);
CREATE TABLE IF NOT EXISTS discount_curve_point_eod (
    curve_id TEXT NOT NULL,
    as_of TEXT NOT NULL,
    tenor TEXT NOT NULL,
    time_years REAL NOT NULL CHECK (time_years > 0.0),
    zero_rate REAL NOT NULL CHECK (zero_rate >= 0.0),
    discount_factor REAL NOT NULL CHECK (discount_factor > 0.0 AND discount_factor <= 1.0),
    PRIMARY KEY (curve_id, as_of, tenor),
    FOREIGN KEY (curve_id, as_of) REFERENCES discount_curve_eod (curve_id, as_of) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_discount_curve_point_curve_as_of ON discount_curve_point_eod (curve_id, as_of);
-- -------------------------------------------------------
-- EOD implied-volatility surfaces (sparse points, not a dense strike/expiry matrix).
--
-- Built in-app from option closes + spot + rates (see `option_daily_price_eod` comment).
-- `IMarketData::ImpliedVolatility(underlying, K, T)` should interpolate these points in C++.
--
-- `vol_surface_eod` — one official surface per (underlying, as_of, surface_kind).
-- `vol_surface_point_eod` — variable row count per surface (only strikes/expiries with data).
--
-- `underlying_id` — same convention as `products.underlying_id` (e.g. NDX), not the Polygon
--     index ticker (I:NDX) unless you deliberately align them.
-- `surface_kind` — e.g. implied_bs_eod (EOD close inversion); room for intraday kinds later.
-- `coordinate_system` — strike_expiry v1 (absolute K + expiration_date + years_to_maturity).
-- `contract_type` — call and put may both exist at the same (K, T); store separately or
--     publish a mid surface in a later job — schema allows both legs.
CREATE TABLE IF NOT EXISTS vol_surface_eod (
    surface_id INTEGER PRIMARY KEY AUTOINCREMENT,
    underlying_id TEXT NOT NULL,
    as_of TEXT NOT NULL,
    surface_kind TEXT NOT NULL DEFAULT 'implied_bs_eod',
    coordinate_system TEXT NOT NULL DEFAULT 'strike_expiry',
    spot_used REAL NOT NULL,
    risk_free_rate REAL NOT NULL,
    dividend_yield REAL NOT NULL DEFAULT 0.0,
    model TEXT NOT NULL DEFAULT 'black_scholes_european',
    price_source TEXT NOT NULL DEFAULT 'option_daily_price_eod.close',
    currency TEXT NOT NULL DEFAULT 'USD',
    point_count INTEGER,
    ingested_at TEXT NOT NULL,
    batch_run_id TEXT,
    UNIQUE (underlying_id, as_of, surface_kind)
);
CREATE INDEX IF NOT EXISTS idx_vol_surface_eod_underlying_as_of ON vol_surface_eod (underlying_id, as_of);
CREATE TABLE IF NOT EXISTS vol_surface_point_eod (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    surface_id INTEGER NOT NULL,
    expiration_date TEXT NOT NULL,
    years_to_maturity REAL NOT NULL,
    strike REAL NOT NULL,
    contract_type TEXT NOT NULL CHECK (contract_type IN ('call', 'put')),
    implied_vol REAL NOT NULL,
    source_option_ticker TEXT,
    input_price REAL,
    quality TEXT NOT NULL DEFAULT 'ok',
    FOREIGN KEY (surface_id) REFERENCES vol_surface_eod (surface_id) ON DELETE CASCADE,
    UNIQUE (
        surface_id,
        expiration_date,
        strike,
        contract_type
    )
);
CREATE INDEX IF NOT EXISTS idx_vol_surface_point_surface_id ON vol_surface_point_eod (surface_id);
CREATE INDEX IF NOT EXISTS idx_vol_surface_point_surface_expiry ON vol_surface_point_eod (surface_id, expiration_date);
