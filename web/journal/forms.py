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


def _resolve_listed_tenor(
    form: forms.Form,
    *,
    underlying_id: str | None,
    ticker: str | None,
    field: str,
) -> dict | None:
    """Map underlier + ticker to futures_contract terms. Errors land on `field`."""
    if not underlying_id or not ticker:
        return None
    product_code = commodity_product_code(underlying_id)
    contract = lookup_futures_contract(product_code, ticker)
    if contract is None:
        form.add_error(
            field,
            f'No futures_contract row for {product_code}/{ticker} on the latest listing day.',
        )
        return None
    settle_raw = (contract.settlement_date or '').strip()
    if not settle_raw:
        form.add_error(
            field,
            f'{ticker} has no settlement_date in futures_contract — cannot set expiry.',
        )
        return None
    try:
        expiry_date = date_cls.fromisoformat(settle_raw)
    except ValueError:
        form.add_error(
            field,
            f'{ticker} settlement_date {settle_raw!r} is not YYYY-MM-DD.',
        )
        return None
    return {
        'product_code': product_code,
        'underlying_id': product_code,
        'expiry_date': expiry_date,
        'contract_ticker': contract.ticker,
        'tick_size': contract.trade_tick_size,
    }


class CalendarTradeForm(forms.Form):
    """Two listed outrights, one trade: near vs far on the same curve."""

    underlying_id = forms.ChoiceField(
        label='Underlier',
        widget=forms.HiddenInput(),
    )
    near_ticker = forms.ChoiceField(
        label='Near tenor',
        widget=forms.Select(attrs=_SELECT),
        help_text='Shorter-dated listed contract (front / nearer expiry).',
    )
    far_ticker = forms.ChoiceField(
        label='Far tenor',
        widget=forms.Select(attrs=_SELECT),
        help_text='Deferred listed contract on the same underlier.',
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
        label='Calendar',
        choices=(
            ('long', 'long (short near / long far)'),
            ('short', 'short (long near / short far)'),
        ),
        widget=forms.Select(attrs=_SELECT),
        help_text='Long calendar sells the nearer contract and buys the deferred.',
    )
    quantity = forms.FloatField(
        label='Quantity',
        widget=forms.NumberInput(attrs={**_TEXT, 'step': 'any'}),
        help_text='Contracts per leg (1:1). Same size on near and far.',
    )
    commission_per_contract = forms.FloatField(
        label='Commission / contract',
        required=False,
        initial=0.0,
        widget=forms.NumberInput(attrs={**_TEXT, 'step': 'any'}),
        help_text='Charged on each leg; total = rate × quantity × 2.',
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
        choices = list(contract_choices or [])
        if choices:
            blank = [('', f'— {len(choices)} tenors with EOD —')]
            help_text = (
                'Tenors that have futures_daily_eod on the latest session for this underlier.'
            )
        else:
            blank = [('', '— pick underlier above first —')]
            help_text = 'Use the underlier filter above (page reloads). Needs futures_daily_eod rows.'
        self.fields['near_ticker'].choices = blank + choices
        self.fields['far_ticker'].choices = blank + choices
        self.fields['near_ticker'].help_text = help_text
        self.fields['far_ticker'].help_text = help_text
        self.fields['contract_size'].initial = spec.default_contract_size
        self.fields['contract_size'].help_text = spec.contract_size_help
        self.fields['settlement'].initial = spec.default_settlement
        self.fields['strategy_type'].initial = spec.strategy_type

        und = None
        if self.data.get('underlying_id'):
            und = self.data.get('underlying_id')
        elif self.initial.get('underlying_id'):
            und = self.initial.get('underlying_id')
        if und:
            mult = default_futures_multiplier(commodity_product_code(str(und)))
            if mult is not None:
                self.fields['contract_size'].initial = mult

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

    def clean_near_ticker(self) -> str:
        return self.cleaned_data['near_ticker'].strip().upper()

    def clean_far_ticker(self) -> str:
        return self.cleaned_data['far_ticker'].strip().upper()

    def clean(self):
        cleaned = super().clean()
        und = cleaned.get('underlying_id')
        near_ticker = cleaned.get('near_ticker')
        far_ticker = cleaned.get('far_ticker')
        trade_date = cleaned.get('trade_date')

        if near_ticker and far_ticker and near_ticker == far_ticker:
            self.add_error('far_ticker', 'Far tenor must differ from the near tenor.')
            return cleaned

        near = _resolve_listed_tenor(
            self, underlying_id=und, ticker=near_ticker, field='near_ticker'
        )
        far = _resolve_listed_tenor(
            self, underlying_id=und, ticker=far_ticker, field='far_ticker'
        )
        if near is None or far is None:
            return cleaned

        if far['expiry_date'] <= near['expiry_date']:
            self.add_error(
                'far_ticker',
                f'Far tenor {far["contract_ticker"]} settles {far["expiry_date"]:%Y-%m-%d}, '
                f'which is not after near {near["contract_ticker"]} '
                f'({near["expiry_date"]:%Y-%m-%d}).',
            )
            return cleaned

        if trade_date and near['expiry_date'] < trade_date:
            self.add_error(
                'near_ticker',
                f'Expiry {near["expiry_date"]:%Y-%m-%d} is before the trade date '
                f'{trade_date:%Y-%m-%d} — the near contract would already be dead at booking.',
            )
            return cleaned

        cleaned['product_code'] = near['product_code']
        cleaned['underlying_id'] = near['underlying_id']
        cleaned['near_ticker'] = near['contract_ticker']
        cleaned['far_ticker'] = far['contract_ticker']
        cleaned['near_expiry_date'] = near['expiry_date']
        cleaned['far_expiry_date'] = far['expiry_date']
        cleaned['near_tick_size'] = near['tick_size']
        cleaned['far_tick_size'] = far['tick_size']
        if cleaned.get('direction') == 'short':
            cleaned['near_direction'] = 'long'
            cleaned['far_direction'] = 'short'
        else:
            cleaned['near_direction'] = 'short'
            cleaned['far_direction'] = 'long'

        near_pid = build_product_id(
            self.spec,
            underlying_id=cleaned['underlying_id'],
            expiry_date=near['expiry_date'],
            strike=None,
            option_type=None,
            contract_ticker=near['contract_ticker'],
        )
        far_pid = build_product_id(
            self.spec,
            underlying_id=cleaned['underlying_id'],
            expiry_date=far['expiry_date'],
            strike=None,
            option_type=None,
            contract_ticker=far['contract_ticker'],
        )
        cleaned['near_product_id'] = near_pid
        cleaned['far_product_id'] = far_pid
        self.product_id = f'{near_pid}+{far_pid}'

        terms_base = {
            'underlying_id': cleaned['underlying_id'],
            'settlement': cleaned['settlement'],
            'currency': cleaned.get('currency', 'USD'),
            'contract_size': cleaned['contract_size'],
            'product_code': cleaned['product_code'],
        }
        for pid, expiry, ticker, field in (
            (near_pid, near['expiry_date'], near['contract_ticker'], 'near_ticker'),
            (far_pid, far['expiry_date'], far['contract_ticker'], 'far_ticker'),
        ):
            conflicts = product_conflicts(
                pid,
                spec=self.spec,
                terms={
                    **terms_base,
                    'expiry_date': expiry,
                    'contract_ticker': ticker,
                },
            )
            if conflicts:
                self.add_error(
                    field,
                    f'Product {pid} already exists on different terms, and the '
                    'importer would silently reuse the existing one. '
                    + '; '.join(conflicts)
                    + '. Adjust the inputs or book against the existing product terms.',
                )
        return cleaned
