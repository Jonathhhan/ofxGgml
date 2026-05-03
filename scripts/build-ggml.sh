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
#   --with-debug   Also build Debug libraries when using a multi-config generator
#   --ref REF      Git ref to checkout from the ggml repo (default: v0.10.0)
#   --repo URL     Upstream ggml repository (default: https://github.com/ggml-org/ggml.git)
#   --help         Show this help message
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADDON_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GGML_DIR="$ADDON_ROOT/libs/ggml"
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
WITH_DEBUG=0
CMAKE_CMD="cmake"
CMAKE_GENERATOR=""
CMAKE_GENERATOR_ARGS=()

OS_NAME="$(uname -s 2>/dev/null || echo unknown)"

if [[ "$OS_NAME" == MINGW* || "$OS_NAME" == MSYS* || "$OS_NAME" == CYGWIN* ]]; then
	WINDOWS_CACHE_ROOT=""
	if [[ -n "${LOCALAPPDATA:-}" ]]; then
		WINDOWS_CACHE_ROOT="$(cygpath -u "$LOCALAPPDATA" 2>/dev/null || true)"
	fi
	if [[ -z "$WINDOWS_CACHE_ROOT" ]]; then
		WINDOWS_CACHE_ROOT="/tmp"
	fi
	WORK_ROOT="$WINDOWS_CACHE_ROOT/ofxGgml/ggml"
	DOWNLOAD_DIR="$WORK_ROOT/download"
	SRC_DIR="$DOWNLOAD_DIR/ggml"
	BUILD_DIR="$WORK_ROOT/build"
else
	DOWNLOAD_DIR="$GGML_DIR/.download"
	SRC_DIR="$DOWNLOAD_DIR/ggml"
	BUILD_DIR="$GGML_DIR/build"
fi

write_step() {
	printf '==> %s\n' "$1"
}

die() {
	printf 'Error: %s\n' "$1" >&2
	exit 1
}

cmake_supports_generator() {
	local generator="$1"
	"$CMAKE_CMD" --help 2>/dev/null | grep -Fq "$generator"
}

resolve_windows_cmake() {
	local candidates=(
		"/c/Program Files/CMake/bin/cmake.exe"
		"/c/Program Files (x86)/CMake/bin/cmake.exe"
	)
	local candidate

	for candidate in "${candidates[@]}"; do
		if [[ -x "$candidate" ]]; then
			printf '%s\n' "$candidate"
			return 0
		fi
	done

	return 1
}

resolve_vswhere() {
	local candidates=(
		"/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
		"/c/Program Files/Microsoft Visual Studio/Installer/vswhere.exe"
	)
	local candidate

	for candidate in "${candidates[@]}"; do
		if [[ -x "$candidate" ]]; then
			printf '%s\n' "$candidate"
			return 0
		fi
	done

	if command -v vswhere.exe >/dev/null 2>&1; then
		command -v vswhere.exe
		return 0
	fi
	if command -v vswhere >/dev/null 2>&1; then
		command -v vswhere
		return 0
	fi

	return 1
}

generator_for_visual_studio_version() {
	local version="$1"

	case "$version" in
		18.*) printf '%s\n' "Visual Studio 18 2026" ;;
		17.*) printf '%s\n' "Visual Studio 17 2022" ;;
		16.*) printf '%s\n' "Visual Studio 16 2019" ;;
		*) return 1 ;;
	esac
}

detect_windows_cmake_generator() {
	local generators=(
		"Visual Studio 18 2026"
		"Visual Studio 17 2022"
		"Visual Studio 16 2019"
	)
	local vswhere
	local version
	local generator

	if vswhere="$(resolve_vswhere)"; then
		while IFS= read -r version; do
			[[ -n "$version" ]] || continue
			generator="$(generator_for_visual_studio_version "$version" || true)"
			[[ -n "$generator" ]] || continue
			if cmake_supports_generator "$generator"; then
				printf '%s\n' "$generator"
				return 0
			fi
		done < <("$vswhere" -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion 2>/dev/null | sort -Vr)

		return 1
	fi

	for generator in "${generators[@]}"; do
		if cmake_supports_generator "$generator"; then
			printf '%s\n' "$generator"
			return 0
		fi
	done

	return 1
}

read_cmake_cache_value() {
	local cache_file="$1"
	local key="$2"

	if [[ ! -f "$cache_file" ]]; then
		return 1
	fi

	awk -F= -v key="$key" '
		$1 ~ ("^" key "(:|$)") {
			print substr($0, index($0, "=") + 1)
			exit
		}
	' "$cache_file"
}

reset_cmake_build_dir() {
	local reason="$1"

	write_step "$reason"
	rm -rf "$BUILD_DIR"
	mkdir -p "$BUILD_DIR"
}

prepare_cmake_build_dir() {
	local cache_file="$BUILD_DIR/CMakeCache.txt"
	local cached_generator
	local cached_option
	local option_name
	local desired_option

	if [[ ! -f "$cache_file" ]]; then
		return 0
	fi

	cached_generator="$(read_cmake_cache_value "$cache_file" "CMAKE_GENERATOR" || true)"
	if [[ -n "$CMAKE_GENERATOR" && -n "$cached_generator" && "$cached_generator" != "$CMAKE_GENERATOR" ]]; then
		reset_cmake_build_dir "CMake generator changed from '$cached_generator' to '$CMAKE_GENERATOR'; resetting build tree..."
		return 0
	fi

	for option_name in GGML_CUDA GGML_VULKAN GGML_METAL; do
		case "$option_name" in
			GGML_CUDA) desired_option="$ENABLE_CUDA" ;;
			GGML_VULKAN) desired_option="$ENABLE_VULKAN" ;;
			GGML_METAL) desired_option="$ENABLE_METAL" ;;
		esac

		if [[ -z "$desired_option" ]]; then
			continue
		fi

		cached_option="$(read_cmake_cache_value "$cache_file" "$option_name" || true)"
		if [[ -n "$cached_option" && "$cached_option" != "$desired_option" ]]; then
			reset_cmake_build_dir "CMake option $option_name changed from '$cached_option' to '$desired_option'; resetting build tree..."
			return 0
		fi
	done
}

prune_stale_build_libraries() {
	if [[ ! -d "$BUILD_DIR" ]]; then
		return 0
	fi

	find "$BUILD_DIR" -type f \( -name "libggml*.a" -o -name "ggml*.lib" \) -delete
}

files_match_ignoring_cr() {
	local left="$1"
	local right="$2"

	cmp -s <(awk '{ sub(/\r$/, ""); print }' "$left") <(awk '{ sub(/\r$/, ""); print }' "$right")
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
		--with-debug)
			WITH_DEBUG=1
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

command -v git >/dev/null 2>&1 || die "Required command not found: git"

if [[ "$OS_NAME" == MINGW* || "$OS_NAME" == MSYS* || "$OS_NAME" == CYGWIN* ]]; then
	if CMAKE_CMD="$(resolve_windows_cmake)"; then
		:
	else
		command -v cmake >/dev/null 2>&1 || die "Required command not found: cmake"
	fi

	if CMAKE_GENERATOR="$(detect_windows_cmake_generator)"; then
		CMAKE_GENERATOR_ARGS=(-G "$CMAKE_GENERATOR")
	else
		die "No supported Visual Studio CMake generator was found. Install Visual Studio Build Tools or run from a configured native build environment."
	fi
else
	command -v cmake >/dev/null 2>&1 || die "Required command not found: cmake"
fi

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

if [[ -z "$ENABLE_CUDA" ]]; then
	ENABLE_CUDA="OFF"
fi
if [[ -z "$ENABLE_VULKAN" ]]; then
	ENABLE_VULKAN="OFF"
fi
if [[ -z "$ENABLE_METAL" ]]; then
	ENABLE_METAL="OFF"
fi

prepare_cmake_build_dir

# ---------------------------------------------------------------------------
# Configure
# ---------------------------------------------------------------------------

write_step "Configuring ggml build..."

CMAKE_ARGS=(
	-DCMAKE_BUILD_TYPE=Release
	-DBUILD_SHARED_LIBS=OFF
	-DCMAKE_C_FLAGS=-DGGML_MAX_NAME=128
	-DCMAKE_CXX_FLAGS=-DGGML_MAX_NAME=128
	-DCMAKE_CUDA_FLAGS=-DGGML_MAX_NAME=128
	-DGGML_BUILD_TESTS=OFF
	-DGGML_BUILD_EXAMPLES=OFF
	-DGGML_NATIVE=ON
	-DGGML_STATIC=ON
	-DGGML_BACKEND_DL=OFF
	-DGGML_CUDA="$ENABLE_CUDA"
	-DGGML_VULKAN="$ENABLE_VULKAN"
	-DGGML_METAL="$ENABLE_METAL"
)

if [[ -n "$CMAKE_GENERATOR" ]]; then
	write_step "Using CMake generator: $CMAKE_GENERATOR"
fi

"$CMAKE_CMD" "${CMAKE_GENERATOR_ARGS[@]}" -B "$BUILD_DIR" -S "$SRC_DIR" "${CMAKE_ARGS[@]}"

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

write_step "Building ggml with $JOBS parallel jobs..."
prune_stale_build_libraries
"$CMAKE_CMD" --build "$BUILD_DIR" --config Release -j "$JOBS"

if [[ "$WITH_DEBUG" -eq 1 ]]; then
	write_step "Building ggml Debug libraries with $JOBS parallel jobs..."
	"$CMAKE_CMD" --build "$BUILD_DIR" --config Debug -j "$JOBS"
fi

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

# Static libraries (recursively for ggml 0.10.x backend layout)
declare -A LIB_MAP=()
declare -A LIB_PRIO=()
declare -A DEBUG_LIB_MAP=()

while IFS= read -r -d '' lib; do
	base_name="$(basename "$lib")"

	if [[ "$lib" == *"/Debug/"* ]]; then
		DEBUG_LIB_MAP[$base_name]="$lib"
	else
		# Prefer non-config/Makefile outputs, then Release/RelWithDebInfo/MinSizeRel.
		priority=1
		if [[ "$lib" == *"/Release/"* || "$lib" == *"/RelWithDebInfo/"* || "$lib" == *"/MinSizeRel/"* ]]; then
			priority=1
		else
			priority=2
		fi

		if [[ -z "${LIB_MAP[$base_name]:-}" || "${LIB_PRIO[$base_name]:-0}" -lt "$priority" ]]; then
			LIB_MAP[$base_name]="$lib"
			LIB_PRIO[$base_name]="$priority"
		fi
	fi
done < <(find "$BUILD_DIR" -type f \( -name "libggml*.a" -o -name "ggml*.lib" \) -print0)

if [[ ${#LIB_MAP[@]} -eq 0 ]]; then
	die "No ggml libraries found in build output"
fi

for base_name in $(printf '%s\n' "${!LIB_MAP[@]}" | sort); do
	cp "${LIB_MAP[$base_name]}" "$LIB_DIR/$base_name"
done

if [[ "$WITH_DEBUG" -eq 1 && ${#DEBUG_LIB_MAP[@]} -gt 0 ]]; then
	mkdir -p "$LIB_DIR/Release" "$LIB_DIR/Debug"
	for base_name in $(printf '%s\n' "${!LIB_MAP[@]}" | sort); do
		cp "${LIB_MAP[$base_name]}" "$LIB_DIR/Release/$base_name"
	done
	for base_name in $(printf '%s\n' "${!DEBUG_LIB_MAP[@]}" | sort); do
		cp "${DEBUG_LIB_MAP[$base_name]}" "$LIB_DIR/Debug/$base_name"
	done
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
	local use_vs_config_dirs=0

	if [[ "$section" == "vs" && -d "$LIB_DIR/Release" && -d "$LIB_DIR/Debug" ]]; then
		use_vs_config_dirs=1
		shopt -s nullglob
		for lib in "$LIB_DIR/Release"/*"$ext"; do
			base_name="$(basename "$lib")"
			if [[ -f "$LIB_DIR/Debug/$base_name" ]]; then
				selected["$base_name"]="libs/ggml/lib/\$(Configuration)/$base_name"
			else
				write_step "Warning: Debug library missing for $base_name; skipping config-specific Visual Studio entry."
			fi
		done
		shopt -u nullglob
	else
		shopt -s nullglob
		for lib in "$LIB_DIR"/*"$ext"; do
			base_name="$(basename "$lib")"
			selected["$base_name"]="$lib"
		done
		shopt -u nullglob
	fi

	for base_name in "${ordered_names[@]}"; do
		if [[ -n "${selected[$base_name]:-}" ]]; then
			if [[ "$use_vs_config_dirs" -eq 1 ]]; then
				rel_path="${selected[$base_name]}"
			else
				rel_path="${selected[$base_name]#"$ADDON_ROOT"/}"
			fi
			libs+=("$rel_path")
			unset "selected[$base_name]"
		fi
	done

	if [[ ${#selected[@]} -gt 0 ]]; then
		for base_name in $(printf '%s\n' "${!selected[@]}" | sort); do
			if [[ "$use_vs_config_dirs" -eq 1 ]]; then
				rel_path="${selected[$base_name]}"
			else
				rel_path="${selected[$base_name]#"$ADDON_ROOT"/}"
			fi
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
		if files_match_ignoring_cr "$tmpfile" "$config_file"; then
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
