#!/usr/bin/env python3
"""Insert product + products_equity + trades from one JSON bundle into SQLite.

Uses only the Python standard library. Expects schema from sql/schema_v1.sql
(tables must exist — run dev_main once or apply the schema manually).

Example:
  NUMERAIRE_DB_PATH=db.sqlite3 python3 scripts/import_trade_bundle.py trades/incoming/my_trade.json
"""

from __future__ import annotations

import argparse
import json
import os
import sqlite3
import sys
from pathlib import Path
from typing import Any, Mapping

REQUIRED_PRODUCT_KEYS = (
    "product_id",
    "asset_kind",
    "underlying_id",
    "expiry_date",
    "settlement",
    "day_count",
    "calendar",
)
REQUIRED_TRADE_KEYS = (
    "trade_id",
    "product_id",
    "booking_timestamp",
    "trade_date",
    "updated_at",
    "status",
    "direction",
    "quantity",
)


def _die(msg: str, code: int = 1) -> None:
    print(msg, file=sys.stderr)
    sys.exit(code)


def _require_mapping(obj: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(obj, Mapping):
        _die(f"{label}: expected JSON object, got {type(obj).__name__}")
    return obj


def _missing_keys(d: Mapping[str, Any], keys: tuple[str, ...]) -> list[str]:
    return [k for k in keys if k not in d]


def _normalize_direction(raw: str) -> str:
    k = raw.strip().lower()
    if k not in ("long", "short"):
        _die(f'trade.direction must be "long" or "short", got {raw!r}')
    return k


def _structured_params_to_text(raw: Any) -> str:
    if raw is None:
        return "{}"
    if isinstance(raw, str):
        return raw if raw.strip() else "{}"
    if isinstance(raw, Mapping):
        return json.dumps(raw, separators=(",", ":"), sort_keys=True)
    _die(f"equity.structured_params: expected object or string, got {type(raw).__name__}")


def default_db_path() -> str:
    return os.environ.get("NUMERAIRE_DB_PATH", "db.sqlite3")


def load_bundle(path: Path) -> tuple[Mapping[str, Any], Mapping[str, Any], Mapping[str, Any]]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except OSError as e:
        _die(f"cannot read {path}: {e}")
    except json.JSONDecodeError as e:
        _die(f"invalid JSON in {path}: {e}")

    root = _require_mapping(data, "root")
    product = _require_mapping(root.get("product"), "product")
    equity = _require_mapping(root.get("equity"), "equity")
    trade = _require_mapping(root.get("trade"), "trade")

    mp = _missing_keys(product, REQUIRED_PRODUCT_KEYS)
    if mp:
        _die(f"product: missing keys: {mp}")
    mt = _missing_keys(trade, REQUIRED_TRADE_KEYS)
    if mt:
        _die(f"trade: missing keys: {mt}")
    if "product_id" not in equity:
        _die('equity: missing key "product_id"')

    pid = product["product_id"]
    if equity["product_id"] != pid:
        _die(
            f'equity.product_id ({equity["product_id"]!r}) must match '
            f'product.product_id ({pid!r})'
        )
    if trade["product_id"] != pid:
        _die(
            f'trade.product_id ({trade["product_id"]!r}) must match '
            f'product.product_id ({pid!r})'
        )

    _normalize_direction(str(trade["direction"]))
    return product, equity, trade


def insert_bundle(conn: sqlite3.Connection, product: Mapping[str, Any], equity: Mapping[str, Any], trade: Mapping[str, Any]) -> None:
    pid = product["product_id"]
    instrument_type = equity.get("instrument_type", "plain_vanilla_european_option")
    exercise_style = equity.get("exercise_style", "european")
    structured_params = _structured_params_to_text(equity.get("structured_params"))

    opt_raw = equity.get("option_type", None)
    strike_raw = equity.get("strike", None)
    option_type: str | None
    if opt_raw is None:
        option_type = None
    else:
        option_type = str(opt_raw)
        if not option_type.strip():
            option_type = None
    strike: float | None
    if strike_raw is None:
        strike = None
    else:
        try:
            strike = float(strike_raw)
        except (TypeError, ValueError):
            _die(f"equity.strike: expected number, got {strike_raw!r}")

    direction = _normalize_direction(str(trade["direction"]))
    try:
        quantity = float(trade["quantity"])
    except (TypeError, ValueError):
        _die(f'trade.quantity: expected number, got {trade["quantity"]!r}')

    commission = trade.get("commission", None)
    if commission is not None:
        try:
            commission = float(commission)
        except (TypeError, ValueError):
            _die(f'trade.commission: expected number or null, got {trade["commission"]!r}')

    cur = conn.cursor()
    cur.execute(
        """
        INSERT INTO products (
            product_id, asset_kind, underlying_id, expiry_date,
            settlement, day_count, calendar
        ) VALUES (?, ?, ?, ?, ?, ?, ?)
        """,
        (
            pid,
            str(product["asset_kind"]),
            str(product["underlying_id"]),
            str(product["expiry_date"]),
            str(product["settlement"]),
            str(product["day_count"]),
            str(product["calendar"]),
        ),
    )
    cur.execute(
        """
        INSERT INTO products_equity (
            product_id, option_type, strike,
            instrument_type, exercise_style, structured_params
        ) VALUES (?, ?, ?, ?, ?, ?)
        """,
        (pid, option_type, strike, str(instrument_type), str(exercise_style), structured_params),
    )
    cur.execute(
        """
        INSERT INTO trades (
            trade_id, product_id, booking_timestamp, trade_date, updated_at,
            status, direction, quantity, commission
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            str(trade["trade_id"]),
            pid,
            str(trade["booking_timestamp"]),
            str(trade["trade_date"]),
            str(trade["updated_at"]),
            str(trade["status"]),
            direction,
            quantity,
            commission,
        ),
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Import product + equity + trade from JSON into SQLite.")
    parser.add_argument(
        "json_path",
        type=Path,
        help="Path to bundle JSON (keys: product, equity, trade)",
    )
    parser.add_argument(
        "--db",
        dest="db_path",
        type=Path,
        default=None,
        help=f"SQLite database path (default: env NUMERAIRE_DB_PATH or {default_db_path()!r})",
    )
    args = parser.parse_args()

    db_path = args.db_path if args.db_path is not None else Path(default_db_path())
    product, equity, trade = load_bundle(args.json_path)

    try:
        conn = sqlite3.connect(str(db_path))
    except sqlite3.Error as e:
        _die(f"cannot open database {db_path}: {e}")

    try:
        conn.execute("PRAGMA foreign_keys = ON")
        conn.execute("BEGIN")
        insert_bundle(conn, product, equity, trade)
        conn.commit()
    except sqlite3.IntegrityError as e:
        conn.rollback()
        _die(f"database constraint failed (duplicate id or missing parent?): {e}")
    except sqlite3.Error as e:
        conn.rollback()
        _die(f"SQLite error: {e}")
    finally:
        conn.close()

    print(f"OK: inserted trade {trade['trade_id']!r} product {product['product_id']!r} into {db_path}")


if __name__ == "__main__":
    main()
