"""Booking form — mirrors the validation in `scripts/import_trade_bundle.py`.

The importer stays the authority: it re-checks everything before any INSERT. These
rules exist so a mistake comes back as a red field instead of a subprocess error.
"""

from __future__ import annotations

from datetime import date as date_cls

from django import forms

from journal.booking import (
    BookableInstrument,
    build_product_id,
    commodity_product_code,
    default_futures_multiplier,
    lookup_futures_contract,
    product_conflicts,
)

_TEXT = {'class': 'form-control form-control-sm'}
_SELECT = {'class': 'form-select form-select-sm'}
_DATE = {'class': 'form-control form-control-sm', 'type': 'date'}


class NewTradeForm(forms.Form):
    """Single-leg booking for one wired instrument type."""

    underlying_id = forms.ChoiceField(
        label='Underlier',
        widget=forms.Select(attrs=_SELECT),
        help_text='Universe instruments with ingest coverage for this asset class.',
    )
    contract_ticker = forms.ChoiceField(
        label='Futures contract',
        widget=forms.Select(attrs=_SELECT),
        help_text='Listed tenor from futures_contract (latest listing day).',
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
        contract_choices: list[tuple[str, str]] | None = None,
        **kwargs,
    ):
        super().__init__(*args, **kwargs)
        self.spec = spec
        self.product_id: str | None = None

        self.fields['underlying_id'].choices = underlier_choices or []
        if spec.has_strike:
            self.fields['strike'].label = spec.strike_label
            self.fields['strike'].help_text = spec.strike_help
        self.fields['contract_size'].help_text = spec.contract_size_help

        if not spec.has_option_type:
            del self.fields['option_type']
        if not spec.has_strike:
            del self.fields['strike']
        if not spec.has_expiry or spec.has_contract_ticker:
            # Commodity: expiry comes from futures_contract.settlement_date.
            if 'expiry_date' in self.fields:
                del self.fields['expiry_date']
        if not spec.has_contract_ticker:
            del self.fields['contract_ticker']
        else:
            # Underlier is chosen via a GET filter (see trade_new.html); keep it as a
            # hidden POST field so clean()/bundle still see underlying_id.
            self.fields['underlying_id'].widget = forms.HiddenInput()
            self.fields['underlying_id'].help_text = ''
            choices = list(contract_choices or [])
            if choices:
                self.fields['contract_ticker'].choices = [
                    ('', f'— {len(choices)} tenors with EOD —')
                ] + choices
                self.fields['contract_ticker'].help_text = (
                    'Tenors that have futures_daily_eod on the latest session for this underlier '
                    '(not the full deferred listing).'
                )
            else:
                self.fields['contract_ticker'].choices = [
                    ('', '— pick underlier above first —')
                ]
                self.fields['contract_ticker'].help_text = (
                    'Use the underlier filter above (page reloads). Needs futures_daily_eod rows.'
                )
        self.fields['contract_size'].initial = spec.default_contract_size
        self.fields['settlement'].initial = spec.default_settlement
        self.fields['strategy_type'].initial = spec.strategy_type

        # Prefer Massive unit_of_measure_qty when underlier already chosen.
        und = None
        if self.data.get('underlying_id'):
            und = self.data.get('underlying_id')
        elif self.initial.get('underlying_id'):
            und = self.initial.get('underlying_id')
        if spec.has_contract_ticker and und:
            mult = default_futures_multiplier(commodity_product_code(str(und)))
            if mult is not None:
                self.fields['contract_size'].initial = mult

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

    def clean_contract_ticker(self) -> str:
        return self.cleaned_data['contract_ticker'].strip().upper()

    def clean(self):
        cleaned = super().clean()
        trade_date = cleaned.get('trade_date')
        expiry_date = cleaned.get('expiry_date')

        if self.spec.has_contract_ticker:
            und = cleaned.get('underlying_id')
            ticker = cleaned.get('contract_ticker')
            if und and ticker:
                product_code = commodity_product_code(und)
                cleaned['product_code'] = product_code
                cleaned['underlying_id'] = product_code  # book convention: CL not a local alias
                contract = lookup_futures_contract(product_code, ticker)
                if contract is None:
                    self.add_error(
                        'contract_ticker',
                        f'No futures_contract row for {product_code}/{ticker} on the latest listing day.',
                    )
                    return cleaned
                settle_raw = (contract.settlement_date or '').strip()
                if not settle_raw:
                    self.add_error(
                        'contract_ticker',
                        f'{ticker} has no settlement_date in futures_contract — cannot set expiry.',
                    )
                    return cleaned
                try:
                    expiry_date = date_cls.fromisoformat(settle_raw)
                except ValueError:
                    self.add_error(
                        'contract_ticker',
                        f'{ticker} settlement_date {settle_raw!r} is not YYYY-MM-DD.',
                    )
                    return cleaned
                cleaned['expiry_date'] = expiry_date
                cleaned['contract_ticker'] = contract.ticker
                cleaned['contract_month'] = None
                cleaned['tick_size'] = contract.trade_tick_size
                cleaned['tick_value'] = None

        if (
            self.spec.has_expiry
            and trade_date
            and expiry_date
            and expiry_date < trade_date
        ):
            field = 'contract_ticker' if self.spec.has_contract_ticker else 'expiry_date'
            self.add_error(
                field,
                f'Expiry {expiry_date:%Y-%m-%d} is before the trade date '
                f'{trade_date:%Y-%m-%d} — the product would already be dead at booking.',
            )
            return cleaned

        required = ['underlying_id', 'settlement', 'contract_size']
        if self.spec.has_strike:
            required.append('strike')
        if self.spec.has_expiry:
            required.append('expiry_date')
        if self.spec.has_contract_ticker:
            required.append('contract_ticker')
        if any(cleaned.get(name) is None for name in required):
            return cleaned

        self.product_id = build_product_id(
            self.spec,
            underlying_id=cleaned['underlying_id'],
            expiry_date=cleaned.get('expiry_date'),
            strike=cleaned.get('strike'),
            option_type=cleaned.get('option_type'),
            contract_ticker=cleaned.get('contract_ticker'),
        )
        conflicts = product_conflicts(
            self.product_id,
            spec=self.spec,
            terms={
                'underlying_id': cleaned['underlying_id'],
                'expiry_date': cleaned.get('expiry_date'),
                'settlement': cleaned['settlement'],
                'currency': cleaned.get('currency', 'USD'),
                'contract_size': cleaned['contract_size'],
                'strike': cleaned.get('strike'),
                'option_type': cleaned.get('option_type'),
                'product_code': cleaned.get('product_code'),
                'contract_ticker': cleaned.get('contract_ticker'),
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
