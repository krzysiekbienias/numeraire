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
