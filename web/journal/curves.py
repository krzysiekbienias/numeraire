"""Discount-curve helpers aligned with ``numeraire::quant::DiscountFactorAtTime``."""

from __future__ import annotations

import math
from datetime import date

from journal.models import (
    DiscountCurveEod,
    DiscountCurvePointEod,
    ParCurveEod,
    ParCurvePointEod,
)

_TIME_TOL = 1e-12


def discount_factor_from_zero(zero_rate: float, time_years: float) -> float:
    if time_years <= 0.0:
        return 1.0
    return math.exp(-zero_rate * time_years)


def interpolate_zero_rate(pillars: list[tuple[float, float]], time_years: float) -> float | None:
    """Linear interpolation in zero rate vs time (same as C++ ``InterpolateZeroRateAtTime``)."""
    if not pillars or not math.isfinite(time_years):
        return None
    curve = sorted(pillars, key=lambda p: p[0])
    if time_years <= curve[0][0] + _TIME_TOL:
        return curve[0][1]
    if time_years >= curve[-1][0] - _TIME_TOL:
        return curve[-1][1]
    for left, right in zip(curve, curve[1:]):
        t0, z0 = left
        t1, z1 = right
        if time_years <= t1 + _TIME_TOL:
            if abs(t1 - t0) <= _TIME_TOL:
                return z1
            w = (time_years - t0) / (t1 - t0)
            return z0 + w * (z1 - z0)
    return curve[-1][1]


def discount_factor_at_time(pillars: list[tuple[float, float]], time_years: float) -> float | None:
    if time_years < 0.0 or not math.isfinite(time_years):
        return None
    zero = interpolate_zero_rate(pillars, time_years)
    if zero is None:
        return None
    return discount_factor_from_zero(zero, time_years)


def load_curve_pillars(curve_id: str, as_of: date) -> list[tuple[float, float]]:
    return list(
        DiscountCurvePointEod.objects.filter(curve_id=curve_id, as_of=as_of)
        .order_by('time_years')
        .values_list('time_years', 'zero_rate')
    )


def nearest_curve_as_of(as_of: date, curve_id: str | None = None) -> tuple[str, date] | None:
    """Pick the newest discount curve with ``curve.as_of <= as_of``."""
    qs = DiscountCurveEod.objects.filter(as_of__lte=as_of).order_by('-as_of', 'curve_id')
    if curve_id:
        qs = qs.filter(curve_id=curve_id)
    row = qs.values_list('curve_id', 'as_of').first()
    if row is None:
        return None
    return row[0], row[1]


def curve_discount_for_maturity(as_of: date, time_years: float) -> dict | None:
    """Interpolate DF(τ) on the nearest available curve on or before ``as_of``."""
    picked = nearest_curve_as_of(as_of)
    if picked is None:
        return None
    curve_id, curve_as_of = picked
    pillars = load_curve_pillars(curve_id, curve_as_of)
    df = discount_factor_at_time(pillars, time_years)
    if df is None:
        return None
    zero = interpolate_zero_rate(pillars, time_years)
    return {
        'curve_id': curve_id,
        'curve_as_of': curve_as_of,
        'zero_rate': zero,
        'discount_factor': df,
        'stale': curve_as_of != as_of,
    }


def list_curve_ids() -> list[str]:
    return list(
        DiscountCurveEod.objects.order_by()
        .values_list('curve_id', flat=True)
        .distinct()
        .order_by('curve_id')
    )


def list_curve_as_of(curve_id: str) -> list[date]:
    return list(
        DiscountCurveEod.objects.filter(curve_id=curve_id)
        .order_by()
        .values_list('as_of', flat=True)
        .distinct()
        .order_by('-as_of')
    )


def load_curve_snapshot(curve_id: str, as_of: date) -> dict | None:
    """Header + pillars + source par instruments for one discount curve day."""
    header = DiscountCurveEod.objects.filter(curve_id=curve_id, as_of=as_of).first()
    if header is None:
        return None

    pillars = list(
        DiscountCurvePointEod.objects.filter(curve_id=curve_id, as_of=as_of)
        .order_by('time_years')
        .values('tenor', 'time_years', 'zero_rate', 'discount_factor')
    )
    par_header = ParCurveEod.objects.filter(
        curve_id=header.source_par_curve_id,
        as_of=header.source_par_as_of,
    ).first()
    instruments = list(
        ParCurvePointEod.objects.filter(
            curve_id=header.source_par_curve_id,
            as_of=header.source_par_as_of,
        )
        .order_by('tenor_days', 'tenor')
        .values(
            'tenor',
            'tenor_days',
            'instrument_type',
            'fred_series_id',
            'quoted_rate',
            'quote_style',
        )
    )
    for row in instruments:
        rate = row.get('quoted_rate')
        row['quoted_pct'] = (float(rate) * 100.0) if rate is not None else None
    chart = [
        {
            'tenor': p['tenor'],
            't': p['time_years'],
            'zero': p['zero_rate'],
            'df': p['discount_factor'],
        }
        for p in pillars
    ]
    return {
        'header': header,
        'pillars': pillars,
        'par_header': par_header,
        'instruments': instruments,
        'chart': chart,
    }
