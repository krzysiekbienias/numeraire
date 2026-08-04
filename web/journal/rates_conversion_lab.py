"""Rates Conversion Lab — continuous zero ↔ discount factor sandbox.

Not persisted. Thin wrapper around `numeraire_cpp` interest-rate transforms.
More compounding conventions can land here later (see C++ header stubs).
"""

from __future__ import annotations

from dataclasses import dataclass

# ── Symbols / fields ─────────────────────────────────────────────────────
# Light legend — fixed form, no instrument inventory.

SYMBOL_LEGEND = (
    ('z', 'Zero', 'Continuous zero rate (absolute, e.g. 0.05 = 5%)'),
    ('DF', 'Discount', 'Discount factor P(0,t) = e^{-z t}'),
    ('t', 'Tenor', 'Time in years'),
)

_DIRECTIONS = frozenset({'z_to_df', 'df_to_z'})

_DEFAULTS = {
    'direction': 'z_to_df',
    'zero_rate': 0.05,
    'discount_factor': 0.951229,
    'time_years': 1.0,
}


@dataclass(frozen=True)
class RatesLabParams:
    """Parsed sandbox inputs (from request.GET). Both form sides kept."""

    direction: str  # z_to_df | df_to_z
    zero_rate: float
    discount_factor: float
    time_years: float


@dataclass(frozen=True)
class RatesLabResult:
    """One conversion shot from C++ (or error state)."""

    ok: bool
    engine_label: str
    message: str
    direction: str = ''
    discount_factor: float | None = None
    zero_rate: float | None = None
    # later: simple_rate, annual_rate, ...


def _try_import_cpp():
    try:
        import numeraire_cpp  # type: ignore

        return numeraire_cpp
    except ImportError:
        return None


def _parse_float(get, key: str, default: float) -> float:
    raw = get.get(key)
    if raw is None or str(raw).strip() == '':
        return default
    try:
        return float(raw)
    except (TypeError, ValueError):
        return default


def resolve_rates_params(get) -> RatesLabParams:
    """Parse sandbox inputs from request.GET (bad/missing → defaults)."""
    direction = (get.get('direction') or _DEFAULTS['direction']).strip().lower()
    if direction not in _DIRECTIONS:
        direction = str(_DEFAULTS['direction'])

    zero_rate = _parse_float(get, 'zero_rate', float(_DEFAULTS['zero_rate']))
    discount_factor = _parse_float(
        get, 'discount_factor', float(_DEFAULTS['discount_factor'])
    )
    time_years = _parse_float(get, 'time_years', float(_DEFAULTS['time_years']))
    if time_years < 0.0:
        time_years = float(_DEFAULTS['time_years'])
    if discount_factor <= 0.0:
        discount_factor = float(_DEFAULTS['discount_factor'])

    return RatesLabParams(
        direction=direction,
        zero_rate=zero_rate,
        discount_factor=discount_factor,
        time_years=time_years,
    )


def convert_rates(params: RatesLabParams) -> RatesLabResult:
    """Orchestrate continuous z ↔ DF via C++."""
    mod = _try_import_cpp()
    if mod is None:
        return RatesLabResult(
            ok=False,
            engine_label='lab',
            message='C++ module `numeraire_cpp` missing '
            '(rebuild with NUMERAIRE_BUILD_PYTHON=ON).',
            direction=params.direction,
        )

    try:
        if params.direction == 'df_to_z':
            if not hasattr(mod, 'continuous_zero_from_discount_factor'):
                return RatesLabResult(
                    ok=False,
                    engine_label='lab',
                    message='C++ module missing continuous_zero_from_discount_factor.',
                    direction=params.direction,
                )
            if params.time_years <= 0.0:
                return RatesLabResult(
                    ok=False,
                    engine_label='lab',
                    message='Tenor t must be > 0 to recover z from DF.',
                    direction=params.direction,
                )
            z = float(
                mod.continuous_zero_from_discount_factor(
                    float(params.discount_factor),
                    float(params.time_years),
                )
            )
            return RatesLabResult(
                ok=True,
                engine_label='c++_rates',
                message='',
                direction=params.direction,
                discount_factor=float(params.discount_factor),
                zero_rate=z,
            )

        # default: z_to_df
        if not hasattr(mod, 'discount_factor_from_continuous_zero'):
            return RatesLabResult(
                ok=False,
                engine_label='lab',
                message='C++ module missing discount_factor_from_continuous_zero.',
                direction=params.direction,
            )
        df = float(
            mod.discount_factor_from_continuous_zero(
                float(params.zero_rate),
                float(params.time_years),
            )
        )
        z_back = None
        if params.time_years > 0.0 and hasattr(
            mod, 'continuous_zero_from_discount_factor'
        ):
            z_back = float(
                mod.continuous_zero_from_discount_factor(df, float(params.time_years))
            )
        return RatesLabResult(
            ok=True,
            engine_label='c++_rates',
            message='',
            direction='z_to_df',
            discount_factor=df,
            zero_rate=z_back,
        )
    except Exception as exc:  # noqa: BLE001 — surface to lab UI
        return RatesLabResult(
            ok=False,
            engine_label='c++_rates',
            message=f'C++ rates error: {exc}',
            direction=params.direction,
        )


def build_rates_conversion_lab(get) -> dict:
    """Assemble template context for the rates conversion sandbox."""
    params = resolve_rates_params(get)
    result = convert_rates(params)
    return {
        'legend': SYMBOL_LEGEND,
        'params': params,
        'result': result,
        'show_result': bool(result.ok),
    }
