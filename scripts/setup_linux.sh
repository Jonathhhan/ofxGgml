#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# setup_linux.sh — Linux/macOS setup entry point for ofxGgml.
#
# Usage:
#   ./scripts/setup_linux.sh [OPTIONS]
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec "$SCRIPT_DIR/setup.sh" "$@"
