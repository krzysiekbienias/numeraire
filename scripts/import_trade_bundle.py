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
    "legs",
)

REQUIRED_LEG_KEYS = (
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


def _canonical_product_id(product: Mapping[str, Any]) -> str:
    if "product_id" not in product:
        _die('product: missing key "product_id"')
    pid = str(product["product_id"]).strip()
    if not pid:
        _die("product.product_id: required (set once under product — copied to equity and legs)")
    return pid


def _resolve_product_id_field(
    holder: dict[str, Any],
    label: str,
    canonical_pid: str,
    notes: list[str],
) -> None:
    raw = holder.get("product_id", None)
    if _is_blank(raw):
        holder["product_id"] = canonical_pid
        notes.append(f"{label}.product_id ← product.product_id ({canonical_pid!r})")
        return
    other = str(raw).strip()
    if other != canonical_pid:
        _die(
            f"{label}.product_id ({other!r}) must match product.product_id ({canonical_pid!r})"
        )
    holder["product_id"] = other


def normalize_bundle(
    product: dict[str, Any],
    equity: dict[str, Any],
    trade: dict[str, Any],
) -> list[str]:
    """Fill product_id / leg_id from product.trade; error on conflicting product_id."""
    notes: list[str] = []
    canonical_pid = _canonical_product_id(product)
    product["product_id"] = canonical_pid

    _resolve_product_id_field(equity, "equity", canonical_pid, notes)

    if "trade_id" not in trade or _is_blank(trade.get("trade_id")):
        _die("trade.trade_id: required")
    trade_id = str(trade["trade_id"]).strip()
    trade["trade_id"] = trade_id

    legs_raw = trade.get("legs")
    if not isinstance(legs_raw, list) or len(legs_raw) == 0:
        _die('trade.legs: expected a non-empty array')

    for i, leg in enumerate(legs_raw):
        if not isinstance(leg, dict):
            _die(f"trade.legs[{i}]: expected JSON object")
        label = f"trade.legs[{i}]"
        _resolve_product_id_field(leg, label, canonical_pid, notes)

        if _is_blank(leg.get("leg_id")):
            leg_id = f"{trade_id}_L{i + 1}"
            leg["leg_id"] = leg_id
            notes.append(f"{label}.leg_id ← {leg_id!r}")

    _resolve_trade_status_for_import(trade, notes)

    return notes


def _resolve_trade_status_for_import(trade: dict[str, Any], notes: list[str]) -> None:
    """New trades are always PENDING until dev_main --price-booking promotes to LIVE."""
    raw = trade.get("status", None)
    if not _is_blank(raw):
        requested = str(raw).strip().upper()
        if requested != "PENDING":
            notes.append(
                f"trade.status ← PENDING (import forces PENDING; JSON had {raw!r}; "
                "LIVE only after --price-booking)"
            )
    elif "status" not in trade:
        notes.append("trade.status ← PENDING (default for new import)")
    trade["status"] = "PENDING"


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
    product = dict(_require_mapping(root.get("product"), "product"))
    equity = dict(_require_mapping(root.get("equity"), "equity"))
    trade = dict(_require_mapping(root.get("trade"), "trade"))

    mp = _missing_keys(product, REQUIRED_PRODUCT_KEYS)
    if mp:
        _die(f"product: missing keys: {mp}")
    mt = _missing_keys(trade, REQUIRED_TRADE_KEYS)
    if mt:
        _die(f"trade: missing keys: {mt}")

    auto_notes = normalize_bundle(product, equity, trade)

    legs_raw = trade["legs"]
    for i, leg in enumerate(legs_raw):
        label = f"trade.legs[{i}]"
        lg = _require_mapping(leg, label)
        ml = _missing_keys(lg, REQUIRED_LEG_KEYS)
        if ml:
            _die(f"{label}: missing keys: {ml}")
        _normalize_leg_direction_db(str(lg["direction"]))

    return product, equity, trade, auto_notes


def _optional_str(d: Mapping[str, Any], key: str) -> str | None:
    if key not in d:
        return None
    v = d[key]
    if v is None:
        return None
    s = str(v).strip()
    if not s:
        return None
    return s


def _is_blank(value: Any) -> bool:
    if value is None:
        return True
    if isinstance(value, str):
        return not value.strip()
    return False


def _require_non_blank_str(d: Mapping[str, Any], key: str, label: str) -> str:
    if key not in d:
        _die(f"{label}: missing required key {key!r}")
    v = d[key]
    if _is_blank(v):
        _die(f"{label}.{key}: required (see _{key}_comment in bundle template)")
    return str(v).strip()


def _parse_settlement(raw: Any, label: str) -> str:
    if _is_blank(raw):
        _die(f"{label}.settlement: required — PHYSICAL or CASH (see _settlement_comment in bundle template)")
    settlement = str(raw).strip().upper()
    if settlement not in ("PHYSICAL", "CASH"):
        _die(f'{label}.settlement: must be "PHYSICAL" or "CASH", got {raw!r}')
    return settlement


def _parse_strike(raw: Any) -> float:
    if _is_blank(raw):
        _die('equity.strike: required number (see _strike_comment in bundle template)')
    try:
        strike = float(raw)
    except (TypeError, ValueError):
        _die(f"equity.strike: expected number, got {raw!r}")
    if strike <= 0.0:
        _die(f"equity.strike: must be positive, got {strike}")
    return strike


def _parse_deferred_execution_price(lg: Mapping[str, Any], leg_id: str) -> float:
    if "execution_price" not in lg or _is_blank(lg.get("execution_price")):
        return 0.0
    try:
        exe = float(lg["execution_price"])
    except (TypeError, ValueError):
        _die(f"leg {leg_id!r}: execution_price must be a number or null")
    if exe < 0.0:
        _die(f"leg {leg_id!r}: execution_price must be non-negative")
    return exe


def _parse_commission(lg: Mapping[str, Any], quantity: float, leg_id: str) -> float:
    per_contract = lg.get("commission_per_contract", None)
    if not _is_blank(per_contract):
        try:
            rate = float(per_contract)
        except (TypeError, ValueError):
            _die(f"leg {leg_id!r}: commission_per_contract must be a number")
        if rate < 0.0:
            _die(f"leg {leg_id!r}: commission_per_contract must be non-negative")
        return rate * quantity

    comm_raw = lg.get("commission", None)
    if _is_blank(comm_raw):
        return 0.0
    try:
        commission = float(comm_raw)
    except (TypeError, ValueError):
        _die(f"leg {leg_id!r}: commission must be number or null")
    if commission < 0.0:
        _die(f"leg {leg_id!r}: commission must be non-negative")
    return commission


def insert_bundle(conn: sqlite3.Connection, product: Mapping[str, Any], equity: Mapping[str, Any], trade: Mapping[str, Any]) -> None:
    pid = product["product_id"]
    instrument_type = equity.get("instrument_type", "plain_vanilla_european_option")
    exercise_style = equity.get("exercise_style", "european")
    structured_params = _structured_params_to_text(equity.get("structured_params"))

    strike = _parse_strike(equity.get("strike", None))

    option_type = _normalize_option_type(equity.get("option_type", None))
    if option_type is None:
        _die('equity.option_type: required — "call" or "put"')

    expiry_date = _require_non_blank_str(product, "expiry_date", "product")

    currency = str(product.get("currency", "USD"))
    contract_size_raw = product.get("contract_size", 100.0)
    try:
        contract_size = float(contract_size_raw)
    except (TypeError, ValueError):
        _die(f"product.contract_size: expected number, got {contract_size_raw!r}")

    settlement = _parse_settlement(product.get("settlement", None), "product")

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
            settlement,
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
    trade_date = _require_non_blank_str(trade, "trade_date", "trade")
    booking_timestamp = _optional_str(trade, "booking_timestamp")
    updated_at = _optional_str(trade, "updated_at")
    if updated_at is None:
        cur.execute(
            """
            INSERT INTO trades (
                trade_id, portfolio_id, strategy_type,
                booking_timestamp, trade_date, updated_at, status
            ) VALUES (?, ?, ?, ?, ?, datetime('now'), ?)
            """,
            (
                tid,
                str(trade["portfolio_id"]),
                str(trade["strategy_type"]),
                booking_timestamp,
                trade_date,
                str(trade["status"]),
            ),
        )
    else:
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
                booking_timestamp,
                trade_date,
                updated_at,
                str(trade["status"]),
            ),
        )

    legs_raw = trade["legs"]
    assert isinstance(legs_raw, list)
    for leg in legs_raw:
        lg = _require_mapping(leg, "leg")
        direction_db = _normalize_leg_direction_db(str(lg["direction"]))
        leg_id = str(lg["leg_id"])
        try:
            qty = float(lg["quantity"])
        except (TypeError, ValueError):
            _die(f"leg {leg_id!r}: quantity must be a number")
        if qty <= 0.0:
            _die(f"leg {leg_id!r}: quantity must be positive")

        exe = _parse_deferred_execution_price(lg, leg_id)
        commission = _parse_commission(lg, qty, leg_id)

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
            product, equity, trade, auto_notes = load_bundle(path)
            for note in auto_notes:
                print(f"  {note}")
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
