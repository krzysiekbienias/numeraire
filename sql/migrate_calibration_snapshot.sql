-- Numeraire++ — migrate `historical_calibration*` to the generic `calibration_*` tables.
--
-- What changes:
--   * `historical_calibration`             -> `calibration_snapshot`
--   * `calibration_method`                 -> split into `model` + `source`
--       'historical_eod_gbm'               -> model='gbm', source='historical'
--   * `source` ('dev_main', the writer)    -> `produced_by`
--   * history / lookback columns           -> nullable (only set when source='historical')
--   * `historical_calibration_factor`      -> `calibration_factor`
--       `underlying_id`                    -> `factor_id`
--       `spot_as_of`                       -> `factor_level` (nullable)
--   * `historical_calibration_correlation` -> `calibration_correlation`
--   * `historical_calibration_cholesky`    -> `calibration_cholesky`
--   * new `calibration_param` for model-specific scalars
--
-- Run once per existing database:
--   sqlite3 db.sqlite3 < sql/migrate_calibration_snapshot.sql
--
-- Idempotent: re-running after a completed migration is a no-op because the
-- source tables no longer exist.
PRAGMA foreign_keys = OFF;

BEGIN TRANSACTION;

-- New tables (kept byte-identical to sql/schema_v1.sql).
CREATE TABLE IF NOT EXISTS calibration_snapshot (
    calibration_id INTEGER PRIMARY KEY AUTOINCREMENT,
    model TEXT NOT NULL,
    source TEXT NOT NULL CHECK (source IN ('historical', 'implied', 'fit')),
    calibration_scope TEXT NOT NULL DEFAULT 'book' CHECK (calibration_scope IN ('book', 'underlying')),
    scope_key TEXT NOT NULL DEFAULT 'ALL',
    as_of TEXT NOT NULL,
    num_factors INTEGER NOT NULL CHECK (num_factors > 0),
    history_start TEXT,
    history_end TEXT,
    lookback_calendar_days INTEGER,
    min_return_observations INTEGER,
    vol_annualization_days INTEGER,
    eod_adjusted INTEGER CHECK (eod_adjusted IS NULL OR eod_adjusted IN (0, 1)),
    num_return_observations INTEGER,
    batch_run_id TEXT,
    calculated_at TEXT NOT NULL DEFAULT (datetime('now')),
    produced_by TEXT NOT NULL DEFAULT 'dev_main',
    remarks TEXT NOT NULL DEFAULT '',
    UNIQUE (calibration_scope, scope_key, as_of, model, source),
    CHECK (source <> 'historical' OR (
        history_start IS NOT NULL
        AND history_end IS NOT NULL
        AND lookback_calendar_days IS NOT NULL
        AND min_return_observations IS NOT NULL
        AND vol_annualization_days IS NOT NULL
        AND eod_adjusted IS NOT NULL
        AND num_return_observations IS NOT NULL
    ))
);
CREATE INDEX IF NOT EXISTS idx_calibration_snapshot_lookup ON calibration_snapshot (
    calibration_scope,
    scope_key,
    model,
    source,
    as_of DESC
);
CREATE TABLE IF NOT EXISTS calibration_factor (
    calibration_id INTEGER NOT NULL,
    factor_index INTEGER NOT NULL,
    factor_id TEXT NOT NULL,
    factor_level REAL CHECK (factor_level IS NULL OR factor_level > 0.0),
    volatility REAL NOT NULL CHECK (volatility >= 0.0),
    PRIMARY KEY (calibration_id, factor_index),
    FOREIGN KEY (calibration_id) REFERENCES calibration_snapshot (calibration_id) ON DELETE CASCADE,
    UNIQUE (calibration_id, factor_id)
);
CREATE INDEX IF NOT EXISTS idx_calibration_factor_factor_id ON calibration_factor (factor_id);
CREATE TABLE IF NOT EXISTS calibration_correlation (
    calibration_id INTEGER NOT NULL,
    factor_i INTEGER NOT NULL,
    factor_j INTEGER NOT NULL,
    rho REAL NOT NULL CHECK (rho >= -1.0 AND rho <= 1.0),
    PRIMARY KEY (calibration_id, factor_i, factor_j),
    FOREIGN KEY (calibration_id) REFERENCES calibration_snapshot (calibration_id) ON DELETE CASCADE,
    CHECK (factor_i <= factor_j)
);
CREATE TABLE IF NOT EXISTS calibration_cholesky (
    calibration_id INTEGER NOT NULL,
    row_i INTEGER NOT NULL,
    col_j INTEGER NOT NULL,
    l_value REAL NOT NULL,
    PRIMARY KEY (calibration_id, row_i, col_j),
    FOREIGN KEY (calibration_id) REFERENCES calibration_snapshot (calibration_id) ON DELETE CASCADE,
    CHECK (col_j <= row_i)
);
CREATE TABLE IF NOT EXISTS calibration_param (
    calibration_id INTEGER NOT NULL,
    factor_id TEXT NOT NULL DEFAULT '',
    param_name TEXT NOT NULL,
    param_value REAL NOT NULL,
    PRIMARY KEY (calibration_id, factor_id, param_name),
    FOREIGN KEY (calibration_id) REFERENCES calibration_snapshot (calibration_id) ON DELETE CASCADE
);

-- Carry over existing snapshots. `calibration_id` is preserved so that
-- `trade_leg_exposure_eod.calibration_id` keeps pointing at the right snapshot.
INSERT INTO calibration_snapshot (
    calibration_id, model, source, calibration_scope, scope_key, as_of, num_factors,
    history_start, history_end, lookback_calendar_days, min_return_observations,
    vol_annualization_days, eod_adjusted, num_return_observations, batch_run_id,
    calculated_at, produced_by, remarks
)
SELECT
    calibration_id,
    CASE calibration_method
        WHEN 'historical_eod_gbm' THEN 'gbm'
        ELSE calibration_method
    END,
    'historical',
    calibration_scope,
    scope_key,
    as_of,
    num_factors,
    history_start,
    history_end,
    lookback_calendar_days,
    min_return_observations,
    vol_annualization_days,
    eod_adjusted,
    num_return_observations,
    batch_run_id,
    calculated_at,
    source,
    remarks
FROM historical_calibration;

INSERT INTO calibration_factor (calibration_id, factor_index, factor_id, factor_level, volatility)
SELECT calibration_id, factor_index, underlying_id, spot_as_of, volatility
FROM historical_calibration_factor;

INSERT INTO calibration_correlation (calibration_id, factor_i, factor_j, rho)
SELECT calibration_id, factor_i, factor_j, rho
FROM historical_calibration_correlation;

INSERT INTO calibration_cholesky (calibration_id, row_i, col_j, l_value)
SELECT calibration_id, row_i, col_j, l_value
FROM historical_calibration_cholesky;

DROP TABLE historical_calibration_factor;
DROP TABLE historical_calibration_correlation;
DROP TABLE historical_calibration_cholesky;
DROP TABLE historical_calibration;

COMMIT;

PRAGMA foreign_keys = ON;
