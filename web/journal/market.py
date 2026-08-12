"""Helpers for reading underlier OHLC from equity / index / futures EOD tables."""

from datetime import date
from typing import Any

from django.db.models import Count, Max, Min, QuerySet

from journal.models import EquityDailyEod, FuturesDailyEod, IndexDailyEod

# Product.underlying_id uses bare symbols (NDX); index bars use Polygon tickers (I:NDX).
_INDEX_ALIASES = {
    'NDX': 'I:NDX',
}


def futures_eod_on_or_before(ticker: str, as_of: date) -> FuturesDailyEod | None:
    """Exact session bar for ``ticker``, else nearest session on or before ``as_of``."""
    ticker = (ticker or '').strip().upper()
    if not ticker or as_of is None:
        return None
    exact = (
        FuturesDailyEod.objects.filter(ticker=ticker, as_of=as_of)
        .order_by('-id')
        .first()
    )
    if exact is not None:
        return exact
    return (
        FuturesDailyEod.objects.filter(ticker=ticker, as_of__lte=as_of)
        .order_by('-as_of', '-id')
        .first()
    )


def futures_market_bundle(ticker: str, as_of: date) -> dict[str, Any] | None:
    """Market panel payload from ``futures_daily_eod`` (independent of MTM)."""
    row = futures_eod_on_or_before(ticker, as_of)
    if row is None:
        return None
    lag_days = (as_of - row.as_of).days
    return {
        'ticker': row.ticker,
        'product_code': row.product_code,
        'as_of': row.as_of,
        'requested_as_of': as_of,
        'stale': lag_days > 0,
        'lag_days': lag_days,
        'settlement_price': row.settlement_price,
        'close': row.close,
        'open': row.open,
        'high': row.high,
        'low': row.low,
        'volume': row.volume,
        'session_calendar': row.session_calendar,
        'source': row.source,
        'currency': row.currency,
    }


def futures_close_series(ticker: str) -> list[dict[str, Any]]:
    """Chronological settle/close series for the trade underlier chart."""
    ticker = (ticker or '').strip().upper()
    if not ticker:
        return []
    rows = (
        FuturesDailyEod.objects.filter(ticker=ticker)
        .order_by('as_of')
        .values_list('as_of', 'settlement_price', 'close')
    )
    out: list[dict[str, Any]] = []
    for d, settle, close in rows:
        px = settle if settle is not None else close
        if px is None:
            continue
        out.append({'date': d.isoformat(), 'close': px})
    return out


def resolve_underlier(ticker: str) -> tuple[str, str, QuerySet] | None:
    """Return ``(kind, canonical_ticker, queryset)`` for daily adjusted bars, or None."""
    ticker = (ticker or '').strip()
    if not ticker:
        return None

    candidates: list[tuple[str, type, str]] = [
        ('equity', EquityDailyEod, ticker),
        ('index', IndexDailyEod, ticker),
    ]
    alias = _INDEX_ALIASES.get(ticker.upper())
    if alias and alias != ticker:
        candidates.append(('index', IndexDailyEod, alias))
    if not ticker.startswith('I:') and ticker.upper() == ticker:
        # Bare symbol → try I:SYMBOL for indices.
        candidates.append(('index', IndexDailyEod, f'I:{ticker}'))

    seen: set[tuple[str, str]] = set()
    for kind, model, candidate in candidates:
        key = (kind, candidate)
        if key in seen:
            continue
        seen.add(key)
        qs = model.objects.filter(ticker=candidate, timespan='1d', adjusted=1)
        if qs.exists():
            return kind, candidate, qs.order_by('-as_of')
    return None


def list_underliers() -> list[dict]:
    """Catalog of tickers present in equity/index daily EOD tables."""
    rows: list[dict] = []
    for kind, model in (('equity', EquityDailyEod), ('index', IndexDailyEod)):
        summaries = (
            model.objects.filter(timespan='1d', adjusted=1)
            .values('ticker')
            .annotate(
                bars=Count('id'),
                first_as_of=Min('as_of'),
                last_as_of=Max('as_of'),
            )
            .order_by('ticker')
        )
        for summary in summaries:
            latest = (
                model.objects.filter(
                    ticker=summary['ticker'],
                    timespan='1d',
                    adjusted=1,
                    as_of=summary['last_as_of'],
                )
                .order_by('-id')
                .first()
            )
            rows.append(
                {
                    'kind': kind,
                    'ticker': summary['ticker'],
                    'bars': summary['bars'],
                    'first_as_of': summary['first_as_of'],
                    'last_as_of': summary['last_as_of'],
                    'last_close': latest.close if latest else None,
                    'currency': latest.currency if latest else None,
                    'source': latest.source if latest else None,
                }
            )
    rows.sort(key=lambda r: (r['kind'], r['ticker']))
    return rows
