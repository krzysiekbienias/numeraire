"""Template globals for Numeraire Journal."""

from django.conf import settings


def app_meta(request):
    return {
        'app_version': getattr(settings, 'APP_VERSION', '0.5.7'),
    }
