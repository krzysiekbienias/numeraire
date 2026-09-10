#!/usr/bin/env python3
"""Parse configs/cme_manual_settles.xlsx (CME settlements paste) — dry-run only.

Workbook contract (per sheet named like CL / NG):
  A1 = 'As Of Date', B1 = YYYY-MM-DD
  next row = CME headers including MONTH and SETTLE
  following rows = paste from cmegroup.com settlements

Default is dry-run (print HAVE / WOULD_INSERT). Pass --apply to INSERT
only missing listed rows (source=cme_manual). Never updates existing EOD.

  python3 scripts/parse_cme_manual_settles.py
  python3 scripts/parse_cme_manual_settles.py --sheets CL,NG
  python3 scripts/parse_cme_manual_settles.py --apply

Dev only. To copy those rows onto prod (not the book DB), see
scripts/sync_cme_manual_settles.py (export JSON → scp → import --apply).
"""

from __future__ import annotations

import argparse
import os
import re
import sqlite3
import xml.etree.ElementTree as ET
import zipfile
from datetime import date, datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
NS = {"m": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}
REL_NS = {
    "r": "http://schemas.openxmlformats.org/officeDocument/2006/relationships",
    "pr": "http://schemas.openxmlformats.org/package/2006/relationships",
}

MONTH_CODE = {
    "JAN": "F",
    "FEB": "G",
    "MAR": "H",
    "APR": "J",
    "MAY": "K",
    "JUN": "M",
    "JUL": "N",
    "AUG": "Q",
    "SEP": "U",
    "OCT": "V",
    "NOV": "X",
    "DEC": "Z",
}
MONTH_RE = re.compile(
    r"^\s*(JAN|FEB|MAR|APR|MAY|JUN|JUL|AUG|SEP|OCT|NOV|DEC)\s+'?(\d{2})\s*$",
    re.IGNORECASE,
)
ASOF_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")


def _col_row(ref: str) -> tuple[int, int]:
    m = re.match(r"^([A-Z]+)(\d+)$", ref)
    if not m:
        raise ValueError(f"bad cell ref {ref!r}")
    col = 0
    for ch in m.group(1):
        col = col * 26 + (ord(ch) - 64)
    return col, int(m.group(2))


def _load_shared_strings(zf: zipfile.ZipFile) -> list[str]:
    if "xl/sharedStrings.xml" not in zf.namelist():
        return []
    root = ET.fromstring(zf.read("xl/sharedStrings.xml"))
    out: list[str] = []
    for si in root.findall("m:si", NS):
        out.append("".join(t.text or "" for t in si.findall(".//m:t", NS)))
    return out


def _cell_text(cell: ET.Element, shared: list[str]) -> str:
    t = cell.get("t")
    v = cell.find("m:v", NS)
    is_el = cell.find("m:is", NS)
    if t == "inlineStr" and is_el is not None:
        return "".join(x.text or "" for x in is_el.findall(".//m:t", NS)).strip()
    if v is None or v.text is None:
        return ""
    raw = v.text.strip()
    if t == "s":
        return shared[int(raw)].strip()
    return raw


def _sheet_paths(zf: zipfile.ZipFile) -> dict[str, str]:
    wb = ET.fromstring(zf.read("xl/workbook.xml"))
    rels = ET.fromstring(zf.read("xl/_rels/workbook.xml.rels"))
    rid_to_target: dict[str, str] = {}
    for rel in rels:
        rid = rel.get("Id")
        target = rel.get("Target")
        if rid and target:
            rid_to_target[rid] = target.lstrip("/")
            if not rid_to_target[rid].startswith("xl/"):
                rid_to_target[rid] = "xl/" + rid_to_target[rid]
    out: dict[str, str] = {}
    for sh in wb.findall("m:sheets/m:sheet", NS):
        name = sh.get("name") or ""
        rid = sh.get(
            "{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id"
        )
        if name and rid and rid in rid_to_target:
            out[name] = rid_to_target[rid]
    return out


def _read_grid(zf: zipfile.ZipFile, sheet_path: str, shared: list[str]) -> dict[tuple[int, int], str]:
    root = ET.fromstring(zf.read(sheet_path))
    grid: dict[tuple[int, int], str] = {}
    for cell in root.findall("m:sheetData/m:row/m:c", NS):
        ref = cell.get("r")
        if not ref:
            continue
        col, row = _col_row(ref)
        grid[(row, col)] = _cell_text(cell, shared)
    return grid


def _parse_number(raw: str) -> float | None:
    s = (raw or "").strip()
    if not s or s in {"-", "—", "–"}:
        return None
    s = s.replace(",", "")
    s = re.sub(r"[A-Za-z]+$", "", s).strip()
    s = s.replace("+", "")
    if s.startswith("."):
        s = "0" + s
    if s.startswith("-."):
        s = "-0" + s[1:]
    try:
        return float(s)
    except ValueError:
        return None


def month_to_candidates(product: str, month_label: str) -> list[str]:
    m = MONTH_RE.match(month_label or "")
    if not m:
        return []
    code = MONTH_CODE[m.group(1).upper()]
    yy = m.group(2)
    prod = product.strip().upper()
    # CL-style 1-digit year (CLV6) and NG-style 2-digit (NGZ26).
    return [f"{prod}{code}{yy}", f"{prod}{code}{yy[-1]}"]


def parse_sheet(grid: dict[tuple[int, int], str], product: str) -> tuple[str, list[dict]]:
    as_of = ""
    header_row = None
    col_month = col_settle = col_vol = None

    max_row = max((r for r, _ in grid), default=0)
    max_col = max((c for _, c in grid), default=0)

    a1 = grid.get((1, 1), "")
    b1 = grid.get((1, 2), "")
    if ASOF_RE.match(b1):
        as_of = b1
    elif ASOF_RE.match(a1):
        as_of = a1

    for r in range(1, min(max_row, 8) + 1):
        labels = {grid.get((r, c), "").strip().upper(): c for c in range(1, max_col + 1)}
        if "MONTH" in labels and "SETTLE" in labels:
            header_row = r
            col_month = labels["MONTH"]
            col_settle = labels["SETTLE"]
            col_vol = labels.get("EST. VOLUME") or labels.get("EST VOLUME")
            break

    if not as_of:
        raise ValueError(f"{product}: missing As Of Date (B1 as YYYY-MM-DD)")
    date.fromisoformat(as_of)
    if header_row is None or col_month is None or col_settle is None:
        raise ValueError(f"{product}: no MONTH/SETTLE header row")

    rows: list[dict] = []
    for r in range(header_row + 1, max_row + 1):
        month = grid.get((r, col_month), "").strip()
        if not month or month.upper() == "MONTH":
            continue
        settle = _parse_number(grid.get((r, col_settle), ""))
        volume = _parse_number(grid.get((r, col_vol), "")) if col_vol else None
        cands = month_to_candidates(product, month)
        rows.append(
            {
                "product": product,
                "as_of": as_of,
                "month": month.upper(),
                "ticker_candidates": cands,
                "settlement_price": settle,
                "volume": volume,
            }
        )
    return as_of, rows


def latest_listing(db: sqlite3.Connection, product: str) -> str | None:
    cur = db.execute(
        "SELECT MAX(listing_as_of) FROM futures_contract WHERE product_code = ?",
        (product,),
    )
    row = cur.fetchone()
    return row[0] if row and row[0] else None


def resolve_against_db(
    db: sqlite3.Connection, parsed: list[dict]
) -> list[dict]:
    out: list[dict] = []
    listing_cache: dict[str, str | None] = {}
    listed_cache: dict[str, set[str]] = {}
    eod_cache: dict[tuple[str, str], tuple[float | None, str | None]] = {}

    for rec in parsed:
        product = rec["product"]
        as_of = rec["as_of"]
        if product not in listing_cache:
            listing_cache[product] = latest_listing(db, product)
            listing = listing_cache[product]
            if listing:
                rows = db.execute(
                    "SELECT ticker FROM futures_contract "
                    "WHERE product_code = ? AND listing_as_of = ?",
                    (product, listing),
                ).fetchall()
                listed_cache[product] = {r[0] for r in rows}
            else:
                listed_cache[product] = set()

        ticker = None
        for cand in rec["ticker_candidates"]:
            if cand in listed_cache[product]:
                ticker = cand
                break
        if ticker is None and rec["ticker_candidates"]:
            ticker = rec["ticker_candidates"][0]

        key = (ticker or "", as_of)
        if key not in eod_cache and ticker:
            row = db.execute(
                "SELECT settlement_price, source FROM futures_daily_eod "
                "WHERE ticker = ? AND as_of = ? AND timespan = '1session'",
                (ticker, as_of),
            ).fetchone()
            eod_cache[key] = (row[0], row[1]) if row else (None, None)

        have, source = eod_cache.get(key, (None, None))
        listed = ticker in listed_cache[product] if ticker else False
        if rec["settlement_price"] is None:
            action = "SKIP_NO_SETTLE"
        elif not listed:
            action = "UNLISTED"
        elif have is not None:
            action = "SKIP_HAVE"
        else:
            action = "WOULD_INSERT"

        out.append(
            {
                **rec,
                "ticker": ticker,
                "listed": listed,
                "have_settle": have,
                "have_source": source,
                "action": action,
            }
        )
    return out


def print_table(rows: list[dict]) -> None:
    headers = (
        "product",
        "as_of",
        "month",
        "ticker",
        "cme_settle",
        "db_settle",
        "db_source",
        "action",
    )
    widths = [len(h) for h in headers]
    lines: list[tuple] = []
    for r in rows:
        line = (
            r["product"],
            r["as_of"],
            r["month"],
            r.get("ticker") or "",
            "" if r["settlement_price"] is None else f"{r['settlement_price']:.4g}",
            "" if r.get("have_settle") is None else f"{r['have_settle']:.4g}",
            r.get("have_source") or "",
            r["action"],
        )
        lines.append(line)
        widths = [max(w, len(str(x))) for w, x in zip(widths, line)]

    fmt = "  ".join(f"{{:{w}}}" for w in widths)
    print(fmt.format(*headers))
    print(fmt.format(*("-" * w for w in widths)))
    for line in lines:
        print(fmt.format(*line))

    counts: dict[str, int] = {}
    for r in rows:
        counts[r["action"]] = counts.get(r["action"], 0) + 1
    print()
    print("counts:", "  ".join(f"{k}={v}" for k, v in sorted(counts.items())))


def apply_inserts(db: sqlite3.Connection, rows: list[dict]) -> int:
    now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    sql = """
        INSERT INTO futures_daily_eod (
            ticker, product_code, as_of, session_calendar,
            open, high, low, close, settlement_price,
            currency, volume, source, timespan, ingested_at
        )
        SELECT
            ?, ?, ?, 'America/Chicago',
            ?, ?, ?, ?, ?,
            'USD', ?, 'cme_manual', '1session', ?
        WHERE NOT EXISTS (
            SELECT 1 FROM futures_daily_eod
            WHERE ticker = ? AND as_of = ? AND timespan = '1session'
        )
    """
    n = 0
    for r in rows:
        if r["action"] != "WOULD_INSERT":
            continue
        px = float(r["settlement_price"])
        vol = r["volume"] if r["volume"] is not None else 0.0
        ticker = r["ticker"]
        as_of = r["as_of"]
        cur = db.execute(
            sql,
            (
                ticker,
                r["product"],
                as_of,
                px,
                px,
                px,
                px,
                px,
                vol,
                now,
                ticker,
                as_of,
            ),
        )
        n += cur.rowcount
        r["action"] = "INSERTED" if cur.rowcount else "SKIP_HAVE"
    return n


def main() -> int:
    parser = argparse.ArgumentParser(description="Dry-run parse of CME manual settles xlsx.")
    parser.add_argument(
        "--xlsx",
        default=str(REPO_ROOT / "configs" / "cme_manual_settles.xlsx"),
        help="workbook path",
    )
    parser.add_argument(
        "--sheets",
        default="CL,NG",
        help="comma list of tabs (default CL,NG)",
    )
    parser.add_argument(
        "--db",
        default=os.environ.get("NUMERAIRE_DB_PATH", str(REPO_ROOT / "db.sqlite3")),
        help="SQLite path for HAVE/MISSING compare (read-only)",
    )
    parser.add_argument(
        "--no-db",
        action="store_true",
        help="parse only, do not open the database",
    )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="INSERT WOULD_INSERT rows (cme_manual). Default: dry-run.",
    )
    args = parser.parse_args()

    xlsx = Path(args.xlsx)
    if not xlsx.is_file():
        raise SystemExit(f"xlsx not found: {xlsx}")

    wanted = [s.strip().upper() for s in args.sheets.split(",") if s.strip()]
    parsed: list[dict] = []
    as_ofs: set[str] = set()

    with zipfile.ZipFile(xlsx) as zf:
        shared = _load_shared_strings(zf)
        paths = _sheet_paths(zf)
        for name in wanted:
            if name not in paths:
                print(f"warn: sheet {name} missing")
                continue
            grid = _read_grid(zf, paths[name], shared)
            as_of, rows = parse_sheet(grid, name)
            as_ofs.add(as_of)
            parsed.extend(rows)
            print(f"parsed {name}: as_of={as_of} rows={len(rows)}")

    if len(as_ofs) > 1:
        print(f"warn: mixed as_of in workbook: {sorted(as_ofs)}")

    if args.no_db:
        if args.apply:
            raise SystemExit("--apply requires the database (drop --no-db)")
        for rec in parsed:
            rec["ticker"] = (rec["ticker_candidates"] or [""])[0]
            rec["action"] = "PARSED" if rec["settlement_price"] is not None else "SKIP_NO_SETTLE"
            rec["have_settle"] = None
            rec["have_source"] = None
        print()
        print_table(parsed)
        return 0

    db_path = Path(args.db)
    if not db_path.is_file():
        raise SystemExit(f"database not found: {db_path}")

    if args.apply:
        db = sqlite3.connect(str(db_path))
        try:
            resolved = resolve_against_db(db, parsed)
            inserted = apply_inserts(db, resolved)
            db.commit()
        except Exception:
            db.rollback()
            raise
        finally:
            db.close()
        print()
        print(f"applied vs {db_path}: inserted={inserted}")
        print_table(resolved)
        return 0

    db = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    try:
        resolved = resolve_against_db(db, parsed)
    finally:
        db.close()

    print()
    print(f"dry-run vs {db_path} (no inserts)")
    print_table(resolved)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
