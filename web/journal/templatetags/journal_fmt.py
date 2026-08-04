"""Journal display helpers — selective number formatting (not for τ / IV / DF)."""

from __future__ import annotations

from django import template
from django.conf import settings

register = template.Library()


@register.simple_tag
def app_version():
    """Product version from settings (works even if context processors lag a reload)."""
    return getattr(settings, 'APP_VERSION', '0.4.2')

# Model theta is ∂V/∂t per calendar year (QuantLib / architecture.md).
# Desk UI shows calendar-day decay for comparison with PV / daily PnL.
THETA_DAYS_PER_YEAR = 365.0


@register.filter(name='nj_num')
def nj_num(value, decimals=2):
    """Format money / greek / exposure totals with thousands separators.

    Keep using ``floatformat`` for unit-scale market inputs (IV, r, q, τ, DF).
    """
    if value is None:
        return '—'
    try:
        number = float(value)
    except (TypeError, ValueError):
        return value
    try:
        places = int(decimals)
    except (TypeError, ValueError):
        places = 2
    return f'{number:,.{places}f}'


@register.filter(name='nj_theta_day')
def nj_theta_day(value, decimals=2):
    """Position theta as calendar-day decay: ``theta_total / 365``.

    Stored MTM theta is per year; do not compare the raw column to PV.
    """
    if value is None:
        return '—'
    try:
        number = float(value) / THETA_DAYS_PER_YEAR
    except (TypeError, ValueError):
        return value
    return nj_num(number, decimals)


@register.filter(name='nj_pct')
def nj_pct(value, decimals=3):
    """Format a fraction as a signed percent, e.g. 0.0012 → ``0.120%``."""
    if value is None:
        return '—'
    try:
        number = float(value) * 100.0
    except (TypeError, ValueError):
        return value
    try:
        places = int(decimals)
    except (TypeError, ValueError):
        places = 3
    return f'{number:+,.{places}f}%'
