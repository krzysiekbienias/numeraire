#!/usr/bin/env python3
"""
Upsert commodity futures underliers into SQLite `universe_instrument`.

Local helper to refresh the controlled ingest/pricing scope (CL, GC, SI, NG by
default). Does not fetch prices — only universe rows. Optionally enriches
display_name / sector from `futures_product` when that catalog is present.

Requires: existing DB with `universe_instrument` (apply sql/schema_v1.sql and
column patches for ingest_futures_* if needed).

Examples:
  python3 scripts/seed_universe_commodity_futures.py --dry-run
  python3 scripts/seed_universe_commodity_futures.py
  python3 scripts/seed_universe_commodity_futures.py --config configs/universe_commodity_futures.json
"""

from __future__ import annotations

import argparse
import json
import os
import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Mapping, Sequence


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONFIG = REPO_ROOT / "configs" / "universe_commodity_futures.json"

REQUIRED_KEYS = ("instrument_id", "provider_symbol", "asset_class")


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


def _load_config(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        _die(f"config not found: {path}")
    with path.open(encoding="utf-8") as f:
        cfg = json.load(f)
    instruments = cfg.get("instruments")
    if not isinstance(instruments, list) or not instruments:
        _die(f"config must contain non-empty 'instruments' list: {path}")
    out: list[dict[str, Any]] = []
    for i, row in enumerate(instruments):
        if not isinstance(row, dict):
            _die(f"instruments[{i}] must be an object")
        for key in REQUIRED_KEYS:
            if not row.get(key):
                _die(f"instruments[{i}] missing required key {key!r}")
        out.append(dict(row))
    return out


def _table_columns(conn: sqlite3.Connection, table: str) -> set[str]:
    cur = conn.execute(f"PRAGMA table_info({table})")
    return {str(r[1]) for r in cur.fetchall()}


def _ensure_futures_flag_columns(conn: sqlite3.Connection) -> None:
    cols = _table_columns(conn, "universe_instrument")
    if not cols:
        _die("table universe_instrument missing — apply sql/schema_v1.sql first")
    for col in ("ingest_futures_product", "ingest_futures_eod"):
        if col not in cols:
            conn.execute(
                f"ALTER TABLE universe_instrument ADD COLUMN {col} "
                f"INTEGER NOT NULL DEFAULT 0 CHECK ({col} IN (0, 1))"
            )
            print(f"patched universe_instrument.{col}")
    conn.commit()


def _futures_product_meta(
    conn: sqlite3.Connection, product_code: str
) -> Mapping[str, Any] | None:
    cols = _table_columns(conn, "futures_product")
    if not cols:
        return None
    cur = conn.execute(
        "SELECT name, asset_sub_class, sector FROM futures_product WHERE product_code = ?",
        (product_code,),
    )
    row = cur.fetchone()
    if not row:
        return None
    return {"name": row[0], "asset_sub_class": row[1], "sector": row[2]}


def _normalize_row(
    raw: Mapping[str, Any],
    *,
    enrich: Mapping[str, Any] | None,
    now: str,
) -> dict[str, Any]:
    row = {
        "instrument_id": str(raw["instrument_id"]).strip().upper(),
        "provider_symbol": str(raw["provider_symbol"]).strip(),
        "display_name": raw.get("display_name"),
        "asset_class": str(raw["asset_class"]).strip().upper(),
        "sector": raw.get("sector"),
        "industry": raw.get("industry"),
        "quote_currency": str(raw.get("quote_currency") or "USD"),
        "session_calendar": str(raw.get("session_calendar") or "America/Chicago"),
        "country": raw.get("country"),
        "data_vendor": str(raw.get("data_vendor") or "POLYGON"),
        "is_active": int(raw.get("is_active", 1)),
        "ingest_equity_eod": int(raw.get("ingest_equity_eod", 0)),
        "ingest_index_eod": int(raw.get("ingest_index_eod", 0)),
        "ingest_futures_product": int(raw.get("ingest_futures_product", 1)),
        "ingest_futures_eod": int(raw.get("ingest_futures_eod", 1)),
        "ingest_priority": int(raw.get("ingest_priority", 100)),
        "notes": raw.get("notes"),
        "created_at": now,
        "updated_at": now,
    }
    if enrich:
        if not row["display_name"] and enrich.get("name"):
            row["display_name"] = enrich["name"]
        if not row["sector"] and enrich.get("sector"):
            row["sector"] = enrich["sector"]
        if not row["industry"] and enrich.get("asset_sub_class"):
            row["industry"] = enrich["asset_sub_class"]
    if row["asset_class"] != "COMMODITY":
        _die(f"{row['instrument_id']}: this seeder expects asset_class=COMMODITY")
    return row


def _upsert(conn: sqlite3.Connection, row: Mapping[str, Any]) -> None:
    conn.execute(
        """
        INSERT INTO universe_instrument (
            instrument_id, provider_symbol, display_name, asset_class, sector, industry,
            quote_currency, session_calendar, country, data_vendor, is_active,
            ingest_equity_eod, ingest_index_eod, ingest_futures_product, ingest_futures_eod,
            ingest_priority, notes, created_at, updated_at
        ) VALUES (
            :instrument_id, :provider_symbol, :display_name, :asset_class, :sector, :industry,
            :quote_currency, :session_calendar, :country, :data_vendor, :is_active,
            :ingest_equity_eod, :ingest_index_eod, :ingest_futures_product, :ingest_futures_eod,
            :ingest_priority, :notes, :created_at, :updated_at
        )
        ON CONFLICT (instrument_id) DO UPDATE SET
            provider_symbol = excluded.provider_symbol,
            display_name = excluded.display_name,
            asset_class = excluded.asset_class,
            sector = excluded.sector,
            industry = excluded.industry,
            quote_currency = excluded.quote_currency,
            session_calendar = excluded.session_calendar,
            country = excluded.country,
            data_vendor = excluded.data_vendor,
            is_active = excluded.is_active,
            ingest_equity_eod = excluded.ingest_equity_eod,
            ingest_index_eod = excluded.ingest_index_eod,
            ingest_futures_product = excluded.ingest_futures_product,
            ingest_futures_eod = excluded.ingest_futures_eod,
            ingest_priority = excluded.ingest_priority,
            notes = excluded.notes,
            updated_at = excluded.updated_at
        """,
        row,
    )


def main() -> None:
    _load_dotenv(REPO_ROOT / ".env")

    parser = argparse.ArgumentParser(
        description="Upsert commodity futures underliers into universe_instrument."
    )
    parser.add_argument(
        "--config",
        default=str(DEFAULT_CONFIG),
        help="JSON scope file (default: configs/universe_commodity_futures.json)",
    )
    parser.add_argument(
        "--db-path",
        default=os.environ.get("NUMERAIRE_DB_PATH", "db.sqlite3"),
        help="SQLite path (default: NUMERAIRE_DB_PATH or db.sqlite3)",
    )
    parser.add_argument(
        "--no-enrich",
        action="store_true",
        help="Do not fill blanks from futures_product catalog",
    )
    parser.add_argument("--dry-run", action="store_true", help="Print rows; do not write DB")
    args = parser.parse_args()

    instruments = _load_config(Path(args.config))
    now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    db_path = Path(args.db_path)
    if not db_path.is_absolute():
        db_path = (Path.cwd() / db_path).resolve()
    if not db_path.is_file():
        _die(f"database not found: {db_path}")

    conn = sqlite3.connect(db_path)
    try:
        conn.execute("PRAGMA foreign_keys = ON")
        _ensure_futures_flag_columns(conn)

        prepared: list[dict[str, Any]] = []
        for raw in instruments:
            enrich = None
            if not args.no_enrich:
                enrich = _futures_product_meta(conn, str(raw["provider_symbol"]))
            prepared.append(_normalize_row(raw, enrich=enrich, now=now))

        for row in prepared:
            print(
                f"  {row['instrument_id']:<6} {row['provider_symbol']:<6} "
                f"{row['industry'] or '-':<8} {row['sector'] or '-':<12} "
                f"futures_eod={row['ingest_futures_eod']}  {row['display_name']}"
            )

        if args.dry_run:
            print(f"dry-run: would upsert {len(prepared)} universe_instrument rows")
            return

        for row in prepared:
            _upsert(conn, row)
        conn.commit()
    finally:
        conn.close()

    print(f"ok: upserted {len(prepared)} commodity universe rows -> {db_path}")


if __name__ == "__main__":
    main()
