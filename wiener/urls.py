
from django.urls import path
from . import views

app_name='wiener'

urlpatterns=[path("", views.landing_page, name="landing-page")
             ]
