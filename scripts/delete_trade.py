#!/usr/bin/env python3
"""Delete trades from the book: header, legs, marks and exposure history.

Counterpart of `import_trade_bundle.py` — the two are the only code that write to
`trades` / `trade_legs`. Removal leans entirely on the schema's ON DELETE CASCADE,
so `PRAGMA foreign_keys = ON` is mandatory: without it SQLite drops the header and
silently leaves orphaned marks behind.

Products are deliberately left alone. They are shared, reusable catalog entries and
a product with no trade is a normal state — it is an instrument definition ready to
be booked again.

The bundle in `trades/incoming/` is not touched either, so a deleted trade can be
restored by re-running the importer on the same file.

Examples:
  NUMERAIRE_DB_PATH=db.sqlite3 python3 scripts/delete_trade.py TRD_10005

  # Show what would go without touching the database
  python3 scripts/delete_trade.py TRD_10005 TRD_10006 --dry-run

  # Explicit database
  python3 scripts/delete_trade.py TRD_10005 --db /tmp/scratch.sqlite3
"""

from __future__ import annotations

import argparse
import os
import sqlite3
import sys
from pathlib import Path

# Every table that cascades off a trade, in the order a reader cares about.
DEPENDENT_TABLES = (
    ("trade_legs", "leg(s)"),
    ("trade_leg_mtm_eod", "mark(s)"),
    ("trade_leg_mtm_eod_archive", "archived mark(s)"),
    ("trade_leg_exposure_eod", "exposure row(s)"),
    ("trade_leg_exposure_eod_archive", "archived exposure row(s)"),
)


def _die(msg: str, code: int = 1) -> None:
    print(msg, file=sys.stderr)
    sys.exit(code)


def default_db_path() -> str:
    return os.environ.get("NUMERAIRE_DB_PATH", "db.sqlite3")


def _trade_exists(conn: sqlite3.Connection, trade_id: str) -> bool:
    cur = conn.cursor()
    cur.execute("SELECT 1 FROM trades WHERE trade_id = ? LIMIT 1", (trade_id,))
    return cur.fetchone() is not None


def _dependent_counts(conn: sqlite3.Connection, trade_id: str) -> list[tuple[str, int]]:
    counts = []
    cur = conn.cursor()
    for table, label in DEPENDENT_TABLES:
        cur.execute(f"SELECT COUNT(*) FROM {table} WHERE trade_id = ?", (trade_id,))
        counts.append((label, int(cur.fetchone()[0])))
    return counts


def _describe(counts: list[tuple[str, int]]) -> str:
    parts = [f"{n} {label}" for label, n in counts if n]
    return ", ".join(parts) if parts else "no dependent rows"


def _require_cascade(conn: sqlite3.Connection) -> None:
    """Cascade is the whole deletion strategy — refuse to run if it is off."""
    cur = conn.cursor()
    cur.execute("PRAGMA foreign_keys")
    row = cur.fetchone()
    if not row or not int(row[0]):
        _die(
            "cannot enable PRAGMA foreign_keys — deleting now would orphan marks "
            "and exposure rows; aborting without touching the database"
        )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Delete trades (header, legs, marks, exposure) from SQLite. Products are kept."
    )
    parser.add_argument(
        "trade_ids",
        nargs="+",
        help="Trade ids to delete (TRD_10005 …)",
    )
    parser.add_argument(
        "--dry-run",
        dest="dry_run",
        action="store_true",
        help="Report what would be deleted and exit without writing",
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

    try:
        conn = sqlite3.connect(str(db_path))
    except sqlite3.Error as e:
        _die(f"cannot open database {db_path}: {e}")

    deleted = 0
    skipped = 0
    try:
        conn.execute("PRAGMA foreign_keys = ON")
        _require_cascade(conn)

        for trade_id in args.trade_ids:
            tid = trade_id.strip()
            if not _trade_exists(conn, tid):
                print(f"SKIP: {tid} (not in database)")
                skipped += 1
                continue

            counts = _dependent_counts(conn, tid)
            if args.dry_run:
                print(f"DRY-RUN: {tid} would delete {_describe(counts)}")
                continue

            try:
                conn.execute("BEGIN")
                conn.execute("DELETE FROM trades WHERE trade_id = ?", (tid,))
                conn.commit()
            except sqlite3.Error as e:
                conn.rollback()
                _die(f"{tid}: SQLite error: {e}")

            print(f"OK: {tid} deleted ({_describe(counts)})")
            deleted += 1
    finally:
        conn.close()

    if args.dry_run:
        print(f"Done: dry run, nothing written -> {db_path}")
    else:
        print(f"Done: {deleted} deleted, {skipped} skipped -> {db_path}")


if __name__ == "__main__":
    main()
