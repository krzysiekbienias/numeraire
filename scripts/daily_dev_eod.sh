#!/usr/bin/env bash
# ============================================================================
# DEPRECATED — use the three-job split instead:
#   scripts/daily_market_prep.sh     all Polygon / market-data ingest
#   scripts/daily_book_mtm.sh        LIVE book MTM (no booking)
#   scripts/daily_book_exposure.sh   CCR EE/PFE after official FO MTM
#
# This wrapper runs all three in order for backward-compatible cron entries.
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

log() {
    printf '[%s] %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$*"
}

log "DEPRECATED: daily_dev_eod.sh — use daily_market_prep.sh + daily_book_mtm.sh + daily_book_exposure.sh"

"${SCRIPT_DIR}/daily_market_prep.sh"
"${SCRIPT_DIR}/daily_book_mtm.sh"
"${SCRIPT_DIR}/daily_book_exposure.sh"
