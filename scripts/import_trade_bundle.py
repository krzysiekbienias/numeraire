#!/usr/bin/env python3
"""Insert product + products_equity + trade header + trade_legs from one JSON bundle.

Uses only the Python standard library. Expects schema from sql/refactor.sql
(tables must exist — apply the schema manually or via your bootstrap).

Examples:
  NUMERAIRE_DB_PATH=db.sqlite3 python3 scripts/import_trade_bundle.py trades/incoming/my_trade.json

  # Wszystkie *.json z katalogu — ponowny import pomija już istniejące trade_id (SKIP)
  python3 scripts/import_trade_bundle.py --incoming-dir trades/incoming --db db.sqlite3

  # Jawna lista plików
  python3 scripts/import_trade_bundle.py trades/incoming/a.json trades/incoming/b.json --db db.sqlite3
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
    "settlement",
    "day_count",
    "calendar",
)

REQUIRED_TRADE_KEYS = (
    "trade_id",
    "portfolio_id",
    "strategy_type",
    "status",
    "legs",
)

REQUIRED_LEG_KEYS = (
    "leg_id",
    "product_id",
    "direction",
    "quantity",
    "execution_price",
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


def _normalize_leg_direction_db(raw: str) -> str:
    k = raw.strip().lower()
    if k == "long":
        return "LONG"
    if k == "short":
        return "SHORT"
    _die(f'leg.direction must be "long" or "short", got {raw!r}')


def _normalize_option_type(raw: Any) -> str | None:
    if raw is None:
        return None
    s = str(raw).strip()
    if not s:
        return None
    k = s.lower()
    if k not in ("call", "put"):
        _die(f'equity.option_type must be "call", "put", or null, got {raw!r}')
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


def _trade_exists(conn: sqlite3.Connection, trade_id: str) -> bool:
    cur = conn.cursor()
    cur.execute("SELECT 1 FROM trades WHERE trade_id = ? LIMIT 1", (trade_id,))
    return cur.fetchone() is not None


def _collect_json_paths(json_paths: list[Path], incoming_dir: Path | None) -> list[Path]:
    """Unique paths: positional .json files first, then sorted glob from incoming_dir."""
    out: list[Path] = []
    seen: set[str] = set()

    def add(path: Path) -> None:
        if path.suffix.lower() != ".json":
            _die(f"not a .json file: {path}")
        key = str(path.resolve())
        if key not in seen:
            seen.add(key)
            out.append(path)

    for p in json_paths:
        add(p)
    if incoming_dir is not None:
        if not incoming_dir.is_dir():
            _die(f"--incoming-dir is not a directory: {incoming_dir}")
        for p in sorted(incoming_dir.glob("*.json")):
            add(p)
    return out


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

    legs_raw = trade.get("legs")
    if not isinstance(legs_raw, list) or len(legs_raw) == 0:
        _die('trade.legs: expected a non-empty array')

    pid = product["product_id"]
    if equity["product_id"] != pid:
        _die(
            f'equity.product_id ({equity["product_id"]!r}) must match '
            f'product.product_id ({pid!r})'
        )

    tid = str(trade["trade_id"])
    for i, leg in enumerate(legs_raw):
        label = f"trade.legs[{i}]"
        lg = _require_mapping(leg, label)
        ml = _missing_keys(lg, REQUIRED_LEG_KEYS)
        if ml:
            _die(f"{label}: missing keys: {ml}")
        if str(lg["product_id"]) != pid:
            _die(
                f'{label}.product_id ({lg["product_id"]!r}) must match '
                f'product.product_id ({pid!r})'
            )
        _normalize_leg_direction_db(str(lg["direction"]))

    return product, equity, trade


def _optional_str(d: Mapping[str, Any], key: str) -> str | None:
    if key not in d:
        return None
    v = d[key]
    if v is None:
        return None
    return str(v)


def insert_bundle(conn: sqlite3.Connection, product: Mapping[str, Any], equity: Mapping[str, Any], trade: Mapping[str, Any]) -> None:
    pid = product["product_id"]
    instrument_type = equity.get("instrument_type", "plain_vanilla_european_option")
    exercise_style = equity.get("exercise_style", "european")
    structured_params = _structured_params_to_text(equity.get("structured_params"))

    strike_raw = equity.get("strike", None)
    strike: float | None
    if strike_raw is None:
        strike = None
    else:
        try:
            strike = float(strike_raw)
        except (TypeError, ValueError):
            _die(f"equity.strike: expected number, got {strike_raw!r}")

    option_type = _normalize_option_type(equity.get("option_type", None))

    expiry_val = product.get("expiry_date", None)
    expiry_date: str | None
    if expiry_val is None:
        expiry_date = None
    else:
        expiry_date = str(expiry_val)

    currency = str(product.get("currency", "USD"))
    contract_size_raw = product.get("contract_size", 100.0)
    try:
        contract_size = float(contract_size_raw)
    except (TypeError, ValueError):
        _die(f"product.contract_size: expected number, got {contract_size_raw!r}")

    cur = conn.cursor()
    cur.execute(
        """
        INSERT OR IGNORE INTO products (
            product_id, asset_kind, underlying_id, expiry_date,
            settlement, currency, contract_size, day_count, calendar
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            pid,
            str(product["asset_kind"]),
            str(product["underlying_id"]),
            expiry_date,
            str(product["settlement"]),
            currency,
            contract_size,
            str(product["day_count"]),
            str(product["calendar"]),
        ),
    )
    cur.execute(
        """
        INSERT OR IGNORE INTO products_equity (
            product_id, option_type, strike,
            instrument_type, exercise_style, structured_params
        ) VALUES (?, ?, ?, ?, ?, ?)
        """,
        (pid, option_type, strike, str(instrument_type), str(exercise_style), structured_params),
    )
    tid = str(trade["trade_id"])
    cur.execute(
        """
        INSERT INTO trades (
            trade_id, portfolio_id, strategy_type,
            booking_timestamp, trade_date, updated_at, status
        ) VALUES (?, ?, ?, ?, ?, ?, ?)
        """,
        (
            tid,
            str(trade["portfolio_id"]),
            str(trade["strategy_type"]),
            _optional_str(trade, "booking_timestamp"),
            _optional_str(trade, "trade_date"),
            _optional_str(trade, "updated_at"),
            str(trade["status"]),
        ),
    )

    legs_raw = trade["legs"]
    assert isinstance(legs_raw, list)
    for leg in legs_raw:
        lg = _require_mapping(leg, "leg")
        direction_db = _normalize_leg_direction_db(str(lg["direction"]))
        try:
            qty = float(lg["quantity"])
        except (TypeError, ValueError):
            _die(f'leg {lg["leg_id"]!r}: quantity must be a number')
        try:
            exe = float(lg["execution_price"])
        except (TypeError, ValueError):
            _die(f'leg {lg["leg_id"]!r}: execution_price must be a number')

        comm_raw = lg.get("commission", None)
        if comm_raw is None:
            commission = 0.0
        else:
            try:
                commission = float(comm_raw)
            except (TypeError, ValueError):
                _die(f'leg {lg["leg_id"]!r}: commission must be number or null')

        cur.execute(
            """
            INSERT INTO trade_legs (
                leg_id, trade_id, product_id,
                direction, quantity, execution_price, commission
            ) VALUES (?, ?, ?, ?, ?, ?, ?)
            """,
            (
                str(lg["leg_id"]),
                tid,
                str(lg["product_id"]),
                direction_db,
                qty,
                exe,
                commission,
            ),
        )


def main() -> None:
    parser = argparse.ArgumentParser(description="Import product + equity + trade + legs from JSON into SQLite.")
    parser.add_argument(
        "json_paths",
        nargs="*",
        type=Path,
        help="One or more bundle JSON files (keys: product, equity, trade with legs[])",
    )
    parser.add_argument(
        "--incoming-dir",
        dest="incoming_dir",
        type=Path,
        default=None,
        help="Also import every *.json in this directory (sorted; duplicates skipped)",
    )
    parser.add_argument(
        "--db",
        dest="db_path",
        type=Path,
        default=None,
        help=f"SQLite database path (default: env NUMERAIRE_DB_PATH or {default_db_path()!r})",
    )
    args = parser.parse_args()

    paths = _collect_json_paths(list(args.json_paths), args.incoming_dir)
    if not paths:
        _die("No JSON bundles to import. Pass file paths and/or --incoming-dir.")

    db_path = args.db_path if args.db_path is not None else Path(default_db_path())

    try:
        conn = sqlite3.connect(str(db_path))
    except sqlite3.Error as e:
        _die(f"cannot open database {db_path}: {e}")

    imported = 0
    skipped = 0
    try:
        conn.execute("PRAGMA foreign_keys = ON")
        for path in paths:
            product, equity, trade = load_bundle(path)
            tid = str(trade["trade_id"])
            if _trade_exists(conn, tid):
                print(f"SKIP: {path.name} (trade_id {tid!r} already in database)")
                skipped += 1
                continue
            try:
                conn.execute("BEGIN")
                insert_bundle(conn, product, equity, trade)
                conn.commit()
            except sqlite3.IntegrityError as e:
                conn.rollback()
                print(f"SKIP: {path.name} (database constraint: {e})")
                skipped += 1
                continue
            except sqlite3.Error as e:
                conn.rollback()
                _die(f"{path}: SQLite error: {e}")
            n_legs = len(trade["legs"])  # type: ignore[arg-type]
            print(
                f"OK: {path.name} -> trade {trade['trade_id']!r} product {product['product_id']!r} "
                f"({n_legs} leg(s))"
            )
            imported += 1
    finally:
        conn.close()

    print(f"Done: {imported} imported, {skipped} skipped -> {db_path}")


if __name__ == "__main__":
    main()
