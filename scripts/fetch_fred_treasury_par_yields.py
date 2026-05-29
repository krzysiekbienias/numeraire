#!/usr/bin/env python3
"""
Fetch US Treasury constant-maturity par yields from FRED (DGS*) and upsert into SQLite.

Requires: FRED_API_KEY in environment or repo-root `.env`.
Optional: FRED_BASE_URL (default https://api.stlouisfed.org/fred).

Example:
  python3 scripts/fetch_fred_treasury_par_yields.py --as-of 2026-05-27
  python3 scripts/fetch_fred_treasury_par_yields.py --as-of 2026-05-27 --dry-run
"""

from __future__ import annotations

import argparse
import json
import os
import sqlite3
import sys
import urllib.error
import urllib.parse
import urllib.request
from datetime import date
from pathlib import Path
from typing import Any, Mapping, Sequence


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CURVE_CONFIG = REPO_ROOT / "configs" / "fred_usd_treasury_par_curve.json"
DEFAULT_FRED_BASE = "https://api.stlouisfed.org/fred"


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


def _fred_observation(
    base_url: str,
    api_key: str,
    series_id: str,
    as_of: str,
) -> float | None:
    params = urllib.parse.urlencode(
        {
            "series_id": series_id,
            "api_key": api_key,
            "file_type": "json",
            "observation_start": as_of,
            "observation_end": as_of,
        }
    )
    url = f"{base_url.rstrip('/')}/series/observations?{params}"
    req = urllib.request.Request(url, headers={"User-Agent": "numeraire-fetch-fred/1.0"})
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            payload = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")[:500]
        _die(f"FRED HTTP {e.code} for {series_id}: {body}")
    except urllib.error.URLError as e:
        _die(f"FRED request failed for {series_id}: {e}")

    observations = payload.get("observations") or []
    if not observations:
        return None
    raw = observations[0].get("value")
    if raw in (None, ".", ""):
        return None
    try:
        pct = float(raw)
    except (TypeError, ValueError):
        return None
    return round(pct / 100.0, 8)


def _load_curve_config(path: Path) -> Mapping[str, Any]:
    if not path.is_file():
        _die(f"curve config not found: {path}")
    with path.open(encoding="utf-8") as f:
        cfg = json.load(f)
    if "curve_id" not in cfg or "pillars" not in cfg:
        _die(f"invalid curve config (need curve_id, pillars): {path}")
    return cfg


def _upsert_curve(
    conn: sqlite3.Connection,
    cfg: Mapping[str, Any],
    as_of: str,
    points: Sequence[tuple[Mapping[str, Any], float]],
) -> None:
    curve_id = str(cfg["curve_id"])
    cur = conn.cursor()
    cur.execute(
        """
        INSERT INTO discount_curve_eod (
            curve_id, as_of, currency, curve_kind, source,
            day_count, session_calendar, ingested_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, datetime('now'))
        ON CONFLICT (curve_id, as_of) DO UPDATE SET
            ingested_at = excluded.ingested_at
        """,
        (
            curve_id,
            as_of,
            str(cfg.get("currency", "USD")),
            str(cfg.get("curve_kind", "treasury_par_zero_fred")),
            str(cfg.get("source", "FRED")),
            str(cfg.get("day_count", "Actual365Fixed")),
            str(cfg.get("session_calendar", "America/New_York")),
        ),
    )
    for pillar, zero_rate in points:
        cur.execute(
            """
            INSERT INTO discount_curve_point_eod (
                curve_id, as_of, tenor, tenor_days, instrument_type,
                fred_series_id, zero_rate
            ) VALUES (?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT (curve_id, as_of, tenor) DO UPDATE SET
                tenor_days = excluded.tenor_days,
                instrument_type = excluded.instrument_type,
                fred_series_id = excluded.fred_series_id,
                zero_rate = excluded.zero_rate
            """,
            (
                curve_id,
                as_of,
                str(pillar["tenor"]),
                pillar.get("tenor_days"),
                str(pillar["instrument_type"]),
                str(pillar["fred_series_id"]),
                zero_rate,
            ),
        )
    conn.commit()


def main() -> None:
    _load_dotenv(REPO_ROOT / ".env")

    parser = argparse.ArgumentParser(
        description="Fetch FRED Treasury par yields (DGS*) into SQLite discount_curve_* tables."
    )
    parser.add_argument("--as-of", required=True, help="Valuation date YYYY-MM-DD")
    parser.add_argument(
        "--curve-config",
        default=str(DEFAULT_CURVE_CONFIG),
        help="JSON pillar map (default: configs/fred_usd_treasury_par_curve.json)",
    )
    parser.add_argument(
        "--db-path",
        default=os.environ.get("NUMERAIRE_DB_PATH", "db.sqlite3"),
        help="SQLite path (default: NUMERAIRE_DB_PATH or db.sqlite3)",
    )
    parser.add_argument("--dry-run", action="store_true", help="Fetch only; do not write DB")
    args = parser.parse_args()

    as_of = _parse_as_of(args.as_of)
    api_key = os.environ.get("FRED_API_KEY", "").strip()
    if not api_key:
        _die("FRED_API_KEY is not set (add to .env or export)")

    base_url = os.environ.get("FRED_BASE_URL", DEFAULT_FRED_BASE).strip() or DEFAULT_FRED_BASE
    cfg = _load_curve_config(Path(args.curve_config))

    fetched: list[tuple[Mapping[str, Any], float]] = []
    missing: list[str] = []
    for pillar in cfg["pillars"]:
        series_id = str(pillar["fred_series_id"])
        rate = _fred_observation(base_url, api_key, series_id, as_of)
        if rate is None:
            missing.append(f"{pillar['tenor']} ({series_id})")
            continue
        fetched.append((pillar, rate))
        print(f"  {pillar['tenor']:>4}  {series_id:8}  {rate:.6f}")

    if not fetched:
        _die(f"no FRED observations for as_of={as_of}; missing all pillars")

    if missing:
        print(f"warn: skipped {len(missing)} pillars with no quote: {', '.join(missing)}", file=sys.stderr)

    if args.dry_run:
        print(f"dry-run: would upsert {len(fetched)} points for {cfg['curve_id']} @ {as_of}")
        return

    db_path = Path(args.db_path)
    if not db_path.is_file():
        _die(f"database not found: {db_path} (run schema or sql/apply_discount_curve_fred.sql)")

    conn = sqlite3.connect(db_path)
    try:
        conn.execute("PRAGMA foreign_keys = ON")
        _upsert_curve(conn, cfg, as_of, fetched)
    finally:
        conn.close()

    print(f"ok: {cfg['curve_id']} @ {as_of} -> {db_path} ({len(fetched)} pillars)")


if __name__ == "__main__":
    main()
