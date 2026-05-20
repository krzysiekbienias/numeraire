# Incoming trade bundles

Copy [`trade_bundle.sample.json`](trade_bundle.sample.json) to a new filename, fill empty/required fields, then import with [`scripts/import_trade_bundle.py`](../scripts/import_trade_bundle.py).

Only `trade_bundle.sample.json` is tracked; other `*.json` files in this directory are gitignored. **Do not import the sample as-is** — it intentionally fails validation until filled.

**Auto-fill at import** (set `product.product_id` once):

- `equity.product_id` and each `legs[].product_id` ← `product.product_id` (conflict → error)
- `legs[].leg_id` ← `{trade_id}_L1`, `_L2`, … if omitted

Fields filled later by booking pricer: `execution_price` (null → `0`). Optional: `commission_per_contract` (× `quantity`) or `commission`. `updated_at` → `datetime('now')` when omitted.

**Pipeline:** import with `status: "PENDING"` → `dev_main --price-booking <trade_id>` (sets `execution_price` on `trade_date`; promotes to `LIVE` when all legs `execution_price > 0`) → `dev_main --as-of …` MTM (requires `LIVE` + booked legs). See [`docs/architecture.md`](../../docs/architecture.md) § *Trade lifecycle*.
