#!/usr/bin/env python3
"""
Fetch Massive/Polygon futures product catalog into SQLite `futures_product`.

Local / one-shot helper (same idea as fetch_fred_treasury_par_yields.py).
Does **not** touch `universe_instrument` — seed that separately for the products
you actually want to price / ingest.

Default scope: asset_sub_class in {energy, metals}, type=single (no FX currency
futures, no combo products).

Requires: POLYGON_API_KEY in environment or repo-root `.env`.
Optional: POLYGON_BASE_URL (default https://api.polygon.io; also https://api.massive.com).

Examples:
  python3 scripts/fetch_massive_futures_products.py --dry-run
  python3 scripts/fetch_massive_futures_products.py
  python3 scripts/fetch_massive_futures_products.py --asset-sub-class energy,metals --type single
"""

from __future__ import annotations

import argparse
import json
import os
import sqlite3
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import date, datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_POLYGON_BASE = "https://api.polygon.io"
DEFAULT_SUB_CLASSES = ("energy", "metals")
SOURCE = "massive"


def _load_dotenv(path: Path) -> None:
    if not path.is_file():
        return
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key, value = key.strip(), value.strip()
        if key and key not in os.environ:
            os.environ[key] = value


def _die(msg: str) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def _split_csv(raw: str) -> list[str]:
    return [p.strip() for p in raw.split(",") if p.strip()]


def _url_with_api_key(url: str, api_key: str) -> str:
    if "apiKey=" in url:
        return url
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}apiKey={urllib.parse.quote(api_key)}"


def _http_get_json(url: str, timeout_sec: float = 60.0) -> Mapping[str, Any]:
    req = urllib.request.Request(
        url,
        headers={"User-Agent": "numeraire-fetch-futures-products/1.0", "Accept": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout_sec) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")[:800]
        _die(f"HTTP {e.code} for {url.split('?', 1)[0]}: {body}")
    except urllib.error.URLError as e:
        _die(f"request failed: {e}")
    return {}


def _fetch_products(
    base_url: str,
    api_key: str,
    *,
    asset_sub_classes: Sequence[str],
    product_type: str | None,
    as_of: str | None,
    limit: int,
    sleep_sec: float,
) -> list[dict[str, Any]]:
    params: dict[str, str] = {
        "limit": str(limit),
        "sort": "name.asc",
    }
    if asset_sub_classes:
        params["asset_sub_class.any_of"] = ",".join(asset_sub_classes)
    if product_type:
        params["type"] = product_type
    if as_of:
        params["date"] = as_of

    path = "/futures/v1/products"
    url = _url_with_api_key(
        f"{base_url.rstrip('/')}{path}?{urllib.parse.urlencode(params)}",
        api_key,
    )

    out: list[dict[str, Any]] = []
    page = 0
    while url:
        page += 1
        payload = _http_get_json(url)
        results = payload.get("results") or []
        if not isinstance(results, list):
            _die(f"unexpected results type on page {page}")
        for row in results:
            if isinstance(row, dict):
                out.append(row)
        print(f"  page {page}: +{len(results)} (total {len(out)})")
        next_url = payload.get("next_url")
        if not next_url:
            break
        url = _url_with_api_key(str(next_url), api_key)
        if sleep_sec > 0:
            time.sleep(sleep_sec)
    return out


def _dedupe_latest_by_product_code(items: Sequence[Mapping[str, Any]]) -> list[dict[str, Any]]:
    """API can return point-in-time rows; keep the newest `date` per product_code."""
    best: dict[str, dict[str, Any]] = {}
    for raw in items:
        if not isinstance(raw, dict):
            continue
        code = raw.get("product_code")
        if not code:
            continue
        key = str(code)
        date_s = str(raw.get("date") or "")
        prev = best.get(key)
        if prev is None or date_s >= str(prev.get("date") or ""):
            best[key] = dict(raw)
    return [best[k] for k in sorted(best)]


def _row_from_api(item: Mapping[str, Any], ingested_at: str) -> tuple[Any, ...] | None:
    product_code = item.get("product_code")
    if not product_code:
        return None
    uom_qty = item.get("unit_of_measure_qty")
    try:
        uom_qty_f = float(uom_qty) if uom_qty is not None else None
    except (TypeError, ValueError):
        uom_qty_f = None
    return (
        str(product_code),
        item.get("name"),
        item.get("asset_class"),
        item.get("asset_sub_class"),
        item.get("sector"),
        item.get("sub_sector"),
        item.get("trading_venue"),
        item.get("type"),
        item.get("trade_currency_code"),
        item.get("settlement_currency_code"),
        item.get("settlement_method"),
        item.get("settlement_type"),
        item.get("price_quotation"),
        item.get("unit_of_measure"),
        uom_qty_f,
        item.get("date"),
        item.get("last_updated"),
        SOURCE,
        ingested_at,
    )


def _upsert_products(conn: sqlite3.Connection, rows: Iterable[tuple[Any, ...]]) -> int:
    sql = """
        INSERT INTO futures_product (
            product_code, name, asset_class, asset_sub_class, sector, sub_sector,
            trading_venue, type, trade_currency_code, settlement_currency_code,
            settlement_method, settlement_type, price_quotation, unit_of_measure,
            unit_of_measure_qty, as_of, last_updated, source, ingested_at
        ) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
        ON CONFLICT (product_code) DO UPDATE SET
            name = excluded.name,
            asset_class = excluded.asset_class,
            asset_sub_class = excluded.asset_sub_class,
            sector = excluded.sector,
            sub_sector = excluded.sub_sector,
            trading_venue = excluded.trading_venue,
            type = excluded.type,
            trade_currency_code = excluded.trade_currency_code,
            settlement_currency_code = excluded.settlement_currency_code,
            settlement_method = excluded.settlement_method,
            settlement_type = excluded.settlement_type,
            price_quotation = excluded.price_quotation,
            unit_of_measure = excluded.unit_of_measure,
            unit_of_measure_qty = excluded.unit_of_measure_qty,
            as_of = excluded.as_of,
            last_updated = excluded.last_updated,
            source = excluded.source,
            ingested_at = excluded.ingested_at
    """
    cur = conn.cursor()
    n = 0
    for row in rows:
        cur.execute(sql, row)
        n += 1
    conn.commit()
    return n


def _summarize(items: Sequence[Mapping[str, Any]]) -> None:
    by_sub: dict[str, int] = {}
    by_venue: dict[str, int] = {}
    for it in items:
        sub = str(it.get("asset_sub_class") or "?")
        venue = str(it.get("trading_venue") or "?")
        by_sub[sub] = by_sub.get(sub, 0) + 1
        by_venue[venue] = by_venue.get(venue, 0) + 1
    print("by asset_sub_class:")
    for k in sorted(by_sub):
        print(f"  {k}: {by_sub[k]}")
    print("by trading_venue:")
    for k in sorted(by_venue):
        print(f"  {k}: {by_venue[k]}")
    sample = [str(it.get("product_code")) for it in items[:15]]
    if sample:
        print(f"sample product_codes: {', '.join(sample)}")


def main() -> None:
    _load_dotenv(REPO_ROOT / ".env")

    parser = argparse.ArgumentParser(
        description="Fetch Massive futures products (energy/metals) into futures_product."
    )
    parser.add_argument(
        "--asset-sub-class",
        default=",".join(DEFAULT_SUB_CLASSES),
        help="Comma-separated asset_sub_class filter (default: energy,metals)",
    )
    parser.add_argument(
        "--type",
        default="single",
        help="Product type filter: single | combo | all (default: single)",
    )
    parser.add_argument(
        "--as-of",
        default="",
        help="Point-in-time product date YYYY-MM-DD (default: today UTC). Empty string keeps API default.",
    )
    parser.add_argument(
        "--db-path",
        default=os.environ.get("NUMERAIRE_DB_PATH", "db.sqlite3"),
        help="SQLite path (default: NUMERAIRE_DB_PATH or db.sqlite3)",
    )
    parser.add_argument("--limit", type=int, default=1000, help="Page size (max 50000)")
    parser.add_argument(
        "--sleep-sec",
        type=float,
        default=0.0,
        help="Sleep between paginated HTTP calls (default 0)",
    )
    parser.add_argument("--dry-run", action="store_true", help="Fetch only; do not write DB")
    args = parser.parse_args()

    api_key = os.environ.get("POLYGON_API_KEY", "").strip()
    if not api_key:
        _die("POLYGON_API_KEY is not set (add to .env or export)")

    base_url = os.environ.get("POLYGON_BASE_URL", DEFAULT_POLYGON_BASE).strip() or DEFAULT_POLYGON_BASE
    sub_classes = _split_csv(args.asset_sub_class)
    if not sub_classes:
        _die("--asset-sub-class must list at least one value")

    product_type: str | None
    type_raw = str(args.type).strip().lower()
    if type_raw in ("", "all", "*"):
        product_type = None
    elif type_raw in ("single", "combo"):
        product_type = type_raw
    else:
        _die("--type must be single, combo, or all")

    as_of: str | None
    if args.as_of.strip() == "":
        as_of = date.today().isoformat()
    else:
        try:
            date.fromisoformat(args.as_of.strip())
        except ValueError:
            _die(f"--as-of must be YYYY-MM-DD, got {args.as_of!r}")
        as_of = args.as_of.strip()

    print(
        f"fetching futures products from {base_url} "
        f"(asset_sub_class={','.join(sub_classes)}, type={product_type or 'all'}, date={as_of})"
    )
    items = _fetch_products(
        base_url,
        api_key,
        asset_sub_classes=sub_classes,
        product_type=product_type,
        as_of=as_of,
        limit=max(1, min(int(args.limit), 50000)),
        sleep_sec=max(0.0, float(args.sleep_sec)),
    )
    if not items:
        _die("no products returned (check plan access / filters)")

    print(f"raw rows from API: {len(items)}")
    items = _dedupe_latest_by_product_code(items)
    print(f"after latest-per-product_code: {len(items)}")
    _summarize(items)

    ingested_at = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    rows = [r for r in (_row_from_api(it, ingested_at) for it in items) if r is not None]
    if not rows:
        _die("no rows with product_code")

    if args.dry_run:
        print(f"dry-run: would upsert {len(rows)} rows into futures_product")
        return

    db_path = Path(args.db_path)
    if not db_path.is_absolute():
        db_path = (Path.cwd() / db_path).resolve()
    if not db_path.is_file():
        _die(f"database not found: {db_path} (apply sql/schema_v1.sql or run dev_main bootstrap)")

    conn = sqlite3.connect(db_path)
    try:
        conn.execute("PRAGMA foreign_keys = ON")
        # Ensure table exists on DBs that predate this catalog.
        schema = (REPO_ROOT / "sql" / "schema_v1.sql").read_text(encoding="utf-8")
        conn.executescript(schema)
        n = _upsert_products(conn, rows)
    finally:
        conn.close()

    print(f"ok: upserted {n} futures_product rows -> {db_path}")


if __name__ == "__main__":
    main()
