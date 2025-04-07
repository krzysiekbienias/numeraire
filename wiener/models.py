from django.db import models
from django.utils import timezone


# Create your models here.
class TradeBook(models.Model):
    """
    TradeBook Model:

    The TradeBook model is designed to store and manage derivative product trades, including
    both vanilla and structured products. It provides a flexible schema to accommodate
    various option strategies and financial instruments, enabling efficient trade tracking
    and risk management.

    Fields:
    ------
    - trade_id (AutoField): Unique identifier for each trade.
    - underlying_ticker (CharField): The ticker symbol of the underlying asset.
    - product_type (CharField): Type of derivative product (e.g., "Option", "Butterfly Spread",).
    - payoff (CharField): Payoff structure of the derivative (e.g., "Call", "Put").
    - trade_date (DateField): The execution date of the trade.
    - trade_maturity (DateField): The expiration/maturity date of the derivative contract.
    - strike (FloatField, nullable): Strike price of the option (for single-leg options).
    - dividend (FloatField): Dividend yield associated with the underlying asset.
    - option_style (CharField, nullable): Option exercise style (e.g., "European", "American").
    - structured_params (JSONField, nullable): JSON field for storing additional parameters
      required for structured products, such as multiple strike prices for spreads.
    - user_id (CharField): Identifier of the user who booked the trade.

    Notes:
    ------
    - The `structured_params` field enables dynamic storage of complex trade parameters,
      making this model suitable for multi-leg strategies and exotic derivatives.
    - The `strike` field is primarily for single-leg options; structured products should
      define their strike levels in `structured_params`.
    """
    trade_id = models.AutoField(primary_key=True)
    underlying_ticker = models.CharField(max_length=25)
    product_type = models.CharField(max_length=25)
    payoff = models.CharField(max_length=10, null=True, blank=True)
    trade_date = models.DateField()
    trade_maturity = models.DateField()
    strike = models.FloatField(null=True, blank=True)
    dividend = models.FloatField()
    option_style = models.CharField(max_length=50,null=True,blank=True)
    structured_params = models.JSONField(null=True, blank=True)

    user_id = models.CharField(default="kbienias", max_length=100)


class DerivativePrice(models.Model):
    """
    DerivativePrice Model:

    Stores valuation details for derivative trades, supporting multiple pricing runs
    with different methodologies and frameworks.

    Fields:
    -------
    - run_id (AutoField): Unique identifier for each pricing run.
    - trade_id (ForeignKey): Reference to the TradeBook model.
    - valuation_date (DateField): The date on which the trade is valued.
    - price_status (CharField): Status of the pricing (e.g., "Pending", "Finalized", "Error").
    - official_price (FloatField): The computed official price for the valuation.
    - pricing_framework (CharField, nullable): The theoretical pricing framework used (e.g., "Black-Scholes", "LMM", "Stochastic Volatility").
    - pricing_method (CharField, nullable): The numerical approach used for valuation (e.g., "Monte Carlo", "Analytical").
    - notes (TextField, nullable): Additional comments on pricing assumptions, errors, or methodology details.
    - created_at (DateTimeField): Timestamp indicating when the valuation was recorded.
    - user_id (CharField): Identifier of the user performing the valuation.

    Notes:
    ------
    - Supports multiple valuation methodologies and detailed price tracking.
    - `pricing_framework` represents the underlying financial model, while `pricing_method` defines the computational approach.
    - The `notes` field maintains an audit trail for pricing decisions and assumptions.
"""
    run_id = models.AutoField(primary_key=True)
    trade_id = models.ForeignKey(TradeBook, on_delete=models.CASCADE, db_column="trade_id")
    valuation_date = models.DateField()
    price_status = models.CharField(max_length=25)
    official_price = models.FloatField()
    pricing_framework = models.CharField(max_length=50, null=True, blank=True)
    pricing_method = models.CharField(max_length=50, null=True, blank=True)  # Pricing methodology
    notes = models.TextField(null=True, blank=True)  # Pricing details, errors, or assumptions
    created_at = models.DateTimeField(default=timezone.now)
    user_id = models.CharField(default="kbienias", max_length=100)

    def __str__(self):
        return f"Price {self.run_id} - Trade {self.trade_id.trade_id} - {self.valuation_date}"