"""Portfolio / trade exposure helpers over ``trade_leg_exposure_eod``."""

from __future__ import annotations

from datetime import date

from django.db.models import Count, Max, Sum

from journal.models import Trade, TradeLegExposureEod


def list_exposure_as_of(portfolio_id: str | None = None) -> list[date]:
    qs = TradeLegExposureEod.objects.order_by()
    if portfolio_id:
        qs = qs.filter(trade__portfolio_id=portfolio_id)
    return list(qs.values_list('as_of', flat=True).distinct().order_by('-as_of'))


def list_exposure_portfolios() -> list[str]:
    return list(
        Trade.objects.filter(exposure_rows__isnull=False)
        .order_by()
        .values_list('portfolio_id', flat=True)
        .distinct()
        .order_by('portfolio_id')
    )


def portfolio_exposure_profile(
    as_of: date,
    portfolio_id: str,
    *,
    pillar_id: str | None = None,
) -> dict | None:
    """Aggregate leg EE / PFE to a portfolio pillar profile.

    EE sums across legs are exact (linearity of expectation). Summed PFE is a
    conservative upper bound — not a joint portfolio PFE.

    ``by_trade`` is attribution **at one pillar** (same tenor as the chart /
    pillar table), not a sum across the time grid.
    """
    qs = TradeLegExposureEod.objects.filter(as_of=as_of, trade__portfolio_id=portfolio_id)
    if not qs.exists():
        return None

    meta = qs.aggregate(
        rows=Count('id'),
        trades=Count('trade_id', distinct=True),
        legs=Count('leg_id', distinct=True),
        paths=Max('num_paths'),
        seed=Max('mc_seed'),
    )
    engines = list(qs.order_by().values_list('pricing_engine', flat=True).distinct())
    scopes = list(qs.order_by().values_list('scope_key', flat=True).distinct())

    pillars = list(
        qs.values('pillar_id', 'grid_step', 'year_fraction', 'exposure_date')
        .annotate(
            ee=Sum('ee'),
            pfe_95=Sum('pfe_95'),
            pfe_975=Sum('pfe_975'),
        )
        .order_by('grid_step', 'pillar_id')
    )
    pillar_ids = [p['pillar_id'] for p in pillars]
    selected = pillar_id if pillar_id in pillar_ids else (pillar_ids[0] if pillar_ids else None)

    by_trade = []
    if selected is not None:
        by_trade = list(
            qs.filter(pillar_id=selected)
            .values('trade_id')
            .annotate(
                ee=Sum('ee'),
                pfe_95=Sum('pfe_95'),
                pfe_975=Sum('pfe_975'),
                legs=Count('leg_id', distinct=True),
            )
            .order_by('-pfe_95', 'trade_id')
        )

    chart = [
        {
            'pillar': p['pillar_id'],
            'step': p['grid_step'],
            't': p['year_fraction'],
            'date': p['exposure_date'].isoformat() if p['exposure_date'] else None,
            'ee': p['ee'],
            'pfe_95': p['pfe_95'],
            'pfe_975': p['pfe_975'],
        }
        for p in pillars
    ]
    return {
        'meta': meta,
        'engines': engines,
        'scopes': scopes,
        'pillars': pillars,
        'pillar_ids': pillar_ids,
        'attribution_pillar': selected,
        'by_trade': by_trade,
        'chart': chart,
    }


def trade_exposure_profile(trade_id: str, as_of: date) -> dict | None:
    qs = TradeLegExposureEod.objects.filter(trade_id=trade_id, as_of=as_of)
    if not qs.exists():
        return None
    pillars = list(
        qs.values('pillar_id', 'grid_step', 'year_fraction', 'exposure_date')
        .annotate(
            ee=Sum('ee'),
            pfe_95=Sum('pfe_95'),
            pfe_975=Sum('pfe_975'),
        )
        .order_by('grid_step', 'pillar_id')
    )
    return {
        'as_of': as_of,
        'pillars': pillars,
        'chart': [
            {
                'pillar': p['pillar_id'],
                'step': p['grid_step'],
                't': p['year_fraction'],
                'ee': p['ee'],
                'pfe_95': p['pfe_95'],
                'pfe_975': p['pfe_975'],
            }
            for p in pillars
        ],
    }
