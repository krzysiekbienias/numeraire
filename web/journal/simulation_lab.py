"""Simulation Lab — path sandbox (separate from Quant Lab pricing).

Uniform toy time grid over a selectable horizon + C++ EvolveSingleFactorGbm.
Not the production CCR schedule. Nothing persisted. Non-GBM models are stubs.
"""

from __future__ import annotations

SIM_MODELS = (
    {'id': 'gbm', 'label': 'GBM', 'wired': True},
    {'id': 'bachelier', 'label': 'Bachelier', 'wired': False},
    {'id': 'hull_white', 'label': 'Hull–White', 'wired': False},
    {'id': 'heston', 'label': 'Heston', 'wired': False},
)

# Fixed lab horizons (days) — independent of production exposure pillars.
HORIZONS = (
    {'id': '14', 'days': 14, 'label': '2 weeks'},
    {'id': '30', 'days': 30, 'label': '1 month'},
    {'id': '90', 'days': 90, 'label': '3 months'},
    {'id': '180', 'days': 180, 'label': '6 months'},
    {'id': '365', 'days': 365, 'label': '1 year'},
)

_DEFAULTS = {
    'model': 'gbm',
    'spot': 100.0,
    'rate': 0.04,
    'div': 0.0,
    'vol': 0.20,
    'n_paths': 30,
    'seed': 42,
    'horizon_days': 90,
    'n_intervals': 48,
}
_MAX_PATHS = 100
_ALLOWED_HORIZONS = {h['days'] for h in HORIZONS}


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


def _parse_float(get, key: str, default: float) -> float:
    raw = get.get(key)
    if raw is None or str(raw).strip() == '':
        return default
    try:
        return float(raw)
    except (TypeError, ValueError):
        return default


def resolve_sim_params(get) -> dict:
    model = (get.get('model') or _DEFAULTS['model']).strip().lower()
    known = {m['id'] for m in SIM_MODELS}
    if model not in known:
        model = 'gbm'
    horizon = _parse_int(
        get, 'horizon_days', int(_DEFAULTS['horizon_days']), lo=7, hi=730
    )
    if horizon not in _ALLOWED_HORIZONS:
        horizon = int(_DEFAULTS['horizon_days'])
    return {
        'model': model,
        'spot': max(_parse_float(get, 'spot', float(_DEFAULTS['spot'])), 1e-8),
        'rate': _parse_float(get, 'rate', float(_DEFAULTS['rate'])),
        'div': _parse_float(get, 'div', float(_DEFAULTS['div'])),
        'vol': max(_parse_float(get, 'vol', float(_DEFAULTS['vol'])), 0.0),
        'n_paths': _parse_int(get, 'n_paths', int(_DEFAULTS['n_paths']), lo=1, hi=_MAX_PATHS),
        'seed': _parse_int(get, 'seed', int(_DEFAULTS['seed']), lo=0, hi=2_147_483_647),
        'horizon_days': horizon,
        'n_intervals': int(_DEFAULTS['n_intervals']),
        'run': (get.get('run') or '').strip() in {'1', 'true', 'yes'},
    }


def _simulate_paths_cpp(sim: dict) -> dict:
    wired = next((m for m in SIM_MODELS if m['id'] == sim['model']), None)
    if wired is None or not wired['wired']:
        return {
            'ok': False,
            'model': sim['model'],
            'message': f"Model '{sim['model']}' is a stub — only GBM is wired today.",
        }
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
        'n_steps': raw.get('n_steps'),
        'seed': raw.get('seed'),
        'times': list(raw.get('times') or []),
        'days': list(raw.get('days') or []),
        'paths': paths,
        'mean_path': mean_path,
        'message': '',
    }


def build_simulation_lab(get) -> dict:
    sim = resolve_sim_params(get)
    result = _simulate_paths_cpp(sim) if sim['run'] else None
    return {
        'sim_models': SIM_MODELS,
        'horizons': HORIZONS,
        'sim': sim,
        'sim_result': result,
        'show_sim_chart': bool(result and result.get('ok') and result.get('paths')),
    }
