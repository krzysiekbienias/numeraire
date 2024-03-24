from django.shortcuts import render
from django.http import HttpResponse,HttpResponseNotFound,HttpResponseRedirect,JsonResponse
from .models import TradeBook
from .forms import MarketForm
from .src.wiener.analytical_methods import EuropeanPlainVanillaOption

# Create your views here.


# --------------------------
# sudo form
# --------------------------

valuation_date:str="2023-05-12"
trade_id=1

# --------------------------
# end sudo form
# --------------------------



def view_suite():
    european_option=EuropeanPlainVanillaOption(valuation_date=valuation_date,trade_id=trade_id)
    european_option.prepare_priceable_dictionary()
    price=european_option.run_analytical_pricer()
    print(price)
    print("the end.")

#     # this function mimic behavior of wraper 




view_suite()
# --------------------------
# End Region source code objects
# --------------------------


def landing_page(request):
    return render(request,"wiener/landing-page.html")

def home_page(request):
    return render(request,"wiener/home-page.html")

def equities(request):
    return render (request,"wiener/eq-dashboard.html")

def trade_book(request):
    if request.headers.get('x-requested-with')=='XMLHttpRequest': #check if request is ajax
        all_trades_qs=TradeBook.objects.all()
        data=[]
        for obj in all_trades_qs:
            item={'pk':obj.pk,
                  'underlier_ticker':obj.underlier_ticker,
                  'und':obj.underlier_ticker,
            'strike':obj.strike,
            'underlier_ticker':obj.underlier_ticker,
            'product_type':obj.product_type,
            'payoff':obj.payoff,
            'trade_date':obj.trade_date,
            'trade_maturity':obj.trade_maturity,
            'dividend':obj.dividend

            }
            data.append(item)
        return JsonResponse({'trades':data})
    return render(request,"wiener/trade-book.html")


def single_trade(request,trade_id:int):
    
    single_trade=TradeBookModel.objects.get(pk=trade_id)
    underlier=single_trade.underlier_ticker

    market_data_form=MarketForm()
    if request.method=="POST":
        filled_form=MarketForm(request.POST)
        #we need to add button submit to triger form
        if filled_form.is_valid():

            european_option=EuropeanPlainVanillaOption(valuation_date=filled_form.cleaned_data['valuation_date'],trade_id=single_trade.pk)
            european_option.prepare_priceable_dictionary()
            return JsonResponse({"pv":european_option.pricable_dict})
            
            
    content={"single_trade":single_trade,
            "form":market_data_form}
    
    return render(request,'geralt/single_trade.html',content)


def test(request):
    return render(request,"wiener/test.html")