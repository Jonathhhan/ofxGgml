#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run-tests.sh — Build and run the ofxGgml test suite
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADDON_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GGML_LIB_DIR="$ADDON_ROOT/libs/ggml/lib"
TEST_BUILD_DIR="$ADDON_ROOT/build/tests"

write_step() {
	printf '==> %s\n' "$1"
}

die() {
	printf 'Error: %s\n' "$1" >&2
	exit 1
}

# Check if ggml is built
if [[ ! -d "$GGML_LIB_DIR" ]] || [[ -z "$(find "$GGML_LIB_DIR" -maxdepth 1 -type f \( -name 'libggml*' -o -name 'ggml*.lib' \) | head -n1)" ]]; then
	die "ggml libraries not found. Run ./scripts/build-ggml.sh first."
fi

# Build tests
write_step "Configuring tests"
cmake -B "$TEST_BUILD_DIR" -S "$SCRIPT_DIR"

write_step "Building tests"
cmake --build "$TEST_BUILD_DIR" --config Release

# Run tests
write_step "Running tests"
"$TEST_BUILD_DIR/ofxGgml-tests" "$@"

write_step "Tests completed successfully"
