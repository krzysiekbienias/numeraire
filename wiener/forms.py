from django import forms

class MarketForm(forms.Form):
    valuation_date=forms.CharField(label="Market Date",initial="2023-05-12")
    risk_free_rate=forms.FloatField(label="Risk Free Rate",initial=0.06)
    volatility=forms.FloatField(min_value=0,label="Volatility",initial=0.22)