"""Educational greeks-vs-spot lab (sandbox, Python BS). Standalone — not trade-bound."""

from __future__ import annotations

from dataclasses import dataclass

from journal.black_scholes import black_scholes

# Calendar-day tenors (Act/365).
MATURITIES = (
    ('5D', 5),
    ('1M', 30),
    ('3M', 91),
    ('6M', 182),
)

DAYS_PER_YEAR = 365.0
# Spot grid: ±30% around strike (classic moneyness window).
SPOT_RANGE_FRAC = 0.30

# Chart panels: row1 Δ|Γ, row2 ν|Θ/day — same spot bump, four τ.
PANELS = (
    {'key': 'delta', 'title': 'Delta', 'y_name': 'Δ'},
    {'key': 'gamma', 'title': 'Gamma', 'y_name': 'Γ'},
    {'key': 'vega', 'title': 'Vega', 'y_name': 'ν'},
    {'key': 'theta', 'title': 'Theta / day', 'y_name': 'Θ/d'},
)


@dataclass(frozen=True)
class GreeksLabParams:
    spot: float  # reference spot (mark on chart)
    strike: float
    vol: float
    rate: float
    div: float
    is_call: bool
    n_points: int = 160


def _parse_float(raw: str | None, default: float) -> float:
    if raw is None or str(raw).strip() == '':
        return default
    return float(raw)


def params_from_get(get, *, defaults: dict | None = None) -> GreeksLabParams:
    d = defaults or {}
    spot = _parse_float(get.get('spot'), float(d.get('spot', 100.0)))
    strike = _parse_float(get.get('strike'), float(d.get('strike', 100.0)))
    vol = _parse_float(get.get('vol'), float(d.get('vol', 0.20)))
    rate = _parse_float(get.get('rate'), float(d.get('rate', 0.04)))
    div = _parse_float(get.get('div'), float(d.get('div', 0.0)))
    side = (get.get('side') or d.get('side') or 'call').strip().lower()
    is_call = side not in ('put', 'p')
    if spot <= 0:
        spot = 100.0
    if strike <= 0:
        strike = 100.0
    if vol < 0:
        vol = 0.0
    return GreeksLabParams(
        spot=spot, strike=strike, vol=vol, rate=rate, div=div, is_call=is_call
    )


def _spot_grid(strike: float, n: int) -> list[float]:
    lo = strike * (1.0 - SPOT_RANGE_FRAC)
    hi = strike * (1.0 + SPOT_RANGE_FRAC)
    if lo <= 0:
        lo = strike * 0.05
    n = max(40, n)
    step = (hi - lo) / n
    return [round(lo + i * step, 6) for i in range(n + 1)]


def _series_vs_spot(params: GreeksLabParams, tenor_days: int, spots: list[float]) -> dict:
    """Analytic BS greeks along a spot grid at fixed τ."""
    tau = tenor_days / DAYS_PER_YEAR
    xs: list[float] = []
    delta: list[float] = []
    gamma: list[float] = []
    vega: list[float] = []
    theta: list[float] = []  # per calendar day
    for s in spots:
        try:
            g = black_scholes(
                s,
                params.strike,
                tau,
                params.vol,
                params.rate,
                params.div,
                is_call=params.is_call,
            )
        except (ValueError, OverflowError):
            continue
        xs.append(s)
        delta.append(g.delta)
        gamma.append(g.gamma)
        vega.append(g.vega)
        theta.append(g.theta / DAYS_PER_YEAR)
    return {
        'tenor_days': tenor_days,
        'x': xs,
        'delta': delta,
        'gamma': gamma,
        'vega': vega,
        'theta': theta,
    }


def build_greeks_vs_spot(params: GreeksLabParams) -> dict:
    spots = _spot_grid(params.strike, params.n_points)
    series = []
    for label, days in MATURITIES:
        s = _series_vs_spot(params, days, spots)
        s['label'] = label
        series.append(s)

    table = []
    for s in series:
        tau = s['tenor_days'] / DAYS_PER_YEAR
        try:
            g = black_scholes(
                params.spot,
                params.strike,
                tau,
                params.vol,
                params.rate,
                params.div,
                is_call=params.is_call,
            )
        except (ValueError, OverflowError):
            continue
        table.append(
            {
                'dte': s['tenor_days'],
                'label': s['label'],
                'delta': g.delta,
                'gamma': g.gamma,
                'vega': g.vega,
                'theta': g.theta / DAYS_PER_YEAR,
            }
        )

    maturities = [
        {
            'label': s['label'],
            'tenor_days': s['tenor_days'],
            'x': s['x'],
            'delta': s['delta'],
            'gamma': s['gamma'],
            'vega': s['vega'],
            'theta': s['theta'],
        }
        for s in series
    ]

    return {
        'params': params,
        'series': series,
        'table': table,
        'panels': PANELS,
        'chart': {
            'is_call': params.is_call,
            'ref_spot': params.spot,
            'strike': params.strike,
            'x_min': spots[0] if spots else 0.0,
            'x_max': spots[-1] if spots else 0.0,
            'panels': list(PANELS),
            'maturities': maturities,
        },
    }


def build_greeks_vs_time(params: GreeksLabParams) -> dict:
    """View alias (name kept for imports)."""
    return build_greeks_vs_spot(params)
