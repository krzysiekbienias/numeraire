"""Browse-only admin over the C++ batch tables.

Nothing here may write: the schema belongs to `sql/schema_v1.sql` and the numbers
are produced by `dev_main`. Every ModelAdmin therefore denies add/change/delete
and exposes list filters instead.
"""

from django.contrib import admin

from journal import models


class ReadOnlyAdmin(admin.ModelAdmin):
    """Deny every mutation; detail pages still render through the view permission."""

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False

    def has_delete_permission(self, request, obj=None):
        return False

    def get_readonly_fields(self, request, obj=None):
        return [field.name for field in self.model._meta.fields]


@admin.register(models.Trade)
class TradeAdmin(ReadOnlyAdmin):
    list_display = ('trade_id', 'portfolio_id', 'strategy_type', 'trade_date', 'status')
    list_filter = ('portfolio_id', 'status', 'strategy_type')
    search_fields = ('trade_id', 'portfolio_id')
    date_hierarchy = 'trade_date'


@admin.register(models.Product)
class ProductAdmin(ReadOnlyAdmin):
    list_display = (
        'product_id', 'asset_kind', 'underlying_id', 'expiry_date',
        'settlement', 'currency', 'contract_size',
    )
    list_filter = ('asset_kind', 'underlying_id', 'settlement', 'currency')
    search_fields = ('product_id', 'underlying_id')


@admin.register(models.ProductEquity)
class ProductEquityAdmin(ReadOnlyAdmin):
    list_display = ('product', 'instrument_type', 'option_type', 'strike', 'exercise_style')
    list_filter = ('instrument_type', 'option_type', 'exercise_style')
    search_fields = ('product__product_id',)


@admin.register(models.TradeLeg)
class TradeLegAdmin(ReadOnlyAdmin):
    list_display = (
        'leg_id', 'trade', 'product', 'direction',
        'quantity', 'execution_price', 'commission',
    )
    list_filter = ('direction',)
    search_fields = ('leg_id', 'trade__trade_id', 'product__product_id')


@admin.register(models.TradeLegMtmEod)
class TradeLegMtmEodAdmin(ReadOnlyAdmin):
    list_display = (
        'as_of', 'leg', 'trade', 'underlying_spot', 'implied_vol_used',
        'years_to_maturity', 'pv_unit', 'pv_total', 'pnl_daily', 'pricing_engine',
    )
    list_filter = ('as_of', 'pricing_engine', 'numeraire_currency')
    search_fields = ('leg__leg_id', 'trade__trade_id', 'batch_run_id')
    date_hierarchy = 'as_of'


@admin.register(models.TradeLegExposureEod)
class TradeLegExposureEodAdmin(ReadOnlyAdmin):
    list_display = (
        'as_of', 'leg', 'pillar_id', 'grid_step', 'exposure_date',
        'year_fraction', 'ee', 'pfe_95', 'pfe_975', 'num_paths',
    )
    list_filter = ('as_of', 'pillar_id', 'pricing_engine', 'scope_key')
    search_fields = ('leg__leg_id', 'trade__trade_id', 'batch_run_id')
    date_hierarchy = 'as_of'


@admin.register(models.VolSurfaceEod)
class VolSurfaceEodAdmin(ReadOnlyAdmin):
    list_display = (
        'as_of', 'underlying_id', 'surface_kind', 'spot_used',
        'risk_free_rate', 'dividend_yield', 'point_count',
    )
    list_filter = ('underlying_id', 'surface_kind', 'as_of')
    search_fields = ('underlying_id', 'batch_run_id')
    date_hierarchy = 'as_of'


@admin.register(models.VolSurfacePointEod)
class VolSurfacePointEodAdmin(ReadOnlyAdmin):
    list_display = (
        'surface', 'expiration_date', 'years_to_maturity',
        'strike', 'contract_type', 'implied_vol', 'input_price', 'quality',
    )
    list_filter = ('contract_type', 'quality', 'surface__underlying_id')
    search_fields = ('source_option_ticker',)


@admin.register(models.EquityDailyEod)
class EquityDailyEodAdmin(ReadOnlyAdmin):
    list_display = (
        'ticker', 'as_of', 'open', 'high', 'low', 'close',
        'volume', 'source', 'adjusted',
    )
    list_filter = ('ticker', 'source', 'adjusted')
    search_fields = ('ticker',)
    date_hierarchy = 'as_of'


@admin.register(models.IndexDailyEod)
class IndexDailyEodAdmin(ReadOnlyAdmin):
    list_display = (
        'ticker', 'as_of', 'open', 'high', 'low', 'close',
        'volume', 'source', 'adjusted',
    )
    list_filter = ('ticker', 'source', 'adjusted')
    search_fields = ('ticker',)
    date_hierarchy = 'as_of'


# DiscountCurveEod / DiscountCurvePointEod are deliberately absent: their primary key
# is composite (curve_id, as_of[, tenor]) and admin rejects such models outright
# ("cannot be registered with admin"). They are read through the ORM in the curve views,
# where a yield-curve chart is the useful presentation anyway.


@admin.register(models.CatalogInstrumentType)
class CatalogInstrumentTypeAdmin(ReadOnlyAdmin):
    list_display = (
        'code', 'family', 'maps_to_instrument_type', 'sort_order', 'is_active',
        'example_product_id',
    )
    list_filter = ('family', 'is_active')
    search_fields = ('code', 'maps_to_instrument_type', 'description_en')
