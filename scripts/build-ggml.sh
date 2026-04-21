#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# build-ggml.sh — Fetch and build ggml into the addon lib layout.
#
# This script downloads ggml from upstream, builds static libraries, and
# copies the resulting headers and archives into libs/ggml/include and
# libs/ggml/lib. The libs/ggml folder stays empty in git (only gitkeep
# placeholders are tracked) to match the ofxProjectM-style layout.
#
# Usage:
#   ./scripts/build-ggml.sh [OPTIONS]
#
# Options:
#   --jobs N       Parallel build jobs (default: number of CPU cores)
#   --gpu, --cuda  Enable CUDA backend (requires CUDA toolkit)
#   --vulkan       Enable Vulkan backend (requires Vulkan SDK)
#   --metal        Enable Metal backend (macOS only)
#   --auto         Auto-detect available GPU backends (default)
#   --cpu-only     Disable GPU autodetection, build CPU backend only
#   --clean        Remove build and download cache before building
#   --ref REF      Git ref to checkout from the ggml repo (default: v0.10.0)
#   --repo URL     Upstream ggml repository (default: https://github.com/ggml-org/ggml.git)
#   --help         Show this help message
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADDON_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GGML_DIR="$ADDON_ROOT/libs/ggml"
DOWNLOAD_DIR="$GGML_DIR/.download"
SRC_DIR="$DOWNLOAD_DIR/ggml"
BUILD_DIR="$GGML_DIR/build"
INCLUDE_DIR="$GGML_DIR/include"
LIB_DIR="$GGML_DIR/lib"
GGML_REPO="https://github.com/ggml-org/ggml.git"
GGML_REF="v0.10.0"
JOBS=""
ENABLE_CUDA=""
ENABLE_VULKAN=""
ENABLE_METAL=""
AUTO_DETECT=1
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
		--ref)
			[[ $# -ge 2 ]] || die "--ref requires a value"
			GGML_REF="$2"
			shift 2
			;;
		--repo)
			[[ $# -ge 2 ]] || die "--repo requires a value"
			GGML_REPO="$2"
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
command -v git >/dev/null 2>&1 || die "Required command not found: git"

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------

if [[ "$CLEAN" -eq 1 ]]; then
	write_step "Cleaning previous build and download cache..."
	rm -rf "$BUILD_DIR" "$DOWNLOAD_DIR"
	if [[ -d "$INCLUDE_DIR" ]]; then
		find "$INCLUDE_DIR" -mindepth 1 -not -name '.gitkeep' -delete
	fi
	if [[ -d "$LIB_DIR" ]]; then
		find "$LIB_DIR" -mindepth 1 -not -name '.gitkeep' -delete
	fi
fi

mkdir -p "$DOWNLOAD_DIR" "$BUILD_DIR" "$INCLUDE_DIR" "$LIB_DIR"

# ---------------------------------------------------------------------------
# Fetch ggml source
# ---------------------------------------------------------------------------

write_step "Fetching ggml ($GGML_REF) from $GGML_REPO..."

if [[ ! -d "$SRC_DIR/.git" ]]; then
	rm -rf "$SRC_DIR"
	git clone --depth 1 --branch "$GGML_REF" "$GGML_REPO" "$SRC_DIR"
else
	git -C "$SRC_DIR" fetch --depth 1 origin "$GGML_REF"
	git -C "$SRC_DIR" checkout --detach "FETCH_HEAD"
fi

GGML_COMMIT="$(git -C "$SRC_DIR" rev-parse --short HEAD)"
write_step "Using ggml commit: $GGML_COMMIT"

# ---------------------------------------------------------------------------
# Detect GPU backends (optional)
# ---------------------------------------------------------------------------

if [[ "$AUTO_DETECT" -eq 1 ]]; then
	if [[ -z "$ENABLE_CUDA" ]] && command -v nvcc >/dev/null 2>&1; then
		ENABLE_CUDA="ON"
	fi
	if [[ -z "$ENABLE_VULKAN" ]] && { command -v glslc >/dev/null 2>&1 || [[ -n "${VULKAN_SDK:-}" ]]; }; then
		ENABLE_VULKAN="ON"
	fi
	if [[ "$(uname -s)" == "Darwin" && -z "$ENABLE_METAL" ]]; then
		ENABLE_METAL="ON"
	fi
fi

# ---------------------------------------------------------------------------
# Configure
# ---------------------------------------------------------------------------

write_step "Configuring ggml build..."

CMAKE_ARGS=(
	-DCMAKE_BUILD_TYPE=Release
	-DBUILD_SHARED_LIBS=OFF
	-DGGML_BUILD_TESTS=OFF
	-DGGML_BUILD_EXAMPLES=OFF
	-DGGML_NATIVE=ON
	-DGGML_STATIC=ON
	-DGGML_BACKEND_DL=OFF
)

if [[ -n "$ENABLE_CUDA" ]]; then
	CMAKE_ARGS+=(-DGGML_CUDA="$ENABLE_CUDA")
fi
if [[ -n "$ENABLE_VULKAN" ]]; then
	CMAKE_ARGS+=(-DGGML_VULKAN="$ENABLE_VULKAN")
fi
if [[ -n "$ENABLE_METAL" ]]; then
	CMAKE_ARGS+=(-DGGML_METAL="$ENABLE_METAL")
fi

cmake -B "$BUILD_DIR" -S "$SRC_DIR" "${CMAKE_ARGS[@]}"

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

write_step "Building ggml with $JOBS parallel jobs..."
cmake --build "$BUILD_DIR" --config Release -j "$JOBS"

# ---------------------------------------------------------------------------
# Collect headers and libs
# ---------------------------------------------------------------------------

write_step "Exporting headers and libraries..."
find "$INCLUDE_DIR" -mindepth 1 -not -name '.gitkeep' -delete
find "$LIB_DIR" -mindepth 1 -not -name '.gitkeep' -delete

# Headers from upstream include/
if [[ -d "$SRC_DIR/include" ]]; then
	cp -a "$SRC_DIR/include/." "$INCLUDE_DIR/"
fi
touch "$INCLUDE_DIR/.gitkeep"

# Static libraries
LIB_SEARCH=(
	"$BUILD_DIR"/src/libggml*.a
	"$BUILD_DIR"/src/*/Release/ggml*.lib
	"$BUILD_DIR"/src/Release/ggml*.lib
)

FOUND_LIBS=0
for libglob in "${LIB_SEARCH[@]}"; do
	for lib in $libglob; do
		if [[ -f "$lib" ]]; then
			cp "$lib" "$LIB_DIR/"
			FOUND_LIBS=1
		fi
	done
done

if [[ "$FOUND_LIBS" -eq 0 ]]; then
	die "No ggml libraries found in build output"
fi
touch "$LIB_DIR/.gitkeep"

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
	local ext=""

	case "$OS_NAME" in
		Linux*)
			section="linux64"
			ext=".a"
			;;
		Darwin*)
			section="osx"
			ext=".a"
			;;
		MINGW*|MSYS*|CYGWIN*)
			section="vs"
			ext=".lib"
			;;
		*)
			write_step "Warning: Unknown OS '$OS_NAME', skipping addon_config.mk update."
			return 0
			;;
	esac

	local libs=()
	local ordered_names=(
		"libggml$ext"
		"ggml$ext"
		"libggml-base$ext"
		"ggml-base$ext"
		"libggml-cpu$ext"
		"ggml-cpu$ext"
		"libggml-cuda$ext"
		"ggml-cuda$ext"
		"libggml-vulkan$ext"
		"ggml-vulkan$ext"
		"libggml-metal$ext"
		"ggml-metal$ext"
		"libggml-opencl$ext"
		"ggml-opencl$ext"
		"libggml-sycl$ext"
		"ggml-sycl$ext"
	)

	# Collect libs present in LIB_DIR
	local -A selected=()
	local base_name
	local rel_path

	shopt -s nullglob
	for lib in "$LIB_DIR"/*"$ext"; do
		base_name="$(basename "$lib")"
		selected["$base_name"]="$lib"
	done
	shopt -u nullglob

	for base_name in "${ordered_names[@]}"; do
		if [[ -n "${selected[$base_name]:-}" ]]; then
			rel_path="${selected[$base_name]#"$ADDON_ROOT"/}"
			libs+=("$rel_path")
			unset "selected[$base_name]"
		fi
	done

	if [[ ${#selected[@]} -gt 0 ]]; then
		for base_name in $(printf '%s\n' "${!selected[@]}" | sort); do
			rel_path="${selected[$base_name]#"$ADDON_ROOT"/}"
			libs+=("$rel_path")
		done
	fi

	if [[ ${#libs[@]} -eq 0 ]]; then
		write_step "Warning: No libraries found to update in addon_config.mk."
		return 0
	fi

	local replacement=""
	replacement=$'\t'"# @DIFFUSION_LIBS_START $section"$'\n'
	for lib_path in "${libs[@]}"; do
		replacement+=$'\t'"ADDON_LIBS += $lib_path"$'\n'
	done
	replacement+=$'\t'"# @DIFFUSION_LIBS_END $section"

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
		if cmp -s "$tmpfile" "$config_file"; then
			rm -f "$tmpfile"
			write_step "addon_config.mk [$section] already up to date (${#libs[@]} libraries)."
		else
			mv "$tmpfile" "$config_file"
			write_step "Updated addon_config.mk [$section] with ${#libs[@]} libraries."
		fi
	else
		write_step "Warning: Could not find markers in addon_config.mk for section '$section'."
	fi
}

update_addon_config

write_step "Done! ggml built to $LIB_DIR (commit $GGML_COMMIT)"
write_step ""
write_step "Headers: $INCLUDE_DIR"
write_step "Libraries: $LIB_DIR"
