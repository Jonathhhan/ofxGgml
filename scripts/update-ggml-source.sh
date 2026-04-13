#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# update-ggml-source.sh — Update the bundled ggml source to the latest version.
#
# Downloads fresh ggml source from upstream and replaces the bundled copy
# in libs/ggml/.  Preserves the addon's CMakeLists.txt wrapper.
#
# Usage:
#   ./scripts/update-ggml-source.sh [--branch BRANCH] [--commit SHA]
#
# Options:
#   --branch BRANCH   Git branch to clone (default: master)
#   --commit SHA      Checkout a specific commit after cloning
#   --help            Show this help message
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADDON_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GGML_DIR="$ADDON_ROOT/libs/ggml"
GGML_REPO="https://github.com/ggml-org/ggml.git"
GGML_BRANCH="master"
GGML_COMMIT=""
TMP_ROOT="${TMPDIR:-/tmp}"
TMP_CLONE="$TMP_ROOT/ggml-update-$$"

write_step() {
	printf '==> %s\n' "$1"
}

die() {
	printf 'Error: %s\n' "$1" >&2
	exit 1
}

usage() {
	sed -n '2,/^# ---/{ /^# ---/d; s/^# //; s/^#//; p }' "$0"
	exit 0
}

cleanup() {
	rm -rf "$TMP_CLONE"
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------

while [[ $# -gt 0 ]]; do
	case "$1" in
		--branch)
			[[ $# -ge 2 ]] || die "--branch requires a value"
			GGML_BRANCH="$2"
			shift 2
			;;
		--commit)
			[[ $# -ge 2 ]] || die "--commit requires a value"
			GGML_COMMIT="$2"
			shift 2
			;;
		--help|-h)
			usage
			;;
		*)
			die "Unknown option: $1"
			;;
	esac
done

# ---------------------------------------------------------------------------
# Clone
# ---------------------------------------------------------------------------

write_step "Cloning ggml from $GGML_REPO (branch: $GGML_BRANCH)..."
git clone --branch "$GGML_BRANCH" --depth 1 "$GGML_REPO" "$TMP_CLONE"

if [[ -n "$GGML_COMMIT" ]]; then
	write_step "Checking out commit $GGML_COMMIT..."
	cd "$TMP_CLONE"
	git fetch --depth 1 origin "$GGML_COMMIT"
	git checkout "$GGML_COMMIT"
fi

# Record the commit we're bundling
BUNDLED_COMMIT="$(cd "$TMP_CLONE" && git rev-parse --short HEAD)"
write_step "Bundling ggml commit: $BUNDLED_COMMIT"

# ---------------------------------------------------------------------------
# Update bundled source
# ---------------------------------------------------------------------------

write_step "Updating bundled ggml source..."

# Save our custom CMakeLists.txt
cp "$GGML_DIR/CMakeLists.txt" "$TMP_CLONE/CMakeLists.txt.addon"

# Remove old source (keep build/ directory if it exists)
SAVE_BUILD=""
if [[ -d "$GGML_DIR/build" ]]; then
	SAVE_BUILD="$TMP_ROOT/ggml-build-save-$$"
	mv "$GGML_DIR/build" "$SAVE_BUILD"
fi

rm -rf "$GGML_DIR/include" "$GGML_DIR/src" "$GGML_DIR/cmake"

# Copy new source
cp -r "$TMP_CLONE/include" "$GGML_DIR/"
mkdir -p "$GGML_DIR/src"

# Core source files
for f in ggml.c ggml.cpp ggml-alloc.c ggml-backend.cpp ggml-opt.cpp \
         ggml-threading.cpp ggml-threading.h ggml-quants.c ggml-quants.h \
         ggml-impl.h ggml-backend-impl.h ggml-common.h gguf.cpp \
         ggml-backend-reg.cpp ggml-backend-dl.cpp ggml-backend-dl.h; do
	cp "$TMP_CLONE/src/$f" "$GGML_DIR/src/"
done

# Copy upstream src/CMakeLists.txt
cp "$TMP_CLONE/src/CMakeLists.txt" "$GGML_DIR/src/"

# Backend directories
cp -r "$TMP_CLONE/src/ggml-cpu"    "$GGML_DIR/src/"
cp -r "$TMP_CLONE/src/ggml-cuda"   "$GGML_DIR/src/" 2>/dev/null || true
cp -r "$TMP_CLONE/src/ggml-vulkan" "$GGML_DIR/src/" 2>/dev/null || true
cp -r "$TMP_CLONE/src/ggml-metal"  "$GGML_DIR/src/" 2>/dev/null || true

# cmake helpers
cp -r "$TMP_CLONE/cmake" "$GGML_DIR/"

# Restore our custom CMakeLists.txt
mv "$TMP_CLONE/CMakeLists.txt.addon" "$GGML_DIR/CMakeLists.txt"

# Restore build directory
if [[ -n "$SAVE_BUILD" ]] && [[ -d "$SAVE_BUILD" ]]; then
	mv "$SAVE_BUILD" "$GGML_DIR/build"
fi

# Update commit reference in CMakeLists.txt
sed -i "s/set(GGML_BUILD_COMMIT .*/set(GGML_BUILD_COMMIT \"$BUNDLED_COMMIT\")/" "$GGML_DIR/CMakeLists.txt"

# Update version from upstream
UPSTREAM_MAJOR=$(grep 'set(GGML_VERSION_MAJOR' "$TMP_CLONE/CMakeLists.txt" | grep -o '[0-9]\+')
UPSTREAM_MINOR=$(grep 'set(GGML_VERSION_MINOR' "$TMP_CLONE/CMakeLists.txt" | grep -o '[0-9]\+')
UPSTREAM_PATCH=$(grep 'set(GGML_VERSION_PATCH' "$TMP_CLONE/CMakeLists.txt" | grep -o '[0-9]\+')

if [[ -n "$UPSTREAM_MAJOR" ]]; then
	sed -i "s/set(GGML_VERSION_MAJOR .*/set(GGML_VERSION_MAJOR $UPSTREAM_MAJOR)/" "$GGML_DIR/CMakeLists.txt"
	sed -i "s/set(GGML_VERSION_MINOR .*/set(GGML_VERSION_MINOR $UPSTREAM_MINOR)/" "$GGML_DIR/CMakeLists.txt"
	sed -i "s/set(GGML_VERSION_PATCH .*/set(GGML_VERSION_PATCH $UPSTREAM_PATCH)/" "$GGML_DIR/CMakeLists.txt"
fi

write_step "Done! ggml source updated to commit $BUNDLED_COMMIT"
write_step "Run ./scripts/build-ggml.sh --clean to rebuild."
