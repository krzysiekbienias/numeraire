from django import forms

class MarketForm(forms.Form):
    valuation_date=forms.CharField(label="Market Date",initial="2023-05-12")
    risk_free_rate=forms.FloatField(label="Risk Free Rate",initial=0.06)
    volatility=forms.FloatField(min_value=0,label="Volatility",initial=0.22)

prod_type_selection=(("European Option","European Option"),("Asian Option", "Asian Option"),("Digital Option", "Digital Option"))

payoff_type_selection=(("Call","Call"),("Put","Put"))
class BookTradeForm(forms.Form):
    ticker=forms.CharField(label="Ticker",max_length=100)
    product_type=forms.ChoiceField(choices=prod_type_selection)
    payoff_type=forms.ChoiceField(choices=payoff_type_selection)
    trade_date=forms.CharField(label="Trade Date")
    trade_maturity=forms.CharField(label="Trade Maturity")
    strike=forms.FloatField(label="Strike")
    dividend=forms.FloatField(label="Dividend")
    created_at=forms.CharField(label="Created at")
    user=forms.CharField(label="User",max_length=100,initial="kb007")
