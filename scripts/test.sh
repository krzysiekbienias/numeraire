#!/usr/bin/env bash
# ============================================================================
# Numeraire++ — run unit tests
#
# Usage:
#   scripts/test.sh                  # all tests
#   scripts/test.sh Logger           # only tests in suite "Logger.*"
#   scripts/test.sh Logger.LevelInit # exact test
# ============================================================================
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build}"
FILTER="${1:-}"

cd "${REPO_ROOT}"

if [[ ! -x "${BUILD_DIR}/test_environment" ]]; then
    echo "[test] test_environment not built. Running scripts/build.sh first..."
    "${REPO_ROOT}/scripts/build.sh"
fi

if [[ -n "${FILTER}" ]]; then
    case "${FILTER}" in
        *.*) ARG="--gtest_filter=${FILTER}" ;;
        *)   ARG="--gtest_filter=${FILTER}.*" ;;
    esac
    "${BUILD_DIR}/test_environment" "${ARG}"
else
    "${BUILD_DIR}/test_environment"
fi
