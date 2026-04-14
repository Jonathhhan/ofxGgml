#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run-tests.sh — Build and run the ofxGgml test suite
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADDON_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GGML_BUILD_DIR="$ADDON_ROOT/libs/ggml/build"
TEST_BUILD_DIR="$ADDON_ROOT/build/tests"

write_step() {
	printf '==> %s\n' "$1"
}

die() {
	printf 'Error: %s\n' "$1" >&2
	exit 1
}

# Check if ggml is built
if [[ ! -d "$GGML_BUILD_DIR" ]]; then
	die "ggml not built. Run ./scripts/build-ggml.sh first."
fi

# Build tests
write_step "Configuring tests"
cmake -B "$TEST_BUILD_DIR" -S "$SCRIPT_DIR"

write_step "Building tests"
cmake --build "$TEST_BUILD_DIR" --config Release

# Run tests
write_step "Running tests"
ctest --test-dir "$TEST_BUILD_DIR" --output-on-failure -C Release "$@"

write_step "Tests completed successfully"
