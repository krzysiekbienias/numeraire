#!/usr/bin/env bash
# ============================================================================
# Numeraire++ — configure + build
#
# Usage:
#   scripts/build.sh                # Debug build into ./build/
#   scripts/build.sh Release        # Release build into ./build/
#   BUILD_DIR=out scripts/build.sh  # custom build dir
# ============================================================================
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${1:-Debug}"

cd "${REPO_ROOT}"

echo "[build] Configuring ${BUILD_TYPE} build into ${BUILD_DIR}/ ..."
cmake -S . -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo "[build] Compiling..."
cmake --build "${BUILD_DIR}" -j

echo "[build] Done. Binaries in ${BUILD_DIR}/"
