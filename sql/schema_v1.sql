-- Numeraire++ — reference SQLite schema (v1)
--
-- Purpose:
--   - Single place documenting table/column names the C++ repository layer expects
--   - Bootstrapping empty DBs; unit tests can apply this to :memory: + INSERT fixtures
--
-- Does not replace your existing db.sqlite3 — compare with `sqlite3 file.db ".schema"` if needed.
-- Application should use: PRAGMA foreign_keys = ON; after open.

PRAGMA foreign_keys = ON;

-- Example row: product_id | EQUITY | AAPL | 2025-11-04 | PHYSICAL | Actual365Fixed | UnitedStates
CREATE TABLE IF NOT EXISTS products (
    product_id TEXT PRIMARY KEY,
    asset_kind TEXT NOT NULL,
    underlying_id TEXT NOT NULL,
    expiry_date TEXT NOT NULL,
    settlement TEXT NOT NULL,
    day_count TEXT NOT NULL,
    calendar TEXT NOT NULL
);

-- Example row: P_AAPL_001 | CALL | 233 | {}
-- option_type / strike may be NULL for multi-leg rows (ProductFactory may reject for vanilla pricers).
CREATE TABLE IF NOT EXISTS products_equity (
    product_id TEXT PRIMARY KEY,
    option_type TEXT,
    strike REAL,
    structured_params TEXT NOT NULL DEFAULT '{}',
    FOREIGN KEY (product_id) REFERENCES products (product_id)
);

-- Example row: TRD_001 | P_AAPL_001 | booking_ts | trade_date | updated_at | LIVE | LONG | 100 | 0
CREATE TABLE IF NOT EXISTS trades (
    trade_id TEXT PRIMARY KEY,
    product_id TEXT NOT NULL,
    booking_timestamp TEXT NOT NULL,
    trade_date TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    status TEXT NOT NULL,
    direction TEXT NOT NULL,
    quantity REAL NOT NULL,
    commission REAL,
    FOREIGN KEY (product_id) REFERENCES products (product_id)
);

CREATE INDEX IF NOT EXISTS idx_trades_product_id ON trades (product_id);
