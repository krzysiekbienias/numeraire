import decimal

from django.http import JsonResponse
from django.shortcuts import render

from tool_kit.fundamentals import InterestRateFundamentals
from .forms import MarketForm, BookTradeForm, DiscountFactorForm
from .models import TradeBook, DerivativePrice
from wiener.src.wiener.black_scholes_framework.pricer import EuropeanOptionPricer

decimal.getcontext().prec = 6


# Create your views here.


def landing_page(request):
    return render(request, "wiener/landing-page.html")


def home_page(request):
    return render(request, "wiener/home-page.html")


def fundamentals(request):
    return render(request, "wiener/fundamentals.html")


def fundamentals_interest_rate(request):
    discount_factor_form = DiscountFactorForm(request.POST or None)
    if request.method == "POST":
        if discount_factor_form.is_valid():
            discount_factor = InterestRateFundamentals.discount_factor(
                spot_rate=discount_factor_form.cleaned_data['spot_rate'],
                valuation_date=discount_factor_form.cleaned_data['valuation_date'],
                maturity_date=discount_factor_form.cleaned_data['maturity_date'],
                freq_period=discount_factor_form.cleaned_data['freq_period'],
                year_fraction_convention=discount_factor_form.cleaned_data['year_fraction_convention'])
            return JsonResponse({'discount_factor': discount_factor})

    return render(request, "wiener/interest-rate.html")


def fundamentals_fixed_income(request):
    """
    Description
    -----------

    Parameters
    ----------
    request

    Returns
    -------

    """
    return render(request, "wiener/fundamentals/fixed-income.html")


def fundamentals_arbitrage(request):
    return render(request, "wiener/fundamentals/arbitrage.html")


def equities(request):
    return render(request, "wiener/eq-dashboard.html")


def trade_book(request):
    new_trade_model_form = BookTradeForm(request.POST or None, initial={'dividend': 0})
    if request.headers.get('x-requested-with') == 'XMLHttpRequest':  # check if request is ajax
        all_trades_qs = TradeBook.objects.all()

        data = []
        for obj in all_trades_qs:
            item = {'pk': obj.pk,
                    'underlying_ticker': obj.underlying_ticker,
                    'und': obj.underlying_ticker,
                    'strike': obj.strike,
                    'product_type': obj.product_type,
                    'payoff': obj.payoff,
                    'trade_date': obj.trade_date,
                    'trade_maturity': obj.trade_maturity,
                    'dividend': obj.dividend,
                    'user_id': obj.user_id

                    }
            data.append(item)
        if request.method == 'POST':
            if new_trade_model_form.is_valid():
                instance = new_trade_model_form.save(commit=False)
                instance.save()
        return JsonResponse({'trades': data})
    return render(request, "wiener/trade-book.html", {"new_trade_form": new_trade_model_form})


# def add_new_trade(request):
#     new_trade_form = BookTradeForm(request.POST or None)
#     if request.method == 'POST':
#
#         if new_trade_form.is_valid():
#             instance = new_trade_form.save()
#             instance.save()
#     return render(request, "wiener/trade-book.html", {"new_trade_form": new_trade_form})


def single_trade(request, trade_id: int):
    one_trade = TradeBook.objects.get(pk=trade_id)

    market_data_form = MarketForm(request.POST or None)
    if request.method == "POST":
        # we need to add button submit to triger form
        if market_data_form.is_valid():
            european_option = EuropeanOptionPricer(
                valuation_date=market_data_form.cleaned_data["valuation_date"],
                trade_id=one_trade.pk)

            row_to_insert = DerivativePrice(trade_id=TradeBook.objects.get(pk=trade_id),
                                            valuation_date=european_option.__valuation_date,
                                            price_status='Success',
                                            analytical_price=european_option.run_analytical_pricer(),
                                            extra_price=-1,
                                            created_at='2024-04-25',
                                            user_id=one_trade.user_id,
                                            )
            if not DerivativePrice.objects.filter(pk=one_trade.trade_id).exists():
                mess = f"Price for trade {one_trade.trade_id} has been inserted."
                row_to_insert.save()
            else:
                mess = f"Trade {one_trade.trade_id} already has been priced."
            return JsonResponse(
                {"pv_dict": european_option.priceable_dict, "price_trade": european_option.run_analytical_pricer()})

    content = {"id": one_trade.pk, "single_trade": one_trade, "market_data_form": market_data_form}

    return render(request, "wiener/single-trade.html", content)


def test(request):
    return render(request, "wiener/test.html")
