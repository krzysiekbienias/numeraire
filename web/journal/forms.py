"""Booking form — mirrors the validation in `scripts/import_trade_bundle.py`.

The importer stays the authority: it re-checks everything before any INSERT. These
rules exist so a mistake comes back as a red field instead of a subprocess error.
"""

from __future__ import annotations

from django import forms

from journal.booking import BookableInstrument, build_product_id, product_conflicts

_TEXT = {'class': 'form-control form-control-sm'}
_SELECT = {'class': 'form-select form-select-sm'}
_DATE = {'class': 'form-control form-control-sm', 'type': 'date'}


class NewTradeForm(forms.Form):
    """Single-leg booking for one wired instrument type."""

    underlying_id = forms.ChoiceField(
        label='Underlier',
        widget=forms.Select(attrs=_SELECT),
        help_text='Universe instruments with EOD ingest — pricing needs a spot on trade date.',
    )
    option_type = forms.ChoiceField(
        label='Call / put',
        choices=(('call', 'call'), ('put', 'put')),
        widget=forms.Select(attrs=_SELECT),
    )
    strike = forms.FloatField(
        label='Strike',
        widget=forms.NumberInput(attrs={**_TEXT, 'step': 'any'}),
    )
    expiry_date = forms.DateField(
        label='Expiry',
        widget=forms.DateInput(attrs=_DATE, format='%Y-%m-%d'),
        help_text='Must be on or after the trade date.',
    )
    settlement = forms.ChoiceField(
        label='Settlement',
        choices=(('CASH', 'CASH'), ('PHYSICAL', 'PHYSICAL')),
        widget=forms.Select(attrs=_SELECT),
    )
    contract_size = forms.FloatField(
        label='Contract size',
        widget=forms.NumberInput(attrs={**_TEXT, 'step': 'any'}),
    )
    currency = forms.CharField(
        label='Currency',
        max_length=8,
        initial='USD',
        widget=forms.TextInput(attrs=_TEXT),
    )
    trade_date = forms.DateField(
        label='Trade date',
        widget=forms.DateInput(attrs=_DATE, format='%Y-%m-%d'),
    )
    portfolio_id = forms.CharField(
        label='Portfolio',
        max_length=64,
        widget=forms.TextInput(attrs={**_TEXT, 'list': 'nj-portfolio-options'}),
    )
    strategy_type = forms.CharField(
        label='Strategy',
        max_length=64,
        widget=forms.TextInput(attrs={**_TEXT, 'list': 'nj-strategy-options'}),
    )
    direction = forms.ChoiceField(
        label='Direction',
        choices=(('long', 'long'), ('short', 'short')),
        widget=forms.Select(attrs=_SELECT),
    )
    quantity = forms.FloatField(
        label='Quantity',
        widget=forms.NumberInput(attrs={**_TEXT, 'step': 'any'}),
    )
    commission_per_contract = forms.FloatField(
        label='Commission / contract',
        required=False,
        initial=0.0,
        widget=forms.NumberInput(attrs={**_TEXT, 'step': 'any'}),
        help_text='Charged separately from the model premium; total = rate × quantity.',
    )

    def __init__(
        self,
        spec: BookableInstrument,
        *args,
        underlier_choices: list[tuple[str, str]] | None = None,
        **kwargs,
    ):
        super().__init__(*args, **kwargs)
        self.spec = spec
        self.product_id: str | None = None

        self.fields['underlying_id'].choices = underlier_choices or []
        self.fields['strike'].label = spec.strike_label
        self.fields['strike'].help_text = spec.strike_help
        self.fields['contract_size'].help_text = spec.contract_size_help

        if not spec.has_option_type:
            del self.fields['option_type']

        self.fields['contract_size'].initial = spec.default_contract_size
        self.fields['settlement'].initial = spec.default_settlement
        self.fields['strategy_type'].initial = spec.strategy_type

    def clean_strike(self) -> float:
        strike = self.cleaned_data['strike']
        if strike <= 0:
            raise forms.ValidationError('Must be positive.')
        return strike

    def clean_quantity(self) -> float:
        quantity = self.cleaned_data['quantity']
        if quantity <= 0:
            raise forms.ValidationError('Must be positive.')
        return quantity

    def clean_contract_size(self) -> float:
        contract_size = self.cleaned_data['contract_size']
        if contract_size <= 0:
            raise forms.ValidationError('Must be positive.')
        return contract_size

    def clean_commission_per_contract(self) -> float:
        commission = self.cleaned_data.get('commission_per_contract')
        if commission is None:
            return 0.0
        if commission < 0:
            raise forms.ValidationError('Cannot be negative.')
        return commission

    def clean_currency(self) -> str:
        return self.cleaned_data['currency'].strip().upper()

    def clean_portfolio_id(self) -> str:
        return self.cleaned_data['portfolio_id'].strip()

    def clean_strategy_type(self) -> str:
        return self.cleaned_data['strategy_type'].strip()

    def clean(self):
        cleaned = super().clean()
        trade_date = cleaned.get('trade_date')
        expiry_date = cleaned.get('expiry_date')

        if trade_date and expiry_date and expiry_date < trade_date:
            self.add_error(
                'expiry_date',
                f'Expiry {expiry_date:%Y-%m-%d} is before the trade date '
                f'{trade_date:%Y-%m-%d} — the product would already be dead at booking.',
            )
            return cleaned

        required = ('underlying_id', 'strike', 'expiry_date', 'settlement', 'contract_size')
        if any(cleaned.get(name) is None for name in required):
            return cleaned

        self.product_id = build_product_id(
            self.spec,
            underlying_id=cleaned['underlying_id'],
            expiry_date=cleaned['expiry_date'],
            strike=cleaned['strike'],
            option_type=cleaned.get('option_type'),
        )
        conflicts = product_conflicts(
            self.product_id,
            spec=self.spec,
            terms={
                'underlying_id': cleaned['underlying_id'],
                'expiry_date': cleaned['expiry_date'],
                'settlement': cleaned['settlement'],
                'currency': cleaned.get('currency', 'USD'),
                'contract_size': cleaned['contract_size'],
                'strike': cleaned['strike'],
                'option_type': cleaned.get('option_type'),
            },
        )
        if conflicts:
            self.add_error(
                None,
                f'Product {self.product_id} already exists on different terms, and the '
                'importer would silently reuse the existing one. '
                + '; '.join(conflicts)
                + '. Adjust the inputs or book against the existing product terms.',
            )
        return cleaned
