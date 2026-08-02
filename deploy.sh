#!/usr/bin/env bash
set -euo pipefail
export PATH="$HOME/.local/bin:$PATH"
cd /opt/numeraire/prod

echo "[deploy] Pulling latest code..."
git pull origin main

echo "[deploy] Rebuilding dev_main..."
./scripts/build.sh Release

echo "[deploy] Syncing Python deps..."
cd web
uv sync

echo "[deploy] Collecting static..."
uv run python manage.py collectstatic --noinput

echo "[deploy] Running migrations..."
uv run python manage.py migrate --noinput

echo "[deploy] Restarting gunicorn..."
sudo systemctl restart numeraire

echo "[deploy] Done."
