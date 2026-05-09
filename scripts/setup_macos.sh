#!/usr/bin/env bash
# ============================================================================
# Numeraire++ — macOS dev setup
#
# Installs every build/test dependency via Homebrew. Idempotent: brew skips
# packages that are already installed.
# ============================================================================
set -euo pipefail

if ! command -v brew >/dev/null 2>&1; then
    echo "ERROR: Homebrew is not installed." >&2
    echo "       Install it first from https://brew.sh" >&2
    exit 1
fi

echo "[setup] Installing Numeraire++ build dependencies via Homebrew..."
brew install \
    cmake \
    ninja \
    quantlib \
    spdlog \
    fmt \
    nlohmann-json \
    sqlite \
    googletest \
    llvm

cat <<'EOF'

[setup] Done. To get the full toolchain available on your PATH, ensure that:

  - clang-tidy is reachable (Homebrew installs it under /opt/homebrew/opt/llvm/bin)
    Add to your shell rc:
        export PATH="/opt/homebrew/opt/llvm/bin:$PATH"

  - .env exists in repo root (copy from .env.example):
        cp .env.example .env

Next steps:
    scripts/build.sh           # configure + build (Debug)
    scripts/test.sh            # run unit tests
    scripts/format.sh          # run clang-format on all sources
EOF
