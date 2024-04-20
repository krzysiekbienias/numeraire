from django.shortcuts import render
from django.http import HttpResponse, HttpResponseNotFound, HttpResponseRedirect, JsonResponse
from .models import TradeBook, DerivativePrice
from .forms import MarketForm
from .src.wiener.analytical_methods import EuropeanPlainVanillaOption
import decimal

decimal.getcontext().prec = 6

# Create your views here.


# --------------------------
# sudo form
# --------------------------

valuation_date: str = "2023-05-12"
trade_id = 2


# --------------------------
# end sudo form
# --------------------------


def view_suite():
    european_option = EuropeanPlainVanillaOption(valuation_date=valuation_date, trade_id=trade_id)
    european_option.prepare_priceable_dictionary()
    price = european_option.run_analytical_pricer()
    print(price)
    print("the end.")


#     # this function mimic behavior of wrapper


view_suite()
# --------------------------
# End Region source code objects
# --------------------------


def landing_page(request):
    return render(request, "wiener/landing-page.html")


def home_page(request):
    return render(request, "wiener/home-page.html")


def equities(request):
    return render(request, "wiener/eq-dashboard.html")


def trade_book(request):
    if request.headers.get('x-requested-with') == 'XMLHttpRequest':  # check if request is ajax
        all_trades_qs = TradeBook.objects.all()
        data = []
        for obj in all_trades_qs:
            item = {'pk': obj.pk,
                    'underlier_ticker': obj.underlier_ticker,
                    'und': obj.underlier_ticker,
                    'strike': obj.strike,
                    'product_type': obj.product_type,
                    'payoff': obj.payoff,
                    'trade_date': obj.trade_date,
                    'trade_maturity': obj.trade_maturity,
                    'dividend': obj.dividend,
                    'created_at': obj.created_at,
                    'user_id': obj.user_id

                    }
            data.append(item)
        return JsonResponse({'trades': data})
    return render(request, "wiener/trade-book.html")


def single_trade(request, trade_id: int):
    one_trade = TradeBook.objects.get(pk=trade_id)

    market_data_form = MarketForm(request.POST or None)
    if request.method == "POST":
        # we need to add button submit to triger form
        if market_data_form.is_valid():
            european_option = EuropeanPlainVanillaOption(
                valuation_date=market_data_form.cleaned_data["valuation_date"],
                trade_id=one_trade.pk)

            # row_to_insert=DerivativePrice(trade_id=single_trade.valuation,
            #                            valuation_date=single_trade.valuation_date,
            #                            price_status='Success',
            #                            analytical_price=5,
            #                            monte_carlo_price=5.5,
            #                            extra_price=5.1
            #                            )
            # if not DerivativePrice.objects.filter(pk=trade_book.id).exists():
            #     mess=f"Price for trade {trade_book.id} has been inserted."
            #     row_to_insert.save()
            # else:
            #     mess=f"Trade {trade_book.id} already has been priced."
            return JsonResponse(
                {"pv_dict": european_option.pricable_dict, "price_trade": european_option.run_analytical_pricer()})

    content = {"id": one_trade.pk, "single_trade": one_trade, "market_data_form": market_data_form}

    return render(request, "wiener/single-trade.html", content)


def test(request):
    return render(request, "wiener/test.html")
