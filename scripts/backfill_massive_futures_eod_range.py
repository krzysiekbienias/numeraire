#!/usr/bin/env python3
"""
Backfill Massive futures contracts + session EOD for universe commodities.

Strategy (maximize tenors, minimize HTTP):
  1. Snapshot `futures_contract` on monthly listing dates in [--from, --to].
  2. Union distinct single-contract tickers across those snapshots (+ optional
     extra listing dates).
  3. For each ticker, one ranged `1session` aggs call:
       window_start.gte = from-1 day, window_start.lte = to-1 day
     then upsert every bar whose session_end_date is in [from, to].

Requires POLYGON_API_KEY. Default sleep 0 (Futures Starter+).

Examples:
  python3 scripts/backfill_massive_futures_eod_range.py \\
      --from 2025-01-01 --to 2026-08-11

  python3 scripts/backfill_massive_futures_eod_range.py \\
      --from 2025-01-01 --to 2026-08-11 --product-code CL --dry-run
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sqlite3
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import date, datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Mapping, Sequence

_OUTRIGHT_TICKER_RE = re.compile(r"^[A-Z]{1,4}[FGHJKMNQUVXZ]\d{1,2}$")


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_POLYGON_BASE = "https://api.polygon.io"
SOURCE = "massive"
DEFAULT_SLEEP_SEC = 0.0


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
        _die(f"date must be YYYY-MM-DD, got {s!r}")
    return s


def _split_csv(raw: str) -> list[str]:
    return [p.strip().upper() for p in raw.split(",") if p.strip()]


def _url_with_api_key(url: str, api_key: str) -> str:
    if "apiKey=" in url:
        return url
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}apiKey={urllib.parse.quote(api_key)}"


def _http_get_json(url: str, timeout_sec: float = 90.0) -> Mapping[str, Any]:
    req = urllib.request.Request(
        url,
        headers={
            "User-Agent": "numeraire-backfill-futures-eod/1.0",
            "Accept": "application/json",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout_sec) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")[:800]
        raise RuntimeError(f"HTTP {e.code} for {url.split('?', 1)[0]}: {body}") from e
    except urllib.error.URLError as e:
        raise RuntimeError(f"request failed: {e}") from e


def _default_sleep_sec() -> float:
    for env in (
        "NUMERAIRE_POLYGON_FUTURES_SLEEP_SEC",
        "NUMERAIRE_POLYGON_SLEEP_SEC_AFTER_CALL",
    ):
        raw = os.environ.get(env, "").strip()
        if raw:
            try:
                return max(0.0, float(raw))
            except ValueError:
                continue
    return DEFAULT_SLEEP_SEC


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


def _is_outright_single_ticker(ticker: str) -> bool:
    """Keep CLU6 / NGU26 / GCZ6; drop combo symbols like CL:BF F6-G6-H6."""
    t = (ticker or "").strip().upper()
    if not t or ":" in t or " " in t:
        return False
    # product letters + CME month code + 1–2 digit year (and far CLF30-style)
    return bool(_OUTRIGHT_TICKER_RE.fullmatch(t))


def _month_listing_dates(start: date, end: date) -> list[str]:
    """Prefer ~3rd of each month (avoids some holiday empties); always include end."""
    out: list[str] = []
    y, m = start.year, start.month
    while True:
        d = date(y, m, min(3, 28))
        if d < start:
            d = start
        if d > end:
            break
        out.append(d.isoformat())
        if m == 12:
            y, m = y + 1, 1
        else:
            m += 1
    if end.isoformat() not in out:
        out.append(end.isoformat())
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


def _float_or_none(value: Any, *, zero_as_none: bool = False) -> float | None:
    if value is None:
        return None
    try:
        out = float(value)
    except (TypeError, ValueError):
        return None
    if zero_as_none and out == 0.0:
        return None
    return out


def _ns_to_ms(value: Any) -> int | None:
    if value is None:
        return None
    try:
        ns = int(value)
    except (TypeError, ValueError):
        return None
    if ns > 10_000_000_000_000:
        return ns // 1_000_000
    return ns


def _fetch_contracts_page(
    base_url: str,
    api_key: str,
    *,
    product_code: str,
    as_of: str,
    sleep_sec: float,
) -> list[dict[str, Any]]:
    params = {
        "product_code": product_code,
        "date": as_of,
        "type": "single",
        "active": "true",
        "limit": "1000",
        "sort": "ticker.asc",
    }
    url = _url_with_api_key(
        f"{base_url.rstrip('/')}/futures/v1/contracts?{urllib.parse.urlencode(params)}",
        api_key,
    )
    out: list[dict[str, Any]] = []
    while url:
        payload = _http_get_json(url)
        results = payload.get("results") or []
        if isinstance(results, list):
            for row in results:
                if isinstance(row, dict):
                    out.append(row)
        next_url = payload.get("next_url")
        if not next_url:
            break
        url = _url_with_api_key(str(next_url), api_key)
        if sleep_sec > 0:
            time.sleep(sleep_sec)
    return out


def _contract_row(
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


def _upsert_contracts(conn: sqlite3.Connection, rows: Sequence[tuple[Any, ...]]) -> int:
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
    for row in rows:
        cur.execute(sql, row)
    conn.commit()
    return len(rows)


def _fetch_session_bars_range(
    base_url: str,
    api_key: str,
    ticker: str,
    *,
    window_start_gte: str,
    window_start_lte: str,
) -> list[dict[str, Any]]:
    params = {
        "resolution": "1session",
        "window_start.gte": window_start_gte,
        "window_start.lte": window_start_lte,
        "limit": "50000",
        "sort": "window_start.asc",
    }
    url = _url_with_api_key(
        f"{base_url.rstrip('/')}/futures/v1/aggs/{urllib.parse.quote(ticker)}"
        f"?{urllib.parse.urlencode(params)}",
        api_key,
    )
    out: list[dict[str, Any]] = []
    while url:
        payload = _http_get_json(url)
        results = payload.get("results") or []
        if isinstance(results, list):
            for row in results:
                if isinstance(row, dict):
                    out.append(row)
        next_url = payload.get("next_url")
        if not next_url:
            break
        url = _url_with_api_key(str(next_url), api_key)
    return out


def _bar_to_row(
    bar: Mapping[str, Any],
    *,
    ticker: str,
    product_code: str,
    as_of_min: str,
    as_of_max: str,
    ingested_at: str,
) -> tuple[Any, ...] | None:
    session_end = str(bar.get("session_end_date") or "")
    if not session_end or session_end < as_of_min or session_end > as_of_max:
        return None
    try:
        o = float(bar["open"])
        h = float(bar["high"])
        low = float(bar["low"])
        c = float(bar["close"])
    except (KeyError, TypeError, ValueError):
        return None

    settlement_f = _float_or_none(bar.get("settlement_price"), zero_as_none=True)
    volume_f = _float_or_none(bar.get("volume"))
    dollar_f = _float_or_none(bar.get("dollar_volume"))
    try:
        trade_count = int(bar["transactions"]) if bar.get("transactions") is not None else None
    except (TypeError, ValueError):
        trade_count = None

    vwap = None
    if dollar_f is not None and volume_f and volume_f > 0:
        vwap = dollar_f / volume_f

    return (
        ticker,
        product_code or None,
        session_end,
        "America/Chicago",
        o,
        h,
        low,
        c,
        settlement_f,
        "USD",
        volume_f,
        dollar_f,
        vwap,
        trade_count,
        SOURCE,
        "1session",
        _ns_to_ms(bar.get("window_start")),
        ingested_at,
    )


def _upsert_bars(conn: sqlite3.Connection, rows: Sequence[tuple[Any, ...]]) -> int:
    sql = """
        INSERT INTO futures_daily_eod (
            ticker, product_code, as_of, session_calendar, open, high, low, close,
            settlement_price, currency, volume, dollar_volume, vwap, trade_count,
            source, timespan, provider_timestamp_utc_ms, ingested_at
        ) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
        ON CONFLICT (ticker, as_of, timespan) DO UPDATE SET
            product_code = excluded.product_code,
            session_calendar = excluded.session_calendar,
            open = excluded.open,
            high = excluded.high,
            low = excluded.low,
            close = excluded.close,
            settlement_price = excluded.settlement_price,
            currency = excluded.currency,
            volume = excluded.volume,
            dollar_volume = excluded.dollar_volume,
            vwap = excluded.vwap,
            trade_count = excluded.trade_count,
            source = excluded.source,
            provider_timestamp_utc_ms = excluded.provider_timestamp_utc_ms,
            ingested_at = excluded.ingested_at
    """
    cur = conn.cursor()
    for row in rows:
        cur.execute(sql, row)
    conn.commit()
    return len(rows)


def main() -> None:
    _load_dotenv(REPO_ROOT / ".env")

    parser = argparse.ArgumentParser(
        description="Backfill Massive futures contracts + 1session EOD for a date range."
    )
    parser.add_argument("--from", dest="date_from", required=True, help="First session_end date YYYY-MM-DD")
    parser.add_argument("--to", dest="date_to", required=True, help="Last session_end date YYYY-MM-DD")
    parser.add_argument(
        "--product-code",
        default="",
        help="Optional filter, comma-separated (default: universe COMMODITY)",
    )
    parser.add_argument(
        "--db-path",
        default=os.environ.get("NUMERAIRE_DB_PATH", "db.sqlite3"),
        help="SQLite path",
    )
    parser.add_argument(
        "--sleep-sec",
        type=float,
        default=None,
        help=f"Sleep after HTTP calls (default {_default_sleep_sec()})",
    )
    parser.add_argument(
        "--skip-contracts",
        action="store_true",
        help="Do not refresh monthly contract snapshots; use tickers already in DB",
    )
    parser.add_argument(
        "--max-tickers",
        type=int,
        default=0,
        help="Optional cap for probing (0 = all)",
    )
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    date_from = _parse_as_of(args.date_from)
    date_to = _parse_as_of(args.date_to)
    if date_from > date_to:
        _die("--from must be <= --to")

    api_key = os.environ.get("POLYGON_API_KEY", "").strip()
    if not api_key:
        _die("POLYGON_API_KEY is not set")

    base_url = os.environ.get("POLYGON_BASE_URL", DEFAULT_POLYGON_BASE).strip() or DEFAULT_POLYGON_BASE
    sleep_sec = _default_sleep_sec() if args.sleep_sec is None else max(0.0, float(args.sleep_sec))

    db_path = Path(args.db_path)
    if not db_path.is_absolute():
        db_path = (Path.cwd() / db_path).resolve()
    if not db_path.is_file():
        _die(f"database not found: {db_path}")

    d0 = date.fromisoformat(date_from)
    d1 = date.fromisoformat(date_to)
    # Session settling on D has window_start = D-1.
    window_gte = (d0 - timedelta(days=1)).isoformat()
    window_lte = (d1 - timedelta(days=1)).isoformat()
    listing_dates = _month_listing_dates(d0, d1)

    conn = sqlite3.connect(db_path)
    try:
        conn.execute("PRAGMA foreign_keys = ON")
        conn.executescript((REPO_ROOT / "sql" / "schema_v1.sql").read_text(encoding="utf-8"))

        if args.product_code.strip():
            codes = _split_csv(args.product_code)
        else:
            codes = _universe_product_codes(conn)
        if not codes:
            _die("no commodity products in universe (seed first or pass --product-code)")

        print(
            f"backfill futures EOD {base_url}\n"
            f"  products={','.join(codes)}  as_of=[{date_from} .. {date_to}]\n"
            f"  window_start=[{window_gte} .. {window_lte}]  listing_dates={len(listing_dates)}  "
            f"sleep_sec={sleep_sec}"
        )

        ingested_at = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        ticker_product: dict[str, str] = {}

        if not args.skip_contracts:
            contract_rows: list[tuple[Any, ...]] = []
            empty_listings = 0
            for li, listing_as_of in enumerate(listing_dates, start=1):
                day_n = 0
                for code in codes:
                    try:
                        items = _fetch_contracts_page(
                            base_url,
                            api_key,
                            product_code=code,
                            as_of=listing_as_of,
                            sleep_sec=sleep_sec,
                        )
                    except RuntimeError as e:
                        print(f"  contracts {listing_as_of} {code}: ERROR {e}", file=sys.stderr)
                        continue
                    for it in items:
                        row = _contract_row(
                            it,
                            listing_as_of=listing_as_of,
                            fallback_product_code=code,
                            ingested_at=ingested_at,
                        )
                        if row is None:
                            continue
                        if not _is_outright_single_ticker(str(row[0])):
                            continue
                        contract_rows.append(row)
                        ticker_product[str(row[0])] = str(row[2]).upper()
                        day_n += 1
                    if sleep_sec > 0:
                        time.sleep(sleep_sec)
                print(f"  [{li}/{len(listing_dates)}] listing {listing_as_of}: {day_n} singles")
                if day_n == 0:
                    empty_listings += 1
                if not args.dry_run and len(contract_rows) >= 500:
                    _upsert_contracts(conn, contract_rows)
                    contract_rows.clear()
            if not args.dry_run and contract_rows:
                _upsert_contracts(conn, contract_rows)
            print(f"  contract snapshots done (empty listing days={empty_listings})")
        else:
            print("  skip-contracts: loading tickers from existing futures_contract")

        # Always enrich ticker set from DB for these products (covers prior runs).
        placeholders = ",".join("?" * len(codes))
        for t, p in conn.execute(
            f"""
            SELECT DISTINCT ticker, UPPER(product_code)
            FROM futures_contract
            WHERE UPPER(product_code) IN ({placeholders})
              AND (type IS NULL OR type = '' OR type = 'single')
            ORDER BY product_code, ticker
            """,
            codes,
        ):
            if _is_outright_single_ticker(str(t)):
                ticker_product[str(t)] = str(p)

        tickers = sorted(ticker_product.items(), key=lambda kv: (kv[1], kv[0]))
        if args.max_tickers and args.max_tickers > 0:
            tickers = tickers[: int(args.max_tickers)]

        print(f"  unique tickers for EOD range pull: {len(tickers)}")
        if args.dry_run:
            for t, p in tickers[:30]:
                print(f"    would fetch {t} ({p})")
            if len(tickers) > 30:
                print(f"    ... and {len(tickers) - 30} more")
            print("dry-run: stop before aggs")
            return

        ok_bars = 0
        with_settle = 0
        empty_tickers = 0
        errors = 0
        by_product_bars: dict[str, int] = {}
        pending: list[tuple[Any, ...]] = []

        for i, (ticker, product_code) in enumerate(tickers, start=1):
            try:
                bars = _fetch_session_bars_range(
                    base_url,
                    api_key,
                    ticker,
                    window_start_gte=window_gte,
                    window_start_lte=window_lte,
                )
            except RuntimeError as e:
                errors += 1
                print(f"  [{i}/{len(tickers)}] {ticker}: ERROR {e}", file=sys.stderr)
                if sleep_sec > 0:
                    time.sleep(sleep_sec)
                continue

            n_ok = 0
            n_settle = 0
            for bar in bars:
                row = _bar_to_row(
                    bar,
                    ticker=ticker,
                    product_code=product_code,
                    as_of_min=date_from,
                    as_of_max=date_to,
                    ingested_at=ingested_at,
                )
                if row is None:
                    continue
                pending.append(row)
                n_ok += 1
                if row[8] is not None:
                    n_settle += 1

            if n_ok == 0:
                empty_tickers += 1
                print(f"  [{i}/{len(tickers)}] {ticker}: no bars in range")
            else:
                ok_bars += n_ok
                with_settle += n_settle
                by_product_bars[product_code] = by_product_bars.get(product_code, 0) + n_ok
                print(
                    f"  [{i}/{len(tickers)}] {ticker}: bars={n_ok} with_settle={n_settle} "
                    f"(raw_api={len(bars)})"
                )

            if len(pending) >= 2000:
                _upsert_bars(conn, pending)
                pending.clear()

            if sleep_sec > 0 and i < len(tickers):
                time.sleep(sleep_sec)

        if pending:
            _upsert_bars(conn, pending)

        print("summary bars by product:")
        for code in sorted(by_product_bars):
            print(f"  {code}: {by_product_bars[code]}")
        print(
            f"ok: backfill -> {db_path}  tickers={len(tickers)}  bars={ok_bars}  "
            f"with_settle={with_settle}  empty_tickers={empty_tickers}  errors={errors}"
        )

        # Coverage snapshot
        print("coverage (DB):")
        for row in conn.execute(
            f"""
            SELECT product_code,
                   COUNT(*) AS bars,
                   COUNT(DISTINCT ticker) AS tickers,
                   COUNT(DISTINCT as_of) AS days,
                   SUM(CASE WHEN settlement_price IS NOT NULL THEN 1 ELSE 0 END) AS with_settle,
                   MIN(as_of), MAX(as_of)
            FROM futures_daily_eod
            WHERE as_of >= ? AND as_of <= ?
              AND UPPER(product_code) IN ({placeholders})
            GROUP BY product_code
            ORDER BY product_code
            """,
            [date_from, date_to, *codes],
        ):
            print(
                f"  {row[0]}: bars={row[1]} tickers={row[2]} days={row[3]} "
                f"settle={row[4]} range={row[5]}..{row[6]}"
            )
    finally:
        conn.close()


if __name__ == "__main__":
    main()
