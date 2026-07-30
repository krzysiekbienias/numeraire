"""Root URL configuration.

`LoginRequiredMiddleware` guards everything here; the auth views bundled under
`django.contrib.auth.urls` carry their own exemption, so /accounts/login/ stays reachable.
"""

from django.contrib import admin
from django.urls import include, path

urlpatterns = [
    path('admin/', admin.site.urls),
    path('accounts/', include('django.contrib.auth.urls')),
    path('', include('journal.urls')),
]
