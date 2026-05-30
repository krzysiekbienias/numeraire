#!/usr/bin/env bash
# ============================================================================
# Numeraire++ — daily market data prep (Hetzner / cron)
#
# All Polygon / dev_main market-data ingest:
#   - `market_data_prep_scope` rows (per-ticker 0/1 flags)
#   - book underlyings missing from active equity scope (catch-up for MTM spots)
#
# Run before scripts/daily_book_mtm.sh (MTM only; no ingest there).
#
# Typical flow:
#   0. FRED par yields + discount_curve_eod bootstrap (T-2 lag; see NUMERAIRE_FRED_AS_OF_LAG_DAYS)
#   1. index_daily_eod / equity_daily_eod (per scope)
#   2. option_contract catalog (optional)
#   3. option_universe_eod build (optional)
#   4. option_daily_price_eod (optional)
#   5. vol_surface_eod build (optional; index spot via --index-ticker)
#
# Usage:
#   /opt/numeraire/dev/scripts/daily_market_prep.sh
#
# Cron example (e.g. 04:00 UTC Tue–Sat, before MTM at 06:00):
#   0 4 * * 2-6 cd /opt/numeraire/dev && ./scripts/daily_market_prep.sh >> /var/log/numeraire-prep.log 2>&1
#
# Environment:
#   NUMERAIRE_AS_OF=YYYY-MM-DD       session date (default: last Mon–Fri, UTC lag)
#   NUMERAIRE_AS_OF_LAG_DAYS=1        lag for scope ingest / vol (default 1)
#   NUMERAIRE_FRED_AS_OF_LAG_DAYS=2   lag for FRED par yields + discount bootstrap (default 2)
#   NUMERAIRE_FRED_AS_OF=YYYY-MM-DD   override FRED curve as_of (optional)
#   NUMERAIRE_DISCOUNT_CURVE_ID=USD_TREASURY_PAR_FRED
#   NUMERAIRE_PREP_SKIP_FRED_CURVE=1  skip FRED fetch + discount-curve build
#   NUMERAIRE_DB_PATH=db.sqlite3
#   NUMERAIRE_GRID_CONFIG=configs/option_universe_grid.json
#   NUMERAIRE_PREP_SKIP_BOOK_EQUITY=1   skip equity fetch for book underlyings not in scope
#   NUMERAIRE_DRY_RUN=1
# ============================================================================
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build}"
DEV_MAIN="${REPO_ROOT}/${BUILD_DIR}/dev_main"
DB_PATH="${NUMERAIRE_DB_PATH:-${REPO_ROOT}/db.sqlite3}"
GRID_CONFIG="${NUMERAIRE_GRID_CONFIG:-configs/option_universe_grid.json}"

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

resolve_as_of_with_lag() {
    local lag_days="$1"
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

resolve_as_of() {
    if [[ -n "${NUMERAIRE_AS_OF:-}" ]]; then
        echo "${NUMERAIRE_AS_OF}"
        return
    fi
    resolve_as_of_with_lag "${NUMERAIRE_AS_OF_LAG_DAYS:-1}"
}

resolve_fred_as_of() {
    if [[ -n "${NUMERAIRE_FRED_AS_OF:-}" ]]; then
        echo "${NUMERAIRE_FRED_AS_OF}"
        return
    fi
    resolve_as_of_with_lag "${NUMERAIRE_FRED_AS_OF_LAG_DAYS:-2}"
}

table_exists() {
    sqlite3 "${DB_PATH}" "
        SELECT 1 FROM sqlite_master
        WHERE type='table' AND name='market_data_prep_scope'
        LIMIT 1;
    " | grep -q 1
}

update_prep_status() {
    local scope_id="$1"
    local as_of="$2"
    local status="$3"
    local now
    now="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    if [[ "${NUMERAIRE_DRY_RUN:-0}" == "1" ]]; then
        log "DRY_RUN: would set last_prep scope_id=${scope_id} status=${status}"
        return 0
    fi
    sqlite3 "${DB_PATH}" "
        UPDATE market_data_prep_scope
        SET last_prep_as_of = '${as_of}',
            last_prep_at = '${now}',
            last_prep_status = '${status}'
        WHERE scope_id = '${scope_id}';
    "
}

# Prints pipe-separated rows (no header):
# scope_id|instrument_id|provider_symbol|asset_class|flags...|option_underlying_id|grid_name|equity_adj
# Polygon index symbol for option strike-band / vol build (e.g. I:NDX for NDX).
lookup_index_provider_symbol() {
    local underlying="$1"
    sqlite3 "${DB_PATH}" "
        SELECT provider_symbol
        FROM market_data_prep_scope
        WHERE is_active = 1
          AND asset_class = 'INDEX'
          AND instrument_id = '${underlying}'
        ORDER BY ingest_priority, scope_id
        LIMIT 1;
    "
}

read_prep_scope_rows() {
    local as_of="$1"
    sqlite3 -separator '|' "${DB_PATH}" "
        SELECT
            scope_id,
            instrument_id,
            provider_symbol,
            asset_class,
            ingest_index_eod,
            ingest_equity_eod,
            ingest_option_contracts,
            build_option_universe,
            fetch_option_daily_price_eod,
            build_vol_surface_eod,
            IFNULL(option_underlying_id, ''),
            option_grid_config_name,
            equity_eod_adjusted
        FROM market_data_prep_scope
        WHERE is_active = 1
          AND ingest_from_date <= '${as_of}'
          AND (ingest_to_date IS NULL OR ingest_to_date >= '${as_of}')
        ORDER BY ingest_priority, scope_id;
    "
}

run_scope_row() {
    local as_of="$1"
    local line="$2"

    IFS='|' read -r scope_id instrument_id provider_symbol asset_class \
        ingest_index ingest_equity ingest_contracts build_universe fetch_prices build_surface \
        option_underlying grid_name equity_adj <<<"${line}"

    log "scope ${scope_id}: instrument=${instrument_id} provider=${provider_symbol} class=${asset_class}"

    local index_ticker=""
    if [[ "${asset_class}" == "INDEX" ]]; then
        index_ticker="${provider_symbol}"
    elif [[ -n "${option_underlying}" ]]; then
        index_ticker="$(lookup_index_provider_symbol "${option_underlying}")"
    fi
    local -a index_extra=()
    if [[ -n "${index_ticker}" ]]; then
        index_extra=(--index-ticker "${index_ticker}")
    fi

    local failed=0

    if [[ "${ingest_index}" == "1" ]]; then
        log "  ${scope_id}: ingest_index_eod"
        if ! run_cmd "${DEV_MAIN}" --fetch-index-eod-daily --from "${as_of}" --to "${as_of}" --ticker "${provider_symbol}"; then
            failed=1
        fi
    fi

    if [[ "${ingest_equity}" == "1" ]]; then
        log "  ${scope_id}: ingest_equity_eod"
        local -a equity_args=(--fetch-eod-daily --from "${as_of}" --to "${as_of}" --ticker "${provider_symbol}")
        if [[ "${equity_adj}" == "0" ]]; then
            equity_args+=(--raw)
        fi
        if ! run_cmd "${DEV_MAIN}" "${equity_args[@]}"; then
            failed=1
        fi
    fi

    if [[ -n "${option_underlying}" ]]; then
        if [[ "${ingest_contracts}" == "1" ]]; then
            log "  ${scope_id}: fetch_option_contracts"
            local -a contract_args=(
                --fetch-option-contracts
                --from "${as_of}"
                --to "${as_of}"
                --underlying "${option_underlying}"
            )
            if [[ "${#index_extra[@]}" -gt 0 ]]; then
                contract_args+=("${index_extra[@]}")
            fi
            if ! run_cmd "${DEV_MAIN}" "${contract_args[@]}"; then
                failed=1
            fi
        fi

        if [[ "${build_universe}" == "1" ]]; then
            log "  ${scope_id}: build_option_universe"
            local -a universe_args=(
                --build-option-universe
                --from "${as_of}"
                --to "${as_of}"
                --underlying "${option_underlying}"
                --grid-config "${GRID_CONFIG}"
            )
            if [[ "${#index_extra[@]}" -gt 0 ]]; then
                universe_args+=("${index_extra[@]}")
            fi
            if ! run_cmd "${DEV_MAIN}" "${universe_args[@]}"; then
                failed=1
            fi
        fi

        if [[ "${fetch_prices}" == "1" ]]; then
            log "  ${scope_id}: fetch_option_daily_price_eod"
            local grid_for_fetch="${grid_name}"
            if [[ -z "${grid_for_fetch}" ]]; then
                grid_for_fetch="default_index_option_universe"
            fi
            if ! run_cmd "${DEV_MAIN}" --fetch-option-daily-price-eod \
                --from "${as_of}" --to "${as_of}" \
                --listing-as-of "${as_of}" \
                --underlying "${option_underlying}" \
                --grid-config "${grid_for_fetch}"; then
                failed=1
            fi
        fi

        if [[ "${build_surface}" == "1" ]]; then
            log "  ${scope_id}: build_vol_surface_eod"
            local -a surface_args=(
                --build-vol-surface-eod
                --from "${as_of}"
                --to "${as_of}"
                --underlying "${option_underlying}"
            )
            if [[ "${#index_extra[@]}" -gt 0 ]]; then
                surface_args+=("${index_extra[@]}")
            fi
            if ! run_cmd "${DEV_MAIN}" "${surface_args[@]}"; then
                failed=1
            fi
        fi
    else
        if [[ "${ingest_contracts}" == "1" || "${build_universe}" == "1" || "${fetch_prices}" == "1" || "${build_surface}" == "1" ]]; then
            log "WARN: ${scope_id}: option pipeline flags set but option_underlying_id is empty — skipped"
            failed=1
        fi
    fi

    if [[ "${failed}" -eq 0 ]]; then
        update_prep_status "${scope_id}" "${as_of}" "ok"
    else
        update_prep_status "${scope_id}" "${as_of}" "failed"
        return 1
    fi
    return 0
}

# Book underlyings not covered by an active scope row with ingest_equity_eod=1.
read_book_equity_tickers_outside_prep() {
    local as_of="$1"
    sqlite3 "${DB_PATH}" "
        SELECT DISTINCT p.underlying_id
        FROM trade_legs tl
        JOIN products p ON p.product_id = tl.product_id
        WHERE p.underlying_id NOT IN (
            SELECT instrument_id
            FROM market_data_prep_scope
            WHERE is_active = 1
              AND ingest_equity_eod = 1
              AND ingest_from_date <= '${as_of}'
              AND (ingest_to_date IS NULL OR ingest_to_date >= '${as_of}')
        )
        ORDER BY 1;
    "
}

run_book_equity_catchup() {
    local as_of="$1"
    if [[ "${NUMERAIRE_PREP_SKIP_BOOK_EQUITY:-0}" == "1" ]]; then
        log "book equity catch-up skipped (NUMERAIRE_PREP_SKIP_BOOK_EQUITY=1)"
        return 0
    fi

    local -a tickers=()
    while IFS= read -r line; do
        [[ -n "${line}" ]] && tickers+=("${line}")
    done < <(read_book_equity_tickers_outside_prep "${as_of}")

    if [[ "${#tickers[@]}" -eq 0 ]]; then
        log "book equity catch-up: none (all book underlyings in prep scope or empty book)"
        return 0
    fi

    log "book equity catch-up (${#tickers[@]}): ${tickers[*]}"
    local -a fetch_args=(--fetch-eod-daily --from "${as_of}" --to "${as_of}")
    local t
    for t in "${tickers[@]}"; do
        fetch_args+=(--ticker "${t}")
    done
    if ! run_cmd "${DEV_MAIN}" "${fetch_args[@]}"; then
        return 1
    fi
    return 0
}

run_usd_treasury_discount_curve() {
    if [[ "${NUMERAIRE_PREP_SKIP_FRED_CURVE:-0}" == "1" ]]; then
        log "USD Treasury curve prep skipped (NUMERAIRE_PREP_SKIP_FRED_CURVE=1)"
        return 0
    fi

    local fred_as_of curve_id failed=0
    fred_as_of="$(resolve_fred_as_of)"
    curve_id="${NUMERAIRE_DISCOUNT_CURVE_ID:-USD_TREASURY_PAR_FRED}"
    log "USD Treasury curve prep fred_as_of=${fred_as_of} curve_id=${curve_id} (FRED lag=${NUMERAIRE_FRED_AS_OF_LAG_DAYS:-2})"

    if ! run_cmd python3 scripts/fetch_fred_treasury_par_yields.py \
        --as-of "${fred_as_of}" --db-path "${DB_PATH}"; then
        failed=1
    fi
    if [[ "${failed}" -eq 0 ]] && ! run_cmd "${DEV_MAIN}" \
        --build-discount-curve-eod --as-of "${fred_as_of}" --curve-id "${curve_id}"; then
        failed=1
    fi
    return "${failed}"
}

main() {
    log "daily_market_prep start repo=${REPO_ROOT}"

    if [[ ! -x "${DEV_MAIN}" ]]; then
        die "dev_main not found: ${DEV_MAIN} (run scripts/build.sh)"
    fi
    if [[ ! -f "${DB_PATH}" ]]; then
        die "database not found: ${DB_PATH}"
    fi
    if ! table_exists; then
        die "table market_data_prep_scope missing — apply sql/schema_v1.sql and sql/seed_market_data_prep_scope.sql"
    fi

    local as_of
    as_of="$(resolve_as_of)"
    log "as_of=${as_of} db=${DB_PATH} grid=${GRID_CONFIG}"

    local count=0
    local failed_scopes=0
    if ! run_usd_treasury_discount_curve; then
        failed_scopes=$((failed_scopes + 1))
    fi

    while IFS= read -r line; do
        [[ -z "${line}" ]] && continue
        count=$((count + 1))
        if ! run_scope_row "${as_of}" "${line}"; then
            failed_scopes=$((failed_scopes + 1))
        fi
    done < <(read_prep_scope_rows "${as_of}")

    if [[ "${count}" -eq 0 ]]; then
        die "no active market_data_prep_scope rows for as_of=${as_of}"
    fi

    if ! run_book_equity_catchup "${as_of}"; then
        failed_scopes=$((failed_scopes + 1))
    fi

    log "daily_market_prep done as_of=${as_of} scopes=${count} failed=${failed_scopes}"
    if [[ "${failed_scopes}" -gt 0 ]]; then
        exit 1
    fi
}

main "$@"
