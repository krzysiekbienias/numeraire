# Incoming trade bundles

Copy [`trade_bundle.sample.json`](trade_bundle.sample.json) to a new filename, fill empty/required fields, then import with [`scripts/import_trade_bundle.py`](../scripts/import_trade_bundle.py).

Only `trade_bundle.sample.json` is tracked; other `*.json` files in this directory are gitignored. **Do not import the sample as-is** — it intentionally fails validation until filled.

**Auto-fill at import** (set `product.product_id` once):

- `equity.product_id` and each `legs[].product_id` ← `product.product_id` (conflict → error)
- `legs[].leg_id` ← `{trade_id}_L1`, `_L2`, … if omitted

Fields filled later by booking pricer: `execution_price` (null → `0`). **Commission at import:** prefer `commission_per_contract` (× `quantity`, e.g. `0.25` and `100` → `25` in DB); or flat `commission` if `commission_per_contract` is omitted. `updated_at` → `datetime('now')` when omitted.

**Pipeline:** import (always **`PENDING`** in `trades`, even if JSON says `LIVE`) → `dev_main --price-booking <trade_id>` → `LIVE` when all legs `execution_price > 0` → `dev_main --as-of …` MTM. See [`docs/architecture.md`](../../docs/architecture.md) § *Trade lifecycle*.

**Fix existing row booked as LIVE by mistake:** `UPDATE trades SET status='PENDING' WHERE trade_id='…';` and reset `execution_price=0` on legs before re-running `--price-booking`.
