from datetime import date as date_cls

from django.contrib.auth.decorators import login_not_required
from django.db import OperationalError
from django.db.models import Count, Max, Prefetch, Sum
from django.http import Http404
from django.utils.decorators import method_decorator
from django.views.generic import DetailView, ListView, TemplateView

from journal.curves import (
    curve_discount_for_maturity,
    discount_factor_from_zero,
    list_curve_as_of,
    list_curve_ids,
    load_curve_snapshot,
    nearest_curve_as_of,
)
from journal.exposure import (
    list_exposure_as_of,
    list_exposure_portfolios,
    portfolio_exposure_profile,
    trade_exposure_profile,
)
from journal.quant_lab import build_quant_lab
from journal.simulation_lab import build_simulation_lab
from journal.inventory import is_priceable, pricing_notes
from journal.market import list_underliers, resolve_underlier
from journal.payoff import build_trade_payoff_chart
from journal.models import (
    CatalogInstrumentType,
    DiscountCurveEod,
    Product,
    ProductEquity,
    Trade,
    TradeLeg,
    TradeLegExposureEod,
    TradeLegMtmEod,
    VolSurfaceEod,
)
from journal.surfaces import (
    list_surface_as_of,
    list_surface_underlyings,
    load_surface_snapshot,
    nearest_surface_as_of,
)
from journal.whatif import (
    baseline_inputs_from_mtm,
    capabilities_for_equity,
    inputs_are_shocked,
    merge_trade_capabilities,
    parse_whatif_inputs,
    run_trade_whatif,
)
from journal.rates_conversion_lab import build_rates_conversion_lab


def _parse_as_of(raw: str, available: list) -> date_cls | None:
    raw = (raw or '').strip()
    if not raw:
        return None
    try:
        y, m, d = (int(x) for x in raw.split('-', 2))
        candidate = date_cls(y, m, d)
    except (TypeError, ValueError):
        return None
    if candidate in available:
        return candidate
    return None


def _remark_flags(remarks: str | None) -> list[str]:
    if not remarks:
        return []
    return [part.strip() for part in remarks.split(';') if part.strip()]


class DashboardView(TemplateView):
    """Landing page: book snapshot for a selected (or latest) as_of date."""

    template_name = 'journal/dashboard.html'

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)
        try:
            context.update(self._batch_summary())
        except OperationalError as exc:
            # The batch database is owned by the C++ side and may not exist yet on
            # a fresh checkout; show the reason instead of a 500.
            context['db_error'] = str(exc)
        return context

    def _batch_summary(self):
        available_as_of = list(
            TradeLegMtmEod.objects.order_by()
            .values_list('as_of', flat=True)
            .distinct()
            .order_by('-as_of')
        )

        requested = self.request.GET.get('as_of', '').strip()
        as_of = _parse_as_of(requested, available_as_of)
        if as_of is None and available_as_of:
            as_of = available_as_of[0]

        mtm = TradeLegMtmEod.objects.filter(as_of=as_of) if as_of else None

        exposure_qs = TradeLegExposureEod.objects.filter(as_of=as_of) if as_of else TradeLegExposureEod.objects.none()
        surfaces_qs = (
            VolSurfaceEod.objects.filter(as_of=as_of).order_by('underlying_id')
            if as_of
            else VolSurfaceEod.objects.none()
        )
        # Curves often lag the book MTM (provider delay). Prefer exact as_of;
        # otherwise show the newest USD curve on or before the mark day.
        curves: list = []
        curves_meta = None
        if as_of is not None:
            exact = list(
                DiscountCurveEod.objects.filter(as_of=as_of).order_by('curve_id')[:20]
            )
            if exact:
                curves = exact
                curves_meta = {
                    'curve_as_of': as_of,
                    'lag_days': 0,
                    'stale': False,
                }
            else:
                preferred = 'USD_TREASURY_PAR_FRED'
                picked = nearest_curve_as_of(as_of, preferred)
                if picked is None:
                    picked = nearest_curve_as_of(as_of)
                if picked is not None:
                    _cid, curve_as_of = picked
                    curves = list(
                        DiscountCurveEod.objects.filter(as_of=curve_as_of)
                        .order_by('curve_id')[:20]
                    )
                    lag_days = (as_of - curve_as_of).days
                    curves_meta = {
                        'curve_as_of': curve_as_of,
                        'lag_days': lag_days,
                        'stale': lag_days > 0,
                    }

        return {
            'as_of': as_of,
            'available_as_of': available_as_of,
            'mtm': mtm.aggregate(
                legs=Count('pk'),
                pv_total=Sum('pv_total'),
                pnl_daily=Sum('pnl_daily'),
                delta_total=Sum('delta_total'),
                vega_total=Sum('vega_total'),
                theta_total=Sum('theta_total'),
            ) if mtm is not None else {},
            'engines': self._engines_breakdown(mtm) if mtm is not None else [],
            'trade_status': list(
                Trade.objects.order_by()
                .values('status')
                .annotate(count=Count('trade_id'))
                .order_by('status')
            ),
            'exposure': exposure_qs.aggregate(
                as_of=Max('as_of'),
                rows=Count('pk'),
                paths=Max('num_paths'),
            ),
            'surfaces': list(surfaces_qs[:20]),
            'curves': curves,
            'curves_meta': curves_meta,
        }

    @staticmethod
    def _engines_breakdown(mtm):
        """Per-engine legs + PV for the selected as_of; share is by leg count."""
        rows = list(
            mtm.order_by()
            .values('pricing_engine')
            .annotate(legs=Count('pk'), pv_total=Sum('pv_total'))
            .order_by('-legs', 'pricing_engine')
        )
        total_legs = sum(int(r['legs'] or 0) for r in rows)
        for row in rows:
            legs = int(row['legs'] or 0)
            row['share'] = (100.0 * legs / total_legs) if total_legs else 0.0
        return rows


class TradeListView(ListView):
    """Catalog trades with an optional status filter (LIVE / EXPIRED / …)."""

    model = Trade
    template_name = 'journal/trade_list.html'
    context_object_name = 'trades'
    paginate_by = 50

    def get_queryset(self):
        qs = (
            Trade.objects.annotate(leg_count=Count('legs'))
            .order_by('-trade_date', 'trade_id')
        )
        status = self.request.GET.get('status', '').strip().upper()
        if status and status != 'ALL':
            qs = qs.filter(status=status)
        return qs

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)
        context['status_filter'] = self.request.GET.get('status', 'ALL').strip().upper() or 'ALL'
        # Reset ordering before distinct so Meta.ordering does not inflate the set.
        context['status_choices'] = list(
            Trade.objects.order_by().values_list('status', flat=True).distinct()
        )
        return context


class TradeDetailView(DetailView):
    """One trade: definition, market inputs @ as_of, valuation, MTM history."""

    model = Trade
    template_name = 'journal/trade_detail.html'
    context_object_name = 'trade'
    slug_field = 'trade_id'
    slug_url_kwarg = 'trade_id'

    def get_queryset(self):
        return Trade.objects.prefetch_related(
            Prefetch(
                'legs',
                queryset=TradeLeg.objects.select_related('product', 'product__equity'),
            )
        )

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)
        trade = context['trade']
        legs = list(trade.legs.all())

        available_as_of = list(
            TradeLegMtmEod.objects.filter(trade=trade)
            .order_by()
            .values_list('as_of', flat=True)
            .distinct()
            .order_by('-as_of')
        )
        as_of = _parse_as_of(self.request.GET.get('as_of', ''), available_as_of)
        if as_of is None and available_as_of:
            as_of = available_as_of[0]

        mtm_by_leg = {}
        if as_of is not None:
            for row in TradeLegMtmEod.objects.filter(trade=trade, as_of=as_of):
                mtm_by_leg[row.leg_id] = row

        market_rows = []
        for leg in legs:
            equity = getattr(leg.product, 'equity', None)
            mtm = mtm_by_leg.get(leg.leg_id)
            underlier = leg.product.underlying_id
            curve_df = None
            flat_df = None
            if mtm is not None:
                flat_df = discount_factor_from_zero(mtm.risk_free_rate, mtm.years_to_maturity)
                curve_df = curve_discount_for_maturity(as_of, mtm.years_to_maturity)
            market_rows.append(
                {
                    'leg': leg,
                    'equity': equity,
                    'mtm': mtm,
                    'underlier': underlier,
                    'underlier_url_ticker': underlier,
                    'remark_flags': _remark_flags(getattr(mtm, 'remarks', None)),
                    'flat_df': flat_df,
                    'curve_df': curve_df,
                }
            )

        valuation = {}
        if as_of is not None and mtm_by_leg:
            valuation = (
                TradeLegMtmEod.objects.filter(trade=trade, as_of=as_of).aggregate(
                    pv_total=Sum('pv_total'),
                    pnl_daily=Sum('pnl_daily'),
                    pnl_inception=Sum('pnl_inception'),
                    delta_total=Sum('delta_total'),
                    vega_total=Sum('vega_total'),
                    theta_total=Sum('theta_total'),
                )
            )

        mtm_history = list(
            TradeLegMtmEod.objects.filter(trade=trade)
            .values('as_of')
            .annotate(
                legs=Count('pk'),
                pv_total=Sum('pv_total'),
                pnl_daily=Sum('pnl_daily'),
                pnl_inception=Sum('pnl_inception'),
                delta_total=Sum('delta_total'),
                vega_total=Sum('vega_total'),
                theta_total=Sum('theta_total'),
            )
            .order_by('-as_of')
        )

        # Charts: primary underlier close + trade PV path (chronological).
        primary = next((r for r in market_rows if r['mtm'] is not None), market_rows[0] if market_rows else None)
        underlier_chart = []
        underlier_chart_ticker = None
        if primary is not None:
            resolved = resolve_underlier(primary['underlier'])
            if resolved is not None:
                _kind, underlier_chart_ticker, bars_qs = resolved
                underlier_chart = [
                    {'date': d.isoformat(), 'close': close}
                    for d, close in bars_qs.order_by('as_of').values_list('as_of', 'close')
                ]

        pv_chart = [
            {'date': row['as_of'].isoformat(), 'pv': row['pv_total'], 'pnl': row['pnl_daily']}
            for row in reversed(mtm_history)
        ]

        # Yield / DF pillars for the curve actually used on this mark day (may lag).
        curve_chart = []
        curve_meta = None
        if as_of is not None:
            picked = nearest_curve_as_of(as_of)
            if picked is not None:
                cid, curve_as_of = picked
                snap = load_curve_snapshot(cid, curve_as_of)
                if snap is not None:
                    curve_chart = snap['chart']
                    lag_days = (as_of - curve_as_of).days
                    curve_meta = {
                        'curve_id': cid,
                        'curve_as_of': curve_as_of,
                        'lag_days': lag_days,
                        'stale': lag_days > 0,
                        'pillar_count': len(curve_chart),
                    }

        # Trade EE/PFE if an exposure batch exists (sparse history).
        trade_exposure_chart = []
        exposure_meta = None
        exp_dates = list(
            TradeLegExposureEod.objects.filter(trade=trade)
            .order_by()
            .values_list('as_of', flat=True)
            .distinct()
            .order_by('-as_of')
        )
        exp_as_of = as_of if as_of in exp_dates else (exp_dates[0] if exp_dates else None)
        if exp_as_of is not None:
            exp = trade_exposure_profile(trade.trade_id, exp_as_of)
            if exp is not None:
                trade_exposure_chart = exp['chart']
                lag = (as_of - exp_as_of).days if as_of is not None else None
                exposure_meta = {
                    'as_of': exp_as_of,
                    'stale': bool(as_of and exp_as_of != as_of),
                    'lag_days': lag if lag is not None and lag > 0 else 0,
                }

        # Vol surface 3D for the primary underlier (nearest surface day ≤ mark).
        vol_chart = []
        vol_meta = None
        if primary is not None and as_of is not None:
            und = primary['underlier']
            surf_as_of = nearest_surface_as_of(und, as_of)
            if surf_as_of is not None:
                snap = load_surface_snapshot(und, surf_as_of, contract_type='call')
                if snap is not None and snap['chart']:
                    vol_chart = snap['chart']
                    lag = (as_of - surf_as_of).days
                    vol_meta = {
                        'underlying_id': und,
                        'as_of': surf_as_of,
                        'spot': snap['header'].spot_used,
                        'point_count': len(vol_chart),
                        'stale': lag > 0,
                        'lag_days': lag,
                        'x_label': snap['x_label'],
                    }

        # Terminal payoff @ expiry (vanilla / binary) — educational, beside vol.
        ref_spot = None
        if primary is not None and primary.get('mtm') is not None:
            ref_spot = float(primary['mtm'].underlying_spot)
        payoff_chart = build_trade_payoff_chart(market_rows, ref_spot=ref_spot)

        # Sandbox what-if: Python engines from MTM inputs — never writes to SQLite.
        whatif_ctx = None
        if primary is not None and primary.get('mtm') is not None:
            baseline = baseline_inputs_from_mtm(primary['mtm'])
            if baseline is not None:
                trade_caps = merge_trade_capabilities(
                    [capabilities_for_equity(row['equity']) for row in market_rows]
                )
                shocked = parse_whatif_inputs(
                    self.request.GET, baseline, caps=trade_caps
                )
                result = run_trade_whatif(market_rows, shocked, caps=trade_caps)
                replay = run_trade_whatif(market_rows, baseline, caps=trade_caps)
                replay_gap = None
                if (
                    replay.get('whatif_pv_total') is not None
                    and replay.get('baseline_pv_total') is not None
                ):
                    replay_gap = replay['whatif_pv_total'] - replay['baseline_pv_total']
                whatif_ctx = {
                    'baseline': baseline,
                    'inputs': shocked,
                    'result': result,
                    'caps': trade_caps,
                    'replay_gap': replay_gap,
                    'active': inputs_are_shocked(baseline, shocked, trade_caps),
                }

        context.update(
            {
                'as_of': as_of,
                'available_as_of': available_as_of,
                'latest_as_of': available_as_of[0] if available_as_of else None,
                'leg_rows': market_rows,
                'market_rows': market_rows,
                'primary_market': primary,
                'valuation': valuation,
                'mtm_history': mtm_history,
                'underlier_chart': underlier_chart,
                'underlier_chart_ticker': underlier_chart_ticker,
                'pv_chart': pv_chart,
                'curve_chart': curve_chart,
                'curve_meta': curve_meta,
                'exposure_chart': trade_exposure_chart,
                'exposure_meta': exposure_meta,
                'vol_chart': vol_chart,
                'vol_meta': vol_meta,
                'payoff_chart': payoff_chart,
                'whatif': whatif_ctx,
            }
        )
        return context


class UnderlierListView(TemplateView):
    """Catalog of equity / index daily closes available in the batch DB."""

    template_name = 'journal/underlier_list.html'

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)
        try:
            underliers = list_underliers()
            book_underlyings = set(
                Product.objects.order_by()
                .values_list('underlying_id', flat=True)
                .distinct()
            )
            for row in underliers:
                # Highlight tickers that appear on book products (NDX ↔ I:NDX).
                bare = row['ticker'].removeprefix('I:')
                row['in_book'] = (
                    row['ticker'] in book_underlyings
                    or bare in book_underlyings
                )
            context['underliers'] = underliers
            context['equity_count'] = sum(1 for r in underliers if r['kind'] == 'equity')
            context['index_count'] = sum(1 for r in underliers if r['kind'] == 'index')
        except OperationalError as exc:
            context['db_error'] = str(exc)
            context['underliers'] = []
            context['equity_count'] = 0
            context['index_count'] = 0
        return context


class UnderlierDetailView(ListView):
    """Historical daily OHLC for one underlier ticker."""

    template_name = 'journal/underlier_detail.html'
    context_object_name = 'bars'
    paginate_by = 60

    def setup(self, request, *args, **kwargs):
        super().setup(request, *args, **kwargs)
        resolved = resolve_underlier(kwargs.get('ticker', ''))
        if resolved is None:
            raise Http404(f"No daily bars for ticker {kwargs.get('ticker')!r}")
        self.kind, self.canonical_ticker, self._bars_qs = resolved

    def get_queryset(self):
        return self._bars_qs

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)
        latest = self._bars_qs.first()
        # Chronological series for the chart (table stays newest-first via pagination).
        chart_rows = list(
            self._bars_qs.order_by('as_of').values('as_of', 'close', 'volume')
        )
        chart_series = [
            {
                'date': row['as_of'].isoformat(),
                'close': row['close'],
                'volume': row['volume'],
            }
            for row in chart_rows
        ]
        default_start = None
        if chart_rows:
            from datetime import timedelta

            cutoff = chart_rows[-1]['as_of'] - timedelta(days=365)
            for row in chart_rows:
                if row['as_of'] >= cutoff:
                    default_start = row['as_of'].isoformat()
                    break
            if default_start is None:
                default_start = chart_rows[0]['as_of'].isoformat()

        context.update(
            {
                'kind': self.kind,
                'ticker': self.canonical_ticker,
                'requested_ticker': self.kwargs.get('ticker'),
                'latest': latest,
                'bar_count': self._bars_qs.count(),
                'chart_series': chart_series,
                'chart_default_start': default_start,
            }
        )
        return context


class CurveView(TemplateView):
    """Yield / discount curves for a selected curve as_of (may lag book MTM)."""

    template_name = 'journal/curve_detail.html'

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)
        try:
            curve_ids = list_curve_ids()
            requested_curve = self.request.GET.get('curve_id', '').strip()
            curve_id = requested_curve if requested_curve in curve_ids else (
                'USD_TREASURY_PAR_FRED' if 'USD_TREASURY_PAR_FRED' in curve_ids
                else (curve_ids[0] if curve_ids else None)
            )

            available_as_of = list_curve_as_of(curve_id) if curve_id else []
            as_of = _parse_as_of(self.request.GET.get('as_of', ''), available_as_of)
            if as_of is None and available_as_of:
                as_of = available_as_of[0]

            snapshot = load_curve_snapshot(curve_id, as_of) if curve_id and as_of else None

            latest_mtm = (
                TradeLegMtmEod.objects.order_by()
                .values_list('as_of', flat=True)
                .distinct()
                .order_by('-as_of')
                .first()
            )
            lag_days = None
            if as_of is not None and latest_mtm is not None:
                lag_days = (latest_mtm - as_of).days

            context.update(
                {
                    'curve_ids': curve_ids,
                    'curve_id': curve_id,
                    'as_of': as_of,
                    'available_as_of': available_as_of,
                    'snapshot': snapshot,
                    'curve_chart': snapshot['chart'] if snapshot else [],
                    'first_curve_as_of': available_as_of[-1] if available_as_of else None,
                    'latest_curve_as_of': available_as_of[0] if available_as_of else None,
                    'latest_mtm_as_of': latest_mtm,
                    'lag_days': lag_days,
                }
            )
        except OperationalError as exc:
            context['db_error'] = str(exc)
            context.update(
                {
                    'curve_ids': [],
                    'curve_id': None,
                    'as_of': None,
                    'available_as_of': [],
                    'snapshot': None,
                    'curve_chart': [],
                    'first_curve_as_of': None,
                    'latest_curve_as_of': None,
                    'latest_mtm_as_of': None,
                    'lag_days': None,
                }
            )
        return context


class ExposureView(TemplateView):
    """Portfolio EE / PFE profile — sparse; not available for every MTM day."""

    template_name = 'journal/exposure_detail.html'

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)
        try:
            portfolios = list_exposure_portfolios()
            requested = self.request.GET.get('portfolio_id', '').strip()
            portfolio_id = requested if requested in portfolios else (
                portfolios[0] if portfolios else None
            )
            available_as_of = list_exposure_as_of(portfolio_id) if portfolio_id else []
            as_of = _parse_as_of(self.request.GET.get('as_of', ''), available_as_of)
            if as_of is None and available_as_of:
                as_of = available_as_of[0]

            profile = (
                portfolio_exposure_profile(as_of, portfolio_id)
                if portfolio_id and as_of
                else None
            )

            latest_mtm = (
                TradeLegMtmEod.objects.order_by()
                .values_list('as_of', flat=True)
                .distinct()
                .order_by('-as_of')
                .first()
            )
            lag_days = None
            if as_of is not None and latest_mtm is not None:
                lag_days = (latest_mtm - as_of).days

            context.update(
                {
                    'portfolios': portfolios,
                    'portfolio_id': portfolio_id,
                    'as_of': as_of,
                    'available_as_of': available_as_of,
                    'profile': profile,
                    'exposure_chart': profile['chart'] if profile else [],
                    'first_exposure_as_of': available_as_of[-1] if available_as_of else None,
                    'latest_exposure_as_of': available_as_of[0] if available_as_of else None,
                    'latest_mtm_as_of': latest_mtm,
                    'lag_days': lag_days,
                }
            )
        except OperationalError as exc:
            context['db_error'] = str(exc)
            context.update(
                {
                    'portfolios': [],
                    'portfolio_id': None,
                    'as_of': None,
                    'available_as_of': [],
                    'profile': None,
                    'exposure_chart': [],
                    'first_exposure_as_of': None,
                    'latest_exposure_as_of': None,
                    'latest_mtm_as_of': None,
                    'lag_days': None,
                }
            )
        return context


class SurfaceView(TemplateView):
    """Implied vol surface — 3D scatter in ln(K/S) × τ × IV."""

    template_name = 'journal/surface_detail.html'

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)
        try:
            underlyings = list_surface_underlyings()
            requested_u = self.request.GET.get('underlying_id', '').strip()
            underlying_id = requested_u if requested_u in underlyings else (
                underlyings[0] if underlyings else None
            )

            available_as_of = list_surface_as_of(underlying_id) if underlying_id else []
            as_of = _parse_as_of(self.request.GET.get('as_of', ''), available_as_of)
            if as_of is None and available_as_of:
                as_of = available_as_of[0]

            ctype = self.request.GET.get('contract_type', 'call').strip().lower()
            if ctype not in ('call', 'put', 'all'):
                ctype = 'call'
            contract_filter = None if ctype == 'all' else ctype

            snapshot = (
                load_surface_snapshot(
                    underlying_id, as_of, contract_type=contract_filter
                )
                if underlying_id and as_of
                else None
            )

            context.update(
                {
                    'underlyings': underlyings,
                    'underlying_id': underlying_id,
                    'as_of': as_of,
                    'available_as_of': available_as_of,
                    'contract_type': ctype,
                    'snapshot': snapshot,
                    'vol_chart': snapshot['chart'] if snapshot else [],
                    'first_surface_as_of': available_as_of[-1] if available_as_of else None,
                    'latest_surface_as_of': available_as_of[0] if available_as_of else None,
                }
            )
        except OperationalError as exc:
            context['db_error'] = str(exc)
            context.update(
                {
                    'underlyings': [],
                    'underlying_id': None,
                    'as_of': None,
                    'available_as_of': [],
                    'contract_type': 'call',
                    'snapshot': None,
                    'vol_chart': [],
                    'first_surface_as_of': None,
                    'latest_surface_as_of': None,
                }
            )
        return context


class InventoryView(TemplateView):
    """Catalog of instrument types vs what the C++ ProductFactory can price."""

    template_name = 'journal/inventory.html'

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)
        try:
            book_counts = {
                row['instrument_type']: row['n']
                for row in ProductEquity.objects.order_by()
                .values('instrument_type')
                .annotate(n=Count('product_id'))
            }
            rows = []
            for cat in CatalogInstrumentType.objects.all():
                maps = cat.maps_to_instrument_type
                rows.append(
                    {
                        'cat': cat,
                        'priceable': is_priceable(maps),
                        'notes': pricing_notes(maps),
                        'book_count': book_counts.get(maps, 0),
                    }
                )
            context['rows'] = rows
            context['priceable_count'] = sum(1 for r in rows if r['priceable'])
            context['catalog_count'] = len(rows)
            context['book_product_count'] = sum(book_counts.values())
        except OperationalError as exc:
            context['db_error'] = str(exc)
            context['rows'] = []
            context['priceable_count'] = 0
            context['catalog_count'] = 0
            context['book_product_count'] = 0
        return context


@method_decorator(login_not_required, name='dispatch')
class AboutView(TemplateView):
    """Public about page — reachable before sign-in."""

    template_name = 'journal/about.html'


class QuantLabHubView(TemplateView):
    """Quant Lab landing — pick Pricing or Simulation workspace."""

    template_name = 'journal/quant_lab_hub.html'


class QuantLabView(TemplateView):
    """Pricing & sensitivities sandbox (C++ pricers). Nothing persisted."""

    template_name = 'journal/quant_lab.html'

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)
        context.update(build_quant_lab(self.request.GET))
        return context


class SimulationLabView(TemplateView):
    """Simulation workspace — risk-factor paths on the production exposure grid."""

    template_name = 'journal/simulation_lab.html'

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)
        context.update(build_simulation_lab(self.request.GET))
        return context


class RatesConversionLabView(TemplateView):
    """Rates conversion sandbox (continuous zero ↔ DF). Nothing persisted."""

    template_name = 'journal/interest_rate_conversion.html'

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)
        context.update(build_rates_conversion_lab(self.request.GET))
        return context
