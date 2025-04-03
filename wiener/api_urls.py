from django.urls import path
from . import views

urlpatterns = [
    path("trade-data/", views.trade_data_api, name="trade-data-api"),  # ✅ Now API URLs are separate
]