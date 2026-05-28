#!/usr/bin/env bash
# ============================================================================
# DEPRECATED — use the two-job split instead:
#   scripts/daily_market_prep.sh   all Polygon / market-data ingest
#   scripts/daily_book_mtm.sh      LIVE book MTM only (no booking)
#
# This wrapper runs both in order for backward-compatible cron entries.
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

log() {
    printf '[%s] %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$*"
}

log "DEPRECATED: daily_dev_eod.sh — use daily_market_prep.sh + daily_book_mtm.sh"

"${SCRIPT_DIR}/daily_market_prep.sh"
"${SCRIPT_DIR}/daily_book_mtm.sh"
