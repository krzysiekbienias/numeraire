from django.urls import path
from django.views.generic import RedirectView

from journal import views

app_name = 'journal'

urlpatterns = [
    path('', views.LandingView.as_view(), name='landing'),
    path('about/', views.AboutView.as_view(), name='about'),
    path('overview/', views.DashboardView.as_view(), name='dashboard'),
    path('trades/', views.TradeListView.as_view(), name='trade_list'),
    path('trades/new/', views.TradeNewView.as_view(), name='trade_new'),
    path('trades/<str:trade_id>/', views.TradeDetailView.as_view(), name='trade_detail'),
    path(
        'trades/<str:trade_id>/price-booking/',
        views.TradePriceBookingView.as_view(),
        name='trade_price_booking',
    ),
    path(
        'trades/<str:trade_id>/delete/',
        views.TradeDeleteView.as_view(),
        name='trade_delete',
    ),
    path('underliers/', views.UnderlierListView.as_view(), name='underlier_list'),
    path('underliers/<str:ticker>/', views.UnderlierDetailView.as_view(), name='underlier_detail'),
    path('curves/', views.CurveView.as_view(), name='curves'),
    path('exposure/', views.ExposureView.as_view(), name='exposure'),
    path('surfaces/', views.SurfaceView.as_view(), name='surfaces'),
    path('inventory/', views.InventoryView.as_view(), name='inventory'),
    path('lab/', views.QuantLabHubView.as_view(), name='quant_lab'),
    path('lab/pricing/', views.QuantLabView.as_view(), name='quant_lab_pricing'),
    path('lab/simulation/', views.SimulationLabView.as_view(), name='simulation_lab'),
    path(
        'simulation/',
        RedirectView.as_view(pattern_name='journal:simulation_lab', permanent=False),
    ),
    path(
        'greeks/',
        RedirectView.as_view(pattern_name='journal:quant_lab_pricing', permanent=False),
        name='greeks',

    ),
    path(
        'lab/rates_conversion/',
        views.RatesConversionLabView.as_view(),
        name='rates_conversion_lab',
    ),
]
