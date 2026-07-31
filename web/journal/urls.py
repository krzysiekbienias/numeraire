from django.urls import path

from journal import views

app_name = 'journal'

urlpatterns = [
    path('', views.DashboardView.as_view(), name='dashboard'),
    path('trades/', views.TradeListView.as_view(), name='trade_list'),
    path('trades/<str:trade_id>/', views.TradeDetailView.as_view(), name='trade_detail'),
    path('underliers/', views.UnderlierListView.as_view(), name='underlier_list'),
    path('underliers/<str:ticker>/', views.UnderlierDetailView.as_view(), name='underlier_detail'),
    path('curves/', views.CurveView.as_view(), name='curves'),
    path('exposure/', views.ExposureView.as_view(), name='exposure'),
    path('surfaces/', views.SurfaceView.as_view(), name='surfaces'),
    path('inventory/', views.InventoryView.as_view(), name='inventory'),
    path('greeks/', views.GreeksLabView.as_view(), name='greeks'),
]
