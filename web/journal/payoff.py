"""Terminal payoff diagrams for trade detail (vanilla + binary).

Educational overlay next to the vol surface — not a pricer. Scales by
direction × quantity × contract_size so the chart matches the book trade.
"""

from __future__ import annotations

import json
import re
from typing import Any

from journal.black_scholes import position_sign

SPOT_RANGE_FRAC = 0.30
N_POINTS = 160

# Normalized keys after stripping non-alnum lowercasing.
_VANILLA = frozenset({'plainvanillaeuropeanoption', 'vanillaeuropeanoption'})
_CASH_OR_NOTHING = frozenset(
    {
        'cashornothingoption',
        'cashornothing',
        'binarycashornothing',
        'digitaloption',
        'digital',
    }
)
_ASSET_OR_NOTHING = frozenset({'assetornothingoption', 'assetornothing'})


def _norm_type(raw: str | None) -> str:
    return re.sub(r'[^a-z0-9]', '', (raw or '').strip().lower())


def _kind(instrument_type: str | None) -> str | None:
    key = _norm_type(instrument_type)
    if key in _VANILLA:
        return 'vanilla'
    if key in _CASH_OR_NOTHING:
        return 'cash_or_nothing'
    if key in _ASSET_OR_NOTHING:
        return 'asset_or_nothing'
    return None


def _cash_payout(structured_params: str | dict | None) -> float:
    if structured_params is None or structured_params == '':
        return 1.0
    if isinstance(structured_params, dict):
        data = structured_params
    else:
        try:
            data = json.loads(structured_params)
        except (TypeError, json.JSONDecodeError):
            return 1.0
    if not isinstance(data, dict):
        return 1.0
    try:
        v = float(data.get('cash_payout_per_share', 1.0))
    except (TypeError, ValueError):
        return 1.0
    return v if v > 0 else 1.0


def unit_payoff(
    *,
    kind: str,
    is_call: bool,
    strike: float,
    spot: float,
    cash_payout: float = 1.0,
) -> float:
    """Per-share terminal payoff at expiry spot."""
    s, k = float(spot), float(strike)
    if kind == 'vanilla':
        return max(s - k, 0.0) if is_call else max(k - s, 0.0)
    if kind == 'cash_or_nothing':
        itm = (s > k) if is_call else (s < k)
        return float(cash_payout) if itm else 0.0
    if kind == 'asset_or_nothing':
        itm = (s > k) if is_call else (s < k)
        return s if itm else 0.0
    raise ValueError(f'unsupported payoff kind {kind!r}')


def _spot_grid(center: float, n: int = N_POINTS) -> list[float]:
    lo = center * (1.0 - SPOT_RANGE_FRAC)
    hi = center * (1.0 + SPOT_RANGE_FRAC)
    if lo <= 0:
        lo = max(center * 0.05, 1e-6)
    n = max(40, n)
    step = (hi - lo) / n
    xs = [lo + i * step for i in range(n + 1)]
    # Ensure strike neighbourhood is sampled for binary jumps.
    return xs


def build_leg_payoff(
    *,
    instrument_type: str | None,
    option_type: str | None,
    strike: float | None,
    structured_params: str | dict | None,
    direction: str,
    quantity: float,
    contract_size: float,
    ref_spot: float | None,
) -> dict[str, Any] | None:
    kind = _kind(instrument_type)
    if kind is None or strike is None or strike <= 0:
        return None
    side = (option_type or '').strip().lower()
    if side not in ('call', 'put'):
        return None

    is_call = side == 'call'
    cash = _cash_payout(structured_params)
    scale = position_sign(direction) * float(quantity) * float(contract_size)
    center = float(strike)
    spots = _spot_grid(center)
    # Densify around K for step payoffs.
    if kind in ('cash_or_nothing', 'asset_or_nothing'):
        eps = max(center * 1e-4, 1e-6)
        spots = sorted(set(spots + [center - eps, center, center + eps]))

    ys = [
        scale
        * unit_payoff(
            kind=kind,
            is_call=is_call,
            strike=float(strike),
            spot=s,
            cash_payout=cash,
        )
        for s in spots
    ]

    labels = {
        'vanilla': 'vanilla',
        'cash_or_nothing': 'cash-or-nothing',
        'asset_or_nothing': 'asset-or-nothing',
    }
    return {
        'kind': kind,
        'label': labels[kind],
        'side': side,
        'strike': float(strike),
        'ref_spot': float(ref_spot) if ref_spot is not None else None,
        'cash_payout': cash if kind == 'cash_or_nothing' else None,
        'scale': scale,
        'x': [round(s, 6) for s in spots],
        'y': ys,
        'x_min': spots[0],
        'x_max': spots[-1],
    }


def build_trade_payoff_chart(market_rows: list[dict], *, ref_spot: float | None) -> dict | None:
    """Sum supported legs; return chart payload or None."""
    series = []
    for row in market_rows:
        leg = row.get('leg')
        equity = row.get('equity')
        if leg is None or equity is None:
            continue
        product = leg.product
        built = build_leg_payoff(
            instrument_type=equity.instrument_type,
            option_type=equity.option_type,
            strike=equity.strike,
            structured_params=equity.structured_params,
            direction=leg.direction,
            quantity=leg.quantity,
            contract_size=float(product.contract_size),
            ref_spot=ref_spot,
        )
        if built is None:
            continue
        built['leg_id'] = leg.leg_id
        series.append(built)

    if not series:
        return None

    # Shared x-grid from first leg (same underlier/strike family typical); if
    # multi-strike, resample each onto a union grid via nearest — for v1 use
    # first leg's x and only sum when single supported series, else overlay.
    if len(series) == 1:
        s0 = series[0]
        return {
            'mode': 'single',
            'label': s0['label'],
            'side': s0['side'],
            'strike': s0['strike'],
            'ref_spot': ref_spot if ref_spot is not None else s0['ref_spot'],
            'x': s0['x'],
            'y': s0['y'],
            'x_min': s0['x_min'],
            'x_max': s0['x_max'],
            'legs': series,
        }

    # Overlay each leg (do not sum different strikes onto one curve).
    return {
        'mode': 'overlay',
        'label': 'legs',
        'side': None,
        'strike': series[0]['strike'],
        'ref_spot': ref_spot,
        'x_min': min(s['x_min'] for s in series),
        'x_max': max(s['x_max'] for s in series),
        'legs': series,
        'x': series[0]['x'],
        'y': series[0]['y'],
    }
