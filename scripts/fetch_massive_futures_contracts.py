#!/usr/bin/env python3
"""
Fetch Massive/Polygon listed futures contracts into SQLite `futures_contract`.

Reads commodity scope from `universe_instrument` (active + ingest_futures flags)
and upserts contract tickers for a catalog day (`listing_as_of`). Use this strip
later to fetch session bars into `futures_daily_eod`.

Requires: POLYGON_API_KEY in environment or repo-root `.env`.
Optional: POLYGON_BASE_URL (default https://api.polygon.io).

Examples:
  python3 scripts/fetch_massive_futures_contracts.py --as-of 2026-08-12 --dry-run
  python3 scripts/fetch_massive_futures_contracts.py --as-of 2026-08-12
  python3 scripts/fetch_massive_futures_contracts.py --as-of 2026-08-12 --product-code CL,GC
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


def _parse_as_of(s: str) -> str:
    try:
        date.fromisoformat(s)
    except ValueError:
        _die(f"--as-of must be YYYY-MM-DD, got {s!r}")
    return s


def _split_csv(raw: str) -> list[str]:
    return [p.strip().upper() for p in raw.split(",") if p.strip()]


def _url_with_api_key(url: str, api_key: str) -> str:
    if "apiKey=" in url:
        return url
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}apiKey={urllib.parse.quote(api_key)}"


def _http_get_json(url: str, timeout_sec: float = 60.0) -> Mapping[str, Any]:
    req = urllib.request.Request(
        url,
        headers={"User-Agent": "numeraire-fetch-futures-contracts/1.0", "Accept": "application/json"},
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


def _universe_product_codes(conn: sqlite3.Connection) -> list[str]:
    cols = {r[1] for r in conn.execute("PRAGMA table_info(universe_instrument)")}
    if "ingest_futures_eod" in cols and "ingest_futures_product" in cols:
        sql = """
            SELECT provider_symbol FROM universe_instrument
            WHERE is_active = 1
              AND asset_class = 'COMMODITY'
              AND (ingest_futures_eod = 1 OR ingest_futures_product = 1)
            ORDER BY ingest_priority, provider_symbol
        """
    else:
        sql = """
            SELECT provider_symbol FROM universe_instrument
            WHERE is_active = 1 AND asset_class = 'COMMODITY'
            ORDER BY provider_symbol
        """
    return [str(r[0]).strip().upper() for r in conn.execute(sql) if r[0]]


def _fetch_contracts(
    base_url: str,
    api_key: str,
    *,
    product_code: str,
    as_of: str,
    active_only: bool,
    product_type: str | None,
    limit: int,
    sleep_sec: float,
) -> list[dict[str, Any]]:
    params: dict[str, str] = {
        "product_code": product_code,
        "date": as_of,
        "limit": str(limit),
        "sort": "ticker.asc",
    }
    if active_only:
        params["active"] = "true"
    if product_type:
        params["type"] = product_type

    path = "/futures/v1/contracts"
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
            _die(f"unexpected results type for {product_code} page {page}")
        for row in results:
            if isinstance(row, dict):
                out.append(row)
        print(f"    page {page}: +{len(results)} (total {len(out)})")
        next_url = payload.get("next_url")
        if not next_url:
            break
        url = _url_with_api_key(str(next_url), api_key)
        if sleep_sec > 0:
            time.sleep(sleep_sec)
    return out


def _as_int01(value: Any) -> int | None:
    if value is None:
        return None
    if isinstance(value, bool):
        return 1 if value else 0
    if isinstance(value, (int, float)):
        return 1 if int(value) else 0
    s = str(value).strip().lower()
    if s in ("1", "true", "t", "yes"):
        return 1
    if s in ("0", "false", "f", "no"):
        return 0
    return None


def _as_float(value: Any) -> float | None:
    if value is None or value == "":
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _as_int(value: Any) -> int | None:
    if value is None or value == "":
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _row_from_api(
    item: Mapping[str, Any],
    *,
    listing_as_of: str,
    fallback_product_code: str,
    ingested_at: str,
) -> tuple[Any, ...] | None:
    ticker = item.get("ticker")
    if not ticker:
        return None
    product_code = item.get("product_code") or fallback_product_code
    return (
        str(ticker),
        listing_as_of,
        str(product_code).upper(),
        item.get("name"),
        _as_int01(item.get("active")),
        item.get("type"),
        item.get("trading_venue"),
        item.get("group_code"),
        item.get("first_trade_date"),
        item.get("last_trade_date"),
        item.get("settlement_date"),
        _as_int(item.get("days_to_maturity")),
        _as_float(item.get("trade_tick_size")),
        _as_float(item.get("settlement_tick_size")),
        _as_float(item.get("spread_tick_size")),
        _as_int(item.get("min_order_quantity")),
        _as_int(item.get("max_order_quantity")),
        SOURCE,
        ingested_at,
    )


def _upsert_contracts(conn: sqlite3.Connection, rows: Iterable[tuple[Any, ...]]) -> int:
    sql = """
        INSERT INTO futures_contract (
            ticker, listing_as_of, product_code, name, active, type, trading_venue,
            group_code, first_trade_date, last_trade_date, settlement_date,
            days_to_maturity, trade_tick_size, settlement_tick_size, spread_tick_size,
            min_order_quantity, max_order_quantity, source, ingested_at
        ) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
        ON CONFLICT (ticker, listing_as_of) DO UPDATE SET
            product_code = excluded.product_code,
            name = excluded.name,
            active = excluded.active,
            type = excluded.type,
            trading_venue = excluded.trading_venue,
            group_code = excluded.group_code,
            first_trade_date = excluded.first_trade_date,
            last_trade_date = excluded.last_trade_date,
            settlement_date = excluded.settlement_date,
            days_to_maturity = excluded.days_to_maturity,
            trade_tick_size = excluded.trade_tick_size,
            settlement_tick_size = excluded.settlement_tick_size,
            spread_tick_size = excluded.spread_tick_size,
            min_order_quantity = excluded.min_order_quantity,
            max_order_quantity = excluded.max_order_quantity,
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


def main() -> None:
    _load_dotenv(REPO_ROOT / ".env")

    parser = argparse.ArgumentParser(
        description="Fetch Massive futures contracts for universe commodities into futures_contract."
    )
    parser.add_argument("--as-of", required=True, help="Catalog / listing date YYYY-MM-DD")
    parser.add_argument(
        "--product-code",
        default="",
        help="Override universe: comma-separated product codes (e.g. CL,GC)",
    )
    parser.add_argument(
        "--db-path",
        default=os.environ.get("NUMERAIRE_DB_PATH", "db.sqlite3"),
        help="SQLite path (default: NUMERAIRE_DB_PATH or db.sqlite3)",
    )
    parser.add_argument(
        "--include-inactive",
        action="store_true",
        help="Do not pass active=true (include inactive contracts)",
    )
    parser.add_argument(
        "--type",
        default="single",
        help="Contract type filter: single | combo | all (default: single)",
    )
    parser.add_argument("--limit", type=int, default=1000, help="Page size (max 1000)")
    parser.add_argument(
        "--sleep-sec",
        type=float,
        default=0.0,
        help="Sleep between HTTP calls (default 0)",
    )
    parser.add_argument("--dry-run", action="store_true", help="Fetch only; do not write DB")
    args = parser.parse_args()

    as_of = _parse_as_of(args.as_of)
    api_key = os.environ.get("POLYGON_API_KEY", "").strip()
    if not api_key:
        _die("POLYGON_API_KEY is not set (add to .env or export)")

    base_url = os.environ.get("POLYGON_BASE_URL", DEFAULT_POLYGON_BASE).strip() or DEFAULT_POLYGON_BASE

    type_raw = str(args.type).strip().lower()
    if type_raw in ("", "all", "*"):
        product_type: str | None = None
    elif type_raw in ("single", "combo"):
        product_type = type_raw
    else:
        _die("--type must be single, combo, or all")

    db_path = Path(args.db_path)
    if not db_path.is_absolute():
        db_path = (Path.cwd() / db_path).resolve()
    if not db_path.is_file():
        _die(f"database not found: {db_path}")

    conn = sqlite3.connect(db_path)
    try:
        conn.execute("PRAGMA foreign_keys = ON")
        # Ensure futures_contract exists on DBs that predate this table.
        conn.executescript((REPO_ROOT / "sql" / "schema_v1.sql").read_text(encoding="utf-8"))

        if args.product_code.strip():
            codes = _split_csv(args.product_code)
        else:
            codes = _universe_product_codes(conn)
        if not codes:
            _die("no commodity products in universe_instrument (seed scope first)")

        print(
            f"fetching futures contracts from {base_url} "
            f"(as_of={as_of}, products={','.join(codes)}, type={product_type or 'all'}, "
            f"active_only={not args.include_inactive})"
        )

        ingested_at = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        all_rows: list[tuple[Any, ...]] = []
        counts: dict[str, int] = {}

        for i, code in enumerate(codes):
            print(f"  {code}:")
            items = _fetch_contracts(
                base_url,
                api_key,
                product_code=code,
                as_of=as_of,
                active_only=not args.include_inactive,
                product_type=product_type,
                limit=max(1, min(int(args.limit), 1000)),
                sleep_sec=max(0.0, float(args.sleep_sec)),
            )
            rows = [
                r
                for r in (
                    _row_from_api(
                        it,
                        listing_as_of=as_of,
                        fallback_product_code=code,
                        ingested_at=ingested_at,
                    )
                    for it in items
                )
                if r is not None
            ]
            counts[code] = len(rows)
            all_rows.extend(rows)
            sample = [str(r[0]) for r in rows[:8]]
            print(f"    kept {len(rows)} contracts" + (f"  sample: {', '.join(sample)}" if sample else ""))
            if args.sleep_sec > 0 and i + 1 < len(codes):
                time.sleep(args.sleep_sec)

        if not all_rows:
            _die("no contracts returned for scope")

        print("summary:")
        for code in codes:
            print(f"  {code}: {counts.get(code, 0)}")

        if args.dry_run:
            print(f"dry-run: would upsert {len(all_rows)} futures_contract rows")
            return

        n = _upsert_contracts(conn, all_rows)
    finally:
        conn.close()

    print(f"ok: upserted {n} futures_contract rows @ {as_of} -> {db_path}")


if __name__ == "__main__":
    main()
