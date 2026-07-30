"""Vol-surface helpers for Journal 3D / smile views."""

from __future__ import annotations

import math
from datetime import date

from journal.models import VolSurfaceEod, VolSurfacePointEod


def list_surface_underlyings(surface_kind: str = 'implied_bs_eod') -> list[str]:
    return list(
        VolSurfaceEod.objects.filter(surface_kind=surface_kind)
        .order_by()
        .values_list('underlying_id', flat=True)
        .distinct()
        .order_by('underlying_id')
    )


def list_surface_as_of(underlying_id: str, surface_kind: str = 'implied_bs_eod') -> list[date]:
    return list(
        VolSurfaceEod.objects.filter(
            underlying_id=underlying_id,
            surface_kind=surface_kind,
        )
        .order_by()
        .values_list('as_of', flat=True)
        .distinct()
        .order_by('-as_of')
    )


def nearest_surface_as_of(
    underlying_id: str,
    as_of: date,
    surface_kind: str = 'implied_bs_eod',
) -> date | None:
    return (
        VolSurfaceEod.objects.filter(
            underlying_id=underlying_id,
            surface_kind=surface_kind,
            as_of__lte=as_of,
        )
        .order_by('-as_of')
        .values_list('as_of', flat=True)
        .first()
    )


def load_surface_snapshot(
    underlying_id: str,
    as_of: date,
    *,
    surface_kind: str = 'implied_bs_eod',
    contract_type: str | None = 'call',
) -> dict | None:
    header = VolSurfaceEod.objects.filter(
        underlying_id=underlying_id,
        as_of=as_of,
        surface_kind=surface_kind,
    ).first()
    if header is None:
        return None

    qs = VolSurfacePointEod.objects.filter(surface=header).order_by(
        'years_to_maturity', 'strike'
    )
    if contract_type:
        qs = qs.filter(contract_type=contract_type)

    points = []
    spot = header.spot_used
    for row in qs:
        log_m = None
        if spot and spot > 0 and row.strike > 0:
            log_m = math.log(row.strike / spot)
        points.append(
            {
                'strike': row.strike,
                't': row.years_to_maturity,
                'iv': row.implied_vol,
                'log_m': log_m,
                'expiry': row.expiration_date.isoformat() if row.expiration_date else None,
                'type': row.contract_type,
                'quality': row.quality,
            }
        )

    chart = [
        {
            'x': p['log_m'] if p['log_m'] is not None else p['strike'],
            'y': p['t'],
            'z': p['iv'],
            'strike': p['strike'],
            'expiry': p['expiry'],
            'type': p['type'],
        }
        for p in points
        if p['log_m'] is not None
    ]

    return {
        'header': header,
        'points': points,
        'chart': chart,
        'contract_type': contract_type or 'all',
        'x_label': 'ln(K/S)',
    }
