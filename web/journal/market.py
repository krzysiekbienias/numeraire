"""Helpers for reading underlier OHLC from equity / index EOD tables."""

from django.db.models import Count, Max, Min, QuerySet

from journal.models import EquityDailyEod, IndexDailyEod

# Product.underlying_id uses bare symbols (NDX); index bars use Polygon tickers (I:NDX).
_INDEX_ALIASES = {
    'NDX': 'I:NDX',
}


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
