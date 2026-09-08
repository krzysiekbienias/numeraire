"""Simulation Lab — path sandbox (separate from Quant Lab pricing).

Asset class first, then a model within it. Uniform toy time grid over a selectable
horizon, driven by the production evolution kernels through `numeraire_cpp`.
Not the production CCR schedule. Nothing persisted. Non-wired models are stubs.

Deliberately abstract: no instrument, no quoted curve, no calibration. The point is the
shape the dynamics produce, so everything comes off the form. Real curves and fitted
parameters belong to the book on the production side.
"""

from __future__ import annotations

ASSET_CLASSES = (
    {'id': 'equity', 'label': 'Equity'},
    {'id': 'rates', 'label': 'Interest rates'},
    {'id': 'commodity', 'label': 'Commodity'},
)

SIM_MODELS = (
    {'id': 'gbm', 'label': 'GBM', 'asset_class': 'equity', 'wired': True},
    {'id': 'heston', 'label': 'Heston', 'asset_class': 'equity', 'wired': False},
    {'id': 'hull_white', 'label': 'Hull–White', 'asset_class': 'rates', 'wired': False},
    {'id': 'bachelier', 'label': 'Bachelier', 'asset_class': 'rates', 'wired': False},
    {'id': 'gabillon', 'label': 'Gabillon 2F', 'asset_class': 'commodity', 'wired': True},
)

# Fixed lab horizons (days) — independent of production exposure pillars.
HORIZONS = (
    {'id': '14', 'days': 14, 'label': '2 weeks'},
    {'id': '30', 'days': 30, 'label': '1 month'},
    {'id': '90', 'days': 90, 'label': '3 months'},
    {'id': '180', 'days': 180, 'label': '6 months'},
    {'id': '365', 'days': 365, 'label': '1 year'},
)

# Time to delivery of the simulated contract. Each one is a different curve: the short
# factor has decayed further by the time a deferred contract is reached, so the same
# parameters give it a visibly quieter path than the prompt.
TENORS = (
    {'id': '0.25', 'years': 0.25, 'label': '3M'},
    {'id': '0.5', 'years': 0.5, 'label': '6M'},
    {'id': '1.0', 'years': 1.0, 'label': '1Y'},
    {'id': '2.0', 'years': 2.0, 'label': '2Y'},
    {'id': '3.0', 'years': 3.0, 'label': '3Y'},
)

# Starting points that carry the character of each market rather than any quoted curve:
# gas breathes far harder at the front and pulls back much faster than oil.
CURVE_PRESETS = (
    {
        'id': 'oil',
        'label': 'Oil',
        'level': 65.0,
        'mean_reversion': 1.5,
        'short_vol': 0.55,
        'long_vol': 0.35,
        'factor_correlation': 0.0,
    },
    {
        'id': 'gas',
        'label': 'Gas',
        'level': 3.50,
        'mean_reversion': 4.5,
        'short_vol': 0.85,
        'long_vol': 0.32,
        'factor_correlation': 0.85,
    },
)

_DEFAULTS = {
    'asset_class': 'equity',
    'model': 'gbm',
    'spot': 100.0,
    'rate': 0.04,
    'div': 0.0,
    'vol': 0.20,
    'n_paths': 30,
    'seed': 42,
    'horizon_days': 90,
    'n_intervals': 48,
    'curve': 'oil',
    'tenor_years': 1.0,
    'commodity_paths': 4000,
}
_MAX_PATHS = 100
_MAX_COMMODITY_PATHS = 20000
_MAX_DRAWN_PATHS = 50
_ALLOWED_HORIZONS = {h['days'] for h in HORIZONS}
_ALLOWED_TENORS = {t['years'] for t in TENORS}
_PRESET_BY_ID = {p['id']: p for p in CURVE_PRESETS}
_MODEL_BY_ID = {m['id']: m for m in SIM_MODELS}


def _try_import_cpp():
    try:
        import numeraire_cpp  # type: ignore

        return numeraire_cpp
    except ImportError:
        return None


def _parse_int(get, key: str, default: int, *, lo: int, hi: int) -> int:
    raw = get.get(key)
    if raw is None or str(raw).strip() == '':
        return default
    try:
        v = int(float(raw))
    except (TypeError, ValueError):
        return default
    return max(lo, min(hi, v))


def _parse_float(get, key: str, default: float, *, lo: float, hi: float) -> float:
    raw = get.get(key)
    if raw is None or str(raw).strip() == '':
        return default
    try:
        v = float(raw)
    except (TypeError, ValueError):
        return default
    return max(lo, min(hi, v))


def resolve_sim_params(get) -> dict:
    asset_class = (get.get('asset_class') or _DEFAULTS['asset_class']).strip().lower()
    if asset_class not in {a['id'] for a in ASSET_CLASSES}:
        asset_class = str(_DEFAULTS['asset_class'])

    model = (get.get('model') or '').strip().lower()
    spec = _MODEL_BY_ID.get(model)
    if spec is None or spec['asset_class'] != asset_class:
        # Switching asset class leaves a model behind that no longer belongs to it; fall
        # back to the first one offered rather than simulating something unasked for.
        spec = next(m for m in SIM_MODELS if m['asset_class'] == asset_class)
        model = spec['id']

    horizon = _parse_int(
        get, 'horizon_days', int(_DEFAULTS['horizon_days']), lo=7, hi=730
    )
    if horizon not in _ALLOWED_HORIZONS:
        horizon = int(_DEFAULTS['horizon_days'])

    params = {
        'asset_class': asset_class,
        'model': model,
        'horizon_days': horizon,
        'n_intervals': int(_DEFAULTS['n_intervals']),
        'seed': _parse_int(get, 'seed', int(_DEFAULTS['seed']), lo=0, hi=2_147_483_647),
        'run': (get.get('run') or '').strip() in {'1', 'true', 'yes'},
    }

    if asset_class == 'commodity':
        params.update(_resolve_commodity_params(get, horizon))
    else:
        params.update(
            {
                'spot': max(_parse_float(get, 'spot', float(_DEFAULTS['spot']), lo=1e-8, hi=1e9), 1e-8),
                'rate': _parse_float(get, 'rate', float(_DEFAULTS['rate']), lo=-1.0, hi=1.0),
                'div': _parse_float(get, 'div', float(_DEFAULTS['div']), lo=-1.0, hi=1.0),
                'vol': _parse_float(get, 'vol', float(_DEFAULTS['vol']), lo=0.0, hi=5.0),
                'n_paths': _parse_int(
                    get, 'n_paths', int(_DEFAULTS['n_paths']), lo=1, hi=_MAX_PATHS
                ),
            }
        )
    return params


def _resolve_commodity_params(get, horizon_days: int) -> dict:
    curve = (get.get('curve') or _DEFAULTS['curve']).strip().lower()
    if curve not in _PRESET_BY_ID:
        curve = str(_DEFAULTS['curve'])
    preset = _PRESET_BY_ID[curve]

    # Picking a different market means picking its character, so the dynamics reset to
    # that preset; once it stops moving, whatever is in the boxes wins.
    switched = (get.get('preset_applied') or '').strip().lower() != curve
    if switched:
        dynamics = {
            'level': preset['level'],
            'mean_reversion': preset['mean_reversion'],
            'short_vol': preset['short_vol'],
            'long_vol': preset['long_vol'],
            'factor_correlation': preset['factor_correlation'],
        }
    else:
        dynamics = {
            'level': _parse_float(get, 'level', preset['level'], lo=1e-8, hi=1e9),
            'mean_reversion': _parse_float(
                get, 'mean_reversion', preset['mean_reversion'], lo=0.0, hi=50.0
            ),
            'short_vol': _parse_float(get, 'short_vol', preset['short_vol'], lo=0.0, hi=5.0),
            'long_vol': _parse_float(get, 'long_vol', preset['long_vol'], lo=0.0, hi=5.0),
            'factor_correlation': _parse_float(
                get, 'factor_correlation', preset['factor_correlation'], lo=-1.0, hi=1.0
            ),
        }

    tenor = _parse_float(
        get, 'tenor_years', float(_DEFAULTS['tenor_years']), lo=0.05, hi=10.0
    )
    if tenor not in _ALLOWED_TENORS:
        tenor = float(_DEFAULTS['tenor_years'])

    return {
        'curve': curve,
        'curve_label': preset['label'],
        'tenor_years': tenor,
        **dynamics,
        'n_paths': _parse_int(
            get, 'n_paths', int(_DEFAULTS['commodity_paths']), lo=100, hi=_MAX_COMMODITY_PATHS
        ),
        # A contract that delivers inside the horizon stops trading part-way through and
        # its path flatlines from there. Legitimate, but worth saying out loud.
        'settles_early': tenor < horizon_days / 365.0,
    }


def _stub_message(sim: dict) -> dict:
    return {
        'ok': False,
        'model': sim['model'],
        'message': f"Model '{_MODEL_BY_ID[sim['model']]['label']}' is a stub — "
        'GBM and Gabillon 2F are wired today.',
    }


def _simulate_gbm_cpp(sim: dict) -> dict:
    mod = _try_import_cpp()
    if mod is None or not hasattr(mod, 'simulate_paths'):
        return {
            'ok': False,
            'model': sim['model'],
            'message': 'C++ module `numeraire_cpp` missing simulate_paths '
            '(rebuild with NUMERAIRE_BUILD_PYTHON=ON).',
        }
    try:
        raw = mod.simulate_paths(
            model=str(sim['model']),
            spot=float(sim['spot']),
            rate=float(sim['rate']),
            div=float(sim['div']),
            vol=float(sim['vol']),
            n_paths=int(sim['n_paths']),
            seed=int(sim['seed']),
            horizon_days=int(sim['horizon_days']),
            n_intervals=int(sim['n_intervals']),
        )
    except Exception as exc:  # noqa: BLE001
        return {'ok': False, 'model': sim['model'], 'message': f'C++ simulate error: {exc}'}

    paths = [list(p) for p in (raw.get('paths') or [])]
    mean_path: list[float] = []
    if paths:
        n_steps = len(paths[0])
        for j in range(n_steps):
            mean_path.append(sum(p[j] for p in paths) / len(paths))

    return {
        'ok': True,
        'model': raw.get('model', sim['model']),
        'engine': raw.get('engine', ''),
        'grid_name': raw.get('grid_name', 'uniform_lab'),
        'horizon_days': raw.get('horizon_days', sim['horizon_days']),
        'n_paths': raw.get('n_paths'),
        'n_drawn': len(paths),
        'n_steps': raw.get('n_steps'),
        'seed': raw.get('seed'),
        'times': list(raw.get('times') or []),
        'days': list(raw.get('days') or []),
        'paths': paths,
        'mean_path': mean_path,
        'y_label': 'S',
        'message': '',
    }


def _simulate_gabillon_cpp(sim: dict) -> dict:
    mod = _try_import_cpp()
    if mod is None or not hasattr(mod, 'simulate_gabillon_curve'):
        return {
            'ok': False,
            'model': sim['model'],
            'message': 'C++ module `numeraire_cpp` missing simulate_gabillon_curve '
            '(rebuild with NUMERAIRE_BUILD_PYTHON=ON).',
        }
    label = f"{sim['curve_label']} {sim['tenor_years']:g}Y"
    try:
        raw = mod.simulate_gabillon_curve(
            tickers=[label],
            settlement_years=[float(sim['tenor_years'])],
            anchor_prices=[float(sim['level'])],
            mean_reversion=float(sim['mean_reversion']),
            short_vol=float(sim['short_vol']),
            long_vol=float(sim['long_vol']),
            factor_correlation=float(sim['factor_correlation']),
            n_paths=int(sim['n_paths']),
            seed=int(sim['seed']),
            horizon_days=int(sim['horizon_days']),
            n_intervals=int(sim['n_intervals']),
            fan_contract=0,
            fan_paths=_MAX_DRAWN_PATHS,
        )
    except Exception as exc:  # noqa: BLE001
        return {'ok': False, 'model': sim['model'], 'message': f'C++ simulate error: {exc}'}

    stat = (raw.get('contracts') or [{}])[0]
    paths = [list(p) for p in (raw.get('fan') or [])]
    return {
        'ok': True,
        'model': raw.get('model', sim['model']),
        'engine': raw.get('engine', ''),
        'grid_name': raw.get('grid_name', 'uniform_lab'),
        'horizon_days': raw.get('horizon_days', sim['horizon_days']),
        'n_paths': raw.get('n_paths'),
        'n_drawn': len(paths),
        'n_steps': raw.get('n_steps'),
        'seed': raw.get('seed'),
        'times': list(raw.get('times') or []),
        'days': [],
        'paths': paths,
        'mean_path': list(raw.get('mean_path') or []),
        'y_label': 'F',
        'contract_label': label,
        # Realized against model vol is the check that the paths really carry the
        # parameters; the drift is the check that the futures price stayed a martingale.
        'realized_vol': stat.get('realized_vol'),
        'model_vol': stat.get('model_vol'),
        'p5': stat.get('p5'),
        'p50': stat.get('p50'),
        'p95': stat.get('p95'),
        'diffusion_years': stat.get('diffusion_years'),
        'worst_martingale_drift': raw.get('worst_martingale_drift'),
        'message': '',
    }


def _simulate_cpp(sim: dict) -> dict:
    spec = _MODEL_BY_ID[sim['model']]
    if not spec['wired']:
        return _stub_message(sim)
    if sim['model'] == 'gabillon':
        return _simulate_gabillon_cpp(sim)
    return _simulate_gbm_cpp(sim)


def build_simulation_lab(get) -> dict:
    sim = resolve_sim_params(get)
    result = _simulate_cpp(sim) if sim['run'] else None
    return {
        'asset_classes': ASSET_CLASSES,
        'sim_models': [m for m in SIM_MODELS if m['asset_class'] == sim['asset_class']],
        'horizons': HORIZONS,
        'tenors': TENORS,
        'curve_presets': CURVE_PRESETS,
        'sim': sim,
        'sim_result': result,
        'show_sim_chart': bool(result and result.get('ok') and result.get('paths')),
    }
