#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# setup-model.sh — Download model files only.
#
# This is a focused wrapper around setup.sh that skips library/tool builds.
#
# Usage:
#   ./scripts/setup-model.sh [OPTIONS]
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec "$SCRIPT_DIR/setup.sh" --skip-ggml --skip-llama "$@"
