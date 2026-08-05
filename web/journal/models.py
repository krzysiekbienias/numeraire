"""Read-only mirror of the tables created by `sql/schema_v1.sql`.

Every model is `managed = False`: the C++ side owns this schema and Django must
never migrate it. Bootstrapped from `manage.py inspectdb --database numeraire`,
then adjusted by hand:

  * pure `YYYY-MM-DD` columns became `DateField` so date filters and range
    lookups work; SQLite stores them in the same textual form the batches write.
  * timestamp columns stay `TextField` - the batches emit both `2026-05-24T09:54:41Z`
    and `2026-05-24 09:53:48`, and neither round-trips through `DateTimeField`.
  * curve tables use `CompositePrimaryKey`; their composite foreign keys cannot be
    expressed in the ORM, so the parent columns are plain fields.
"""

from django.db import models


class Trade(models.Model):
    trade_id = models.TextField(primary_key=True)
    portfolio_id = models.TextField()
    strategy_type = models.TextField()
    booking_timestamp = models.TextField(blank=True, null=True)
    trade_date = models.DateField(blank=True, null=True)
    updated_at = models.TextField(blank=True, null=True)
    status = models.TextField()

    class Meta:
        managed = False
        db_table = 'trades'
        ordering = ['trade_id']

    def __str__(self):
        return self.trade_id


class Product(models.Model):
    product_id = models.TextField(primary_key=True)
    asset_kind = models.TextField()
    underlying_id = models.TextField()
    expiry_date = models.DateField(blank=True, null=True)
    settlement = models.TextField()
    currency = models.TextField()
    contract_size = models.FloatField()
    day_count = models.TextField()
    calendar = models.TextField()

    class Meta:
        managed = False
        db_table = 'products'
        ordering = ['product_id']

    def __str__(self):
        return self.product_id


class ProductEquity(models.Model):
    product = models.OneToOneField(
        Product, models.DO_NOTHING, primary_key=True, related_name='equity'
    )
    instrument_type = models.TextField()
    option_type = models.TextField(blank=True, null=True)
    strike = models.FloatField(blank=True, null=True)
    exercise_style = models.TextField(blank=True, null=True)
    structured_params = models.TextField()

    class Meta:
        managed = False
        db_table = 'products_equity'

    def __str__(self):
        return self.product_id


class TradeLeg(models.Model):
    leg_id = models.TextField(primary_key=True)
    trade = models.ForeignKey(Trade, models.DO_NOTHING, related_name='legs')
    product = models.ForeignKey(Product, models.DO_NOTHING, related_name='legs')
    direction = models.TextField()
    quantity = models.FloatField()
    execution_price = models.FloatField()
    commission = models.FloatField()

    class Meta:
        managed = False
        db_table = 'trade_legs'
        ordering = ['leg_id']

    def __str__(self):
        return self.leg_id


class TradeLegMtmEod(models.Model):
    """Official current mark per leg, as written by `dev_main` MTM mode."""

    as_of = models.DateField()
    session_calendar = models.TextField()
    trade = models.ForeignKey(Trade, models.DO_NOTHING, related_name='mtm_rows')
    leg = models.ForeignKey(TradeLeg, models.DO_NOTHING, related_name='mtm_rows')
    underlying_spot = models.FloatField()
    risk_free_rate = models.FloatField()
    dividend_yield = models.FloatField()
    implied_vol_used = models.FloatField()
    years_to_maturity = models.FloatField()
    numeraire_currency = models.TextField()
    pv_unit = models.FloatField()
    pv_total = models.FloatField()
    pnl_daily = models.FloatField(blank=True, null=True)
    pnl_inception = models.FloatField(blank=True, null=True)
    delta = models.FloatField()
    delta_total = models.FloatField()
    gamma = models.FloatField()
    gamma_total = models.FloatField()
    vega = models.FloatField()
    vega_total = models.FloatField()
    theta = models.FloatField()
    theta_total = models.FloatField()
    rho = models.FloatField()
    rho_total = models.FloatField()
    pricing_engine = models.TextField()
    # One official mark per leg and day; other engines are informational only, so
    # every total and time series in the Journal filters on this.
    is_official = models.BooleanField(default=False)
    num_paths = models.IntegerField(blank=True, null=True)
    mc_seed = models.IntegerField(blank=True, null=True)
    batch_run_id = models.TextField(blank=True, null=True)
    calculated_at = models.TextField()
    remarks = models.TextField()

    class Meta:
        managed = False
        db_table = 'trade_leg_mtm_eod'
        ordering = ['-as_of', 'leg_id']
        verbose_name = 'MTM (EOD)'
        verbose_name_plural = 'MTM (EOD)'

    def __str__(self):
        return f'{self.leg_id} @ {self.as_of}'


class TradeLegExposureEod(models.Model):
    """EE / PFE per leg and exposure-grid pillar from the Monte Carlo batch."""

    as_of = models.DateField()
    trade = models.ForeignKey(Trade, models.DO_NOTHING, related_name='exposure_rows')
    leg = models.ForeignKey(TradeLeg, models.DO_NOTHING, related_name='exposure_rows')
    pillar_id = models.TextField()
    grid_step = models.IntegerField()
    year_fraction = models.FloatField()
    exposure_date = models.DateField()
    ee = models.FloatField()
    pfe_95 = models.FloatField()
    pfe_975 = models.FloatField()
    num_paths = models.IntegerField()
    mc_seed = models.IntegerField()
    calibration_id = models.IntegerField(blank=True, null=True)
    scope_key = models.TextField()
    batch_run_id = models.TextField(blank=True, null=True)
    pricing_engine = models.TextField()
    calculated_at = models.TextField()
    remarks = models.TextField()

    class Meta:
        managed = False
        db_table = 'trade_leg_exposure_eod'
        ordering = ['-as_of', 'leg_id', 'grid_step']
        verbose_name = 'Exposure (EOD)'
        verbose_name_plural = 'Exposure (EOD)'

    def __str__(self):
        return f'{self.leg_id} @ {self.as_of} / {self.pillar_id}'


class VolSurfaceEod(models.Model):
    surface_id = models.AutoField(primary_key=True)
    underlying_id = models.TextField()
    as_of = models.DateField()
    surface_kind = models.TextField()
    coordinate_system = models.TextField()
    spot_used = models.FloatField()
    risk_free_rate = models.FloatField()
    dividend_yield = models.FloatField()
    model = models.TextField()
    price_source = models.TextField()
    currency = models.TextField()
    point_count = models.IntegerField(blank=True, null=True)
    ingested_at = models.TextField()
    batch_run_id = models.TextField(blank=True, null=True)

    class Meta:
        managed = False
        db_table = 'vol_surface_eod'
        ordering = ['-as_of', 'underlying_id']
        verbose_name = 'Vol surface'

    def __str__(self):
        return f'{self.underlying_id} @ {self.as_of}'


class VolSurfacePointEod(models.Model):
    surface = models.ForeignKey(VolSurfaceEod, models.DO_NOTHING, related_name='points')
    expiration_date = models.DateField()
    years_to_maturity = models.FloatField()
    strike = models.FloatField()
    contract_type = models.TextField()
    implied_vol = models.FloatField()
    source_option_ticker = models.TextField(blank=True, null=True)
    input_price = models.FloatField(blank=True, null=True)
    quality = models.TextField()

    class Meta:
        managed = False
        db_table = 'vol_surface_point_eod'
        ordering = ['expiration_date', 'strike']
        verbose_name = 'Vol surface point'

    def __str__(self):
        return f'{self.expiration_date} K={self.strike} {self.contract_type}'


class EquityDailyEod(models.Model):
    """Daily OHLC for single-name equities (Polygon/Massive aggs)."""

    id = models.AutoField(primary_key=True)
    ticker = models.TextField()
    as_of = models.DateField()
    session_calendar = models.TextField()
    open = models.FloatField()
    high = models.FloatField()
    low = models.FloatField()
    close = models.FloatField()
    currency = models.TextField()
    volume = models.FloatField(blank=True, null=True)
    vwap = models.FloatField(blank=True, null=True)
    trade_count = models.IntegerField(blank=True, null=True)
    source = models.TextField()
    timespan = models.TextField()
    adjusted = models.IntegerField()
    provider_timestamp_utc_ms = models.BigIntegerField(blank=True, null=True)
    ingested_at = models.TextField()

    class Meta:
        managed = False
        db_table = 'equity_daily_eod'
        ordering = ['-as_of', 'ticker']
        verbose_name = 'Equity daily EOD'
        verbose_name_plural = 'Equity daily EOD'

    def __str__(self):
        return f'{self.ticker} @ {self.as_of}'


class IndexDailyEod(models.Model):
    """Daily OHLC for cash / benchmark indices (e.g. ``I:NDX``)."""

    id = models.AutoField(primary_key=True)
    ticker = models.TextField()
    as_of = models.DateField()
    session_calendar = models.TextField()
    open = models.FloatField()
    high = models.FloatField()
    low = models.FloatField()
    close = models.FloatField()
    currency = models.TextField()
    volume = models.FloatField(blank=True, null=True)
    vwap = models.FloatField(blank=True, null=True)
    trade_count = models.IntegerField(blank=True, null=True)
    source = models.TextField()
    timespan = models.TextField()
    adjusted = models.IntegerField()
    provider_timestamp_utc_ms = models.BigIntegerField(blank=True, null=True)
    ingested_at = models.TextField()

    class Meta:
        managed = False
        db_table = 'index_daily_eod'
        ordering = ['-as_of', 'ticker']
        verbose_name = 'Index daily EOD'
        verbose_name_plural = 'Index daily EOD'

    def __str__(self):
        return f'{self.ticker} @ {self.as_of}'


class DiscountCurveEod(models.Model):
    pk = models.CompositePrimaryKey('curve_id', 'as_of')
    curve_id = models.TextField()
    as_of = models.DateField()
    source_par_curve_id = models.TextField()
    source_par_as_of = models.DateField()
    currency = models.TextField()
    day_count = models.TextField()
    session_calendar = models.TextField()
    interpolation_method = models.TextField()
    bootstrap_engine = models.TextField()
    batch_run_id = models.TextField(blank=True, null=True)
    ingested_at = models.TextField()

    class Meta:
        managed = False
        db_table = 'discount_curve_eod'
        ordering = ['-as_of', 'curve_id']
        verbose_name = 'Discount curve'

    def __str__(self):
        return f'{self.curve_id} @ {self.as_of}'


class DiscountCurvePointEod(models.Model):
    pk = models.CompositePrimaryKey('curve_id', 'as_of', 'tenor')
    curve_id = models.TextField()
    as_of = models.DateField()
    tenor = models.TextField()
    time_years = models.FloatField()
    zero_rate = models.FloatField()
    discount_factor = models.FloatField()

    class Meta:
        managed = False
        db_table = 'discount_curve_point_eod'
        ordering = ['as_of', 'time_years']
        verbose_name = 'Discount curve point'

    def __str__(self):
        return f'{self.curve_id} @ {self.as_of} / {self.tenor}'


class ParCurveEod(models.Model):
    """Quoted par instruments (FRED treasury etc.) used as bootstrap input."""

    pk = models.CompositePrimaryKey('curve_id', 'as_of')
    curve_id = models.TextField()
    as_of = models.DateField()
    currency = models.TextField()
    curve_kind = models.TextField()
    source = models.TextField()
    day_count = models.TextField()
    session_calendar = models.TextField()
    notes = models.TextField(blank=True, null=True)
    ingested_at = models.TextField()

    class Meta:
        managed = False
        db_table = 'par_curve_eod'
        ordering = ['-as_of', 'curve_id']
        verbose_name = 'Par curve'

    def __str__(self):
        return f'{self.curve_id} @ {self.as_of}'


class ParCurvePointEod(models.Model):
    pk = models.CompositePrimaryKey('curve_id', 'as_of', 'tenor')
    curve_id = models.TextField()
    as_of = models.DateField()
    tenor = models.TextField()
    tenor_days = models.IntegerField(blank=True, null=True)
    instrument_type = models.TextField()
    fred_series_id = models.TextField()
    quoted_rate = models.FloatField()
    quoted_price = models.FloatField(blank=True, null=True)
    quote_style = models.TextField()

    class Meta:
        managed = False
        db_table = 'par_curve_point_eod'
        ordering = ['as_of', 'tenor_days', 'tenor']
        verbose_name = 'Par curve point'

    def __str__(self):
        return f'{self.curve_id} @ {self.as_of} / {self.tenor}'


class UniverseInstrument(models.Model):
    """Controlled market-data universe — the underliers ingest actually covers."""

    instrument_id = models.TextField(primary_key=True)
    provider_symbol = models.TextField()
    display_name = models.TextField(blank=True, null=True)
    asset_class = models.TextField()
    sector = models.TextField(blank=True, null=True)
    industry = models.TextField(blank=True, null=True)
    quote_currency = models.TextField()
    session_calendar = models.TextField()
    country = models.TextField(blank=True, null=True)
    data_vendor = models.TextField()
    is_active = models.IntegerField()
    ingest_equity_eod = models.IntegerField()
    ingest_index_eod = models.IntegerField()
    ingest_priority = models.IntegerField()
    notes = models.TextField(blank=True, null=True)
    created_at = models.TextField(blank=True, null=True)
    updated_at = models.TextField(blank=True, null=True)

    class Meta:
        managed = False
        db_table = 'universe_instrument'
        ordering = ['instrument_id']
        verbose_name = 'Universe instrument'

    def __str__(self):
        return self.instrument_id


class CatalogInstrumentType(models.Model):
    """Reference codes for equity instrument types (seed / UI inventory)."""

    code = models.TextField(primary_key=True)
    family = models.TextField()
    maps_to_instrument_type = models.TextField()
    description_en = models.TextField()
    example_product_id = models.TextField()
    sort_order = models.IntegerField()
    is_active = models.IntegerField()
    created_at = models.TextField()

    class Meta:
        managed = False
        db_table = 'catalog_instrument_type'
        ordering = ['sort_order', 'code']
        verbose_name = 'Catalog instrument type'

    def __str__(self):
        return self.code
