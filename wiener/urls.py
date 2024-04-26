from django.urls import path
from . import views

app_name = 'wiener'

urlpatterns = [path("", views.landing_page, name="landing-page"),
               path("fundamentals", views.fundamentals, name="fundamentals"),
               path("home-page", views.home_page, name="home-page"),
               path("home-page/eq-dashboard", views.equities, name="eq-dashboard"),
               path("test", views.test, name="test"),
               path("home-page/eq-dashboard/trade-book", views.trade_book, name="trade-book"),
               # path("home-page/eq-dashboard/trade-book", views.add_new_trade, name="trade-book"),
               path("home-page/eq-dashboard/trade-book/<int:trade_id>", views.single_trade, name="single-trade"),

               #  path("home-page/fx-dashboard",views.fx,name="fx-dashboard"),
               #  path("home-page/ir-dashboard",views.ir,name="ir-dashboard")
               ]
