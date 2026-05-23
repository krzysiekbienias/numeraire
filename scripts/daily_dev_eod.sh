#!/usr/bin/env bash
# ============================================================================
# Numeraire++ — daily dev job (Hetzner / cron)
#
# Typical flow (cron defaults: ingest + MTM; booking off unless opted in):
#   1. Polygon equity EOD ingest for each book underlying (one session date)
#   2. --price-booking --all  (opt-in; PENDING only — skipped by default)
#   3. --as-of <date> --all MTM for LIVE booked legs (spot from equity_daily_eod)
#
# Usage (from anywhere):
#   /opt/numeraire/dev/scripts/daily_dev_eod.sh
#
# Cron example (Mon–Fri 22:30 UTC — adjust to your US close + Polygon delay):
#   30 22 * * 1-5 /opt/numeraire/dev/scripts/daily_dev_eod.sh >> /var/log/numeraire-daily.log 2>&1
#
# Environment (optional overrides; .env in repo root is loaded by dev_main):
#   NUMERAIRE_AS_OF=YYYY-MM-DD     session date (default: last Mon–Fri before today, UTC)
#   NUMERAIRE_DB_PATH=db.sqlite3
#   NUMERAIRE_SKIP_INGEST=1        skip Polygon fetch
#   NUMERAIRE_SKIP_BOOKING=0       run --price-booking (default: 1 = skip)
#   NUMERAIRE_SKIP_MTM=1           skip MTM
#   NUMERAIRE_AS_OF_LAG_DAYS=1     when AS_OF unset: session date N calendar days ago (UTC)
#   Manual MTM gap fill (server): for d in 2026-05-20 2026-05-21; do
#     NUMERAIRE_AS_OF=$d ./scripts/daily_dev_eod.sh; done
#   (ingest only: add NUMERAIRE_SKIP_MTM=1; MTM only if spots in DB: NUMERAIRE_SKIP_INGEST=1)
#   NUMERAIRE_DRY_RUN=1            print commands only
#   NUMERAIRE_EXTRA_TICKERS="MSFT" extra --ticker symbols (space-separated)
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

# Like run_cmd but does not abort the daily job (e.g. booking on LIVE trades).
run_cmd_soft() {
    log "+ $*"
    if [[ "${NUMERAIRE_DRY_RUN:-0}" == "1" ]]; then
        return 0
    fi
    set +e
    (
        cd "${REPO_ROOT}"
        "$@"
    )
    local rc=$?
    set -e
    if [[ "${rc}" -ne 0 ]]; then
        log "WARN: command exited ${rc} (continuing daily job)"
    fi
    return 0
}

# Session date for ingest/MTM. Does not skip US market holidays.
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

# Distinct equity underlyings referenced by booked legs.
read_tickers_from_db() {
    if [[ ! -f "${DB_PATH}" ]]; then
        die "database not found: ${DB_PATH}"
    fi
    sqlite3 "${DB_PATH}" "
        SELECT DISTINCT p.underlying_id
        FROM trade_legs tl
        JOIN products p ON p.product_id = tl.product_id
        ORDER BY 1;
    "
}

main() {
    log "daily_dev_eod start repo=${REPO_ROOT}"

    if [[ ! -x "${DEV_MAIN}" ]]; then
        die "dev_main not found or not executable: ${DEV_MAIN} (run scripts/build.sh first)"
    fi

    local as_of
    as_of="$(resolve_as_of)"
    log "as_of=${as_of} db=${DB_PATH}"

    local -a tickers=()
    while IFS= read -r line; do
        [[ -n "${line}" ]] && tickers+=("${line}")
    done < <(read_tickers_from_db)

    if [[ -n "${NUMERAIRE_EXTRA_TICKERS:-}" ]]; then
        # shellcheck disable=SC2206
        local extra=(${NUMERAIRE_EXTRA_TICKERS})
        local t
        for t in "${extra[@]}"; do
            tickers+=("${t}")
        done
    fi

    if [[ "${#tickers[@]}" -eq 0 ]]; then
        die "no tickers from DB (empty book?) — set NUMERAIRE_EXTRA_TICKERS or import trades"
    fi

    # De-duplicate tickers (bash 4+)
    local -A seen=()
    local -a unique=()
    local t
    for t in "${tickers[@]}"; do
        if [[ -z "${seen[$t]+x}" ]]; then
            seen["$t"]=1
            unique+=("${t}")
        fi
    done
    tickers=("${unique[@]}")
    log "tickers (${#tickers[@]}): ${tickers[*]}"

    if [[ "${NUMERAIRE_SKIP_INGEST:-0}" != "1" ]]; then
        log "step 1/3: Polygon equity_daily_eod ingest"
        local -a fetch_args=(--fetch-eod-daily --from "${as_of}" --to "${as_of}")
        for t in "${tickers[@]}"; do
            fetch_args+=(--ticker "${t}")
        done
        run_cmd "${DEV_MAIN}" "${fetch_args[@]}"
    else
        log "step 1/3: ingest skipped (NUMERAIRE_SKIP_INGEST=1)"
    fi

    if [[ "${NUMERAIRE_SKIP_BOOKING:-1}" != "1" ]]; then
        log "step 2/3: price-booking (PENDING trades only; errors do not stop MTM)"
        run_cmd_soft "${DEV_MAIN}" --price-booking --all
    else
        log "step 2/3: booking skipped (default; set NUMERAIRE_SKIP_BOOKING=0 to enable)"
    fi

    if [[ "${NUMERAIRE_SKIP_MTM:-0}" != "1" ]]; then
        log "step 3/3: MTM EOD (LIVE + booked legs, spot from DB)"
        run_cmd env NUMERAIRE_DEV_SPOT_SOURCE=db "${DEV_MAIN}" --as-of "${as_of}" --all
    else
        log "step 3/3: MTM skipped (NUMERAIRE_SKIP_MTM=1)"
    fi

    log "daily_dev_eod done as_of=${as_of}"
}

main "$@"
