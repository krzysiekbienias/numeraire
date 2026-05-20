# Incoming trade bundles

Copy [`trade_bundle.sample.json`](trade_bundle.sample.json) to a new filename, fill empty/required fields, then import with [`scripts/import_trade_bundle.py`](../scripts/import_trade_bundle.py).

Only `trade_bundle.sample.json` is tracked; other `*.json` files in this directory are gitignored. **Do not import the sample as-is** — it intentionally fails validation until filled.

Fields filled later by booking pricer (not import): `execution_price` (null → `0` in DB). Optional: `commission_per_contract` (× `quantity`) or `commission` total. `updated_at` defaults to `datetime('now')` when omitted.
