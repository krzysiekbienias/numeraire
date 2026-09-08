#!/usr/bin/env python3
"""Insert product(s) + (products_equity | products_commodity) + trade + trade_legs.

Uses only the Python standard library. Expects schema from sql/schema_v1.sql
(tables must exist — apply the schema manually or via your bootstrap).

Validates each `product.expiry_date >= trade.trade_date` before any INSERT
(rejects expired-at-trade-date instruments with a clear error).

Bundle shapes:
  - single product: top-level `product` plus exactly one of `equity` / `commodity`
  - multi-product (calendar): `products` array; each item is the product fields
    plus its own `equity` or `commodity`. Legs must name a `product_id` from
    that list. Same trade_id, different listed tenors.

Examples:
  NUMERAIRE_DB_PATH=db.sqlite3 python3 scripts/import_trade_bundle.py trades/incoming/my_trade.json

  # Trade ids → trades/incoming/{TRD_….json} (default incoming dir: repo trades/incoming)
  python3 scripts/import_trade_bundle.py TRD_10005 TRD_10006 TRD_10007 TRD_10008

  # Wszystkie *.json z katalogu — ponowny import pomija już istniejące trade_id (SKIP)
  python3 scripts/import_trade_bundle.py --incoming-dir trades/incoming --db db.sqlite3

  # Jawna lista plików
  python3 scripts/import_trade_bundle.py trades/incoming/a.json trades/incoming/b.json --db db.sqlite3
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sqlite3
import sys
from pathlib import Path
from typing import Any, Mapping

_REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_INCOMING_DIR = _REPO_ROOT / "trades" / "incoming"
_TRADE_ID_RE = re.compile(r"^TRD_[A-Za-z0-9_]+$")

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


def _normalize_instrument_type_key(raw: str) -> str:
    return "".join(ch for ch in str(raw).strip().lower() if ch not in "_ \t")


def _is_equity_forward_instrument(raw: Any) -> bool:
    if raw is None:
        return False
    key = _normalize_instrument_type_key(str(raw))
    return key in ("equityforward", "forward")


def _is_spot_instrument(raw: Any) -> bool:
    if raw is None:
        return False
    key = _normalize_instrument_type_key(str(raw))
    return key in ("equityspot", "spot", "equityshare", "cashequity", "indexspot", "spotindex")


def _is_commodity_futures_outright(raw: Any) -> bool:
    if raw is None:
        return False
    key = _normalize_instrument_type_key(str(raw))
    return key in ("commodityfuturesoutright", "futuresoutright", "commodityoutright")


def _structured_params_to_text(raw: Any, *, label: str = "structured_params") -> str:
    if raw is None:
        return "{}"
    if isinstance(raw, str):
        return raw if raw.strip() else "{}"
    if isinstance(raw, Mapping):
        return json.dumps(raw, separators=(",", ":"), sort_keys=True)
    _die(f"{label}: expected object or string, got {type(raw).__name__}")


def default_db_path() -> str:
    return os.environ.get("NUMERAIRE_DB_PATH", "db.sqlite3")


def _trade_exists(conn: sqlite3.Connection, trade_id: str) -> bool:
    cur = conn.cursor()
    cur.execute("SELECT 1 FROM trades WHERE trade_id = ? LIMIT 1", (trade_id,))
    return cur.fetchone() is not None


def _canonical_product_id(product: Mapping[str, Any], *, label: str = "product") -> str:
    if "product_id" not in product:
        _die(f'{label}: missing key "product_id"')
    pid = str(product["product_id"]).strip()
    if not pid:
        _die(f"{label}.product_id: required")
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
        notes.append(f"{label}.product_id ← {canonical_pid!r}")
        return
    other = str(raw).strip()
    if other != canonical_pid:
        _die(f"{label}.product_id ({other!r}) must match {canonical_pid!r}")
    holder["product_id"] = other


def normalize_bundle(
    items: list[tuple[dict[str, Any], str, dict[str, Any]]],
    trade: dict[str, Any],
) -> list[str]:
    """Fill product_id / leg_id; legs may name any product in this bundle."""
    notes: list[str] = []
    pids: list[str] = []
    for i, (product, extension_label, extension) in enumerate(items):
        label = "product" if len(items) == 1 else f"products[{i}]"
        pid = _canonical_product_id(product, label=label)
        product["product_id"] = pid
        _resolve_product_id_field(extension, f"{label}.{extension_label}", pid, notes)
        if pid in pids:
            _die(f"{label}.product_id: duplicate {pid!r} in this bundle")
        pids.append(pid)

    if "trade_id" not in trade or _is_blank(trade.get("trade_id")):
        _die("trade.trade_id: required")
    trade_id = str(trade["trade_id"]).strip()
    trade["trade_id"] = trade_id

    legs_raw = trade.get("legs")
    if not isinstance(legs_raw, list) or len(legs_raw) == 0:
        _die('trade.legs: expected a non-empty array')

    allowed = set(pids)
    for i, leg in enumerate(legs_raw):
        if not isinstance(leg, dict):
            _die(f"trade.legs[{i}]: expected JSON object")
        label = f"trade.legs[{i}]"
        raw = leg.get("product_id", None)
        if _is_blank(raw):
            if len(pids) == 1:
                leg["product_id"] = pids[0]
                notes.append(f"{label}.product_id ← {pids[0]!r}")
            else:
                _die(f"{label}.product_id: required when the bundle has multiple products")
        else:
            other = str(raw).strip()
            if other not in allowed:
                _die(
                    f"{label}.product_id ({other!r}) must be one of this bundle's "
                    f"products: {', '.join(pids)}"
                )
            leg["product_id"] = other

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


def _looks_like_trade_id(raw: str) -> bool:
    token = raw.removesuffix(".json").removesuffix(".JSON")
    return _TRADE_ID_RE.match(token) is not None


def _resolve_bundle_path(raw: str, incoming_dir: Path) -> Path:
    """Path to a bundle: explicit .json path or trade id → incoming_dir/{id}.json."""
    if _looks_like_trade_id(raw):
        trade_id = raw.removesuffix(".json").removesuffix(".JSON")
        path = incoming_dir / f"{trade_id}.json"
        if not path.is_file():
            _die(f"bundle not found for trade id {trade_id!r}: {path}")
        return path

    path = Path(raw)
    if path.suffix.lower() != ".json":
        _die(f"not a .json file or trade id (TRD_…): {raw!r}")
    if not path.is_file():
        _die(f"bundle file not found: {path}")
    return path


def _collect_json_paths(
        inputs: list[str],
        incoming_dir: Path,
        import_all_in_dir: bool) -> list[Path]:
    """Unique paths: trade ids / .json paths, optionally every *.json in incoming_dir."""
    out: list[Path] = []
    seen: set[str] = set()

    def add(path: Path) -> None:
        if path.suffix.lower() != ".json":
            _die(f"not a .json file: {path}")
        key = str(path.resolve())
        if key not in seen:
            seen.add(key)
            out.append(path)

    if not incoming_dir.is_dir():
        _die(f"incoming dir is not a directory: {incoming_dir}")

    for raw in inputs:
        add(_resolve_bundle_path(raw, incoming_dir))

    if import_all_in_dir:
        for path in sorted(incoming_dir.glob("*.json")):
            add(path)

    return out


def _extension_from_holder(
    holder: Mapping[str, Any], label: str
) -> tuple[str, dict[str, Any]]:
    has_equity = "equity" in holder and holder.get("equity") is not None
    has_commodity = "commodity" in holder and holder.get("commodity") is not None
    if has_equity == has_commodity:
        _die(f"{label}: must contain exactly one of 'equity' or 'commodity'")
    if has_commodity:
        return "commodity", dict(_require_mapping(holder.get("commodity"), f"{label}.commodity"))
    return "equity", dict(_require_mapping(holder.get("equity"), f"{label}.equity"))


def _product_item_from_holder(
    holder: Mapping[str, Any], label: str
) -> tuple[dict[str, Any], str, dict[str, Any]]:
    extension_label, extension = _extension_from_holder(holder, label)
    product = {
        key: value
        for key, value in holder.items()
        if key not in ("equity", "commodity")
    }
    mp = _missing_keys(product, REQUIRED_PRODUCT_KEYS)
    if mp:
        _die(f"{label}: missing keys: {mp}")
    return product, extension_label, extension


def load_bundle(
    path: Path,
) -> tuple[list[tuple[dict[str, Any], str, dict[str, Any]]], dict[str, Any], list[str]]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except OSError as e:
        _die(f"cannot read {path}: {e}")
    except json.JSONDecodeError as e:
        _die(f"invalid JSON in {path}: {e}")

    root = _require_mapping(data, "root")
    trade = dict(_require_mapping(root.get("trade"), "trade"))

    has_products = "products" in root and root.get("products") is not None
    has_product = "product" in root and root.get("product") is not None
    if has_products and has_product:
        _die(f"{path.name}: use either 'product' or 'products', not both")
    if has_products:
        raw_list = root.get("products")
        if not isinstance(raw_list, list) or len(raw_list) == 0:
            _die("products: expected a non-empty array")
        items = [
            _product_item_from_holder(
                _require_mapping(raw, f"products[{i}]"), f"products[{i}]"
            )
            for i, raw in enumerate(raw_list)
        ]
    elif has_product:
        product = dict(_require_mapping(root.get("product"), "product"))
        extension_label, extension = _extension_from_holder(root, path.name)
        mp = _missing_keys(product, REQUIRED_PRODUCT_KEYS)
        if mp:
            _die(f"product: missing keys: {mp}")
        items = [(product, extension_label, extension)]
    else:
        _die(f"{path.name}: missing 'product' or 'products'")

    mt = _missing_keys(trade, REQUIRED_TRADE_KEYS)
    if mt:
        _die(f"trade: missing keys: {mt}")

    auto_notes = normalize_bundle(items, trade)

    legs_raw = trade["legs"]
    for i, leg in enumerate(legs_raw):
        label = f"trade.legs[{i}]"
        lg = _require_mapping(leg, label)
        ml = _missing_keys(lg, REQUIRED_LEG_KEYS)
        if ml:
            _die(f"{label}: missing keys: {ml}")
        _normalize_leg_direction_db(str(lg["direction"]))

    return items, trade, auto_notes


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


def _parse_iso_date(raw: str, label: str) -> tuple[int, int, int]:
    s = raw.strip()
    if len(s) != 10 or s[4] != "-" or s[7] != "-":
        _die(f"{label}: expected ISO date YYYY-MM-DD, got {raw!r}")
    try:
        year = int(s[0:4])
        month = int(s[5:7])
        day = int(s[8:10])
    except ValueError:
        _die(f"{label}: expected ISO date YYYY-MM-DD, got {raw!r}")
    if month < 1 or month > 12 or day < 1 or day > 31:
        _die(f"{label}: date out of range: {raw!r}")
    return year, month, day


def _require_expiry_on_or_after_trade_date(expiry_date: str, trade_date: str, product_id: str) -> None:
    """Product must still be alive on the trade booking date."""
    exp = _parse_iso_date(expiry_date, "product.expiry_date")
    td = _parse_iso_date(trade_date, "trade.trade_date")
    if exp < td:
        _die(
            f"product {product_id!r}: expiry_date {expiry_date!r} must be on or after "
            f"trade.trade_date {trade_date!r}"
        )


def _parse_settlement(raw: Any, label: str) -> str:
    if _is_blank(raw):
        _die(f"{label}.settlement: required — PHYSICAL or CASH (see _settlement_comment in bundle template)")
    settlement = str(raw).strip().upper()
    if settlement not in ("PHYSICAL", "CASH"):
        _die(f'{label}.settlement: must be "PHYSICAL" or "CASH", got {raw!r}')
    return settlement


def _parse_strike(raw: Any, *, required: bool) -> float | None:
    if _is_blank(raw):
        if required:
            _die('equity.strike: required number (see _strike_comment in bundle template)')
        return None
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


def _insert_one_product(
    cur: sqlite3.Cursor,
    product: Mapping[str, Any],
    extension_label: str,
    extension: Mapping[str, Any],
    trade_date: str,
) -> None:
    pid = product["product_id"]
    instrument_type = extension.get("instrument_type", "plain_vanilla_european_option")
    structured_params = _structured_params_to_text(
        extension.get("structured_params"),
        label=f"{extension_label}.structured_params",
    )

    is_commodity = extension_label == "commodity"
    is_spot = (not is_commodity) and _is_spot_instrument(instrument_type)
    is_forward = (not is_commodity) and _is_equity_forward_instrument(instrument_type)
    is_outright = is_commodity and _is_commodity_futures_outright(instrument_type)

    if is_commodity and not is_outright:
        _die(
            f"commodity.instrument_type: unsupported {instrument_type!r} "
            "(only commodity_futures_outright for now)"
        )

    if is_commodity:
        strike = None
        option_type = None
        exercise_style = extension.get("exercise_style", None)
        if not _is_blank(exercise_style):
            exercise_style = str(exercise_style).strip()
        else:
            exercise_style = None
    else:
        exercise_style = extension.get("exercise_style", "european")
        strike = _parse_strike(extension.get("strike", None), required=not is_spot)
        option_type = _normalize_option_type(extension.get("option_type", None))
        if option_type is None and not is_forward and not is_spot:
            _die(
                'equity.option_type: required — "call" or "put" '
                "(null allowed for equity_forward / equity_spot / index_spot only)"
            )

    if is_spot:
        expiry_raw = product.get("expiry_date", None)
        expiry_date = None if _is_blank(expiry_raw) else str(expiry_raw).strip()
    else:
        expiry_date = _require_non_blank_str(product, "expiry_date", "product")
        _require_expiry_on_or_after_trade_date(expiry_date, trade_date, str(pid))

    asset_kind = str(product["asset_kind"]).strip().upper()
    if is_commodity:
        if asset_kind != "COMMODITY":
            _die("product.asset_kind: commodity extension requires COMMODITY")
    elif is_spot:
        itype_key = _normalize_instrument_type_key(str(instrument_type))
        if itype_key in ("indexspot", "spotindex") and asset_kind != "INDEX":
            _die("product.asset_kind: index_spot requires INDEX")
        if itype_key in ("equityspot", "spot", "equityshare", "cashequity") and asset_kind != "EQUITY":
            _die("product.asset_kind: equity_spot requires EQUITY")

    currency = str(product.get("currency", "USD"))
    contract_size_raw = product.get("contract_size", 100.0)
    try:
        contract_size = float(contract_size_raw)
    except (TypeError, ValueError):
        _die(f"product.contract_size: expected number, got {contract_size_raw!r}")

    settlement = _parse_settlement(product.get("settlement", None), "product")

    cur.execute(
        """
        INSERT OR IGNORE INTO products (
            product_id, asset_kind, underlying_id, expiry_date,
            settlement, currency, contract_size, day_count, calendar
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            pid,
            asset_kind,
            str(product["underlying_id"]),
            expiry_date,
            settlement,
            currency,
            contract_size,
            str(product["day_count"]),
            str(product["calendar"]),
        ),
    )

    if is_commodity:
        product_code = str(extension.get("product_code") or product["underlying_id"]).strip().upper()
        if not product_code:
            _die("commodity.product_code: required")
        contract_ticker = extension.get("contract_ticker", None)
        if _is_blank(contract_ticker):
            _die("commodity.contract_ticker: required for commodity_futures_outright")
        contract_ticker = str(contract_ticker).strip().upper()

        def _opt_float(key: str) -> float | None:
            raw = extension.get(key, None)
            if _is_blank(raw):
                return None
            try:
                return float(raw)
            except (TypeError, ValueError):
                _die(f"commodity.{key}: expected number or null, got {raw!r}")

        cur.execute(
            """
            INSERT OR IGNORE INTO products_commodity (
                product_id, instrument_type, product_code, contract_ticker, contract_month,
                settlement_date, multiplier, tick_size, tick_value, option_type, strike,
                exercise_style, option_ticker, underlying_contract_ticker, structured_params
            ) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
            """,
            (
                pid,
                str(instrument_type),
                product_code,
                contract_ticker,
                _optional_str(extension, "contract_month"),
                _optional_str(extension, "settlement_date") or expiry_date,
                _opt_float("multiplier"),
                _opt_float("tick_size"),
                _opt_float("tick_value"),
                option_type,
                strike,
                exercise_style,
                _optional_str(extension, "option_ticker"),
                _optional_str(extension, "underlying_contract_ticker"),
                structured_params,
            ),
        )
    else:
        cur.execute(
            """
            INSERT OR IGNORE INTO products_equity (
                product_id, option_type, strike,
                instrument_type, exercise_style, structured_params
            ) VALUES (?, ?, ?, ?, ?, ?)
            """,
            (pid, option_type, strike, str(instrument_type), str(exercise_style), structured_params),
        )


def insert_items(
    conn: sqlite3.Connection,
    items: list[tuple[Mapping[str, Any], str, Mapping[str, Any]]],
    trade: Mapping[str, Any],
) -> None:
    trade_date = _require_non_blank_str(trade, "trade_date", "trade")
    cur = conn.cursor()
    for product, extension_label, extension in items:
        _insert_one_product(cur, product, extension_label, extension, trade_date)

    tid = str(trade["trade_id"])
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


def insert_bundle(
    conn: sqlite3.Connection,
    product: Mapping[str, Any],
    extension_label: str,
    extension: Mapping[str, Any],
    trade: Mapping[str, Any],
) -> None:
    insert_items(conn, [(product, extension_label, extension)], trade)


def main() -> None:
    parser = argparse.ArgumentParser(description="Import product + equity + trade + legs from JSON into SQLite.")
    parser.add_argument(
        "inputs",
        nargs="*",
        help="Trade ids (TRD_10005) and/or paths to bundle JSON files (product, equity, trade.legs[])",
    )
    parser.add_argument(
        "--incoming-dir",
        dest="incoming_dir",
        type=Path,
        default=None,
        help=(
            f"Directory for trade-id lookup (default: {DEFAULT_INCOMING_DIR}). "
            "If passed with no trade ids, imports every *.json here (same as --all)."
        ),
    )
    parser.add_argument(
        "--all",
        dest="import_all",
        action="store_true",
        help="Also import every *.json in --incoming-dir (sorted; existing trade_id → SKIP)",
    )
    parser.add_argument(
        "--db",
        dest="db_path",
        type=Path,
        default=None,
        help=f"SQLite database path (default: env NUMERAIRE_DB_PATH or {default_db_path()!r})",
    )
    args = parser.parse_args()

    incoming_dir = args.incoming_dir if args.incoming_dir is not None else DEFAULT_INCOMING_DIR
    import_all = args.import_all or (not args.inputs and args.incoming_dir is not None)

    paths = _collect_json_paths(list(args.inputs), incoming_dir, import_all)
    if not paths:
        _die(
            "No JSON bundles to import. Pass trade ids (TRD_10005 …), .json paths, and/or --all with --incoming-dir."
        )

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
            items, trade, auto_notes = load_bundle(path)
            for note in auto_notes:
                print(f"  {note}")
            tid = str(trade["trade_id"])
            if _trade_exists(conn, tid):
                print(f"SKIP: {path.name} (trade_id {tid!r} already in database)")
                skipped += 1
                continue
            try:
                conn.execute("BEGIN")
                insert_items(conn, items, trade)
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
            pids = ", ".join(str(product["product_id"]) for product, _, _ in items)
            print(
                f"OK: {path.name} -> trade {trade['trade_id']!r} product {pids} "
                f"({n_legs} leg(s))"
            )
            imported += 1
    finally:
        conn.close()

    print(f"Done: {imported} imported, {skipped} skipped -> {db_path}")


if __name__ == "__main__":
    main()
