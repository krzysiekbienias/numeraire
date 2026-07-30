"""Trade what-if sandbox: reprice legs in-process (no DB writes).

Capabilities follow product shape:

* **linear** (equity forward) — closed-form only; no IV / MC
* **non-linear** (vanilla EU) — analytic BS + optional GBM MC
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Literal

from journal.black_scholes import black_scholes, scale_unit
from journal.equity_forward import equity_forward
from journal.monte_carlo import monte_carlo_vanilla

ProductKind = Literal['linear', 'nonlinear', 'unsupported']

DEFAULT_MC_PATHS = 20_000
MAX_MC_PATHS = 100_000
DEFAULT_MC_SEED = 42


@dataclass(frozen=True)
class ProductCapabilities:
    kind: ProductKind
    label: str
    uses_vol: bool
    uses_mc: bool
    uses_option_greeks: bool  # vega column meaningful
    analytic_name: str


CAPS_VANILLA = ProductCapabilities(
    kind='nonlinear',
    label='non-linear · vanilla EU',
    uses_vol=True,
    uses_mc=True,
    uses_option_greeks=True,
    analytic_name='Black–Scholes',
)

CAPS_FORWARD = ProductCapabilities(
    kind='linear',
    label='linear · equity forward',
    uses_vol=False,
    uses_mc=False,
    uses_option_greeks=False,
    analytic_name='forward closed-form',
)

CAPS_UNSUPPORTED = ProductCapabilities(
    kind='unsupported',
    label='unsupported in what-if',
    uses_vol=False,
    uses_mc=False,
    uses_option_greeks=False,
    analytic_name='—',
)


def capabilities_for_equity(equity) -> ProductCapabilities:
    if equity is None:
        return CAPS_UNSUPPORTED
    key = (equity.instrument_type or '').strip().lower()
    if key == 'plain_vanilla_european_option':
        return CAPS_VANILLA
    if key == 'equity_forward':
        return CAPS_FORWARD
    return CAPS_UNSUPPORTED


def merge_trade_capabilities(caps_list: list[ProductCapabilities]) -> ProductCapabilities:
    """Aggregate leg capabilities for the trade-level form / metrics."""
    usable = [c for c in caps_list if c.kind != 'unsupported']
    if not usable:
        return CAPS_UNSUPPORTED
    kinds = {c.kind for c in usable}
    if kinds == {'linear'}:
        return CAPS_FORWARD
    if kinds == {'nonlinear'}:
        return CAPS_VANILLA
    return ProductCapabilities(
        kind='nonlinear',  # mixed → show union of controls cautiously
        label='mixed · linear + non-linear',
        uses_vol=any(c.uses_vol for c in usable),
        uses_mc=any(c.uses_mc for c in usable),
        uses_option_greeks=any(c.uses_option_greeks for c in usable),
        analytic_name='per-leg analytic',
    )


@dataclass
class WhatIfInputs:
    spot: float
    vol: float
    rate: float
    div: float
    tau: float
    mc_paths: int = DEFAULT_MC_PATHS
    mc_seed: int = DEFAULT_MC_SEED
    run_mc: bool = True


def _rel_error(diff: float | None, base: float | None) -> float | None:
    if diff is None or base is None:
        return None
    denom = abs(float(base))
    if denom < 1e-12:
        return None
    return float(diff) / denom


@dataclass
class WhatIfLegResult:
    leg_id: str
    supported: bool
    reason: str
    caps: ProductCapabilities
    baseline_pv_total: float | None
    whatif_pv_unit: float | None
    whatif_pv_total: float | None
    delta_pv: float | None
    whatif_delta_total: float | None
    whatif_vega_total: float | None
    whatif_theta_day: float | None
    mc_pv_unit: float | None = None
    mc_pv_total: float | None = None
    mc_stderr_total: float | None = None
    mc_minus_analytic: float | None = None
    mc_rel_error: float | None = None


def _parse_float(raw: str | None, default: float) -> float:
    if raw is None or str(raw).strip() == '':
        return default
    return float(raw)


def _parse_int(raw: str | None, default: int, *, lo: int, hi: int) -> int:
    if raw is None or str(raw).strip() == '':
        return default
    try:
        value = int(float(raw))
    except (TypeError, ValueError):
        return default
    return max(lo, min(hi, value))


def baseline_inputs_from_mtm(mtm) -> WhatIfInputs | None:
    if mtm is None:
        return None
    return WhatIfInputs(
        spot=float(mtm.underlying_spot),
        vol=float(mtm.implied_vol_used),
        rate=float(mtm.risk_free_rate),
        div=float(mtm.dividend_yield),
        tau=float(mtm.years_to_maturity),
    )


def parse_whatif_inputs(
    get,
    baseline: WhatIfInputs,
    *,
    caps: ProductCapabilities,
) -> WhatIfInputs:
    """Read optional wf_* query params; respect product capabilities."""
    want_mc = get.get('wf_mc', '1') not in ('0', 'false', 'False', 'off')
    run_mc = bool(caps.uses_mc and want_mc)
    return WhatIfInputs(
        spot=_parse_float(get.get('wf_spot'), baseline.spot),
        vol=_parse_float(get.get('wf_vol'), baseline.vol) if caps.uses_vol else baseline.vol,
        rate=_parse_float(get.get('wf_rate'), baseline.rate),
        div=_parse_float(get.get('wf_div'), baseline.div),
        tau=_parse_float(get.get('wf_tau'), baseline.tau),
        mc_paths=_parse_int(
            get.get('wf_mc_paths'), DEFAULT_MC_PATHS, lo=100, hi=MAX_MC_PATHS
        ),
        mc_seed=_parse_int(get.get('wf_mc_seed'), DEFAULT_MC_SEED, lo=0, hi=2_147_483_647),
        run_mc=run_mc,
    )


def inputs_are_shocked(baseline: WhatIfInputs, shocked: WhatIfInputs, caps: ProductCapabilities) -> bool:
    fields = ['spot', 'rate', 'div', 'tau']
    if caps.uses_vol:
        fields.append('vol')
    return any(abs(getattr(shocked, f) - getattr(baseline, f)) > 1e-12 for f in fields)


def _unsupported(leg_id: str, reason: str, mtm, caps: ProductCapabilities) -> WhatIfLegResult:
    return WhatIfLegResult(
        leg_id=leg_id,
        supported=False,
        reason=reason,
        caps=caps,
        baseline_pv_total=getattr(mtm, 'pv_total', None),
        whatif_pv_unit=None,
        whatif_pv_total=None,
        delta_pv=None,
        whatif_delta_total=None,
        whatif_vega_total=None,
        whatif_theta_day=None,
    )


def _finish_leg(
    *,
    leg,
    mtm,
    caps: ProductCapabilities,
    pv_unit: float,
    delta_unit: float,
    vega_unit: float,
    theta_unit: float,
    mc_pv_unit: float | None = None,
    mc_stderr_unit: float | None = None,
) -> WhatIfLegResult:
    qty = float(leg.quantity)
    mult = float(leg.product.contract_size)
    direction = leg.direction
    pv_total = scale_unit(direction, qty, mult, pv_unit)
    baseline = float(mtm.pv_total) if mtm is not None else None
    delta_pv = (pv_total - baseline) if baseline is not None else None

    mc_pv_total = None
    mc_stderr_total = None
    mc_minus_analytic = None
    mc_rel = None
    if mc_pv_unit is not None:
        mc_pv_total = scale_unit(direction, qty, mult, mc_pv_unit)
        if mc_stderr_unit is not None:
            mc_stderr_total = abs(scale_unit(direction, qty, mult, mc_stderr_unit))
        mc_minus_analytic = mc_pv_total - pv_total
        mc_rel = _rel_error(mc_minus_analytic, pv_total)

    return WhatIfLegResult(
        leg_id=leg.leg_id,
        supported=True,
        reason='',
        caps=caps,
        baseline_pv_total=baseline,
        whatif_pv_unit=pv_unit,
        whatif_pv_total=pv_total,
        delta_pv=delta_pv,
        whatif_delta_total=scale_unit(direction, qty, mult, delta_unit),
        whatif_vega_total=(
            scale_unit(direction, qty, mult, vega_unit) if caps.uses_option_greeks else None
        ),
        whatif_theta_day=scale_unit(direction, qty, mult, theta_unit) / 365.0,
        mc_pv_unit=mc_pv_unit,
        mc_pv_total=mc_pv_total,
        mc_stderr_total=mc_stderr_total,
        mc_minus_analytic=mc_minus_analytic,
        mc_rel_error=mc_rel,
    )


def price_leg(leg, equity, mtm, inputs: WhatIfInputs) -> WhatIfLegResult:
    caps = capabilities_for_equity(equity)
    leg_id = leg.leg_id

    if caps.kind == 'unsupported':
        itype = getattr(equity, 'instrument_type', None) or 'unknown'
        return _unsupported(leg_id, f'No what-if engine for {itype}', mtm, caps)

    if equity is None or equity.strike is None:
        return _unsupported(leg_id, 'Missing strike', mtm, caps)

    strike = float(equity.strike)

    if caps.kind == 'linear':
        try:
            fwd = equity_forward(
                inputs.spot, strike, inputs.tau, inputs.rate, inputs.div
            )
        except (ValueError, OverflowError) as exc:
            return _unsupported(leg_id, str(exc), mtm, caps)
        return _finish_leg(
            leg=leg,
            mtm=mtm,
            caps=caps,
            pv_unit=fwd.pv_unit,
            delta_unit=fwd.delta,
            vega_unit=0.0,
            theta_unit=fwd.theta,
        )

    # non-linear vanilla
    side = (equity.option_type or '').strip().lower()
    is_call = side in ('call', 'c')
    if side not in ('call', 'c', 'put', 'p'):
        return _unsupported(leg_id, f'Unknown option_type={equity.option_type!r}', mtm, caps)

    try:
        unit = black_scholes(
            inputs.spot,
            strike,
            inputs.tau,
            inputs.vol,
            inputs.rate,
            inputs.div,
            is_call=is_call,
        )
    except (ValueError, OverflowError) as exc:
        return _unsupported(leg_id, str(exc), mtm, caps)

    mc_pv_unit = None
    mc_stderr_unit = None
    if inputs.run_mc and caps.uses_mc:
        try:
            mc = monte_carlo_vanilla(
                inputs.spot,
                strike,
                inputs.tau,
                inputs.vol,
                inputs.rate,
                inputs.div,
                is_call=is_call,
                n_paths=inputs.mc_paths,
                seed=inputs.mc_seed,
            )
            mc_pv_unit = mc.pv_unit
            mc_stderr_unit = mc.stderr
        except (ValueError, OverflowError):
            pass

    return _finish_leg(
        leg=leg,
        mtm=mtm,
        caps=caps,
        pv_unit=unit.pv_unit,
        delta_unit=unit.delta,
        vega_unit=unit.vega,
        theta_unit=unit.theta,
        mc_pv_unit=mc_pv_unit,
        mc_stderr_unit=mc_stderr_unit,
    )


def run_trade_whatif(
    market_rows: list[dict[str, Any]],
    inputs: WhatIfInputs,
    *,
    caps: ProductCapabilities,
) -> dict[str, Any]:
    legs_out = [
        price_leg(row['leg'], row['equity'], row['mtm'], inputs) for row in market_rows
    ]

    supported = [r for r in legs_out if r.supported and r.whatif_pv_total is not None]
    baseline_sum = sum(
        r.baseline_pv_total for r in supported if r.baseline_pv_total is not None
    )
    whatif_sum = sum(r.whatif_pv_total for r in supported)  # type: ignore[misc]
    mc_legs = [r for r in supported if r.mc_pv_total is not None]
    mc_sum = sum(r.mc_pv_total for r in mc_legs) if mc_legs else None  # type: ignore[misc]
    mc_stderr = None
    if mc_legs:
        mc_stderr = sum((r.mc_stderr_total or 0.0) ** 2 for r in mc_legs) ** 0.5

    return {
        'legs': legs_out,
        'caps': caps,
        'supported_count': len(supported),
        'baseline_pv_total': baseline_sum if supported else None,
        'whatif_pv_total': whatif_sum if supported else None,
        'delta_pv': (whatif_sum - baseline_sum) if supported else None,
        'mc_pv_total': mc_sum if caps.uses_mc else None,
        'mc_stderr_total': mc_stderr if caps.uses_mc else None,
        'mc_minus_analytic': (
            (mc_sum - whatif_sum) if (caps.uses_mc and mc_sum is not None) else None
        ),
        'mc_rel_error': (
            _rel_error(mc_sum - whatif_sum, whatif_sum)
            if (caps.uses_mc and mc_sum is not None)
            else None
        ),
        'mc_paths': inputs.mc_paths if (caps.uses_mc and inputs.run_mc) else 0,
        'mc_seed': inputs.mc_seed if (caps.uses_mc and inputs.run_mc) else None,
        'replay_error': None,
    }
