"""Book / underlier calibration snapshots for the Risk journal."""

from __future__ import annotations

from collections import defaultdict
from datetime import date
from typing import Any

from journal.models import (
    CalibrationCorrelation,
    CalibrationFactor,
    CalibrationParam,
    CalibrationSnapshot,
)

MODEL_LABELS = {
    'gbm': 'GBM',
    'gabillon_2f': 'Gabillon 2F',
}

SOURCE_LABELS = {
    'historical': 'historical EOD',
    'fit': 'curve fit',
    'implied': 'implied',
}

SCOPE_LABELS = {
    'book': 'book',
    'underlying': 'underlying',
}

_PARAM_LABELS = {
    'mean_reversion': 'Mean reversion κ',
    'factor_correlation': 'Short–long correlation',
    'vol_fit_rmse': 'Vol fit RMSE',
    'vol_fit_pillars': 'Vol fit pillars',
    'seasonal_sin_1': 'Seasonal sin 1y',
    'seasonal_cos_1': 'Seasonal cos 1y',
    'seasonal_sin_2': 'Seasonal sin 2y',
    'seasonal_cos_2': 'Seasonal cos 2y',
    'curve_short_level': 'Curve short level',
    'curve_long_level': 'Curve long level',
    'curve_mean_reversion': 'Curve κ',
    'curve_num_contracts': 'Contracts on strip',
    'curve_log_rmse': 'Curve log RMSE',
}

_PARAM_GROUP = {
    'mean_reversion': 'dynamics',
    'factor_correlation': 'dynamics',
    'vol_fit_rmse': 'fit',
    'vol_fit_pillars': 'fit',
    'seasonal_sin_1': 'seasonal',
    'seasonal_cos_1': 'seasonal',
    'seasonal_sin_2': 'seasonal',
    'seasonal_cos_2': 'seasonal',
    'curve_short_level': 'curve',
    'curve_long_level': 'curve',
    'curve_mean_reversion': 'curve',
    'curve_num_contracts': 'curve',
    'curve_log_rmse': 'curve',
}

_GROUP_ORDER = ('dynamics', 'curve', 'seasonal', 'fit', 'other')
_GROUP_LABELS = {
    'dynamics': 'Dynamics',
    'curve': 'Forward curve',
    'seasonal': 'Seasonality',
    'fit': 'Fit quality',
    'other': 'Other',
}


def model_label(code: str) -> str:
    return MODEL_LABELS.get(code, code)


def source_label(code: str) -> str:
    return SOURCE_LABELS.get(code, code)


def param_label(name: str) -> str:
    return _PARAM_LABELS.get(name, name.replace('_', ' '))


def param_group(name: str) -> str:
    return _PARAM_GROUP.get(name, 'other')


def _distinct(qs, field: str) -> list[str]:
    return list(
        qs.order_by().values_list(field, flat=True).distinct().order_by(field)
    )


def _parse_date(raw: str, available: list[date]) -> date | None:
    text = (raw or '').strip()
    if not text:
        return None
    try:
        wanted = date.fromisoformat(text)
    except ValueError:
        return None
    return wanted if wanted in available else None


def _parse_id(raw: str) -> int | None:
    text = (raw or '').strip()
    if not text:
        return None
    try:
        value = int(text)
    except ValueError:
        return None
    return value if value > 0 else None


def build_calibration_page(get) -> dict[str, Any]:
    """Cascade filters (book → model → source → as_of) and load one snapshot."""
    empty = {
        'snapshots': [],
        'snapshot': None,
        'detail': None,
        'scope_keys': [],
        'models': [],
        'sources': [],
        'available_as_of': [],
        'scope_key': None,
        'model': None,
        'source': None,
        'as_of': None,
        'model_options': [],
        'source_options': [],
        'model_label': '',
        'source_label': '',
        'param_view': None,
    }

    all_snaps = CalibrationSnapshot.objects.all()
    if not all_snaps.exists():
        return empty

    scope_keys = _distinct(all_snaps, 'scope_key')
    requested_id = _parse_id(get.get('calibration_id', ''))
    pinned = (
        CalibrationSnapshot.objects.filter(calibration_id=requested_id).first()
        if requested_id
        else None
    )

    scope_key = (get.get('scope_key') or '').strip()
    model = (get.get('model') or '').strip()
    source = (get.get('source') or '').strip()

    latest = all_snaps.order_by('-as_of', '-calibration_id').first()

    if pinned is not None and not (scope_key or model or source or get.get('as_of')):
        scope_key = pinned.scope_key
        model = pinned.model
        source = pinned.source

    if scope_key not in scope_keys:
        scope_key = (
            pinned.scope_key
            if pinned is not None
            else latest.scope_key
        )

    by_book = all_snaps.filter(scope_key=scope_key)
    models = _distinct(by_book, 'model')
    if model not in models:
        model = pinned.model if pinned is not None and pinned.scope_key == scope_key else models[0]

    by_model = by_book.filter(model=model)
    sources = _distinct(by_model, 'source')
    if source not in sources:
        source = (
            pinned.source
            if pinned is not None and pinned.scope_key == scope_key and pinned.model == model
            else sources[0]
        )

    filtered = by_model.filter(source=source).order_by('-as_of', '-calibration_id')
    available_as_of = list(
        filtered.order_by().values_list('as_of', flat=True).distinct().order_by('-as_of')
    )
    as_of = _parse_date(get.get('as_of', ''), available_as_of)
    if as_of is None and available_as_of:
        if pinned is not None and pinned.as_of in available_as_of:
            as_of = pinned.as_of
        else:
            as_of = available_as_of[0]

    snapshots = list(filtered.filter(as_of=as_of)) if as_of else []
    snapshot = None
    if pinned is not None and any(s.calibration_id == pinned.calibration_id for s in snapshots):
        snapshot = pinned
    elif snapshots:
        snapshot = snapshots[0]

    detail = load_snapshot_detail(snapshot) if snapshot is not None else None
    param_view = select_param_view(detail, get)

    return {
        'snapshots': snapshots,
        'snapshot': snapshot,
        'detail': detail,
        'param_view': param_view,
        'scope_keys': scope_keys,
        'models': models,
        'sources': sources,
        'available_as_of': available_as_of,
        'scope_key': scope_key,
        'model': model,
        'source': source,
        'as_of': as_of,
        'model_options': [{'code': m, 'label': model_label(m)} for m in models],
        'source_options': [{'code': s, 'label': source_label(s)} for s in sources],
        'model_label': model_label(model) if model else '',
        'source_label': source_label(source) if source else '',
    }


def load_snapshot_detail(snapshot: CalibrationSnapshot) -> dict[str, Any]:
    factors = list(
        CalibrationFactor.objects.filter(calibration_id=snapshot.calibration_id).order_by(
            'factor_index'
        )
    )
    raw_params = list(
        CalibrationParam.objects.filter(calibration_id=snapshot.calibration_id).order_by(
            'factor_id', 'param_name'
        )
    )

    by_curve: dict[str, dict[str, list[dict[str, Any]]]] = defaultdict(
        lambda: {g: [] for g in _GROUP_ORDER}
    )
    for row in raw_params:
        group = param_group(row.param_name)
        curve = row.factor_id or snapshot.scope_key
        by_curve[curve][group].append(
            {
                'name': row.param_name,
                'label': param_label(row.param_name),
                'value': row.param_value,
                'is_int': row.param_name.endswith('_pillars')
                or row.param_name.endswith('_contracts')
                or row.param_name == 'curve_num_contracts',
            }
        )

    param_blocks = []
    for curve, groups in sorted(by_curve.items()):
        sections = []
        for group in _GROUP_ORDER:
            rows = groups[group]
            if rows:
                sections.append({'key': group, 'label': _GROUP_LABELS[group], 'rows': rows})
        if sections:
            param_blocks.append({'curve': curve, 'sections': sections})

    labels = [f.factor_id for f in factors]
    index_of = {f.factor_index: i for i, f in enumerate(factors)}
    n = len(factors)
    matrix = [[None] * n for _ in range(n)]
    for i in range(n):
        matrix[i][i] = 1.0
    corr_rows_sql = CalibrationCorrelation.objects.filter(calibration_id=snapshot.calibration_id)
    for row in corr_rows_sql:
        ii = index_of.get(row.factor_i)
        jj = index_of.get(row.factor_j)
        if ii is None or jj is None:
            continue
        matrix[ii][jj] = row.rho
        matrix[jj][ii] = row.rho

    has_corr = any(matrix[i][j] is not None and i != j for i in range(n) for j in range(n))
    corr_rows = None
    if has_corr:
        corr_rows = [{'label': labels[i], 'values': matrix[i]} for i in range(n)]

    return {
        'factors': factors,
        'param_blocks': param_blocks,
        'corr_labels': labels,
        'corr_rows': corr_rows,
        'products': sorted({_product_from_factor(f.factor_id) for f in factors} - {''}),
    }


def select_param_view(detail: dict[str, Any] | None, get) -> dict[str, Any] | None:
    """Pick one underlier (and optionally one scalar) so a fat book stays readable."""
    if not detail or not detail.get('param_blocks'):
        return None

    blocks = detail['param_blocks']
    curves = [block['curve'] for block in blocks]
    curve = (get.get('param_curve') or '').strip()
    if curve not in curves:
        curve = curves[0]
    block = next(b for b in blocks if b['curve'] == curve)

    param_options = []
    for section in block['sections']:
        for row in section['rows']:
            param_options.append(
                {
                    'name': row['name'],
                    'label': row['label'],
                    'group': section['label'],
                }
            )

    names = {opt['name'] for opt in param_options}
    param_name = (get.get('param_name') or '').strip()
    if param_name not in names:
        param_name = ''

    selected = None
    if param_name:
        for section in block['sections']:
            for row in section['rows']:
                if row['name'] == param_name:
                    selected = {**row, 'group': section['label']}
                    break
            if selected is not None:
                break

    return {
        'curves': curves,
        'curve': curve,
        'block': block,
        'param_options': param_options,
        'param_name': param_name,
        'selected': selected,
    }


def _product_from_factor(factor_id: str) -> str:
    if not factor_id:
        return ''
    for suffix in ('_SHORT', '_LONG', '_M1', '_M2', '_M3', '_M4', '_M5', '_M6'):
        if factor_id.endswith(suffix):
            return factor_id[: -len(suffix)]
    return factor_id
