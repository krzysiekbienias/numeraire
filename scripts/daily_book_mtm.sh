#!/usr/bin/env bash
# ============================================================================
# Numeraire++ — daily book MTM (Hetzner / cron)
#
# End-of-day mark-to-market for LIVE trades only. No Polygon ingest, no booking
# (--price-booking is manual). Market data must already be in SQLite from
# scripts/daily_market_prep.sh.
#
# Usage:
#   /opt/numeraire/dev/scripts/daily_book_mtm.sh
#
# Cron example (after daily_market_prep, e.g. 06:00 UTC Tue–Sat):
#   0 6 * * 2-6 cd /opt/numeraire/dev && ./scripts/daily_book_mtm.sh >> /var/log/numeraire-mtm.log 2>&1
#
# Environment:
#   NUMERAIRE_AS_OF=YYYY-MM-DD       session date (default: last Mon–Fri, UTC lag)
#   NUMERAIRE_AS_OF_LAG_DAYS=1
#   NUMERAIRE_DB_PATH=db.sqlite3
#   NUMERAIRE_DRY_RUN=1
# ============================================================================
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build}"
DEV_MAIN="${REPO_ROOT}/${BUILD_DIR}/dev_main"
DB_PATH="${NUMERAIRE_DB_PATH:-${REPO_ROOT}/db.sqlite3}"

log() {
    printf '[%s] %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$*"
}

die() {
    log "ERROR: $*"
    exit 1
}

run_cmd() {
    log "+ $*"
    if [[ "${NUMERAIRE_DRY_RUN:-0}" == "1" ]]; then
        return 0
    fi
    (
        cd "${REPO_ROOT}"
        "$@"
    )
}

resolve_as_of() {
    if [[ -n "${NUMERAIRE_AS_OF:-}" ]]; then
        echo "${NUMERAIRE_AS_OF}"
        return
    fi
    local lag_days="${NUMERAIRE_AS_OF_LAG_DAYS:-1}"
    local d
    d="$(date -u -d "${lag_days} days ago" +%Y-%m-%d)"
    while true; do
        local dow
        dow="$(date -d "${d}" +%u)"
        if [[ "${dow}" -le 5 ]]; then
            echo "${d}"
            return
        fi
        d="$(date -I -d "${d} - 1 day")"
    done
}

# LIVE trades only (PENDING excluded — booking is manual).
read_live_trade_ids() {
    sqlite3 "${DB_PATH}" "
        SELECT trade_id
        FROM trades
        WHERE upper(trim(status)) = 'LIVE'
        ORDER BY trade_id;
    "
}

write_live_trades_json() {
    local out_path="$1"
    shift
    local -a ids=("$@")
    {
        printf '['
        local i
        for i in "${!ids[@]}"; do
            if [[ "${i}" -gt 0 ]]; then
                printf ','
            fi
            # JSON string escape: ids are TRD_* alphanumeric
            printf '"%s"' "${ids[$i]}"
        done
        printf ']\n'
    } >"${out_path}"
}

main() {
    log "daily_book_mtm start repo=${REPO_ROOT}"

    if [[ ! -x "${DEV_MAIN}" ]]; then
        die "dev_main not found: ${DEV_MAIN} (run scripts/build.sh)"
    fi
    if [[ ! -f "${DB_PATH}" ]]; then
        die "database not found: ${DB_PATH}"
    fi

    local as_of
    as_of="$(resolve_as_of)"
    log "as_of=${as_of} db=${DB_PATH}"

    local -a live_ids=()
    while IFS= read -r line; do
        [[ -n "${line}" ]] && live_ids+=("${line}")
    done < <(read_live_trade_ids)

    if [[ "${#live_ids[@]}" -eq 0 ]]; then
        log "no LIVE trades — nothing to MTM (exit 0)"
        exit 0
    fi

    log "LIVE trades (${#live_ids[@]}): ${live_ids[*]}"

    local trades_json
    trades_json="$(mktemp "${TMPDIR:-/tmp}/numeraire_live_trades.XXXXXX.json")"
    trap 'rm -f "${trades_json}"' EXIT
    write_live_trades_json "${trades_json}" "${live_ids[@]}"

    log "MTM EOD: spot/vol from DB (run daily_market_prep first)"
    run_cmd env NUMERAIRE_DEV_SPOT_SOURCE=db NUMERAIRE_DEV_VOL_SOURCE=db \
        "${DEV_MAIN}" --as-of "${as_of}" --trades-json "${trades_json}"

    log "daily_book_mtm done as_of=${as_of} trades=${#live_ids[@]}"
}

main "$@"
