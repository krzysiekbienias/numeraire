# Incoming trade bundles

Copy [`trade_bundle.sample.json`](trade_bundle.sample.json) (vanilla) or [`trade_bundle_binary.sample.json`](trade_bundle_binary.sample.json) (asset-or-nothing / cash-or-nothing) to a new filename, fill empty/required fields, then import with [`scripts/import_trade_bundle.py`](../scripts/import_trade_bundle.py).

Only `trade_bundle.sample.json` and `trade_bundle_binary.sample.json` are tracked; other `*.json` files in this directory are gitignored. **Do not import a sample as-is** — samples intentionally fail validation until filled.

## Import from CLI

Run from the **repository root**. Bundle file name must match the trade id: **`TRD_10005.json`** for trade **`TRD_10005`**.

**By trade id** (recommended for ready-made bundles in this folder):

```bash
python3 scripts/import_trade_bundle.py TRD_10005 TRD_10006 TRD_10007 TRD_10008
```

**Explicit paths:**

```bash
python3 scripts/import_trade_bundle.py trades/incoming/TRD_10005.json --db db.sqlite3
```

**Every `*.json` in this directory** (skips trades already in DB):

```bash
python3 scripts/import_trade_bundle.py --all --incoming-dir trades/incoming
```

`NUMERAIRE_DB_PATH` or `--db db.sqlite3` selects the database (default `db.sqlite3`).

## Auto-fill at import

Set `product.product_id` once:

- `equity.product_id` and each `legs[].product_id` ← `product.product_id` (conflict → error)
- `legs[].leg_id` ← `{trade_id}_L1`, `_L2`, … if omitted

Fields filled later by booking pricer: `execution_price` (null → `0`). **Commission at import:** prefer `commission_per_contract` (× `quantity`, e.g. `0.25` and `100` → `25` in DB); or flat `commission` if `commission_per_contract` is omitted. `updated_at` → `datetime('now')` when omitted.

## Binary options

`equity.instrument_type` = `AssetOrNothingOption` (payoff \(S_T\) if ITM; `structured_params: {}`) or `CashOrNothingOption` (fixed cash if ITM; **`structured_params.cash_payout_per_share`** required, per-share scale). Pricer support is tracked separately — import + catalog work before booking/MTM.

**Filled examples (local, gitignored):**

| File | Trade | Type |
|------|-------|------|
| `TRD_10005.json` | TRD_10005 | AON call AAPL |
| `TRD_10006.json` | TRD_10006 | AON put GOOGL |
| `TRD_10007.json` | TRD_10007 | CON call NVDA |
| `TRD_10008.json` | TRD_10008 | CON put AAPL |

All four use **`trade_date` = 2026-05-11** (first booking/MTM on or after that date).

```bash
python3 scripts/import_trade_bundle.py TRD_10005 TRD_10006 TRD_10007 TRD_10008
```

## Pipeline

Import (always **`PENDING`** in `trades`, even if JSON says `LIVE`) → `dev_main --price-booking <trade_id>` → `LIVE` when all legs `execution_price > 0` → `dev_main --as-of …` MTM. See [`docs/architecture.md`](../../docs/architecture.md) § *Trade lifecycle*.

**Fix existing row booked as LIVE by mistake:** `UPDATE trades SET status='PENDING' WHERE trade_id='…';` and reset `execution_price=0` on legs before re-running `--price-booking`.
