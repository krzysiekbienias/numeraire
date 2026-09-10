#!/usr/bin/env python3
"""Move CME-manual futures EOD from a dump file — not the whole SQLite book.

Dev: after parse_cme_manual_settles.py --apply, export source=cme_manual rows.
SCP the JSON to prod. Prod: import insert-missing only (Massive bars stay).

  python3 scripts/sync_cme_manual_settles.py export --out /tmp/cme_manual.json
  python3 scripts/sync_cme_manual_settles.py export --as-of 2026-09-08 --out /tmp/cme_manual.json

  python3 scripts/sync_cme_manual_settles.py import --from /tmp/cme_manual.json
  python3 scripts/sync_cme_manual_settles.py import --from /tmp/cme_manual.json --apply

Never copies trades. Never UPDATEs an existing (ticker, as_of, 1session) row.
"""

from __future__ import annotations

import argparse
import json
import os
import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[1]
FORMAT = "numeraire.cme_manual_settles.v1"
SOURCE = "cme_manual"
TIMESPAN = "1session"
COLUMNS = (
    "ticker",
    "product_code",
    "as_of",
    "session_calendar",
    "open",
    "high",
    "low",
    "close",
    "settlement_price",
    "currency",
    "volume",
    "source",
    "timespan",
)


def default_db() -> str:
    return os.environ.get("NUMERAIRE_DB_PATH", str(REPO_ROOT / "db.sqlite3"))


def utc_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def row_to_obj(row: sqlite3.Row) -> dict[str, Any]:
    return {col: row[col] for col in COLUMNS}


def export_rows(
    db: sqlite3.Connection,
    *,
    as_of: str | None,
    products: list[str],
) -> list[dict[str, Any]]:
    sql = f"""
        SELECT {", ".join(COLUMNS)}
        FROM futures_daily_eod
        WHERE source = ? AND timespan = ?
    """
    params: list[Any] = [SOURCE, TIMESPAN]
    if as_of:
        sql += " AND as_of = ?"
        params.append(as_of)
    if products:
        placeholders = ",".join("?" * len(products))
        sql += f" AND upper(trim(product_code)) IN ({placeholders})"
        params.extend(products)
    sql += " ORDER BY as_of, product_code, ticker"
    db.row_factory = sqlite3.Row
    return [row_to_obj(r) for r in db.execute(sql, params)]


def cmd_export(args: argparse.Namespace) -> int:
    db_path = Path(args.db)
    if not db_path.is_file():
        raise SystemExit(f"database not found: {db_path}")
    products = [p.strip().upper() for p in args.products.split(",") if p.strip()]
    conn = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    try:
        rows = export_rows(conn, as_of=args.as_of, products=products)
    finally:
        conn.close()

    payload = {
        "format": FORMAT,
        "exported_at": utc_now(),
        "source_db": str(db_path.resolve()),
        "row_count": len(rows),
        "rows": rows,
    }
    text = json.dumps(payload, indent=2) + "\n"
    if args.out:
        out = Path(args.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text, encoding="utf-8")
        print(f"exported {len(rows)} cme_manual row(s) → {out}")
    else:
        sys.stdout.write(text)
    return 0


def load_payload(path: Path) -> list[dict[str, Any]]:
    raw = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise SystemExit("dump must be a JSON object")
    if raw.get("format") != FORMAT:
        raise SystemExit(f"unexpected format {raw.get('format')!r} (want {FORMAT})")
    rows = raw.get("rows")
    if not isinstance(rows, list):
        raise SystemExit("dump missing rows[]")
    cleaned: list[dict[str, Any]] = []
    for i, rec in enumerate(rows):
        if not isinstance(rec, dict):
            raise SystemExit(f"row {i} is not an object")
        if rec.get("source") != SOURCE:
            raise SystemExit(f"row {i} source={rec.get('source')!r} (only {SOURCE} allowed)")
        if rec.get("timespan") not in (None, TIMESPAN):
            raise SystemExit(f"row {i} timespan={rec.get('timespan')!r}")
        ticker = rec.get("ticker")
        as_of = rec.get("as_of")
        px = rec.get("settlement_price")
        if not ticker or not as_of:
            raise SystemExit(f"row {i} missing ticker/as_of")
        if px is None:
            raise SystemExit(f"row {i} {ticker} {as_of}: settlement_price is null")
        cleaned.append(rec)
    return cleaned


def classify(db: sqlite3.Connection, rec: dict[str, Any]) -> str:
    ticker = rec["ticker"]
    as_of = rec["as_of"]
    row = db.execute(
        """
        SELECT settlement_price, source FROM futures_daily_eod
        WHERE ticker = ? AND as_of = ? AND timespan = ?
        """,
        (ticker, as_of, TIMESPAN),
    ).fetchone()
    rec["have_settle"] = row[0] if row else None
    rec["have_source"] = row[1] if row else None
    rec["action"] = "SKIP_HAVE" if row else "WOULD_INSERT"
    return rec["action"]


def print_table(rows: list[dict[str, Any]]) -> None:
    headers = ("ticker", "as_of", "product", "settle", "db_settle", "db_source", "action")
    lines: list[tuple[str, ...]] = []
    widths = [len(h) for h in headers]
    for r in rows:
        settle = r.get("settlement_price")
        have = r.get("have_settle")
        line = (
            str(r.get("ticker") or ""),
            str(r.get("as_of") or ""),
            str(r.get("product_code") or ""),
            "" if settle is None else f"{float(settle):.4g}",
            "" if have is None else f"{float(have):.4g}",
            str(r.get("have_source") or ""),
            str(r.get("action") or ""),
        )
        lines.append(line)
        widths = [max(w, len(x)) for w, x in zip(widths, line)]
    fmt = "  ".join(f"{{:{w}}}" for w in widths)
    print(fmt.format(*headers))
    print(fmt.format(*("-" * w for w in widths)))
    for line in lines:
        print(fmt.format(*line))
    counts: dict[str, int] = {}
    for r in rows:
        counts[r["action"]] = counts.get(r["action"], 0) + 1
    print()
    print("counts:", "  ".join(f"{k}={v}" for k, v in sorted(counts.items())))


def apply_inserts(db: sqlite3.Connection, rows: list[dict[str, Any]]) -> int:
    now = utc_now()
    sql = """
        INSERT INTO futures_daily_eod (
            ticker, product_code, as_of, session_calendar,
            open, high, low, close, settlement_price,
            currency, volume, source, timespan, ingested_at
        )
        SELECT
            ?, ?, ?, ?,
            ?, ?, ?, ?, ?,
            ?, ?, ?, ?, ?
        WHERE NOT EXISTS (
            SELECT 1 FROM futures_daily_eod
            WHERE ticker = ? AND as_of = ? AND timespan = ?
        )
    """
    n = 0
    for r in rows:
        if r["action"] != "WOULD_INSERT":
            continue
        px = float(r["settlement_price"])
        ticker = r["ticker"]
        as_of = r["as_of"]
        vol = r.get("volume")
        if vol is None:
            vol = 0.0
        cur = db.execute(
            sql,
            (
                ticker,
                r.get("product_code"),
                as_of,
                r.get("session_calendar") or "America/Chicago",
                float(r.get("open") if r.get("open") is not None else px),
                float(r.get("high") if r.get("high") is not None else px),
                float(r.get("low") if r.get("low") is not None else px),
                float(r.get("close") if r.get("close") is not None else px),
                px,
                r.get("currency") or "USD",
                float(vol),
                SOURCE,
                TIMESPAN,
                now,
                ticker,
                as_of,
                TIMESPAN,
            ),
        )
        n += cur.rowcount
        r["action"] = "INSERTED" if cur.rowcount else "SKIP_HAVE"
    return n


def cmd_import(args: argparse.Namespace) -> int:
    dump = Path(args.from_path)
    if not dump.is_file():
        raise SystemExit(f"dump not found: {dump}")
    db_path = Path(args.db)
    if not db_path.is_file():
        raise SystemExit(f"database not found: {db_path}")

    rows = load_payload(dump)
    conn = sqlite3.connect(str(db_path))
    try:
        for rec in rows:
            classify(conn, rec)
        if args.apply:
            inserted = apply_inserts(conn, rows)
            conn.commit()
            print(f"applied vs {db_path}: inserted={inserted}")
        else:
            print(f"dry-run vs {db_path} (no inserts)")
        print()
        print_table(rows)
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Export/import futures_daily_eod rows with source=cme_manual."
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_ex = sub.add_parser("export", help="dump cme_manual rows from this SQLite (dev)")
    p_ex.add_argument("--db", default=default_db())
    p_ex.add_argument("--out", help="write JSON here (default: stdout)")
    p_ex.add_argument("--as-of", dest="as_of", help="only this session date")
    p_ex.add_argument(
        "--products",
        default="",
        help="comma product_code filter (e.g. CL,NG); default all",
    )

    p_im = sub.add_parser("import", help="insert-missing into this SQLite (prod)")
    p_im.add_argument("--from", dest="from_path", required=True, help="JSON from export")
    p_im.add_argument("--db", default=default_db())
    p_im.add_argument(
        "--apply",
        action="store_true",
        help="INSERT WOULD_INSERT rows. Default: dry-run.",
    )

    args = parser.parse_args()
    if args.cmd == "export":
        return cmd_export(args)
    return cmd_import(args)


if __name__ == "__main__":
    raise SystemExit(main())
