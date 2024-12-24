from django import forms
from .models import TradeBook


class MarketForm(forms.Form):
    valuation_date = forms.CharField(label="Market Date", initial="2023-05-12")
    risk_free_rate = forms.FloatField(label="Risk Free Rate", initial=0.05)
    volatility = forms.FloatField(min_value=0, label="Volatility", initial=0.22)


class BookTradeForm(forms.ModelForm):
    class Meta:
        model = TradeBook
        fields = (
            'underlier_ticker', 'product_type', 'payoff', 'trade_date', 'trade_maturity', 'strike', 'dividend'
            , 'user_id')

        PROD_TYPE_SELECTION = (
            ("European Option", "European Option"), ("Asian Option", "Asian Option"),
            ("Digital Option", "Digital Option"))

        PAYOFF = (("Call", "Call"), ("Put", "Put"))
        widgets = {'product_type': forms.Select(choices=PROD_TYPE_SELECTION),
                   'payoff': forms.Select(choices=PAYOFF)}


class DiscountFactorForm(forms.Form):
    spot_rate = forms.FloatField(label="Spot Rate", initial=0.2)
    valuation_date = forms.CharField(label="Market Date", initial="2023-05-12")
    maturity_date = forms.CharField(label="Market Date", initial="2023-05-12")

    FREQUENCY_PERIOD_SELECTION = (('daily', 'daily'), ('once', 'once'), ('monthly', 'monthly'), ('annual', 'annual'),
                                  ('quarterly', 'quarterly'), ('semiannual', 'semiannual'))

    frequency_period = forms.Select(choices=FREQUENCY_PERIOD_SELECTION)

    YEAR_FRACTION_CONVENTION_SECTION = (("Actual360", "Actual360"), ("Actual365", "Actual365"),
                                        ("ActualActual", "ActualActual"), ("Thirty360", "Thirty360"),
                                        ("Business252", "Business252"))

    year_fraction = forms.Select(choices=YEAR_FRACTION_CONVENTION_SECTION)
