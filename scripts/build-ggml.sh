#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# build-ggml.sh — Build the bundled ggml tensor library.
#
# The ggml source is bundled inside libs/ggml/.  This script runs CMake
# to configure and build it, producing static libraries that the addon
# links against.  GPU backends (CUDA, Vulkan, Metal) must be explicitly
# enabled via command-line flags.  By default only the CPU backend is built.
#
# Usage:
#   ./scripts/build-ggml.sh [OPTIONS]
#
# Options:
#   --prefix DIR   Install prefix (default: addon-local libs/ggml/build)
#   --jobs N       Parallel build jobs (default: number of CPU cores)
#   --gpu, --cuda  Enable CUDA backend (requires CUDA toolkit)
#   --vulkan       Enable Vulkan backend (requires Vulkan SDK)
#   --metal        Enable Metal backend (macOS only)
#   --auto         Auto-detect available GPU backends
#   --cpu-only     Disable GPU autodetection, build CPU backend only (default)
#   --clean        Remove build directory before building
#   --help         Show this help message
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADDON_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GGML_DIR="$ADDON_ROOT/libs/ggml"
BUILD_DIR="$GGML_DIR/build"
JOBS=""
ENABLE_CUDA=""
ENABLE_VULKAN=""
ENABLE_METAL=""
AUTO_DETECT=0
CLEAN=0

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

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------

while [[ $# -gt 0 ]]; do
	case "$1" in
		--prefix)
			[[ $# -ge 2 ]] || die "--prefix requires a value"
			BUILD_DIR="$2"
			shift 2
			;;
		--jobs)
			[[ $# -ge 2 ]] || die "--jobs requires a value"
			JOBS="$2"
			shift 2
			;;
		--gpu|--cuda)
			ENABLE_CUDA="ON"
			shift
			;;
		--vulkan)
			ENABLE_VULKAN="ON"
			shift
			;;
		--metal)
			ENABLE_METAL="ON"
			shift
			;;
		--auto)
			AUTO_DETECT=1
			shift
			;;
		--cpu-only)
			AUTO_DETECT=0
			shift
			;;
		--clean)
			CLEAN=1
			shift
			;;
		--help|-h)
			usage
			;;
		*)
			die "Unknown option: $1"
			;;
	esac
done

if [[ -z "$JOBS" ]]; then
	if command -v nproc >/dev/null 2>&1; then
		JOBS="$(nproc)"
	elif command -v sysctl >/dev/null 2>&1; then
		JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
	elif command -v getconf >/dev/null 2>&1; then
		JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
	else
		JOBS=4
	fi
fi

# ---------------------------------------------------------------------------
# Prerequisites
# ---------------------------------------------------------------------------

command -v cmake >/dev/null 2>&1 || die "Required command not found: cmake"

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------

if [[ "$CLEAN" -eq 1 ]]; then
	write_step "Cleaning previous build..."
	rm -rf "$BUILD_DIR"
fi

# ---------------------------------------------------------------------------
# Configure
# ---------------------------------------------------------------------------

write_step "Configuring ggml build..."

mkdir -p "$BUILD_DIR"

CMAKE_ARGS=(
	-DCMAKE_BUILD_TYPE=Release
)

# GPU autodetect is on by default
if [[ "$AUTO_DETECT" -eq 1 ]]; then
	CMAKE_ARGS+=(-DOFXGGML_GPU_AUTODETECT=ON)
else
	CMAKE_ARGS+=(-DOFXGGML_GPU_AUTODETECT=OFF)
fi

# Explicit backend overrides
if [[ -n "$ENABLE_CUDA" ]]; then
	CMAKE_ARGS+=(-DOFXGGML_CUDA="$ENABLE_CUDA")
fi
if [[ -n "$ENABLE_VULKAN" ]]; then
	CMAKE_ARGS+=(-DOFXGGML_VULKAN="$ENABLE_VULKAN")
fi
if [[ -n "$ENABLE_METAL" ]]; then
	CMAKE_ARGS+=(-DOFXGGML_METAL="$ENABLE_METAL")
fi

cmake -B "$BUILD_DIR" "$GGML_DIR" "${CMAKE_ARGS[@]}"

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

write_step "Building ggml with $JOBS parallel jobs..."
cmake --build "$BUILD_DIR" --config Release -j "$JOBS"

# ---------------------------------------------------------------------------
# Verify
# ---------------------------------------------------------------------------

write_step "Verifying build output..."

LIB_DIR="$BUILD_DIR/src"
FOUND=0
for lib in "$LIB_DIR"/libggml*.a "$LIB_DIR"/ggml*.lib "$LIB_DIR"/Release/ggml*.lib; do
	if [[ -f "$lib" ]]; then
		FOUND=1
		write_step "  Found: $lib"
	fi
done

if [[ "$FOUND" -eq 0 ]]; then
	die "No ggml libraries found in $LIB_DIR"
fi

# ---------------------------------------------------------------------------
# Update addon_config.mk with detected libraries
# ---------------------------------------------------------------------------

update_addon_config() {
	local config_file="$ADDON_ROOT/addon_config.mk"
	if [[ ! -f "$config_file" ]]; then
		write_step "Warning: addon_config.mk not found, skipping config update."
		return 0
	fi

	local OS_NAME
	OS_NAME="$(uname -s 2>/dev/null || echo unknown)"

	local section=""
	local search_dir=""
	local ext=""

	case "$OS_NAME" in
		Linux*)
			section="linux64"
			search_dir="$LIB_DIR"
			ext=".a"
			;;
		Darwin*)
			section="osx"
			search_dir="$LIB_DIR"
			ext=".a"
			;;
		MINGW*|MSYS*|CYGWIN*)
			if [[ -d "$LIB_DIR/Release" ]]; then
				section="vs"
				search_dir="$LIB_DIR/Release"
				ext=".lib"
			else
				section="msys2"
				search_dir="$LIB_DIR"
				ext=".a"
			fi
			;;
		*)
			write_step "Warning: Unknown OS '$OS_NAME', skipping addon_config.mk update."
			return 0
			;;
	esac

	# Collect all built ggml libraries
	local libs=()
	local lib_name
	for lib in "$search_dir"/libggml*"$ext" "$search_dir"/ggml*"$ext"; do
		if [[ -f "$lib" ]]; then
			# Make path relative to ADDON_ROOT
			lib_name="${lib#"$ADDON_ROOT"/}"
			libs+=("$lib_name")
		fi
	done

	if [[ ${#libs[@]} -eq 0 ]]; then
		write_step "Warning: No libraries found to update in addon_config.mk."
		return 0
	fi

	# Build the replacement block
	local replacement=""
	replacement=$'\t'"# @DIFFUSION_LIBS_START $section"$'\n'
	for lib_path in "${libs[@]}"; do
		replacement+=$'\t'"ADDON_LIBS += $lib_path"$'\n'
	done
	replacement+=$'\t'"# @DIFFUSION_LIBS_END $section"

	# Use awk to replace the section between markers
	local start_marker="# @DIFFUSION_LIBS_START $section"
	local end_marker="# @DIFFUSION_LIBS_END $section"

	if grep -q "$start_marker" "$config_file" && grep -q "$end_marker" "$config_file"; then
		local tmpfile
		tmpfile="$(mktemp)"
		awk -v start="$start_marker" -v end="$end_marker" -v repl="$replacement" '
			BEGIN { printing=1 }
			$0 ~ start { printing=0; print repl; next }
			$0 ~ end   { printing=1; next }
			printing { print }
		' "$config_file" > "$tmpfile"
		mv "$tmpfile" "$config_file"
		write_step "Updated addon_config.mk [$section] with ${#libs[@]} libraries."
	else
		write_step "Warning: Could not find markers in addon_config.mk for section '$section'."
	fi
}

update_addon_config

write_step "Done! ggml has been built in $BUILD_DIR"
write_step ""
write_step "The addon will automatically find the libraries in this location."
write_step "Build your OF project with ofxGgml."
