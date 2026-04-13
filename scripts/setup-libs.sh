#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# setup-libs.sh — Build local ggml + llama.cpp CLI tools.
#
# This is a focused wrapper around setup.sh that excludes model downloads.
#
# Usage:
#   ./scripts/setup-libs.sh [OPTIONS]
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec "$SCRIPT_DIR/setup.sh" --skip-model "$@"
