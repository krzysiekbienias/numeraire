#!/usr/bin/env python3
"""Unit tests for import_trade_bundle validation (stdlib unittest)."""

from __future__ import annotations

import io
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
                    itb.insert_bundle(conn, product, equity, trade)
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
            itb.insert_bundle(conn, product, equity, trade)
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
            itb.insert_bundle(conn, product, equity, trade)
            row = conn.execute("SELECT 1 FROM trades WHERE trade_id = ?", (trade["trade_id"],)).fetchone()
            self.assertIsNotNone(row)
        finally:
            conn.close()


def _bootstrap_schema(conn: sqlite3.Connection) -> None:
    schema_path = _REPO_ROOT / "sql" / "schema_v1.sql"
    conn.executescript(schema_path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
