#!/usr/bin/env bash
# ============================================================================
# Numeraire++ — clean build outputs and runtime artifacts
# ============================================================================
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

rm -rf build/ out/ logs/ .cache/ compile_commands.json
echo "[clean] Removed: build/ out/ logs/ .cache/ compile_commands.json"
