#!/usr/bin/env python3
"""Unit tests for import_trade_bundle validation (stdlib unittest)."""

from __future__ import annotations

import io
import json
import sqlite3
import sys
import tempfile
import unittest
from contextlib import redirect_stderr
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_REPO_ROOT / "scripts"))

import import_trade_bundle as itb  # noqa: E402


def _minimal_bundle(*, expiry_date: str, trade_date: str) -> tuple[dict, dict, dict]:
    product = {
        "product_id": "FWD_EQF_NVDA_20260320",
        "asset_kind": "EQUITY",
        "underlying_id": "NVDA",
        "expiry_date": expiry_date,
        "settlement": "CASH",
        "currency": "USD",
        "contract_size": 1,
        "day_count": "Actual365Fixed",
        "calendar": "UnitedStates",
    }
    equity = {
        "instrument_type": "equity_forward",
        "option_type": None,
        "strike": 130,
        "exercise_style": "european",
        "structured_params": {},
    }
    trade = {
        "trade_id": "TRD_BAD_DATES",
        "portfolio_id": "BOOK_1",
        "strategy_type": "EQUITY_FORWARD",
        "trade_date": trade_date,
        "status": "PENDING",
        "legs": [
            {
                "leg_id": "TRD_BAD_DATES_L1",
                "product_id": "FWD_EQF_NVDA_20260320",
                "direction": "short",
                "quantity": 100,
                "execution_price": None,
                "commission_per_contract": 0,
            }
        ],
    }
    return product, equity, trade


class ImportExpiryValidationTest(unittest.TestCase):
    def test_expiry_before_trade_date_rejects_import(self) -> None:
        product, equity, trade = _minimal_bundle(expiry_date="2026-03-20", trade_date="2026-05-11")
        conn = sqlite3.connect(":memory:")
        try:
            conn.execute("PRAGMA foreign_keys = ON")
            stderr = io.StringIO()
            with redirect_stderr(stderr):
                with self.assertRaises(SystemExit) as ctx:
                    itb.insert_bundle(conn, product, "equity", equity, trade)
            self.assertEqual(ctx.exception.code, 1)
            msg = stderr.getvalue()
            self.assertIn("expiry_date", msg)
            self.assertIn("trade.trade_date", msg)
        finally:
            conn.close()

    def test_expiry_on_trade_date_allowed(self) -> None:
        product, equity, trade = _minimal_bundle(expiry_date="2026-05-11", trade_date="2026-05-11")
        conn = sqlite3.connect(":memory:")
        try:
            _bootstrap_schema(conn)
            itb.insert_bundle(conn, product, "equity", equity, trade)
            row = conn.execute(
                "SELECT expiry_date FROM products WHERE product_id = ?",
                (product["product_id"],),
            ).fetchone()
            self.assertIsNotNone(row)
            self.assertEqual(row[0], "2026-05-11")
        finally:
            conn.close()

    def test_expiry_after_trade_date_allowed(self) -> None:
        product, equity, trade = _minimal_bundle(expiry_date="2027-03-20", trade_date="2026-05-11")
        conn = sqlite3.connect(":memory:")
        try:
            _bootstrap_schema(conn)
            itb.insert_bundle(conn, product, "equity", equity, trade)
            row = conn.execute("SELECT 1 FROM trades WHERE trade_id = ?", (trade["trade_id"],)).fetchone()
            self.assertIsNotNone(row)
        finally:
            conn.close()


def _bootstrap_schema(conn: sqlite3.Connection) -> None:
    schema_path = _REPO_ROOT / "sql" / "schema_v1.sql"
    conn.executescript(schema_path.read_text(encoding="utf-8"))


def _outright_product(pid: str, ticker: str, expiry: str) -> dict:
    return {
        "product_id": pid,
        "asset_kind": "COMMODITY",
        "underlying_id": "CL",
        "expiry_date": expiry,
        "settlement": "PHYSICAL",
        "currency": "USD",
        "contract_size": 1000,
        "day_count": "Actual365Fixed",
        "calendar": "UnitedStates",
        "commodity": {
            "instrument_type": "commodity_futures_outright",
            "product_code": "CL",
            "contract_ticker": ticker,
            "settlement_date": expiry,
            "multiplier": 1000,
            "structured_params": {},
        },
    }


def _calendar_root(*, trade_id: str = "TRD_CAL_1") -> dict:
    near = _outright_product("FUT_OUTRIGHT_CL_CLV6", "CLV6", "2026-09-22")
    far = _outright_product("FUT_OUTRIGHT_CL_CLX6", "CLX6", "2026-10-20")
    return {
        "products": [near, far],
        "trade": {
            "trade_id": trade_id,
            "portfolio_id": "BOOK_3",
            "strategy_type": "COMMODITY_CALENDAR",
            "trade_date": "2026-09-01",
            "legs": [
                {
                    "product_id": "FUT_OUTRIGHT_CL_CLV6",
                    "direction": "short",
                    "quantity": 1,
                    "execution_price": None,
                    "commission_per_contract": 0,
                },
                {
                    "product_id": "FUT_OUTRIGHT_CL_CLX6",
                    "direction": "long",
                    "quantity": 1,
                    "execution_price": None,
                    "commission_per_contract": 0,
                },
            ],
        },
    }


class ImportCalendarBundleTest(unittest.TestCase):
    def test_products_array_imports_two_outright_legs(self) -> None:
        root = _calendar_root()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "TRD_CAL_1.json"
            path.write_text(json.dumps(root), encoding="utf-8")
            items, trade, _notes = itb.load_bundle(path)
        self.assertEqual(len(items), 2)
        self.assertEqual(items[0][1], "commodity")
        self.assertEqual(trade["legs"][0]["product_id"], "FUT_OUTRIGHT_CL_CLV6")
        self.assertEqual(trade["legs"][1]["product_id"], "FUT_OUTRIGHT_CL_CLX6")

        conn = sqlite3.connect(":memory:")
        try:
            _bootstrap_schema(conn)
            itb.insert_items(conn, items, trade)
            products = {
                row[0]
                for row in conn.execute("SELECT product_id FROM products").fetchall()
            }
            self.assertEqual(
                products,
                {"FUT_OUTRIGHT_CL_CLV6", "FUT_OUTRIGHT_CL_CLX6"},
            )
            legs = conn.execute(
                "SELECT leg_id, product_id, direction FROM trade_legs "
                "WHERE trade_id = ? ORDER BY leg_id",
                (trade["trade_id"],),
            ).fetchall()
            self.assertEqual(
                legs,
                [
                    ("TRD_CAL_1_L1", "FUT_OUTRIGHT_CL_CLV6", "SHORT"),
                    ("TRD_CAL_1_L2", "FUT_OUTRIGHT_CL_CLX6", "LONG"),
                ],
            )
            strategy = conn.execute(
                "SELECT strategy_type FROM trades WHERE trade_id = ?",
                (trade["trade_id"],),
            ).fetchone()
            self.assertEqual(strategy[0], "COMMODITY_CALENDAR")
        finally:
            conn.close()


class ImportCommodityForwardBundleTest(unittest.TestCase):
    def test_forward_persists_strike_on_commodity_extension(self) -> None:
        product = {
            "product_id": "FWD_CFF_CL_CLX6_80_31",
            "asset_kind": "COMMODITY",
            "underlying_id": "CL",
            "expiry_date": "2026-10-20",
            "settlement": "CASH",
            "currency": "USD",
            "contract_size": 1000,
            "day_count": "Actual365Fixed",
            "calendar": "UnitedStates",
        }
        commodity = {
            "instrument_type": "commodity_futures_forward",
            "product_code": "CL",
            "contract_ticker": "CLX6",
            "settlement_date": "2026-10-20",
            "multiplier": 1000,
            "strike": 80.31,
            "structured_params": {},
        }
        trade = {
            "trade_id": "TRD_CFF_1",
            "portfolio_id": "BOOK_3",
            "strategy_type": "COMMODITY_FUTURES_FORWARD",
            "trade_date": "2026-08-11",
            "legs": [
                {
                    "direction": "LONG",
                    "quantity": 1,
                    "execution_price": None,
                    "commission": 0,
                }
            ],
        }
        root = {"product": product, "commodity": commodity, "trade": trade}
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "TRD_CFF_1.json"
            path.write_text(json.dumps(root), encoding="utf-8")
            items, loaded, _notes = itb.load_bundle(path)

        conn = sqlite3.connect(":memory:")
        try:
            _bootstrap_schema(conn)
            itb.insert_items(conn, items, loaded)
            row = conn.execute(
                "SELECT instrument_type, strike, contract_ticker FROM products_commodity "
                "WHERE product_id = ?",
                ("FWD_CFF_CL_CLX6_80_31",),
            ).fetchone()
            self.assertEqual(row[0], "commodity_futures_forward")
            self.assertAlmostEqual(row[1], 80.31)
            self.assertEqual(row[2], "CLX6")
        finally:
            conn.close()

    def test_forward_without_strike_is_rejected(self) -> None:
        product = {
            "product_id": "FWD_CFF_CL_CLX6",
            "asset_kind": "COMMODITY",
            "underlying_id": "CL",
            "expiry_date": "2026-10-20",
            "settlement": "CASH",
            "currency": "USD",
            "contract_size": 1000,
            "day_count": "Actual365Fixed",
            "calendar": "UnitedStates",
        }
        commodity = {
            "instrument_type": "commodity_futures_forward",
            "product_code": "CL",
            "contract_ticker": "CLX6",
            "settlement_date": "2026-10-20",
            "multiplier": 1000,
            "structured_params": {},
        }
        trade = {
            "trade_id": "TRD_CFF_BAD",
            "portfolio_id": "BOOK_3",
            "strategy_type": "COMMODITY_FUTURES_FORWARD",
            "trade_date": "2026-08-11",
            "legs": [{"direction": "LONG", "quantity": 1, "execution_price": None}],
        }
        root = {"product": product, "commodity": commodity, "trade": trade}
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "TRD_CFF_BAD.json"
            path.write_text(json.dumps(root), encoding="utf-8")
            stderr = io.StringIO()
            with redirect_stderr(stderr):
                with self.assertRaises(SystemExit):
                    items, loaded, _notes = itb.load_bundle(path)
                    conn = sqlite3.connect(":memory:")
                    _bootstrap_schema(conn)
                    itb.insert_items(conn, items, loaded)
            self.assertIn("commodity.strike", stderr.getvalue())


    def test_leg_product_id_must_belong_to_bundle(self) -> None:
        root = _calendar_root()
        root["trade"]["legs"][1]["product_id"] = "FUT_OUTRIGHT_CL_CLZ6"
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "TRD_CAL_1.json"
            path.write_text(json.dumps(root), encoding="utf-8")
            stderr = io.StringIO()
            with redirect_stderr(stderr):
                with self.assertRaises(SystemExit):
                    itb.load_bundle(path)
            self.assertIn("must be one of this bundle's products", stderr.getvalue())

    def test_single_product_bundle_still_loads(self) -> None:
        product, equity, trade = _minimal_bundle(
            expiry_date="2027-03-20", trade_date="2026-05-11"
        )
        root = {"product": product, "equity": equity, "trade": trade}
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "TRD_BAD_DATES.json"
            path.write_text(json.dumps(root), encoding="utf-8")
            items, loaded, _notes = itb.load_bundle(path)
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0][0]["product_id"], product["product_id"])
        self.assertEqual(loaded["legs"][0]["product_id"], product["product_id"])


if __name__ == "__main__":
    unittest.main()
