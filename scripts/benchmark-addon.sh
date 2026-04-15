#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build/tests-benchmark"
CONFIG="Release"
FILTER="[benchmark]~[manual]"
WITH_OF="OFF"
NO_BUILD=0

usage() {
cat <<'USAGE'
benchmark-addon.sh - build and run the ofxGgml benchmark suite

Usage:
  ./scripts/benchmark-addon.sh [options]

Options:
  --build-dir PATH            CMake build directory (default: build/tests-benchmark)
  --config NAME               Build type/configuration (default: Release)
  --filter TAG_EXPR           Catch2 filter (default: [benchmark]~[manual])
  --with-openframeworks       Build benchmarks against full openFrameworks
  --no-build                  Reuse existing build directory and only run tests
  --help                      Show this help
USAGE
}

while [[ $# -gt 0 ]]; do
case "$1" in
--build-dir) BUILD_DIR="$2"; shift 2 ;;
--config) CONFIG="$2"; shift 2 ;;
--filter) FILTER="$2"; shift 2 ;;
--with-openframeworks) WITH_OF="ON"; shift ;;
--no-build) NO_BUILD=1; shift ;;
--help|-h) usage; exit 0 ;;
*) echo "Unknown option: $1" >&2; usage; exit 1 ;;
esac
done

cmake -S tests -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$CONFIG" \
  -DOFXGGML_ENABLE_BENCHMARK_TESTS=ON \
  -DOFXGGML_ENABLE_RUNTIME_SOURCES=ON \
  -DOFXGGML_WITH_OPENFRAMEWORKS="$WITH_OF"

if [[ "$NO_BUILD" -ne 1 ]]; then
  cmake --build "$BUILD_DIR" --config "$CONFIG"
fi

TEST_BIN="$BUILD_DIR/ofxGgml-tests"
if [[ -x "$BUILD_DIR/$CONFIG/ofxGgml-tests" ]]; then
  TEST_BIN="$BUILD_DIR/$CONFIG/ofxGgml-tests"
fi

if [[ ! -x "$TEST_BIN" ]]; then
  echo "Error: benchmark executable not found: $TEST_BIN" >&2
  exit 1
fi

"$TEST_BIN" "$FILTER"
