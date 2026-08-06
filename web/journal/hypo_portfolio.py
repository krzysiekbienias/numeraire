"""Hypo portfolio — Quant Lab sandbox mini-book.

Browser holds run state (`localStorage`). Server only prices: market path t1…t10
× up to 3 toy legs (PVE / PVA / EQS / EQF). Start from t1; extend with Next step.
No trades table, no MTM book.
"""

from __future__ import annotations

import json
import re
from typing import Any

from journal.greeks_lab import GreeksLabParams
from journal.quant_lab import QuantLabQuote, price_sandbox

MAX_INSTRUMENTS = 3
MAX_STEPS = 10
T_LABELS = tuple(f't{i}' for i in range(1, MAX_STEPS + 1))
ALLOWED_CODES = frozenset({'PVE', 'PVA', 'EQS', 'EQF'})
_CRR_DEFAULT_STEPS = 200
_T_RE = re.compile(r'^t(\d+)$')

INSTRUMENT_CHOICES = (
    {'code': 'PVE', 'label': 'PVE — Plain Vanilla European'},
    {'code': 'PVA', 'label': 'PVA — Plain Vanilla American'},
    {'code': 'EQS', 'label': 'EQS — Equity Spot'},
    {'code': 'EQF', 'label': 'EQF — Equity Forward'},
)

# UI cold start: one step, ~6M tenor. Extend with Next step.
_DEFAULT_T1: dict[str, Any] = {
    't': 't1',
    'spot': 100.0,
    'vol': 0.20,
    'rate': 0.04,
    'div': 0.0,
    'tau': 0.5,
}

# Full 10-step toy path for Download sample / Load scenario (τ from 1Y → ~0).
SAMPLE_MARKET: dict[str, Any] = {
    '_comment': (
        'Hypo portfolio market scenario — only market path (1..10 steps). '
        't1 starts at τ = 1 year. You may paste a shorter path. Trade legs stay in the UI.'
    ),
    'steps': [
        {'t': 't1', 'spot': 100.0, 'vol': 0.20, 'rate': 0.04, 'div': 0.0, 'tau': 1.0},
        {'t': 't2', 'spot': 101.5, 'vol': 0.21, 'rate': 0.04, 'div': 0.0, 'tau': 0.9},
        {'t': 't3', 'spot': 99.0, 'vol': 0.22, 'rate': 0.04, 'div': 0.0, 'tau': 0.8},
        {'t': 't4', 'spot': 102.0, 'vol': 0.20, 'rate': 0.04, 'div': 0.0, 'tau': 0.7},
        {'t': 't5', 'spot': 103.5, 'vol': 0.19, 'rate': 0.04, 'div': 0.0, 'tau': 0.6},
        {'t': 't6', 'spot': 102.8, 'vol': 0.195, 'rate': 0.04, 'div': 0.0, 'tau': 0.5},
        {'t': 't7', 'spot': 104.0, 'vol': 0.18, 'rate': 0.04, 'div': 0.0, 'tau': 0.4},
        {'t': 't8', 'spot': 105.2, 'vol': 0.185, 'rate': 0.04, 'div': 0.0, 'tau': 0.3},
        {'t': 't9', 'spot': 104.5, 'vol': 0.19, 'rate': 0.04, 'div': 0.0, 'tau': 0.2},
        {'t': 't10', 'spot': 106.0, 'vol': 0.17, 'rate': 0.04, 'div': 0.0, 'tau': 0.1},
    ],
}


def sample_market_json(*, indent: int = 2) -> str:
    return json.dumps(SAMPLE_MARKET, indent=indent) + '\n'


def default_market_steps() -> list[dict[str, Any]]:
    """UI starts with a single step (τ = 0.5) — extend via Next step."""
    return [dict(_DEFAULT_T1)]


def _t_index(label: str) -> int:
    m = _T_RE.match((label or '').strip().lower())
    if not m:
        raise ValueError(f'invalid step label {label!r} (expected t1..t{MAX_STEPS})')
    idx = int(m.group(1))
    if idx < 1 or idx > MAX_STEPS:
        raise ValueError(f'step label out of range: {label}')
    return idx


def _sign(direction: str) -> float:
    d = (direction or 'long').strip().lower()
    if d in {'short', 'sell', '-'}:
        return -1.0
    return 1.0


def _as_float(raw: Any, label: str) -> float:
    try:
        return float(raw)
    except (TypeError, ValueError) as exc:
        raise ValueError(f'{label} must be a number') from exc


def parse_market_scenario(payload: Any) -> list[dict[str, Any]]:
    """Accept market JSON with 1..10 steps (any subset of t1…t10), sorted by t."""
    if isinstance(payload, str):
        try:
            payload = json.loads(payload)
        except json.JSONDecodeError as exc:
            raise ValueError(f'invalid JSON: {exc}') from exc

    if isinstance(payload, list):
        payload = {'steps': payload}

    if not isinstance(payload, dict):
        raise ValueError('scenario must be a JSON object')

    steps_raw = payload.get('steps')
    if not isinstance(steps_raw, list) or not steps_raw:
        raise ValueError('scenario.steps must be a non-empty array')
    if len(steps_raw) > MAX_STEPS:
        raise ValueError(f'at most {MAX_STEPS} market steps')

    by_t: dict[str, dict[str, Any]] = {}
    for i, row in enumerate(steps_raw):
        if not isinstance(row, dict):
            raise ValueError(f'steps[{i}] must be an object')
        t = str(row.get('t') or '').strip().lower()
        _t_index(t)  # validate
        by_t[t] = {
            't': t,
            'spot': _as_float(row.get('spot'), f'{t}.spot'),
            'vol': _as_float(row.get('vol', 0.0), f'{t}.vol'),
            'rate': _as_float(row.get('rate', 0.0), f'{t}.rate'),
            'div': _as_float(row.get('div', 0.0), f'{t}.div'),
            'tau': _as_float(row.get('tau', 0.0), f'{t}.tau'),
        }

    return sorted(by_t.values(), key=lambda s: _t_index(s['t']))


def _normalize_instrument(raw: Any, index: int) -> dict[str, Any]:
    if not isinstance(raw, dict):
        raise ValueError(f'instruments[{index}] must be an object')
    code = str(raw.get('code') or '').strip().upper()
    if code not in ALLOWED_CODES:
        raise ValueError(
            f'instruments[{index}].code must be one of {", ".join(sorted(ALLOWED_CODES))}'
        )
    direction = str(raw.get('direction') or 'long').strip().lower()
    if direction not in {'long', 'short'}:
        raise ValueError(f'instruments[{index}].direction must be long or short')

    slot = raw.get('slot', index + 1)
    try:
        slot_i = int(slot)
    except (TypeError, ValueError) as exc:
        raise ValueError(f'instruments[{index}].slot must be an integer') from exc
    if slot_i < 1 or slot_i > MAX_INSTRUMENTS:
        raise ValueError(f'instruments[{index}].slot must be 1..{MAX_INSTRUMENTS}')

    qty = _as_float(raw.get('qty', 1.0), f'instruments[{index}].qty')
    if qty <= 0.0:
        raise ValueError(f'instruments[{index}].qty must be positive')

    active_from = str(raw.get('active_from') or 't1').strip().lower()
    _t_index(active_from)

    out: dict[str, Any] = {
        'slot': slot_i,
        'code': code,
        'direction': direction,
        'qty': qty,
        'active_from': active_from,
    }

    if code in {'PVE', 'PVA', 'EQF'}:
        if raw.get('strike') is None:
            raise ValueError(f'instruments[{index}].strike required for {code}')
        out['strike'] = _as_float(raw.get('strike'), f'instruments[{index}].strike')

    if code in {'PVE', 'PVA'}:
        side = str(raw.get('option_side') or raw.get('side') or 'call').strip().lower()
        if side not in {'call', 'put'}:
            raise ValueError(f'instruments[{index}].option_side must be call or put')
        out['option_side'] = side

    if code == 'PVA':
        n_steps = raw.get('n_steps', _CRR_DEFAULT_STEPS)
        try:
            n_steps_i = int(n_steps)
        except (TypeError, ValueError) as exc:
            raise ValueError(f'instruments[{index}].n_steps must be an integer') from exc
        if n_steps_i < 1:
            raise ValueError(f'instruments[{index}].n_steps must be >= 1')
        out['n_steps'] = n_steps_i

    return out


def parse_instruments(raw: Any) -> list[dict[str, Any]]:
    if not isinstance(raw, list) or not raw:
        raise ValueError('instruments must be a non-empty array')
    if len(raw) > MAX_INSTRUMENTS:
        raise ValueError(f'at most {MAX_INSTRUMENTS} instruments')
    instruments = [_normalize_instrument(row, i) for i, row in enumerate(raw)]
    slots = [ins['slot'] for ins in instruments]
    if len(set(slots)) != len(slots):
        raise ValueError('duplicate instrument slot')
    return sorted(instruments, key=lambda x: x['slot'])


def _quote_spot(spot: float) -> QuantLabQuote:
    return QuantLabQuote(
        ok=True,
        engine_label='spot_mtm',
        message='',
        pv_unit=float(spot),
        delta=1.0,
        gamma=0.0,
        vega=0.0,
        theta=0.0,
        theta_day=0.0,
        rho=0.0,
    )


def _price_leg(instrument: dict[str, Any], market: dict[str, Any]) -> QuantLabQuote:
    code = instrument['code']
    spot = float(market['spot'])
    if code == 'EQS':
        return _quote_spot(spot)

    params = GreeksLabParams(
        spot=spot,
        strike=float(instrument['strike']),
        vol=float(market['vol']),
        rate=float(market['rate']),
        div=float(market['div']),
        is_call=(instrument.get('option_side', 'call') == 'call'),
    )
    tau = float(market['tau'])

    if code == 'PVE':
        return price_sandbox(params, tau, lab_product='vanilla', exercise='european')
    if code == 'PVA':
        return price_sandbox(
            params,
            tau,
            lab_product='vanilla',
            exercise='american',
            n_steps=int(instrument.get('n_steps', _CRR_DEFAULT_STEPS)),
        )
    if code == 'EQF':
        return price_sandbox(params, tau, lab_product='forward', exercise='n/a')
    return QuantLabQuote(ok=False, engine_label='', message=f'unsupported code {code}')


def _inactive_outputs() -> dict[str, Any]:
    return {
        'ok': True,
        'message': '',
        'engine': 'inactive',
        'npv': 0.0,
        'delta': 0.0,
        'gamma': 0.0,
        'vega': 0.0,
        'theta': 0.0,
        'theta_day': 0.0,
        'rho': 0.0,
        'active': False,
    }


def _scaled_outputs(quote: QuantLabQuote, direction: str, qty: float) -> dict[str, Any]:
    scale = _sign(direction) * float(qty)
    if not quote.ok or quote.pv_unit is None:
        return {
            'ok': False,
            'message': quote.message or 'pricing failed',
            'engine': quote.engine_label,
            'npv': None,
            'delta': None,
            'gamma': None,
            'vega': None,
            'theta': None,
            'theta_day': None,
            'rho': None,
            'active': True,
        }
    return {
        'ok': True,
        'message': '',
        'engine': quote.engine_label,
        'npv': scale * float(quote.pv_unit),
        'delta': None if quote.delta is None else scale * float(quote.delta),
        'gamma': None if quote.gamma is None else scale * float(quote.gamma),
        'vega': None if quote.vega is None else scale * float(quote.vega),
        'theta': None if quote.theta is None else scale * float(quote.theta),
        'theta_day': None if quote.theta_day is None else scale * float(quote.theta_day),
        'rho': None if quote.rho is None else scale * float(quote.rho),
        'active': True,
    }


def price_hypo_run(
    *,
    run_id: str,
    market_steps: list[dict[str, Any]],
    instruments: list[dict[str, Any]],
) -> dict[str, Any]:
    """Price each active leg on each market step; attach ΔNPV vs previous t."""
    rid = (run_id or '').strip() or 'HYPO_PORTFOLIO_1'
    if not market_steps:
        raise ValueError('market steps required')
    if not instruments:
        raise ValueError('instruments required')

    steps_out: list[dict[str, Any]] = []
    prev_leg_npv: dict[int, float | None] = {ins['slot']: None for ins in instruments}
    prev_port: float | None = None

    for market in market_steps:
        t_idx = _t_index(market['t'])
        legs_out: list[dict[str, Any]] = []
        port_npv = 0.0
        any_fail = False
        for ins in instruments:
            slot = ins['slot']
            active_from_idx = _t_index(ins.get('active_from') or 't1')
            if t_idx < active_from_idx:
                signed = _inactive_outputs()
            else:
                quote = _price_leg(ins, market)
                signed = _scaled_outputs(quote, ins['direction'], float(ins['qty']))

            npv = signed['npv']
            prev = prev_leg_npv[slot]
            d_npv = None if (npv is None or prev is None) else (npv - prev)
            if npv is not None:
                port_npv += npv
                prev_leg_npv[slot] = npv
            else:
                any_fail = True
            legs_out.append(
                {
                    'slot': slot,
                    'code': ins['code'],
                    'direction': ins['direction'],
                    'qty': ins['qty'],
                    'active_from': ins.get('active_from') or 't1',
                    'option_side': ins.get('option_side'),
                    'strike': ins.get('strike'),
                    'd_npv': d_npv,
                    **signed,
                }
            )

        port_d = None if (any_fail or prev_port is None) else (port_npv - prev_port)
        if not any_fail:
            prev_port = port_npv

        steps_out.append(
            {
                't': market['t'],
                'market': {
                    'spot': market['spot'],
                    'vol': market['vol'],
                    'rate': market['rate'],
                    'div': market['div'],
                    'tau': market['tau'],
                },
                'legs': legs_out,
                'portfolio_npv': None if any_fail else port_npv,
                'portfolio_d_npv': port_d,
            }
        )

    return {
        'ok': True,
        'run_id': rid,
        'instruments': instruments,
        'steps': steps_out,
    }


def build_hypo_lab_context() -> dict[str, Any]:
    return {
        'instrument_choices': list(INSTRUMENT_CHOICES),
        'max_instruments': MAX_INSTRUMENTS,
        'max_steps': MAX_STEPS,
        't_labels': list(T_LABELS),
        'default_market': default_market_steps(),
        'sample_market_json': sample_market_json(),
        # Bump when default market shape changes so old browser runs don't stick.
        'storage_key_prefix': 'numeraire.hypo.v2.',
    }
