# Sourced by daily_market_prep.sh / daily_book_mtm.sh / daily_book_exposure.sh.
# After as_of is known, duplicate stdout/stderr to
#   $NUMERAIRE_LOG_DIR/numeraire-<kind>-<as_of>.log
# Cron still redirects to the undated numeraire-<kind>.log.
#
# NUMERAIRE_TEE_AS_OF_LOG=0 skips the extra file.

numeraire_tee_as_of_log() {
    local kind="$1"
    local as_of="$2"
    local dir dest

    if [[ "${NUMERAIRE_TEE_AS_OF_LOG:-1}" != "1" ]]; then
        return 0
    fi
    if [[ -z "${kind}" || -z "${as_of}" ]]; then
        return 0
    fi

    dir="${NUMERAIRE_LOG_DIR:-/var/log}"
    dest="${dir}/numeraire-${kind}-${as_of}.log"

    mkdir -p "${dir}" 2>/dev/null || true
    if ! touch "${dest}" 2>/dev/null; then
        log "WARN: cannot write ${dest} — as_of slice skipped"
        return 0
    fi

    exec > >(tee -a "${dest}") 2>&1
    log "as_of log → ${dest}"
}
