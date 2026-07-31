#!/usr/bin/env bash
# ============================================================================
# Numeraire++ — daily book CCR exposure (Hetzner / cron)
#
# After FO MTM: multifactor GBM simulate + path pricing, then persist EE / PFE
# (95% and 97.5%) to trade_leg_exposure_eod. Raw MC paths are not written to
# SQLite (optional CSV dumps via NUMERAIRE_DUMP_* only).
#
# Intended to run from daily_book_mtm.sh (same as_of). Can also be invoked alone.
#
# Usage:
#   /opt/numeraire/dev/scripts/daily_book_exposure.sh
#   NUMERAIRE_AS_OF=2026-06-01 ./scripts/daily_book_exposure.sh
#
# Environment:
#   NUMERAIRE_AS_OF=YYYY-MM-DD       session date (default: last Mon–Fri, UTC lag)
#   NUMERAIRE_AS_OF_LAG_DAYS=1
#   NUMERAIRE_DB_PATH=db.sqlite3
#   NUMERAIRE_SIM_BOOK=BOOK_1        single book (optional)
#   NUMERAIRE_SIM_BOOKS=BOOK_1,BOOK_2  comma/space list (optional; else distinct LIVE portfolios)
#   NUMERAIRE_SKIP_EXPOSURE=1        no-op exit 0
#   NUMERAIRE_DRY_RUN=1
#   NUMERAIRE_MC_PATHS / NUMERAIRE_MC_SEED — passed through to dev_main
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

read_live_portfolio_ids() {
    sqlite3 "${DB_PATH}" "
        SELECT DISTINCT portfolio_id
        FROM trades
        WHERE upper(trim(status)) = 'LIVE'
          AND trim(portfolio_id) != ''
        ORDER BY portfolio_id;
    "
}

resolve_books() {
    local -a books=()
    if [[ -n "${NUMERAIRE_SIM_BOOKS:-}" ]]; then
        # shellcheck disable=SC2206
        books=(${NUMERAIRE_SIM_BOOKS//,/ })
    elif [[ -n "${NUMERAIRE_SIM_BOOK:-}" ]]; then
        books=("${NUMERAIRE_SIM_BOOK}")
    else
        while IFS= read -r line; do
            [[ -n "${line}" ]] && books+=("${line}")
        done < <(read_live_portfolio_ids)
    fi
    printf '%s\n' "${books[@]}"
}

main() {
    log "daily_book_exposure start repo=${REPO_ROOT}"

    if [[ "${NUMERAIRE_SKIP_EXPOSURE:-0}" == "1" ]]; then
        log "NUMERAIRE_SKIP_EXPOSURE=1 — skipping exposure (exit 0)"
        exit 0
    fi

    if [[ ! -x "${DEV_MAIN}" ]]; then
        die "dev_main not found: ${DEV_MAIN} (run scripts/build.sh)"
    fi
    if [[ ! -f "${DB_PATH}" ]]; then
        die "database not found: ${DB_PATH}"
    fi

    local as_of
    as_of="$(resolve_as_of)"
    log "as_of=${as_of} db=${DB_PATH}"

    local -a books=()
    while IFS= read -r line; do
        [[ -n "${line}" ]] && books+=("${line}")
    done < <(resolve_books)

    if [[ "${#books[@]}" -eq 0 ]]; then
        log "no LIVE portfolios / SIM_BOOK — nothing to simulate (exit 0)"
        exit 0
    fi

    log "books (${#books[@]}): ${books[*]}"

    local book
    for book in "${books[@]}"; do
        log "CCR exposure: book=${book} (EE / PFE 95% / PFE 97.5% → trade_leg_exposure_eod)"
        # Persist via CLI flag (does not require NUMERAIRE_PERSIST_EXPOSURE in .env).
        # Market quotes for path pricing: same DB sources as FO MTM.
        run_cmd env \
            NUMERAIRE_DEV_SPOT_SOURCE=db \
            NUMERAIRE_DEV_VOL_SOURCE=db \
            NUMERAIRE_DEV_RATE_SOURCE=db \
            NUMERAIRE_PERSIST_EXPOSURE=1 \
            "${DEV_MAIN}" \
            --simulate \
            --as-of "${as_of}" \
            --book "${book}" \
            --price-paths \
            --persist-exposure
    done

    log "daily_book_exposure done as_of=${as_of} books=${#books[@]}"
}

main "$@"
