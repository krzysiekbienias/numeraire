#!/usr/bin/env python3
"""
Fetch Massive/Polygon futures session marks into SQLite `futures_daily_eod`.

Backfill / probe helper (daily production job should later live in C++ / dev_main).
Reads contract tickers from `futures_contract` for a listing day.

Sources:
  aggs     — `GET /futures/v1/aggs/{ticker}?resolution=1session` (historical session,
             including official `settlement_price` when published). Preferred for T-1 risk.
  snapshot — `GET /futures/v1/snapshot` (current delayed session). Good for live marks;
             `previous_settlement` is often 0, so it is *not* a historical settle API.

Massive session note (aggs): a session that settles on date D opens the evening before,
so we query `window_start=D-1` to obtain the bar with `session_end_date=D`.

Requires: POLYGON_API_KEY. Default sleep 0 (Futures Starter+); set sleep for basic tier.

Examples:
  # Official settle for risk T-1 (use listing strip from the next calendar day if needed)
  python3 scripts/fetch_massive_futures_daily_eod.py \\
      --as-of 2026-08-11 --listing-as-of 2026-08-12 --source aggs

  python3 scripts/fetch_massive_futures_daily_eod.py --as-of 2026-08-12 --source snapshot
  python3 scripts/fetch_massive_futures_daily_eod.py --as-of 2026-08-11 --product-code CL --dry-run
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
from datetime import date, datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Mapping, Sequence


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_POLYGON_BASE = "https://api.polygon.io"
SOURCE = "massive"
DEFAULT_SLEEP_SEC = 0.0
SNAPSHOT_TICKER_BATCH = 40


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
        headers={"User-Agent": "numeraire-fetch-futures-daily-eod/1.1", "Accept": "application/json"},
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
        "NUMERAIRE_POLYGON_EQUITY_SLEEP_SEC",
        "NUMERAIRE_POLYGON_SLEEP_SEC_AFTER_CALL",
    ):
        raw = os.environ.get(env, "").strip()
        if raw:
            try:
                return max(0.0, float(raw))
            except ValueError:
                continue
    return DEFAULT_SLEEP_SEC


def _session_window_start(as_of: str) -> str:
    """Session settling on as_of starts the prior calendar day (Massive docs)."""
    d = date.fromisoformat(as_of)
    return (d - timedelta(days=1)).isoformat()


def _load_tickers(
    conn: sqlite3.Connection,
    *,
    listing_as_of: str,
    product_codes: Sequence[str] | None,
    active_only: bool,
    max_contracts: int | None,
) -> list[tuple[str, str]]:
    sql = """
        SELECT ticker, product_code
        FROM futures_contract
        WHERE listing_as_of = ?
    """
    params: list[Any] = [listing_as_of]
    if product_codes:
        placeholders = ",".join("?" * len(product_codes))
        sql += f" AND UPPER(product_code) IN ({placeholders})"
        params.extend(product_codes)
    if active_only:
        sql += " AND active = 1"
    sql += " ORDER BY product_code, settlement_date, ticker"
    rows = [(str(r[0]), str(r[1] or "").upper()) for r in conn.execute(sql, params)]
    if max_contracts is not None and max_contracts >= 0:
        rows = rows[:max_contracts]
    return rows


def _ns_to_ms(value: Any) -> int | None:
    if value is None:
        return None
    try:
        ns = int(value)
    except (TypeError, ValueError):
        return None
    # Massive futures window_start is nanoseconds; equity bars use ms.
    if ns > 10_000_000_000_000:  # clearly ns
        return ns // 1_000_000
    return ns


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


def _bar_to_row(
    bar: Mapping[str, Any],
    *,
    ticker: str,
    product_code: str,
    expected_as_of: str,
    ingested_at: str,
    timespan: str = "1session",
) -> tuple[Any, ...] | None:
    session_end = str(bar.get("session_end_date") or "")
    if session_end and session_end != expected_as_of:
        return None
    as_of = session_end or expected_as_of
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
        as_of,
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
        timespan,
        _ns_to_ms(bar.get("window_start")),
        ingested_at,
    )


def _snapshot_to_bar(
    snap: Mapping[str, Any],
    *,
    expected_as_of: str,
    settle_field: str,
) -> Mapping[str, Any] | None:
    """Map a snapshot result to an aggs-like bar dict for `_bar_to_row`."""
    session = snap.get("session") if isinstance(snap.get("session"), dict) else {}
    details = snap.get("details") if isinstance(snap.get("details"), dict) else {}

    if settle_field == "previous_settlement":
        settle = _float_or_none(session.get("previous_settlement"), zero_as_none=True)
    elif settle_field == "settlement_price":
        settle = _float_or_none(session.get("settlement_price"), zero_as_none=True)
    else:
        # auto: prefer previous_settlement when present (T-1 style), else settlement_price
        settle = _float_or_none(session.get("previous_settlement"), zero_as_none=True)
        if settle is None:
            settle = _float_or_none(session.get("settlement_price"), zero_as_none=True)

    try:
        o = float(session["open"])
        h = float(session["high"])
        low = float(session["low"])
        c = float(session["close"])
    except (KeyError, TypeError, ValueError):
        # Settle-only mark (snapshot has no historical OHLC for a past as_of).
        if settle is None:
            return None
        o = h = low = c = settle

    # When we only trust settle for a past risk date, pin OHLC to settle so we do not
    # accidentally store today's intraday session as yesterday's bar.
    today = datetime.now(timezone.utc).date().isoformat()
    if expected_as_of < today and settle is not None:
        o = h = low = c = settle

    last_minute = snap.get("last_minute") if isinstance(snap.get("last_minute"), dict) else {}
    return {
        "open": o,
        "high": h,
        "low": low,
        "close": c,
        "settlement_price": settle,
        "volume": session.get("volume"),
        "dollar_volume": None,
        "transactions": None,
        "session_end_date": expected_as_of,
        "window_start": last_minute.get("last_updated") or details.get("settlement_date"),
    }


def _result_ticker(row: Mapping[str, Any]) -> str:
    if row.get("ticker"):
        return str(row["ticker"])
    details = row.get("details") if isinstance(row.get("details"), dict) else {}
    return str(details.get("ticker") or "")


def _fetch_session_bar(
    base_url: str,
    api_key: str,
    ticker: str,
    *,
    as_of: str,
) -> Mapping[str, Any] | None:
    window_start = _session_window_start(as_of)
    params = urllib.parse.urlencode(
        {
            "resolution": "1session",
            "window_start": window_start,
            "limit": "5",
        }
    )
    path = f"/futures/v1/aggs/{urllib.parse.quote(ticker)}"
    url = _url_with_api_key(f"{base_url.rstrip('/')}{path}?{params}", api_key)
    payload = _http_get_json(url)
    results = payload.get("results") or []
    if not isinstance(results, list) or not results:
        return None
    for bar in results:
        if isinstance(bar, dict) and str(bar.get("session_end_date") or "") == as_of:
            return bar
    first = results[0]
    return first if isinstance(first, dict) else None


def _fetch_snapshots_for_tickers(
    base_url: str,
    api_key: str,
    tickers: Sequence[str],
    *,
    sleep_sec: float,
) -> dict[str, Mapping[str, Any]]:
    """Batch `ticker.any_of` snapshot calls; return map ticker -> result row."""
    out: dict[str, Mapping[str, Any]] = {}
    root = base_url.rstrip("/")
    batches = [
        list(tickers[i : i + SNAPSHOT_TICKER_BATCH])
        for i in range(0, len(tickers), SNAPSHOT_TICKER_BATCH)
    ]
    for bi, batch in enumerate(batches, start=1):
        params = urllib.parse.urlencode(
            {
                "ticker.any_of": ",".join(batch),
                "limit": str(max(len(batch), 100)),
            }
        )
        url = _url_with_api_key(f"{root}/futures/v1/snapshot?{params}", api_key)
        payload = _http_get_json(url)
        results = payload.get("results") or []
        got = 0
        if isinstance(results, list):
            for row in results:
                if not isinstance(row, dict):
                    continue
                t = _result_ticker(row)
                if t:
                    out[t] = row
                    got += 1
        print(f"  snapshot batch {bi}/{len(batches)}: asked={len(batch)} got={got}")
        if sleep_sec > 0 and bi < len(batches):
            time.sleep(sleep_sec)
    return out


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
        description="Fetch Massive futures session marks into futures_daily_eod."
    )
    parser.add_argument("--as-of", required=True, help="Session end / risk date YYYY-MM-DD")
    parser.add_argument(
        "--listing-as-of",
        default="",
        help="futures_contract.listing_as_of (default: same as --as-of)",
    )
    parser.add_argument(
        "--source",
        choices=("aggs", "snapshot"),
        default="aggs",
        help="aggs = historical 1session (official settle); snapshot = current delayed marks",
    )
    parser.add_argument(
        "--settle-field",
        choices=("auto", "settlement_price", "previous_settlement"),
        default="auto",
        help="Snapshot only: which session field to store as settlement_price",
    )
    parser.add_argument(
        "--product-code",
        default="",
        help="Optional filter, comma-separated (e.g. CL,GC)",
    )
    parser.add_argument(
        "--db-path",
        default=os.environ.get("NUMERAIRE_DB_PATH", "db.sqlite3"),
        help="SQLite path (default: NUMERAIRE_DB_PATH or db.sqlite3)",
    )
    parser.add_argument(
        "--sleep-sec",
        type=float,
        default=None,
        help=f"Sleep after each HTTP call (default {_default_sleep_sec()} / env)",
    )
    parser.add_argument(
        "--max-contracts",
        type=int,
        default=0,
        help="Optional cap for probing (0 = all)",
    )
    parser.add_argument(
        "--include-inactive",
        action="store_true",
        help="Also fetch inactive contracts from futures_contract",
    )
    parser.add_argument("--dry-run", action="store_true", help="List tickers only")
    parser.add_argument(
        "--commit-every",
        type=int,
        default=50,
        help="Commit DB every N successful bars (default 50)",
    )
    args = parser.parse_args()

    as_of = _parse_as_of(args.as_of)
    listing_as_of = _parse_as_of(args.listing_as_of) if args.listing_as_of.strip() else as_of
    api_key = os.environ.get("POLYGON_API_KEY", "").strip()
    if not api_key:
        _die("POLYGON_API_KEY is not set (add to .env or export)")

    base_url = os.environ.get("POLYGON_BASE_URL", DEFAULT_POLYGON_BASE).strip() or DEFAULT_POLYGON_BASE
    sleep_sec = _default_sleep_sec() if args.sleep_sec is None else max(0.0, float(args.sleep_sec))
    product_codes = _split_csv(args.product_code) if args.product_code.strip() else None
    max_contracts = int(args.max_contracts) if int(args.max_contracts) > 0 else None

    db_path = Path(args.db_path)
    if not db_path.is_absolute():
        db_path = (Path.cwd() / db_path).resolve()
    if not db_path.is_file():
        _die(f"database not found: {db_path}")

    today = datetime.now(timezone.utc).date().isoformat()
    if args.source == "snapshot" and as_of < today:
        print(
            f"note: snapshot is a *current* feed; for historical as_of={as_of} prefer --source aggs "
            f"(official session_end_date + settlement_price). Continuing with settle-field="
            f"{args.settle_field} …",
            file=sys.stderr,
        )

    conn = sqlite3.connect(db_path)
    try:
        conn.execute("PRAGMA foreign_keys = ON")
        conn.executescript((REPO_ROOT / "sql" / "schema_v1.sql").read_text(encoding="utf-8"))

        tickers = _load_tickers(
            conn,
            listing_as_of=listing_as_of,
            product_codes=product_codes,
            active_only=not args.include_inactive,
            max_contracts=max_contracts,
        )
        if not tickers:
            _die(
                f"no futures_contract rows for listing_as_of={listing_as_of} "
                f"(run fetch_massive_futures_contracts.py first)"
            )

        window_start = _session_window_start(as_of)
        print(
            f"fetching futures marks from {base_url} "
            f"(source={args.source}, as_of={as_of}, listing_as_of={listing_as_of}, "
            f"window_start={window_start}, contracts={len(tickers)}, sleep_sec={sleep_sec})"
        )
        if args.dry_run:
            for t, p in tickers[:20]:
                print(f"  would fetch {t} ({p})")
            if len(tickers) > 20:
                print(f"  ... and {len(tickers) - 20} more")
            print(f"dry-run: would fetch {len(tickers)} contracts via {args.source}")
            return

        ingested_at = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        ok_rows: list[tuple[Any, ...]] = []
        missing = 0
        errors = 0
        with_settle = 0
        by_product: dict[str, int] = {}

        if args.source == "snapshot":
            try:
                snaps = _fetch_snapshots_for_tickers(
                    base_url,
                    api_key,
                    [t for t, _ in tickers],
                    sleep_sec=sleep_sec,
                )
            except RuntimeError as e:
                _die(str(e))

            for i, (ticker, product_code) in enumerate(tickers, start=1):
                snap = snaps.get(ticker)
                if snap is None:
                    missing += 1
                    print(f"  [{i}/{len(tickers)}] {ticker}: no snapshot")
                    continue
                bar = _snapshot_to_bar(
                    snap,
                    expected_as_of=as_of,
                    settle_field=args.settle_field,
                )
                if bar is None:
                    missing += 1
                    print(f"  [{i}/{len(tickers)}] {ticker}: snapshot skipped (no OHLC/settle)")
                    continue
                row = _bar_to_row(
                    bar,
                    ticker=ticker,
                    product_code=product_code,
                    expected_as_of=as_of,
                    ingested_at=ingested_at,
                )
                if row is None:
                    missing += 1
                    print(f"  [{i}/{len(tickers)}] {ticker}: bar skipped (bad/mismatch)")
                    continue
                ok_rows.append(row)
                by_product[product_code] = by_product.get(product_code, 0) + 1
                if row[8] is not None:
                    with_settle += 1
                settle = row[8]
                settle_s = f"{settle:.4f}" if isinstance(settle, float) else "n/a"
                print(
                    f"  [{i}/{len(tickers)}] {ticker}: close={row[7]:.4f} "
                    f"settle={settle_s} vol={row[10]}"
                )
                if len(ok_rows) >= max(1, int(args.commit_every)):
                    _upsert_bars(conn, ok_rows)
                    ok_rows.clear()
        else:
            for i, (ticker, product_code) in enumerate(tickers, start=1):
                try:
                    bar = _fetch_session_bar(base_url, api_key, ticker, as_of=as_of)
                except RuntimeError as e:
                    errors += 1
                    print(f"  [{i}/{len(tickers)}] {ticker}: ERROR {e}", file=sys.stderr)
                    if sleep_sec > 0:
                        time.sleep(sleep_sec)
                    continue

                if bar is None:
                    missing += 1
                    print(f"  [{i}/{len(tickers)}] {ticker}: no bar")
                else:
                    row = _bar_to_row(
                        bar,
                        ticker=ticker,
                        product_code=product_code,
                        expected_as_of=as_of,
                        ingested_at=ingested_at,
                    )
                    if row is None:
                        missing += 1
                        print(f"  [{i}/{len(tickers)}] {ticker}: bar skipped (bad/mismatch)")
                    else:
                        ok_rows.append(row)
                        by_product[product_code] = by_product.get(product_code, 0) + 1
                        if row[8] is not None:
                            with_settle += 1
                        settle = row[8]
                        settle_s = f"{settle:.4f}" if isinstance(settle, float) else "n/a"
                        print(
                            f"  [{i}/{len(tickers)}] {ticker}: close={row[7]:.4f} "
                            f"settle={settle_s} vol={row[10]}"
                        )
                        if len(ok_rows) >= max(1, int(args.commit_every)):
                            _upsert_bars(conn, ok_rows)
                            ok_rows.clear()

                if sleep_sec > 0 and i < len(tickers):
                    time.sleep(sleep_sec)

        if ok_rows:
            _upsert_bars(conn, ok_rows)

        print("summary by product:")
        for code in sorted(by_product):
            print(f"  {code}: {by_product[code]}")
        print(
            f"ok: wrote bars for as_of={as_of} source={args.source} -> {db_path} "
            f"(ok={sum(by_product.values())}, with_settle={with_settle}, "
            f"missing={missing}, errors={errors})"
        )
    finally:
        conn.close()


if __name__ == "__main__":
    main()
