"""Quant Lab — instrument-type sandbox.

Not persisted. Dropdown = catalog codes (PVE, AON, BIN, EQF, …); inputs appear only
after a pick. Point price via C++ (`numeraire_cpp`); greeks charts still Python BS
shapes for vanillas only.
"""

from __future__ import annotations

from dataclasses import dataclass

from journal.black_scholes import BsResult, black_scholes
from journal.greeks_lab import GreeksLabParams, build_greeks_vs_spot, params_from_get
from journal.models import CatalogInstrumentType

DAYS_PER_YEAR = 365.0

# Full legend; UI filters to symbols needed by the selected instrument (+ greeks when priced).
SYMBOL_LEGEND = (
    ('S', 'Spot', 'Underlying price used for pricing / chart mark'),
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
}
_FIELD_STRIKE = {
    'name': 'strike',
    'sym': 'K',
    'label': 'Strike',
    'kind': 'number',
}
_FIELD_FORWARD = {
    'name': 'strike',
    'sym': 'K',
    'label': 'Forward',
    'kind': 'number',
}
_FIELD_VOL = {
    'name': 'vol',
    'sym': 'σ',
    'label': 'IV',
    'kind': 'number',
}
_FIELD_RATE = {
    'name': 'rate',
    'sym': 'r',
    'label': 'Rate',
    'kind': 'number',
}
_FIELD_DIV = {
    'name': 'div',
    'sym': 'q',
    'label': 'Dividend',
    'kind': 'number',
}
_FIELD_TAU = {
    'name': 'tau',
    'sym': 'τ',
    'label': 'Tenor (y)',
    'kind': 'number',
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
}
_FIELD_STEPS = {
    'name': 'n_steps',
    'sym': 'N',
    'label': 'Steps',
    'kind': 'number',
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
_CRR_TREE_MAX_STEPS = 8
_CRR_DEFAULT_STEPS = 200
_CRR_LAB_TREE_DEFAULT = 3
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

_GREEK_SYMS = frozenset({'Δ', 'Γ', 'ν', 'Θ', 'ρ'})


@dataclass(frozen=True)
class InventoryChoice:
    code: str
    label: str
    maps_to: str
    family: str
    is_vanilla: bool
    exercise: str  # european | american | n/a
    param_kind: str  # option | forward | unsupported


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
    """Sandbox product wire: vanilla | aon | con | forward | unsupported."""
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
    return 'unsupported'


def _exercise_for(maps_to: str) -> str:
    key = (maps_to or '').strip().lower()
    if 'american' in key:
        return 'american'
    if 'forward' in key or key in {'fra', 'listed_future'}:
        return 'n/a'
    return 'european'


def _param_kind(code: str, family: str, maps_to: str) -> str:
    """Which input set to show for this catalog row."""
    fam = (family or '').strip().upper()
    key = (maps_to or '').strip().lower()
    code_u = (code or '').strip().upper()
    if fam in {'FORWARD', 'FUTURE'} or 'forward' in key or code_u in {'EQF', 'FXF', 'IRF', 'EFT'}:
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
    if product == 'con':
        return [dict(f) for f in _CON_FIELDS]
    if product == 'forward' or param_kind == 'forward':
        return [dict(f) for f in _FORWARD_FIELDS]
    if product == 'vanilla' and ex == 'american':
        return [dict(f) for f in _AMERICAN_VANILLA_FIELDS]
    if product in {'vanilla', 'aon'} or param_kind == 'option':
        return [dict(f) for f in _OPTION_FIELDS]
    return []


def legend_for_fields(fields: list[dict], *, include_greeks: bool) -> list[tuple[str, str, str]]:
    syms = {f['sym'] for f in fields if f.get('sym')}
    if include_greeks:
        syms |= _GREEK_SYMS
    return [row for row in SYMBOL_LEGEND if row[0] in syms]


def list_inventory_choices() -> list[InventoryChoice]:
    try:
        rows = list(
            CatalogInstrumentType.objects.filter(is_active=True).order_by('sort_order', 'code')
        )
    except Exception:
        return []
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
            return {}
    except Exception:
        return {}

    maps = (cat.maps_to_instrument_type or '').strip()
    family = (cat.family or '').strip()
    exercise = _exercise_for(maps)
    kind = _param_kind(code_u, family, maps)
    product = _lab_product(code_u, maps, kind)
    return {
        'instrument': code_u,
        'spot': 100.0,
        'strike': 100.0,
        'vol': 0.20,
        'rate': 0.04,
        'div': 0.0,
        'side': 'call',
        'cash_payout': 1.0,
        'n_steps': _CRR_LAB_TREE_DEFAULT if exercise == 'american' else _CRR_DEFAULT_STEPS,
        'tau': 30.0 / DAYS_PER_YEAR,
        'exercise': exercise if exercise != 'n/a' else 'european',
        'instrument_code': code_u,
        'instrument_title': _title_from_maps_to(maps),
        'maps_to': maps,
        'is_vanilla': _is_vanilla(maps),
        'param_kind': kind,
        'lab_product': product,
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
    tau = _parse_tau(get, float(instrument_defaults.get('tau', 30.0 / DAYS_PER_YEAR)))
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


def _dump_crr_tree(
    params: GreeksLabParams,
    tau: float,
    exercise: str,
    n_steps: int,
) -> dict | None:
    """Return CRR node dump for small N, or None if unavailable / too large."""
    if n_steps < 1 or n_steps > _CRR_TREE_MAX_STEPS:
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

    return QuantLabQuote(
        ok=False,
        engine_label='lab',
        message='This instrument type is not wired in the lab yet.',
    )


def build_quant_lab(get) -> dict:
    choices = list_inventory_choices()
    params, tau, meta = resolve_lab_params(get)
    selected = bool(meta.get('instrument_code'))
    kind = str(meta.get('param_kind') or '')
    product = str(meta.get('lab_product') or '')
    exercise = str(meta.get('exercise') or 'european')
    fields = (
        fields_for_kind(kind, lab_product=product, exercise=exercise) if selected else []
    )

    quote: QuantLabQuote | None = None
    charts = None
    crr_tree = None
    n_steps = int(meta.get('n_steps') or _CRR_DEFAULT_STEPS)
    if selected and params is not None and product in {'vanilla', 'aon', 'con', 'forward'}:
        quote = price_sandbox(
            params,
            tau,
            lab_product=product,
            exercise=exercise,
            cash_payout=float(meta.get('cash_payout') or 1.0),
            n_steps=n_steps,
        )
        # Educational greeks-vs-spot shapes only for European vanillas (Python BS).
        if product == 'vanilla' and exercise != 'american':
            charts = build_greeks_vs_spot(params)
        if product == 'vanilla' and exercise == 'american' and quote and quote.ok:
            steps_used = quote.n_steps if quote.n_steps is not None else n_steps
            crr_tree = _dump_crr_tree(params, tau, exercise, steps_used)

    has_greeks = bool(
        quote
        and quote.ok
        and any(x is not None for x in (quote.delta, quote.gamma, quote.vega, quote.theta, quote.rho))
    )
    legend = (
        legend_for_fields(fields, include_greeks=charts is not None or has_greeks)
        if selected
        else []
    )

    return {
        'choices': choices,
        'instrument_selected': selected,
        'fields': fields,
        'params': params,
        'meta': meta,
        'tau': tau,
        'quote': quote,
        'lab': charts,
        'greeks_chart': charts['chart'] if charts else None,
        'legend': legend,
        'show_charts': charts is not None,
        'show_quote_greeks': has_greeks,
        'show_formulas': selected and product in {'vanilla', 'aon', 'con', 'forward'},
        'crr_tree': crr_tree,
        'show_crr_tree': bool(crr_tree and crr_tree.get('nodes')),
        'crr_tree_max_steps': _CRR_TREE_MAX_STEPS,
    }
