"""Quant Lab — instrument-type sandbox.

Not persisted. Dropdown = catalog codes (PVE, AON, BIN, EQF, …); inputs appear only
after a pick. Point price via C++ (`numeraire_cpp`); greeks charts still Python BS
shapes for vanillas only.
"""

from __future__ import annotations

import math
import re
from dataclasses import dataclass

from journal.black_scholes import BsResult, black_scholes
from journal.greeks_lab import GreeksLabParams, build_greeks_vs_spot, params_from_get
from journal.models import CatalogInstrumentType
from journal.payoff import unit_payoff

DAYS_PER_YEAR = 365.0

# Payoff vs PV chart (European vanilla): spot grid ±30% around K.
_VALUE_CHART_RANGE = 0.30
_VALUE_CHART_POINTS = 81

# Full legend; UI filters to symbols needed by the selected instrument (+ greeks when priced).
SYMBOL_LEGEND = (
    ('S', 'Spot', 'Underlying price used for pricing / chart mark'),
    ('F', 'Settle', 'Listed futures settlement / mark price (pv_unit = F)'),
    ('K', 'Strike', 'Option strike (or forward delivery price)'),
    ('σ', 'IV', 'Black–Scholes implied volatility (absolute, e.g. 0.25 = 25%)'),
    ('r', 'Rate', 'Continuous risk-free rate'),
    ('q', 'Dividend', 'Continuous dividend / borrow yield'),
    ('τ', 'Tenor', 'Time to expiry in years (Act/365 Fixed)'),
    ('Δ', 'Delta', '∂V/∂S'),
    ('Γ', 'Gamma', '∂²V/∂S²'),
    ('ν', 'Vega', '∂V/∂σ (per 1.0 vol point)'),
    ('Θ', 'Theta', '∂V/∂t per year; charts show Θ/day = Θ/365'),
    ('ρ', 'Rho', '∂V/∂r'),
    ('Q', 'Cash payout', 'Cash-or-nothing fixed cash amount paid if in the money'),
    ('N', 'CRR steps', 'Binomial tree steps (American PVA); small N draws the tree'),
)

# Form field specs: which inputs exist for a given instrument kind.
_FIELD_SPOT = {
    'name': 'spot',
    'sym': 'S',
    'label': 'Spot',
    'kind': 'number',
    'step': '5',
}
_FIELD_STRIKE = {
    'name': 'strike',
    'sym': 'K',
    'label': 'Strike',
    'kind': 'number',
    'step': '5',
}
_FIELD_FORWARD = {
    'name': 'strike',
    'sym': 'K',
    'label': 'Forward',
    'kind': 'number',
    'step': '5',
}
_FIELD_VOL = {
    'name': 'vol',
    'sym': 'σ',
    'label': 'IV',
    'kind': 'number',
    # HTML step=0.1 rejects 0.25 / 0.19 (browser constraint validation).
    'step': 'any',
}
_FIELD_RATE = {
    'name': 'rate',
    'sym': 'r',
    'label': 'Rate',
    'kind': 'number',
    'step': 'any',
}
_FIELD_DIV = {
    'name': 'div',
    'sym': 'q',
    'label': 'Dividend',
    'kind': 'number',
    'step': 'any',
}
_FIELD_TAU = {
    'name': 'tau',
    'sym': 'τ',
    'label': 'Tenor (y)',
    'kind': 'number',
    # Free-form years (1Y, 6M=0.5, …). Fixed step rejects values like 1 after 30/365 defaults.
    'step': 'any',
}
_FIELD_SIDE = {
    'name': 'side',
    'sym': '',
    'label': 'Side',
    'kind': 'side',
}
_FIELD_CASH = {
    'name': 'cash_payout',
    'sym': 'Q',
    'label': 'Cash Q',
    'kind': 'number',
    'step': '1',
}
_FIELD_STEPS = {
    'name': 'n_steps',
    'sym': 'N',
    'label': 'CRR steps',
    'kind': 'number',
    'step': '1',
}
_FIELD_MC_PATHS = {
    'name': 'mc_paths',
    'sym': '',
    'label': 'MC paths',
    'kind': 'mc_paths',
}

_OPTION_FIELDS = (
    _FIELD_SPOT,
    _FIELD_STRIKE,
    _FIELD_VOL,
    _FIELD_RATE,
    _FIELD_DIV,
    _FIELD_TAU,
    _FIELD_SIDE,
)
_EUROPEAN_VANILLA_FIELDS = (
    _FIELD_SPOT,
    _FIELD_STRIKE,
    _FIELD_VOL,
    _FIELD_RATE,
    _FIELD_DIV,
    _FIELD_TAU,
    _FIELD_STEPS,
    _FIELD_MC_PATHS,
    _FIELD_SIDE,
)
_AMERICAN_VANILLA_FIELDS = (
    _FIELD_SPOT,
    _FIELD_STRIKE,
    _FIELD_VOL,
    _FIELD_RATE,
    _FIELD_DIV,
    _FIELD_TAU,
    _FIELD_STEPS,
    _FIELD_SIDE,
)

# Draw full CRR tree in the lab only for small N (O(n²) nodes).
# Pricing CRR uses _CRR_DEFAULT_STEPS (batch); tree dump is a separate 2..10 control.
_CRR_TREE_MAX_STEPS = 8
_CRR_DEFAULT_STEPS = 200
_CRR_LAB_TREE_DEFAULT = 3
_MC_PATH_CHOICES = (10_000, 50_000, 100_000)
_MC_DEFAULT_PATHS = 10_000
_MC_DEFAULT_SEED = 42
_CRR_TREE_DRAW_MIN = 2
_CRR_TREE_DRAW_MAX = 10
_CRR_TREE_DRAW_DEFAULT = 3
_CON_FIELDS = (
    _FIELD_SPOT,
    _FIELD_STRIKE,
    _FIELD_VOL,
    _FIELD_RATE,
    _FIELD_DIV,
    _FIELD_TAU,
    _FIELD_CASH,
    _FIELD_SIDE,
)
_FORWARD_FIELDS = (
    _FIELD_SPOT,
    _FIELD_FORWARD,
    _FIELD_RATE,
    _FIELD_DIV,
    _FIELD_TAU,
)

_FIELD_FUTURES = {
    'name': 'spot',
    'sym': 'F',
    'label': 'Settle',
    'kind': 'number',
    'step': '0.01',
}
_FUTURES_FIELDS = (_FIELD_FUTURES,)

_GREEK_SYMS = frozenset({'Δ', 'Γ', 'ν', 'Θ', 'ρ'})

# Quant Lab asset-class filters (catalog codes).
_EQUITY_CODES = frozenset({'PVE', 'PVA', 'AON', 'BIN', 'DIG', 'ASN', 'BRR', 'EQF'})
_COMMODITY_CODES = frozenset({'EFT', 'FUT'})
_EQUITY_MAPS = frozenset({
    'plain_vanilla_european_option',
    'plain_vanilla_american_option',
    'asset_or_nothing',
    'binary_cash_or_nothing',
    'digital',
    'cash_or_nothing',
    'asian_option',
    'barrier_option',
    'equity_forward',
})
_COMMODITY_MAPS = frozenset({
    'listed_future',
    'commodity_futures_outright',
    'futures_outright',
})


@dataclass(frozen=True)
class InventoryChoice:
    code: str
    label: str
    maps_to: str
    family: str
    is_vanilla: bool
    exercise: str  # european | american | n/a
    param_kind: str  # option | forward | futures | unsupported


@dataclass(frozen=True)
class QuantLabQuote:
    """Point price from C++ (or Python BS fallback for EU vanilla)."""

    ok: bool
    engine_label: str
    message: str
    pv_unit: float | None = None
    delta: float | None = None
    gamma: float | None = None
    vega: float | None = None
    theta: float | None = None  # per year
    theta_day: float | None = None
    rho: float | None = None
    n_steps: int | None = None
    mc_paths: int | None = None
    mc_seed: int | None = None
    mc_std_err: float | None = None
    diagnostics: str | None = None


def _title_from_maps_to(maps_to: str) -> str:
    """plain_vanilla_european_option → Plain Vanilla European Option."""
    return (maps_to or '').replace('_', ' ').strip().title()


def _is_vanilla(maps_to: str | None) -> bool:
    key = (maps_to or '').strip().lower()
    return key in {
        'plain_vanilla_european_option',
        'plain_vanilla_american_option',
    }


def _lab_product(code: str, maps_to: str, param_kind: str) -> str:
    """Sandbox product wire: vanilla | aon | con | forward | futures_outright | unsupported."""
    code_u = (code or '').strip().upper()
    key = (maps_to or '').strip().lower()
    if _is_vanilla(key) or code_u in {'PVE', 'PVA'}:
        return 'vanilla'
    if key in {'asset_or_nothing'} or code_u == 'AON':
        return 'aon'
    if key in {'binary_cash_or_nothing', 'digital', 'cash_or_nothing'} or code_u in {
        'BIN',
        'DIG',
    }:
        return 'con'
    if key in {'equity_forward', 'forward'} or code_u == 'EQF' or (
        param_kind == 'forward' and 'equity' in key
    ):
        return 'forward'
    if (
        key in {'listed_future', 'commodity_futures_outright', 'futures_outright'}
        or code_u in {'EFT', 'FUT'}
        or param_kind == 'futures'
    ):
        return 'futures_outright'
    return 'unsupported'


def _exercise_for(maps_to: str) -> str:
    key = (maps_to or '').strip().lower()
    if 'american' in key:
        return 'american'
    if 'forward' in key or key in {
        'fra',
        'listed_future',
        'commodity_futures_outright',
        'futures_outright',
    }:
        return 'n/a'
    return 'european'


def _param_kind(code: str, family: str, maps_to: str) -> str:
    """Which input set to show for this catalog row."""
    fam = (family or '').strip().upper()
    key = (maps_to or '').strip().lower()
    code_u = (code or '').strip().upper()
    if fam == 'FUTURE' or key in {
        'listed_future',
        'commodity_futures_outright',
        'futures_outright',
    } or code_u in {'EFT', 'FUT'}:
        return 'futures'
    if fam == 'FORWARD' or 'forward' in key or code_u in {'EQF', 'FXF', 'IRF'}:
        return 'forward'
    if fam == 'OPTION' or _is_vanilla(key) or code_u in {
        'PVE',
        'PVA',
        'AON',
        'BIN',
        'DIG',
        'ASN',
        'BRR',
    }:
        return 'option'
    return 'unsupported'


def fields_for_kind(
    param_kind: str,
    *,
    lab_product: str | None = None,
    exercise: str | None = None,
) -> list[dict]:
    product = (lab_product or '').strip().lower()
    ex = (exercise or '').strip().lower()
    if product == 'futures_outright' or param_kind == 'futures':
        return [dict(f) for f in _FUTURES_FIELDS]
    if product == 'con':
        return [dict(f) for f in _CON_FIELDS]
    if product == 'forward' or param_kind == 'forward':
        return [dict(f) for f in _FORWARD_FIELDS]
    if product == 'vanilla' and ex == 'american':
        return [dict(f) for f in _AMERICAN_VANILLA_FIELDS]
    if product == 'vanilla' and ex == 'european':
        return [dict(f) for f in _EUROPEAN_VANILLA_FIELDS]
    if product in {'vanilla', 'aon'} or param_kind == 'option':
        return [dict(f) for f in _OPTION_FIELDS]
    return []


def legend_for_fields(fields: list[dict], *, include_greeks: bool) -> list[tuple[str, str, str]]:
    syms = {f['sym'] for f in fields if f.get('sym')}
    if include_greeks:
        syms |= _GREEK_SYMS
    return [row for row in SYMBOL_LEGEND if row[0] in syms]


def _choice_asset_class(choice: InventoryChoice) -> str:
    code = (choice.code or '').strip().upper()
    maps = (choice.maps_to or '').strip().lower()
    if code in _COMMODITY_CODES or maps in _COMMODITY_MAPS or choice.family.upper() == 'FUTURE':
        return 'commodity'
    if code in _EQUITY_CODES or maps in _EQUITY_MAPS:
        return 'equity'
    return 'other'


def list_inventory_choices(*, asset_class: str | None = None) -> list[InventoryChoice]:
    try:
        rows = list(
            CatalogInstrumentType.objects.filter(is_active=True).order_by('sort_order', 'code')
        )
    except Exception:
        rows = []
    out: list[InventoryChoice] = []
    for cat in rows:
        maps = (cat.maps_to_instrument_type or '').strip()
        code = (cat.code or '').strip().upper()
        if not code:
            continue
        family = (cat.family or '').strip()
        title = _title_from_maps_to(maps) or (cat.description_en or code)
        out.append(
            InventoryChoice(
                code=code,
                label=f'{code} — {title}',
                maps_to=maps,
                family=family,
                is_vanilla=_is_vanilla(maps),
                exercise=_exercise_for(maps),
                param_kind=_param_kind(code, family, maps),
            )
        )

    if not any(c.code == 'EFT' for c in out):
        out.append(
            InventoryChoice(
                code='EFT',
                label='EFT — Listed Future Outright',
                maps_to='commodity_futures_outright',
                family='FUTURE',
                is_vanilla=False,
                exercise='n/a',
                param_kind='futures',
            )
        )

    wanted = (asset_class or '').strip().lower()
    if wanted in {'equity', 'commodity'}:
        out = [c for c in out if _choice_asset_class(c) == wanted]
    return out


def defaults_from_instrument(code: str) -> dict:
    """Neutral theoretical defaults for a catalog CODE."""
    code_u = (code or '').strip().upper()
    if not code_u:
        return {}
    try:
        cat = CatalogInstrumentType.objects.get(pk=code_u)
    except CatalogInstrumentType.DoesNotExist:
        try:
            cat = CatalogInstrumentType.objects.get(code__iexact=code_u)
        except Exception:
            if code_u in {'EFT', 'FUT'}:
                return _synthetic_futures_defaults(code_u)
            return {}
    except Exception:
        if code_u in {'EFT', 'FUT'}:
            return _synthetic_futures_defaults(code_u)
        return {}

    maps = (cat.maps_to_instrument_type or '').strip()
    family = (cat.family or '').strip()
    exercise = _exercise_for(maps)
    kind = _param_kind(code_u, family, maps)
    product = _lab_product(code_u, maps, kind)
    defaults = {
        'instrument': code_u,
        'spot': 100.0,
        'strike': 100.0,
        'vol': 0.20,
        'rate': 0.04,
        'div': 0.0,
        'side': 'call',
        'cash_payout': 1.0,
        'n_steps': _CRR_LAB_TREE_DEFAULT if exercise == 'american' else _CRR_DEFAULT_STEPS,
        'tau': 1.0,
        'exercise': exercise if exercise != 'n/a' else 'european',
        'instrument_code': code_u,
        'instrument_title': _title_from_maps_to(maps),
        'maps_to': maps,
        'is_vanilla': _is_vanilla(maps),
        'param_kind': kind,
        'lab_product': product,
    }
    if product == 'futures_outright' or kind == 'futures':
        defaults['spot'] = 80.0
        defaults['strike'] = 80.0
        defaults['tau'] = 0.0
        defaults['exercise'] = 'n/a'
        defaults['instrument_title'] = defaults['instrument_title'] or 'Listed Future Outright'
    return defaults


def _synthetic_futures_defaults(code_u: str) -> dict:
    return {
        'instrument': code_u,
        'spot': 80.0,
        'strike': 80.0,
        'vol': 0.0,
        'rate': 0.0,
        'div': 0.0,
        'side': 'call',
        'cash_payout': 1.0,
        'n_steps': _CRR_DEFAULT_STEPS,
        'tau': 0.0,
        'exercise': 'n/a',
        'instrument_code': code_u,
        'instrument_title': 'Listed Future Outright',
        'maps_to': 'commodity_futures_outright',
        'is_vanilla': False,
        'param_kind': 'futures',
        'lab_product': 'futures_outright',
    }
def _parse_tau(get, default: float) -> float:
    raw = get.get('tau')
    if raw is None or str(raw).strip() == '':
        return default
    try:
        v = float(raw)
    except (TypeError, ValueError):
        return default
    return max(v, 0.0)


def tau_month_label(tau: float) -> str | None:
    """If τ is (near) an integer number of months, return e.g. '12M'; else None."""
    try:
        t = float(tau)
    except (TypeError, ValueError):
        return None
    if t < 0.0:
        return None
    months = t * 12.0
    nearest = int(round(months))
    if nearest < 1:
        return None
    if abs(months - nearest) <= 1e-4:
        return f'{nearest}M'
    return None


def _parse_cash_payout(get, default: float = 1.0) -> float:
    raw = get.get('cash_payout')
    if raw is None or str(raw).strip() == '':
        return default
    try:
        v = float(raw)
    except (TypeError, ValueError):
        return default
    return v if v > 0.0 else default


def _parse_n_steps(get, default: int = _CRR_DEFAULT_STEPS) -> int:
    raw = get.get('n_steps')
    if raw is None or str(raw).strip() == '':
        return default
    try:
        v = int(float(raw))
    except (TypeError, ValueError):
        return default
    return max(1, min(v, 2000))


def _parse_mc_paths(get, default: int = _MC_DEFAULT_PATHS) -> int:
    raw = get.get('mc_paths')
    if raw is None or str(raw).strip() == '':
        return default
    try:
        v = int(float(raw))
    except (TypeError, ValueError):
        return default
    if v in _MC_PATH_CHOICES:
        return v
    # Snap to nearest allowed choice.
    return min(_MC_PATH_CHOICES, key=lambda c: abs(c - v))


def _parse_tree_steps(get, default: int = _CRR_TREE_DRAW_DEFAULT) -> int:
    raw = get.get('tree_steps')
    if raw is None or str(raw).strip() == '':
        return default
    try:
        v = int(float(raw))
    except (TypeError, ValueError):
        return default
    return max(_CRR_TREE_DRAW_MIN, min(v, _CRR_TREE_DRAW_MAX))


def _parse_mc_std_err(diagnostics: str | None) -> float | None:
    if not diagnostics:
        return None
    match = re.search(r'mc_std_err=([0-9.eE+-]+)', diagnostics)
    if match is None:
        return None
    try:
        return float(match.group(1))
    except ValueError:
        return None


def resolve_lab_params(get) -> tuple[GreeksLabParams | None, float, dict]:
    """Returns (chart params or None, τ years, meta)."""
    code = (get.get('instrument') or get.get('code') or '').strip().upper()
    if not code:
        return None, 0.0, {'instrument_code': None, 'param_kind': None, 'lab_product': None}

    instrument_defaults = defaults_from_instrument(code)
    if not instrument_defaults:
        return None, 0.0, {
            'instrument_code': code,
            'param_kind': 'unsupported',
            'lab_product': 'unsupported',
        }

    params = params_from_get(get, defaults=instrument_defaults)
    tau = _parse_tau(get, float(instrument_defaults.get('tau', 1.0)))
    kind = str(instrument_defaults.get('param_kind') or 'unsupported')
    maps = str(instrument_defaults.get('maps_to') or '')
    product = str(instrument_defaults.get('lab_product') or _lab_product(code, maps, kind))
    cash_default = float(instrument_defaults.get('cash_payout', 1.0))
    exercise = (
        (get.get('exercise') or instrument_defaults.get('exercise') or 'european').strip().lower()
    )
    steps_default = int(
        instrument_defaults.get(
            'n_steps',
            _CRR_LAB_TREE_DEFAULT if exercise == 'american' else _CRR_DEFAULT_STEPS,
        )
    )
    draw_tree = str(get.get('draw_tree') or '').strip().lower() in {'1', 'true', 'yes'}
    meta = {
        'instrument_code': code,
        'instrument_title': instrument_defaults.get('instrument_title') or '',
        'exercise': exercise,
        'maps_to': maps,
        'is_vanilla': bool(instrument_defaults.get('is_vanilla', False)),
        'param_kind': kind,
        'lab_product': product,
        'cash_payout': _parse_cash_payout(get, cash_default),
        'n_steps': _parse_n_steps(get, steps_default),
        'mc_paths': _parse_mc_paths(get, _MC_DEFAULT_PATHS),
        'mc_seed': _MC_DEFAULT_SEED,
        'tree_steps': _parse_tree_steps(get, _CRR_TREE_DRAW_DEFAULT),
        'draw_tree': draw_tree,
        'tau': tau,
    }
    return params, tau, meta


def _try_import_cpp():
    try:
        import numeraire_cpp  # type: ignore

        return numeraire_cpp
    except ImportError:
        return None


def _quote_from_raw(raw: dict) -> QuantLabQuote:
    theta = raw.get('theta')
    n_steps = raw.get('n_steps')
    diagnostics = raw.get('diagnostics')
    diag_s = str(diagnostics) if diagnostics is not None else None
    mc_paths = raw.get('mc_paths')
    mc_seed = raw.get('mc_seed')
    return QuantLabQuote(
        ok=True,
        engine_label=str(raw.get('engine') or 'c++_pricer'),
        message='',
        pv_unit=float(raw['npv']),
        delta=float(raw['delta']) if raw.get('delta') is not None else None,
        gamma=float(raw['gamma']) if raw.get('gamma') is not None else None,
        vega=float(raw['vega']) if raw.get('vega') is not None else None,
        theta=float(theta) if theta is not None else None,
        theta_day=(float(theta) / DAYS_PER_YEAR) if theta is not None else None,
        rho=float(raw['rho']) if raw.get('rho') is not None else None,
        n_steps=int(n_steps) if n_steps is not None else None,
        mc_paths=int(mc_paths) if mc_paths is not None else None,
        mc_seed=int(mc_seed) if mc_seed is not None else None,
        mc_std_err=_parse_mc_std_err(diag_s),
        diagnostics=diag_s,
    )


def _cpp_error(exc: Exception) -> QuantLabQuote:
    return QuantLabQuote(
        ok=False,
        engine_label='c++_pricer',
        message=f'C++ pricer error: {exc}',
    )


def _quote_from_cpp_vanilla(
    params: GreeksLabParams,
    tau: float,
    exercise: str,
    *,
    n_steps: int = 0,
) -> QuantLabQuote | None:
    mod = _try_import_cpp()
    if mod is None:
        return None
    ex = (exercise or 'european').strip().lower()
    steps = int(n_steps) if ex == 'american' else 0
    try:
        raw = mod.price_vanilla(
            float(params.spot),
            float(params.strike),
            float(params.vol),
            float(params.rate),
            float(params.div),
            float(tau),
            bool(params.is_call),
            ex,
            steps,
        )
    except Exception as exc:  # noqa: BLE001 — surface to lab UI
        return _cpp_error(exc)
    return _quote_from_raw(raw)


def _quote_from_cpp_vanilla_binomial(
    params: GreeksLabParams,
    tau: float,
    exercise: str,
    *,
    n_steps: int = _CRR_DEFAULT_STEPS,
) -> QuantLabQuote | None:
    mod = _try_import_cpp()
    if mod is None or not hasattr(mod, 'price_vanilla_binomial'):
        return None
    ex = (exercise or 'european').strip().lower()
    try:
        raw = mod.price_vanilla_binomial(
            float(params.spot),
            float(params.strike),
            float(params.vol),
            float(params.rate),
            float(params.div),
            float(tau),
            bool(params.is_call),
            ex,
            int(n_steps),
        )
    except Exception as exc:  # noqa: BLE001
        return _cpp_error(exc)
    return _quote_from_raw(raw)


def _quote_from_cpp_vanilla_mc(
    params: GreeksLabParams,
    tau: float,
    *,
    num_paths: int = _MC_DEFAULT_PATHS,
    seed: int = _MC_DEFAULT_SEED,
) -> QuantLabQuote | None:
    mod = _try_import_cpp()
    if mod is None or not hasattr(mod, 'price_vanilla_mc'):
        return None
    try:
        raw = mod.price_vanilla_mc(
            float(params.spot),
            float(params.strike),
            float(params.vol),
            float(params.rate),
            float(params.div),
            float(tau),
            bool(params.is_call),
            int(num_paths),
            int(seed),
        )
    except Exception as exc:  # noqa: BLE001
        return _cpp_error(exc)
    return _quote_from_raw(raw)


def _pv_diff(a: QuantLabQuote | None, b: QuantLabQuote | None) -> float | None:
    if not a or not b or not a.ok or not b.ok:
        return None
    if a.pv_unit is None or b.pv_unit is None:
        return None
    return float(a.pv_unit) - float(b.pv_unit)


def _dump_crr_tree(
    params: GreeksLabParams,
    tau: float,
    exercise: str,
    n_steps: int,
) -> dict | None:
    """Return CRR node dump for small N, or None if unavailable / too large."""
    if n_steps < _CRR_TREE_DRAW_MIN or n_steps > _CRR_TREE_DRAW_MAX:
        return None
    mod = _try_import_cpp()
    if mod is None or not hasattr(mod, 'dump_crr_tree'):
        return None
    ex = (exercise or 'american').strip().lower()
    try:
        return mod.dump_crr_tree(
            float(params.spot),
            float(params.strike),
            float(params.vol),
            float(params.rate),
            float(params.div),
            float(tau),
            bool(params.is_call),
            ex,
            int(n_steps),
        )
    except Exception:
        return None


def _crr_params_from_tree(tree: dict | None) -> dict | None:
    """Numeric CRR factors from a C++ tree dump (same u,d,Δt as the drawn tree)."""
    if not tree:
        return None
    try:
        p_up = float(tree['p_up'])
        return {
            'n_steps': int(tree['n_steps']),
            'dt': float(tree['dt']),
            'u': float(tree['u']),
            'd': float(tree['d']),
            'p_up': p_up,
            'p_down': 1.0 - p_up,
            'discount': float(tree.get('discount') or 0.0),
            'source': 'c++',
        }
    except (KeyError, TypeError, ValueError):
        return None


def _compute_crr_params(
    *,
    vol: float,
    rate: float,
    div: float,
    tau: float,
    n_steps: int,
) -> dict | None:
    """Same CRR factor formulas as ``CoxRossRubinsteinVanillaTree`` (for large N)."""
    n = int(n_steps)
    if n < 1 or tau <= 0.0 or vol <= 0.0:
        return None
    dt = float(tau) / float(n)
    u = math.exp(float(vol) * math.sqrt(dt))
    d = 1.0 / u
    growth = math.exp((float(rate) - float(div)) * dt)
    p_up = (growth - d) / (u - d)
    p_up = min(1.0, max(0.0, p_up))
    return {
        'n_steps': n,
        'dt': dt,
        'u': u,
        'd': d,
        'p_up': p_up,
        'p_down': 1.0 - p_up,
        'discount': math.exp(-float(rate) * dt),
        'source': 'formula',
    }


def _quote_from_cpp_aon(params: GreeksLabParams, tau: float) -> QuantLabQuote | None:
    mod = _try_import_cpp()
    if mod is None:
        return None
    try:
        raw = mod.price_asset_or_nothing(
            float(params.spot),
            float(params.strike),
            float(params.vol),
            float(params.rate),
            float(params.div),
            float(tau),
            bool(params.is_call),
        )
    except Exception as exc:  # noqa: BLE001
        return _cpp_error(exc)
    return _quote_from_raw(raw)


def _quote_from_cpp_con(
    params: GreeksLabParams, tau: float, cash_payout: float
) -> QuantLabQuote | None:
    mod = _try_import_cpp()
    if mod is None:
        return None
    try:
        raw = mod.price_cash_or_nothing(
            float(params.spot),
            float(params.strike),
            float(params.vol),
            float(params.rate),
            float(params.div),
            float(tau),
            bool(params.is_call),
            float(cash_payout),
        )
    except Exception as exc:  # noqa: BLE001
        return _cpp_error(exc)
    return _quote_from_raw(raw)


def _quote_from_cpp_forward(params: GreeksLabParams, tau: float) -> QuantLabQuote | None:
    mod = _try_import_cpp()
    if mod is None:
        return None
    try:
        raw = mod.price_equity_forward(
            float(params.spot),
            float(params.strike),
            float(params.rate),
            float(params.div),
            float(tau),
        )
    except Exception as exc:  # noqa: BLE001
        return _cpp_error(exc)
    return _quote_from_raw(raw)


def _quote_from_python(params: GreeksLabParams, tau: float) -> QuantLabQuote:
    """Fallback educational European BS (Python) if native module is missing."""
    try:
        g: BsResult = black_scholes(
            params.spot,
            params.strike,
            tau,
            params.vol,
            params.rate,
            params.div,
            is_call=params.is_call,
        )
    except (ValueError, OverflowError) as exc:
        return QuantLabQuote(
            ok=False,
            engine_label='python_bs_fallback',
            message=f'Could not price: {exc}',
        )
    return QuantLabQuote(
        ok=True,
        engine_label='python_bs_fallback (build numeraire_cpp for C++)',
        message='',
        pv_unit=g.pv_unit,
        delta=g.delta,
        gamma=g.gamma,
        vega=g.vega,
        theta=g.theta,
        theta_day=g.theta / DAYS_PER_YEAR,
        rho=g.rho,
    )


def _needs_cpp(lab_product: str) -> QuantLabQuote:
    return QuantLabQuote(
        ok=False,
        engine_label='lab',
        message=f'This product ({lab_product}) needs the C++ module `numeraire_cpp` '
        '(rebuild with NUMERAIRE_BUILD_PYTHON=ON).',
    )


def price_sandbox(
    params: GreeksLabParams,
    tau: float,
    *,
    lab_product: str,
    exercise: str,
    cash_payout: float = 1.0,
    n_steps: int = _CRR_DEFAULT_STEPS,
) -> QuantLabQuote:
    """Point price via production C++ pricers; Python BS fallback only for EU vanilla."""
    product = (lab_product or '').strip().lower()

    if product == 'vanilla':
        ex = (exercise or 'european').strip().lower()
        cpp = _quote_from_cpp_vanilla(params, tau, ex, n_steps=n_steps)
        if cpp is not None:
            return cpp
        if ex == 'american':
            return QuantLabQuote(
                ok=False,
                engine_label='lab',
                message='American (PVA) needs the C++ module `numeraire_cpp` '
                '(rebuild with NUMERAIRE_BUILD_PYTHON=ON).',
            )
        return _quote_from_python(params, tau)

    if product == 'aon':
        cpp = _quote_from_cpp_aon(params, tau)
        return cpp if cpp is not None else _needs_cpp('AON')

    if product == 'con':
        cpp = _quote_from_cpp_con(params, tau, cash_payout)
        return cpp if cpp is not None else _needs_cpp('BIN/DIG')

    if product == 'forward':
        cpp = _quote_from_cpp_forward(params, tau)
        return cpp if cpp is not None else _needs_cpp('EQF')

    if product == 'futures_outright':
        # Same economics as C++ AnalyticFuturesOutrightPricer: pv_unit = settle, Δ = 1.
        f = float(params.spot)
        if not (f > 0.0):
            return QuantLabQuote(
                ok=False,
                engine_label='analytic_futures_outright',
                message='Settle (F) must be positive.',
            )
        return QuantLabQuote(
            ok=True,
            engine_label='analytic_futures_outright',
            message='',
            pv_unit=f,
            delta=1.0,
            gamma=0.0,
            vega=0.0,
            theta=0.0,
            theta_day=0.0,
            rho=0.0,
        )

    return QuantLabQuote(
        ok=False,
        engine_label='lab',
        message='This instrument type is not wired in the lab yet.',
    )


def _value_spot_grid(strike: float, n: int = _VALUE_CHART_POINTS) -> list[float]:
    lo = strike * (1.0 - _VALUE_CHART_RANGE)
    hi = strike * (1.0 + _VALUE_CHART_RANGE)
    if lo <= 0:
        lo = max(strike * 0.05, 1e-6)
    n = max(40, n)
    step = (hi - lo) / n
    return [round(lo + i * step, 6) for i in range(n + 1)]


def build_payoff_value_chart(
    params: GreeksLabParams,
    tau: float,
) -> dict | None:
    """Payoff at T vs PV today along a spot grid (European vanilla only).

    Payoff is pure Python; PV uses C++ ``price_vanilla`` when available,
    else Python BS fallback. Full reprice per spot (not delta). Also returns
    the delta-tangent at the mark spot: V(S₀)+Δ·(S−S₀) — the gap to PV is Γ.
    """
    if tau < 0.0 or params.strike <= 0.0:
        return None

    spots = _value_spot_grid(params.strike)
    payoffs = [
        unit_payoff(
            kind='vanilla',
            is_call=params.is_call,
            strike=params.strike,
            spot=s,
        )
        for s in spots
    ]

    mod = _try_import_cpp()
    values: list[float] = []
    engine = 'python_bs_fallback'
    mark_pv: float | None = None
    mark_delta: float | None = None

    if mod is not None and hasattr(mod, 'price_vanilla'):
        engine = 'c++_analytic_bs'
        try:
            for s in spots:
                raw = mod.price_vanilla(
                    float(s),
                    float(params.strike),
                    float(params.vol),
                    float(params.rate),
                    float(params.div),
                    float(tau),
                    bool(params.is_call),
                    'european',
                    0,
                )
                values.append(float(raw['npv']))
            mark = mod.price_vanilla(
                float(params.spot),
                float(params.strike),
                float(params.vol),
                float(params.rate),
                float(params.div),
                float(tau),
                bool(params.is_call),
                'european',
                0,
            )
            mark_pv = float(mark['npv'])
            if mark.get('delta') is not None:
                mark_delta = float(mark['delta'])
        except Exception:  # noqa: BLE001
            values = []
            mark_pv = None
            mark_delta = None
            engine = 'python_bs_fallback'

    if not values:
        engine = 'python_bs_fallback'
        try:
            for s in spots:
                g = black_scholes(
                    s,
                    params.strike,
                    tau,
                    params.vol,
                    params.rate,
                    params.div,
                    is_call=params.is_call,
                )
                values.append(float(g.pv_unit))
            mark_g = black_scholes(
                params.spot,
                params.strike,
                tau,
                params.vol,
                params.rate,
                params.div,
                is_call=params.is_call,
            )
            mark_pv = float(mark_g.pv_unit)
            mark_delta = float(mark_g.delta)
        except (ValueError, OverflowError):
            return None

    if mark_pv is None or mark_delta is None:
        return None

    s0 = float(params.spot)
    tangent = [mark_pv + mark_delta * (s - s0) for s in spots]

    return {
        'engine': engine,
        'spot_mark': s0,
        'strike': float(params.strike),
        'is_call': bool(params.is_call),
        'tau': float(tau),
        'mark_pv': mark_pv,
        'mark_delta': mark_delta,
        'spots': spots,
        'payoff': payoffs,
        'value': values,
        'tangent': tangent,
    }


def build_quant_lab(get, *, asset_class: str = 'equity') -> dict:
    asset = (asset_class or 'equity').strip().lower()
    if asset not in {'equity', 'commodity'}:
        asset = 'equity'
    choices = list_inventory_choices(asset_class=asset)
    params, tau, meta = resolve_lab_params(get)
    selected = bool(meta.get('instrument_code'))
    if selected:
        code = str(meta.get('instrument_code') or '').strip().upper()
        if code and not any(c.code == code for c in choices):
            selected = False
            params = None
            meta = {'instrument_code': None, 'param_kind': None, 'lab_product': None}
    kind = str(meta.get('param_kind') or '')
    product = str(meta.get('lab_product') or '')
    exercise = str(meta.get('exercise') or 'european')
    fields = (
        fields_for_kind(kind, lab_product=product, exercise=exercise) if selected else []
    )

    quote: QuantLabQuote | None = None
    quote_mc: QuantLabQuote | None = None
    quote_crr: QuantLabQuote | None = None
    charts = None
    crr_tree = None
    crr_params = None
    payoff_value_chart = None
    n_steps = int(meta.get('n_steps') or _CRR_DEFAULT_STEPS)
    mc_paths = int(meta.get('mc_paths') or _MC_DEFAULT_PATHS)
    mc_seed = int(meta.get('mc_seed') or _MC_DEFAULT_SEED)
    is_eu_vanilla = product == 'vanilla' and exercise == 'european'
    is_am_vanilla = product == 'vanilla' and exercise == 'american'

    if selected and params is not None and product in {
        'vanilla',
        'aon',
        'con',
        'forward',
        'futures_outright',
    }:
        quote = price_sandbox(
            params,
            tau,
            lab_product=product,
            exercise=exercise,
            cash_payout=float(meta.get('cash_payout') or 1.0),
            n_steps=n_steps,
        )
        if is_eu_vanilla:
            quote_mc = _quote_from_cpp_vanilla_mc(
                params, tau, num_paths=mc_paths, seed=mc_seed
            )
            quote_crr = _quote_from_cpp_vanilla_binomial(
                params, tau, 'european', n_steps=n_steps
            )
            charts = build_greeks_vs_spot(params)
            payoff_value_chart = build_payoff_value_chart(params, tau)
            if meta.get('draw_tree'):
                tree_steps = int(meta.get('tree_steps') or _CRR_TREE_DRAW_DEFAULT)
                crr_tree = _dump_crr_tree(params, tau, 'european', tree_steps)
                crr_params = _crr_params_from_tree(crr_tree)
        if is_am_vanilla and quote and quote.ok:
            steps_used = quote.n_steps if quote.n_steps is not None else n_steps
            if _CRR_TREE_DRAW_MIN <= steps_used <= _CRR_TREE_MAX_STEPS:
                crr_tree = _dump_crr_tree(params, tau, exercise, steps_used)
            crr_params = _crr_params_from_tree(crr_tree) or _compute_crr_params(
                vol=float(params.vol),
                rate=float(params.rate),
                div=float(params.div),
                tau=float(tau),
                n_steps=int(steps_used),
            )

    has_greeks = bool(
        quote
        and quote.ok
        and any(x is not None for x in (quote.delta, quote.gamma, quote.vega, quote.theta, quote.rho))
    )
    show_quote_greeks = has_greeks and not is_eu_vanilla
    legend = (
        legend_for_fields(fields, include_greeks=charts is not None or has_greeks)
        if selected
        else []
    )

    pricing_url_name = (
        'journal:quant_lab_pricing_commodities'
        if asset == 'commodity'
        else 'journal:quant_lab_pricing_equities'
    )

    return {
        'asset_class': asset,
        'asset_class_label': 'Commodities' if asset == 'commodity' else 'Equities',
        'pricing_url_name': pricing_url_name,
        'choices': choices,
        'instrument_selected': selected,
        'fields': fields,
        'params': params,
        'meta': meta,
        'tau': tau,
        'tau_months': tau_month_label(tau),
        'quote': quote,
        'quote_mc': quote_mc,
        'quote_crr': quote_crr,
        'mc_minus_bs': _pv_diff(quote_mc, quote),
        'crr_minus_bs': _pv_diff(quote_crr, quote),
        'is_eu_vanilla': is_eu_vanilla,
        'mc_path_choices': _MC_PATH_CHOICES,
        'crr_tree_draw_min': _CRR_TREE_DRAW_MIN,
        'crr_tree_draw_max': _CRR_TREE_DRAW_MAX,
        'crr_tree_draw_default': _CRR_TREE_DRAW_DEFAULT,
        'lab': charts,
        'greeks_chart': charts['chart'] if charts else None,
        'legend': legend,
        'show_charts': charts is not None,
        'show_quote_greeks': show_quote_greeks,
        'show_greeks_panel': is_eu_vanilla and has_greeks,
        'show_formulas': selected
        and product in {'vanilla', 'aon', 'con', 'forward', 'futures_outright'},
        'crr_tree': crr_tree,
        'crr_params': crr_params,
        'show_crr_tree': bool(crr_tree and crr_tree.get('nodes')),
        'crr_tree_max_steps': _CRR_TREE_MAX_STEPS,
        'payoff_value_chart': payoff_value_chart,
        'show_payoff_value': payoff_value_chart is not None,
    }
