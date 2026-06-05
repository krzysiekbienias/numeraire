#!/usr/bin/env python3
"""
Load static par-curve pillars from JSON into SQLite `par_curve_*` tables.

Use for textbook / manual curves where each pillar carries `quoted_rate` (and
optionally `futures_price`) instead of a FRED series id.

Example:
  python3 scripts/load_static_par_curve.py --as-of 2026-06-01
  python3 scripts/load_static_par_curve.py --as-of 2026-06-01 --dry-run
  python3 scripts/load_static_par_curve.py --curve-config configs/eur_euro_rates_par_curve.json
"""

from __future__ import annotations

import argparse
import json
import os
import sqlite3
import sys
from datetime import date
from pathlib import Path
from typing import Any, Mapping, Sequence


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CURVE_CONFIG = REPO_ROOT / "configs" / "eur_euro_rates_par_curve.json"
STATIC_FRED_PLACEHOLDER = "STATIC"


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


def _load_curve_config(path: Path) -> Mapping[str, Any]:
    if not path.is_file():
        _die(f"curve config not found: {path}")
    with path.open(encoding="utf-8") as f:
        cfg = json.load(f)
    if "curve_id" not in cfg or "pillars" not in cfg:
        _die(f"invalid curve config (need curve_id, pillars): {path}")
    return cfg


def _pillar_quoted_rate(pillar: Mapping[str, Any]) -> float:
    if "quoted_rate" in pillar:
        return float(pillar["quoted_rate"])
    if "futures_price" in pillar:
        price = float(pillar["futures_price"])
        return round((100.0 - price) / 100.0, 8)
    _die(f"pillar {pillar.get('tenor', '?')}: need quoted_rate or futures_price")


def _collect_points(cfg: Mapping[str, Any]) -> list[tuple[Mapping[str, Any], float]]:
    points: list[tuple[Mapping[str, Any], float]] = []
    for pillar in cfg["pillars"]:
        rate = _pillar_quoted_rate(pillar)
        if rate < 0.0:
            _die(f"pillar {pillar['tenor']}: quoted_rate must be >= 0, got {rate}")
        points.append((pillar, rate))
        print(f"  {str(pillar['tenor']):>4}  {str(pillar['instrument_type']):>8}  {rate:.6f}")
    return points


def _upsert_curve(
    conn: sqlite3.Connection,
    cfg: Mapping[str, Any],
    as_of: str,
    points: Sequence[tuple[Mapping[str, Any], float]],
) -> None:
    curve_id = str(cfg["curve_id"])
    notes = cfg.get("notes")
    cur = conn.cursor()
    cur.execute(
        """
        INSERT INTO par_curve_eod (
            curve_id, as_of, currency, curve_kind, source,
            day_count, session_calendar, notes, ingested_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, datetime('now'))
        ON CONFLICT (curve_id, as_of) DO UPDATE SET
            currency = excluded.currency,
            curve_kind = excluded.curve_kind,
            source = excluded.source,
            day_count = excluded.day_count,
            session_calendar = excluded.session_calendar,
            notes = excluded.notes,
            ingested_at = excluded.ingested_at
        """,
        (
            curve_id,
            as_of,
            str(cfg.get("currency", "USD")),
            str(cfg.get("curve_kind", "static_par")),
            str(cfg.get("source", "STATIC")),
            str(cfg.get("day_count", "Actual365Fixed")),
            str(cfg.get("session_calendar", "TARGET")),
            str(notes) if notes is not None else None,
        ),
    )
    for pillar, quoted_rate in points:
        series_id = str(pillar.get("fred_series_id", STATIC_FRED_PLACEHOLDER))
        quote_style = str(pillar.get("quote_style", "annualized_decimal"))
        cur.execute(
            """
            INSERT INTO par_curve_point_eod (
                curve_id, as_of, tenor, tenor_days, instrument_type,
                fred_series_id, quoted_rate, quote_style
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT (curve_id, as_of, tenor) DO UPDATE SET
                tenor_days = excluded.tenor_days,
                instrument_type = excluded.instrument_type,
                fred_series_id = excluded.fred_series_id,
                quoted_rate = excluded.quoted_rate,
                quote_style = excluded.quote_style
            """,
            (
                curve_id,
                as_of,
                str(pillar["tenor"]),
                pillar.get("tenor_days"),
                str(pillar["instrument_type"]),
                series_id,
                quoted_rate,
                quote_style,
            ),
        )
    conn.commit()


def main() -> None:
    _load_dotenv(REPO_ROOT / ".env")

    parser = argparse.ArgumentParser(
        description="Load static par-curve quotes from JSON into SQLite par_curve_* tables."
    )
    parser.add_argument(
        "--as-of",
        help="Valuation date YYYY-MM-DD (default: as_of_anchor from config, else required)",
    )
    parser.add_argument(
        "--curve-config",
        default=str(DEFAULT_CURVE_CONFIG),
        help="JSON pillar map (default: configs/eur_euro_rates_par_curve.json)",
    )
    parser.add_argument(
        "--db-path",
        default=os.environ.get("NUMERAIRE_DB_PATH", "db.sqlite3"),
        help="SQLite path (default: NUMERAIRE_DB_PATH or db.sqlite3)",
    )
    parser.add_argument("--dry-run", action="store_true", help="Print quotes only; do not write DB")
    args = parser.parse_args()

    cfg = _load_curve_config(Path(args.curve_config))
    as_of_raw = args.as_of or cfg.get("as_of_anchor")
    if not as_of_raw:
        _die("--as-of is required when config has no as_of_anchor")
    as_of = _parse_as_of(str(as_of_raw))

    print(f"curve {cfg['curve_id']} @ {as_of}")
    points = _collect_points(cfg)
    if not points:
        _die("no pillars in config")

    if args.dry_run:
        print(f"dry-run: would upsert {len(points)} points for {cfg['curve_id']} @ {as_of}")
        return

    db_path = Path(args.db_path)
    if not db_path.is_file():
        _die(f"database not found: {db_path} (apply sql/schema_v1.sql or run dev_main bootstrap)")

    conn = sqlite3.connect(db_path)
    try:
        conn.execute("PRAGMA foreign_keys = ON")
        _upsert_curve(conn, cfg, as_of, points)
    finally:
        conn.close()

    print(f"ok: {cfg['curve_id']} @ {as_of} -> {db_path} ({len(points)} pillars)")


if __name__ == "__main__":
    main()
