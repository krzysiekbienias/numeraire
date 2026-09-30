"""Serve generated Doxygen HTML at /docs/ (runserver / gunicorn without nginx).

On prod nginx usually aliases this path first; this view is the fallback so the
Journal "C++ docs" link works on dev. LoginRequiredMiddleware still applies.
"""

from django.conf import settings
from django.http import Http404
from django.views.static import serve

_HTML_ROOT = settings.REPO_ROOT / 'docs' / 'doxygen' / 'html'


def cpp_docs(request, path=''):
    if not _HTML_ROOT.is_dir():
        raise Http404('C++ docs are not generated. From the repo root run: doxygen Doxyfile')
    rel = (path or '').strip('/') or 'index.html'
    return serve(request, rel, document_root=_HTML_ROOT)
