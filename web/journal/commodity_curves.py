"""Commodity futures term-structure helpers for the Journal UI."""

from __future__ import annotations

import re
from datetime import date
from typing import Any

from journal.models import FuturesContract, FuturesDailyEod

_MONTH_CODES = {
    'F': 1,
    'G': 2,
    'H': 3,
    'J': 4,
    'K': 5,
    'M': 6,
    'N': 7,
    'Q': 8,
    'U': 9,
    'V': 10,
    'X': 11,
    'Z': 12,
}
_TICKER_RE = re.compile(r'^([A-Z]{1,4})([FGHJKMNQUVXZ])(\d{1,2})$')


def list_futures_product_codes() -> list[str]:
    return list(
        FuturesDailyEod.objects.order_by()
        .exclude(product_code__isnull=True)
        .exclude(product_code='')
        .values_list('product_code', flat=True)
        .distinct()
        .order_by('product_code')
    )


def list_futures_curve_as_of(product_code: str) -> list[date]:
    if not product_code:
        return []
    return list(
        FuturesDailyEod.objects.filter(
            product_code=product_code,
            settlement_price__isnull=False,
        )
        .order_by()
        .values_list('as_of', flat=True)
        .distinct()
        .order_by('-as_of')
    )


def list_futures_tickers(product_code: str) -> list[str]:
    if not product_code:
        return []
    return list(
        FuturesDailyEod.objects.filter(
            product_code=product_code,
            settlement_price__isnull=False,
        )
        .order_by()
        .values_list('ticker', flat=True)
        .distinct()
        .order_by('ticker')
    )


def _parse_expiry_from_ticker(ticker: str, as_of: date) -> date | None:
    m = _TICKER_RE.fullmatch((ticker or '').strip().upper())
    if not m:
        return None
    month = _MONTH_CODES.get(m.group(2))
    if month is None:
        return None
    raw_year = m.group(3)
    if len(raw_year) == 1:
        decade = (as_of.year // 10) * 10
        year = decade + int(raw_year)
        if year < as_of.year - 2:
            year += 10
    else:
        yy = int(raw_year)
        year = 2000 + yy if yy < 80 else 1900 + yy
    try:
        return date(year, month, 15)
    except ValueError:
        return None


def _expiry_map(tickers: list[str], ref_as_of: date) -> dict[str, date]:
    out: dict[str, date] = {}
    if not tickers:
        return out

    rows = (
        FuturesContract.objects.filter(ticker__in=tickers)
        .exclude(settlement_date__isnull=True)
        .exclude(settlement_date='')
        .values('ticker', 'settlement_date', 'listing_as_of')
        .order_by('ticker', '-listing_as_of')
    )
    seen: set[str] = set()
    for row in rows:
        t = row['ticker']
        if t in seen:
            continue
        seen.add(t)
        try:
            out[t] = date.fromisoformat(str(row['settlement_date'])[:10])
        except ValueError:
            continue

    for t in tickers:
        if t not in out:
            parsed = _parse_expiry_from_ticker(t, ref_as_of)
            if parsed is not None:
                out[t] = parsed
    return out


def _years_between(start: date, end: date) -> float:
    return (end - start).days / 365.25


def _as_of_axis(d: date, origin: date) -> float:
    """Years from coverage origin — continuous Y for 3D history axis."""
    return _years_between(origin, d)


def load_strip(product_code: str, as_of: date) -> list[dict[str, Any]]:
    """Settlement strip for one product / session day, ordered by expiry."""
    bars = list(
        FuturesDailyEod.objects.filter(
            product_code=product_code,
            as_of=as_of,
            settlement_price__isnull=False,
        ).values('ticker', 'settlement_price', 'close', 'volume')
    )
    if not bars:
        return []

    tickers = [str(b['ticker']) for b in bars]
    expiry = _expiry_map(tickers, as_of)
    points: list[dict[str, Any]] = []
    for b in bars:
        t = str(b['ticker'])
        exp = expiry.get(t)
        if exp is None:
            tau = None
        elif exp <= as_of:
            tau = 0.0
        else:
            tau = _years_between(as_of, exp)
        points.append(
            {
                'ticker': t,
                'settle': float(b['settlement_price']),
                'close': float(b['close']) if b['close'] is not None else None,
                'volume': float(b['volume']) if b['volume'] is not None else None,
                'expiry': exp.isoformat() if exp else None,
                'tau': tau,
            }
        )

    points.sort(
        key=lambda p: (
            0 if p['tau'] is not None else 1,
            p['tau'] if p['tau'] is not None else 0.0,
            p['expiry'] or '',
            p['ticker'],
        )
    )
    return points


def load_commodity_curve_bundle(
    product_code: str,
    as_of: date,
    *,
    ticker: str | None = None,
    max_cloud_days: int = 120,
) -> dict[str, Any]:
    """
    3D term-structure cloud for Journal:

    - X = τ (years to expiry from that session)
    - Y = history axis (years from first covered session)
    - Z = settlement

    Highlights:
    - ``strip`` — all tenors on selected ``as_of``
    - ``tenor_path`` — full history of selected ``ticker``
    """
    available = list_futures_curve_as_of(product_code)
    if not available:
        return {
            'product_code': product_code,
            'as_of': as_of.isoformat(),
            'ticker': ticker,
            'cloud': [],
            'strip': [],
            'tenor_path': [],
            'tickers': [],
            'coverage_days': 0,
            'first_as_of': None,
            'latest_as_of': None,
            'origin_as_of': None,
            'cloud_days': 0,
        }

    # Newest-first available; keep a recent window that includes selected as_of.
    window = [d for d in available if d <= as_of]
    if len(window) > max_cloud_days:
        window = window[:max_cloud_days]
    window_set = set(window)
    if as_of not in window_set:
        window.append(as_of)
        window_set.add(as_of)
    window_chrono = sorted(window)
    origin = window_chrono[0]

    bars = list(
        FuturesDailyEod.objects.filter(
            product_code=product_code,
            as_of__in=window_chrono,
            settlement_price__isnull=False,
        ).values('as_of', 'ticker', 'settlement_price', 'close', 'volume')
    )
    tickers = sorted({str(b['ticker']) for b in bars})
    expiry = _expiry_map(tickers, as_of)

    cloud: list[dict[str, Any]] = []
    for b in bars:
        t = str(b['ticker'])
        d = b['as_of']
        if hasattr(d, 'isoformat'):
            d_date = d
        else:
            d_date = date.fromisoformat(str(d)[:10])
        exp = expiry.get(t)
        if exp is None:
            continue
        tau = 0.0 if exp <= d_date else _years_between(d_date, exp)
        cloud.append(
            {
                'as_of': d_date.isoformat(),
                'y': _as_of_axis(d_date, origin),
                'ticker': t,
                'tau': tau,
                'settle': float(b['settlement_price']),
                'expiry': exp.isoformat(),
                'close': float(b['close']) if b['close'] is not None else None,
                'volume': float(b['volume']) if b['volume'] is not None else None,
            }
        )

    strip_rows = load_strip(product_code, as_of)
    strip_chart = [
        {
            **p,
            'as_of': as_of.isoformat(),
            'y': _as_of_axis(as_of, origin),
        }
        for p in strip_rows
        if p.get('tau') is not None
    ]

    # Default tenor = shortest-τ contract on the selected strip.
    tickers_on_strip = [p['ticker'] for p in strip_chart]
    all_tickers = tickers_on_strip + [t for t in tickers if t not in tickers_on_strip]
    selected_ticker = (ticker or '').strip().upper()
    if selected_ticker not in all_tickers:
        selected_ticker = tickers_on_strip[0] if tickers_on_strip else (all_tickers[0] if all_tickers else None)

    tenor_path = sorted(
        [p for p in cloud if p['ticker'] == selected_ticker],
        key=lambda p: p['as_of'],
    )

    return {
        'product_code': product_code,
        'as_of': as_of.isoformat(),
        'ticker': selected_ticker,
        'cloud': cloud,
        'strip': strip_chart,
        'strip_rows': strip_rows,
        'tenor_path': tenor_path,
        'tickers': all_tickers,
        'coverage_days': len(available),
        'cloud_days': len(window_chrono),
        'first_as_of': available[-1].isoformat() if available else None,
        'latest_as_of': available[0].isoformat() if available else None,
        'origin_as_of': origin.isoformat(),
    }
