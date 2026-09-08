"""Simulated forward curve against the curve that actually printed.

Stand on a past session, take the Gabillon fit that existed by then, evolve the quoted
strip forward to a later session, and lay one simulated curve next to the realized one.

Deliberately a single draw rather than a quantile band. A band answers how wide the
distribution is; one path answers what a curve produced by this model actually looks
like — whether the contracts move as a curve instead of independently, and whether the
seasonal shape survives the trip. No single draw is supposed to land on the realized
curve, so the thing to read here is family resemblance, not accuracy.

Lives in the curves module rather than Quant Lab on purpose: it reads the database — the
quoted history and the persisted calibrations — where the lab is a cache-only sandbox
whose inputs come off the form.
"""

from __future__ import annotations

from datetime import date, timedelta
from typing import Any

from .commodity_curves import (
    list_futures_curve_as_of,
    list_futures_product_codes,
    load_strip,
)
from .models import CalibrationFactor, CalibrationParam, CalibrationSnapshot

_GABILLON_MODEL = 'gabillon_2f'
_GABILLON_SOURCE = 'fit'
_SHORT_SUFFIX = '_SHORT'
_LONG_SUFFIX = '_LONG'

# The evolution kernel takes a lab grid of 7..730 days, which also happens to be the
# range over which a two-factor fit is worth trusting out of sample.
MIN_HORIZON_DAYS = 7
MAX_HORIZON_DAYS = 730

_SEED = 42
_N_INTERVALS = 48
_MAX_CONTRACTS = 60
# Each draw is one simulated world; a handful is enough to flick through and see that no
# two look alike while all of them keep the curve's shape.
N_DRAWS = 8
# The draws answer what a curve from this model looks like; the band behind them answers
# whether what actually happened was plausible under it. That second question needs a
# population rather than a handful, and the draws shown are simply its first few paths.
_N_BAND_PATHS = 2000


def _try_import_cpp():
    try:
        import numeraire_cpp  # type: ignore

        return numeraire_cpp
    except ImportError:
        return None


def _parse_draw(raw: str, count: int) -> int:
    try:
        wanted = int(str(raw).strip())
    except (TypeError, ValueError):
        return 1
    return wanted if 1 <= wanted <= count else 1


def _parse_date(raw: str, available: list[date]) -> date | None:
    try:
        wanted = date.fromisoformat((raw or '').strip())
    except ValueError:
        return None
    return wanted if wanted in available else None


def load_calibration_as_known_at(
    product_code: str, as_of: date, *, allow_lookahead: bool = False
) -> dict[str, Any] | None:
    """Latest `gabillon_2f` fit carrying this curve, dated no later than `as_of`.

    The cutoff is the whole point: a fit calculated after the origin session would let the
    simulation borrow knowledge the trader did not have, and the comparison would flatter
    the model for the wrong reason. `allow_lookahead` drops it anyway, which is only worth
    doing to see the machinery run before the historical fits exist — the page says so
    loudly whenever it happens.
    """
    if not product_code or as_of is None:
        return None
    short_id = f'{product_code}{_SHORT_SUFFIX}'
    long_id = f'{product_code}{_LONG_SUFFIX}'
    carrying = CalibrationFactor.objects.filter(
        factor_id__in=[short_id, long_id]
    ).values_list('calibration_id', flat=True)
    candidates = CalibrationSnapshot.objects.filter(
        model=_GABILLON_MODEL,
        source=_GABILLON_SOURCE,
        calibration_id__in=list(carrying),
    )
    if not allow_lookahead:
        candidates = candidates.filter(as_of__lte=as_of)
    snapshot = candidates.order_by('-as_of', '-calibration_id').first()
    if snapshot is None:
        return None

    vols = dict(
        CalibrationFactor.objects.filter(
            calibration_id=snapshot.calibration_id,
            factor_id__in=[short_id, long_id],
        ).values_list('factor_id', 'volatility')
    )
    params = dict(
        CalibrationParam.objects.filter(
            calibration_id=snapshot.calibration_id, factor_id=product_code
        ).values_list('param_name', 'param_value')
    )
    short_vol = vols.get(short_id)
    long_vol = vols.get(long_id)
    mean_reversion = params.get('mean_reversion')
    if short_vol is None or long_vol is None or mean_reversion is None:
        return None

    vol_fit_rmse = params.get('vol_fit_rmse')
    vol_fit_pillars = params.get('vol_fit_pillars')
    return {
        'calibration_id': snapshot.calibration_id,
        'model': snapshot.model,
        'as_of': snapshot.as_of,
        'scope_key': snapshot.scope_key,
        'stale_days': (as_of - snapshot.as_of).days,
        'is_lookahead': snapshot.as_of > as_of,
        'history_start': snapshot.history_start,
        'history_end': snapshot.history_end,
        'vol_annualization_days': snapshot.vol_annualization_days,
        'mean_reversion': float(mean_reversion),
        'short_vol': float(short_vol),
        'long_vol': float(long_vol),
        'factor_correlation': float(params.get('factor_correlation', 0.0)),
        'vol_fit_rmse': None if vol_fit_rmse is None else float(vol_fit_rmse),
        'vol_fit_pillars': None if vol_fit_pillars is None else int(vol_fit_pillars),
    }


def list_target_sessions(available: list[date], origin: date) -> list[date]:
    """Sessions that can be compared against `origin`, newest first."""
    return [
        d
        for d in available
        if MIN_HORIZON_DAYS <= (d - origin).days <= MAX_HORIZON_DAYS
    ]


def _simulate(strip: list[dict], calibration: dict, horizon_days: int) -> dict[str, Any]:
    mod = _try_import_cpp()
    if mod is None or not hasattr(mod, 'simulate_gabillon_curve'):
        return {
            'ok': False,
            'message': 'C++ module `numeraire_cpp` missing simulate_gabillon_curve '
            '(rebuild with NUMERAIRE_BUILD_PYTHON=ON).',
        }
    try:
        raw = mod.simulate_gabillon_curve(
            tickers=[str(p['ticker']) for p in strip],
            settlement_years=[float(p['tau']) for p in strip],
            anchor_prices=[float(p['settle']) for p in strip],
            mean_reversion=calibration['mean_reversion'],
            short_vol=calibration['short_vol'],
            long_vol=calibration['long_vol'],
            factor_correlation=calibration['factor_correlation'],
            n_paths=_N_BAND_PATHS,
            seed=_SEED,
            horizon_days=int(horizon_days),
            n_intervals=_N_INTERVALS,
            fan_contract=0,
            fan_paths=1,
            curve_draws=N_DRAWS,
        )
    except Exception as exc:  # noqa: BLE001
        return {'ok': False, 'message': f'C++ simulate error: {exc}'}
    return {'ok': True, 'raw': raw}


def _compare(
    stats: list[dict], curve: list[float], realized_by_ticker: dict[str, float]
) -> list[dict]:
    """Join each contract's simulated value to the settle it went on to print."""
    rows: list[dict] = []
    for index, stat in enumerate(stats):
        ticker = str(stat['ticker'])
        realized = realized_by_ticker.get(ticker)
        if realized is None or realized <= 0.0 or index >= len(curve):
            continue
        simulated = float(curve[index])
        p5 = float(stat['p5'])
        p95 = float(stat['p95'])
        rows.append(
            {
                'ticker': ticker,
                'expiry': stat.get('expiry'),
                'settlement_years': float(stat['settlement_years']),
                'anchor': float(stat['anchor']),
                'simulated': simulated,
                'realized': realized,
                'gap_pct': 100.0 * (simulated - realized) / realized,
                'p5': p5,
                'p95': p95,
                # The draw is one story; this is the only column that says whether the
                # story the market told was one the model considered possible.
                'inside_band': p5 <= realized <= p95,
            }
        )
    return rows


def build_curve_backtest(get) -> dict[str, Any]:
    products = list_futures_product_codes()
    requested = (get.get('product_code') or '').strip().upper()
    product_code = requested if requested in products else (products[0] if products else '')
    available = list_futures_curve_as_of(product_code) if product_code else []
    allow_lookahead = (get.get('allow_lookahead') or '').strip() in {'1', 'true', 'yes'}
    show_band = (get.get('band') or '1').strip() not in {'0', 'false', 'no'}

    context: dict[str, Any] = {
        'products': products,
        'product_code': product_code,
        'available_as_of': available,
        'origin_as_of': None,
        'target_as_of': None,
        'target_choices': [],
        'calibration': None,
        'allow_lookahead': allow_lookahead,
        'lookahead_available': False,
        'show_band': show_band,
        'draw': 1,
        'draws': [],
        'result': None,
        'rows': [],
        'chart': [],
        'notice': '',
    }
    if not available:
        context['notice'] = f'No quoted {product_code or "futures"} history in the database.'
        return context

    # Open on a quarter-long comparison: long enough for the cone to have any width, short
    # enough that a two-factor fit still has something to say.
    origin = _parse_date(get.get('origin_as_of', ''), available)
    if origin is None:
        wanted = available[0] - timedelta(days=90)
        usable = [d for d in available if list_target_sessions(available, d)]
        origin = min(
            usable, key=lambda d: abs((d - wanted).days), default=available[-1]
        )
    target_choices = list_target_sessions(available, origin)
    target = _parse_date(get.get('target_as_of', ''), target_choices)
    if target is None:
        target = target_choices[0] if target_choices else None

    context.update(
        {
            'origin_as_of': origin,
            'target_as_of': target,
            'target_choices': target_choices,
        }
    )
    if target is None:
        context['notice'] = (
            f'No session between {MIN_HORIZON_DAYS} and {MAX_HORIZON_DAYS} days after '
            f'{origin:%Y-%m-%d} to compare against.'
        )
        return context

    calibration = load_calibration_as_known_at(
        product_code, origin, allow_lookahead=allow_lookahead
    )
    context['calibration'] = calibration
    if calibration is None:
        # Only worth offering the look-ahead escape hatch when a later fit exists at all;
        # for a curve nobody has ever calibrated it would just be a dead link.
        context['lookahead_available'] = (
            load_calibration_as_known_at(product_code, origin, allow_lookahead=True)
            is not None
        )
        context['notice'] = (
            f'No {_GABILLON_MODEL} fit for {product_code} dated on or before '
            f'{origin:%Y-%m-%d}. Run '
            f'`dev_main --calibrate-gabillon --as-of {origin:%Y-%m-%d} --book BOOK_3` '
            'and come back.'
        )
        return context

    origin_strip = [
        p
        for p in load_strip(product_code, origin)
        if p.get('tau') is not None and p['tau'] > 0.0 and p.get('settle', 0.0) > 0.0
    ][:_MAX_CONTRACTS]
    if not origin_strip:
        context['notice'] = f'No live quoted contracts on {origin:%Y-%m-%d}.'
        return context

    horizon_days = (target - origin).days
    sim = _simulate(origin_strip, calibration, horizon_days)
    if not sim['ok']:
        context['notice'] = sim['message']
        return context

    raw = sim['raw']
    curves = [list(c) for c in (raw.get('curves') or [])]
    if not curves:
        context['notice'] = 'The simulation returned no curves.'
        return context

    draw = _parse_draw(get.get('draw', ''), len(curves))
    expiry_by_ticker = {str(p['ticker']): p.get('expiry') for p in origin_strip}
    stats = []
    for stat in raw.get('contracts') or []:
        row = dict(stat)
        row['expiry'] = expiry_by_ticker.get(str(row.get('ticker')))
        stats.append(row)

    realized_by_ticker = {
        str(p['ticker']): float(p['settle'])
        for p in load_strip(product_code, target)
        if p.get('settle', 0.0) > 0.0
    }
    rows = _compare(stats, curves[draw - 1], realized_by_ticker)
    if not rows:
        context['notice'] = (
            f'None of the contracts quoted on {origin:%Y-%m-%d} still printed a settle on '
            f'{target:%Y-%m-%d}.'
        )
        return context

    gaps = [abs(r['gap_pct']) for r in rows]
    n_inside = sum(1 for r in rows if r['inside_band'])
    context['draw'] = draw
    context['draws'] = list(range(1, len(curves) + 1))
    context['result'] = {
        'horizon_days': horizon_days,
        'seed': raw.get('seed'),
        'engine': raw.get('engine', ''),
        'n_paths': _N_BAND_PATHS,
        'n_compared': len(rows),
        'n_dropped': len(stats) - len(rows),
        'mean_gap_pct': sum(gaps) / len(gaps),
        'max_gap_pct': max(gaps),
        'n_inside': n_inside,
        # A 5–95 band should swallow roughly nineteen contracts in twenty if the widths
        # are honest. Reading it per contract overstates the evidence — neighbouring
        # deliveries move together — but a curve falling wholly outside is still a verdict.
        'coverage_pct': 100.0 * n_inside / len(rows),
    }
    context['rows'] = rows
    context['chart'] = [
        {
            'ticker': r['ticker'],
            'tau': round(r['settlement_years'], 4),
            'simulated': r['simulated'],
            'realized': r['realized'],
            'anchor': r['anchor'],
            'p5': r['p5'],
            'band': r['p95'] - r['p5'],
        }
        for r in rows
    ]
    return context
