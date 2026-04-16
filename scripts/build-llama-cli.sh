#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# build-llama-cli.sh — Clone, compile, and install llama.cpp CLI tools.
#
# Builds both llama-cli (interactive chat) and llama-completion (one-shot
# text completion) from the llama.cpp project.  The ofxGgmlGuiExample uses
# llama-completion for inference and falls back to llama-cli for older
# installations.
#
# Usage:
#   ./scripts/build-llama-cli.sh [--prefix DIR] [--jobs 8] [--gpu]
#
# Options:
#   --prefix DIR   Install prefix (default: addon-local libs/llama)
#   --jobs N       Parallel build jobs (default: number of CPU cores)
#   --gpu, --cuda  Enable CUDA backend
#   --vulkan       Enable Vulkan backend
#   --metal        Enable Metal backend (macOS only)
#   --auto         Auto-detect available GPU backends and enable them
#   --clean        Remove build directory before building
#   --help         Show this help message
# ---------------------------------------------------------------------------
set -euo pipefail

LLAMA_REPO="https://github.com/ggml-org/llama.cpp.git"
LLAMA_BRANCH="master"
ADDON_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ADDON_ROOT/build/llama.cpp-build"
SOURCE_DIR="$ADDON_ROOT/build/llama.cpp-src"
DEFAULT_INSTALL_PREFIX="$ADDON_ROOT/libs/llama"
JOBS=""
ENABLE_CUDA=0
ENABLE_VULKAN=0
ENABLE_METAL=0
VULKAN_EXPLICIT=0
CLEAN=0
AUTO_DETECT=1
CONFIGURE_LOG=""

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

cleanup_temp_files() {
	if [[ -n "${CONFIGURE_LOG:-}" ]] && [[ -f "$CONFIGURE_LOG" ]]; then
		rm -f "$CONFIGURE_LOG"
	fi
}
trap cleanup_temp_files EXIT

is_windows_like() {
	case "$(uname -s 2>/dev/null || echo unknown)" in
		MINGW*|MSYS*|CYGWIN*)
			return 0
			;;
		*)
			return 1
			;;
	esac
}

INSTALL_PREFIX="$DEFAULT_INSTALL_PREFIX"

detect_cuda() {
	if command -v nvcc >/dev/null 2>&1; then return 0; fi
	if command -v nvidia-smi >/dev/null 2>&1; then return 0; fi
	for cuda_dir in /usr/local/cuda /opt/cuda; do
		if [[ -x "$cuda_dir/bin/nvcc" ]]; then return 0; fi
	done
	return 1
}

detect_vulkan() {
	local has_glslc=0
	if command -v glslc >/dev/null 2>&1; then
		has_glslc=1
	elif [[ -n "${VULKAN_SDK:-}" ]]; then
		for glslc_path in \
			"$VULKAN_SDK/Bin/glslc" \
			"$VULKAN_SDK/Bin/glslc.exe" \
			"$VULKAN_SDK/bin/glslc" \
			"$VULKAN_SDK/bin/glslc.exe"; do
			if [[ -x "$glslc_path" ]]; then
				has_glslc=1
				break
			fi
		done
	fi

	# Runtime-only Vulkan installs are insufficient; require shader compiler tooling.
	[[ "$has_glslc" -eq 1 ]] || return 1

	if command -v vulkaninfo >/dev/null 2>&1; then return 0; fi
	if [[ -n "${VULKAN_SDK:-}" ]] && [[ -d "$VULKAN_SDK" ]]; then return 0; fi
	return 1
}

detect_metal() {
	[[ "$(uname -s 2>/dev/null)" == "Darwin" ]]
}

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------

while [[ $# -gt 0 ]]; do
	case "$1" in
		--prefix)
			[[ $# -ge 2 ]] || die "--prefix requires a value"
			INSTALL_PREFIX="$2"
			shift 2
			;;
		--jobs)
			[[ $# -ge 2 ]] || die "--jobs requires a value"
			JOBS="$2"
			shift 2
			;;
		--gpu|--cuda)
			ENABLE_CUDA=1
			shift
			;;
		--vulkan)
			ENABLE_VULKAN=1
			VULKAN_EXPLICIT=1
			shift
			;;
		--metal)
			ENABLE_METAL=1
			shift
			;;
		--auto)
			AUTO_DETECT=1
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
# Auto-detect GPU backends
# ---------------------------------------------------------------------------

if [[ "$AUTO_DETECT" -eq 1 ]]; then
	write_step "Auto-detecting GPU backends..."
	if detect_cuda; then
		write_step "  CUDA detected — enabling CUDA backend."
		ENABLE_CUDA=1
	fi
	if detect_vulkan; then
		write_step "  Vulkan detected — enabling Vulkan backend."
		ENABLE_VULKAN=1
	fi
	if detect_metal; then
		write_step "  Metal detected — enabling Metal backend."
		ENABLE_METAL=1
	fi
	if [[ "$ENABLE_CUDA" -eq 0 ]] && [[ "$ENABLE_VULKAN" -eq 0 ]] && [[ "$ENABLE_METAL" -eq 0 ]]; then
		write_step "  No GPU backends detected — building CPU-only."
	fi
fi

# ---------------------------------------------------------------------------
# Prerequisites
# ---------------------------------------------------------------------------

for cmd in git cmake; do
	command -v "$cmd" >/dev/null 2>&1 || die "Required command not found: $cmd"
done

# ---------------------------------------------------------------------------
# Clone / update source
# ---------------------------------------------------------------------------

if [[ "$CLEAN" -eq 1 ]]; then
	write_step "Cleaning previous build..."
	rm -rf "$BUILD_DIR" "$SOURCE_DIR"
fi

if [[ -d "$SOURCE_DIR/.git" ]]; then
	write_step "Updating existing llama.cpp source in $SOURCE_DIR..."
	cd "$SOURCE_DIR"
	git fetch origin
	git checkout "$LLAMA_BRANCH"
	git pull origin "$LLAMA_BRANCH"
else
	write_step "Cloning llama.cpp from $LLAMA_REPO..."
	rm -rf "$SOURCE_DIR"
	git clone --branch "$LLAMA_BRANCH" --depth 1 "$LLAMA_REPO" "$SOURCE_DIR"
fi

# ---------------------------------------------------------------------------
# Configure
# ---------------------------------------------------------------------------

write_step "Configuring llama.cpp build..."

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# LLAMA_BUILD_SERVER=ON is required because the new llama-cli (interactive
# chat mode, merged via ggml-org/llama.cpp#17824) depends on server-context.
# llama-completion (one-shot text completion) builds without it, but we
# enable it so both tools are available.
CMAKE_ARGS=(
	-DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX"
	-DCMAKE_BUILD_TYPE=Release
	-DBUILD_SHARED_LIBS=OFF
	-DLLAMA_BUILD_TESTS=OFF
	-DLLAMA_BUILD_EXAMPLES=OFF
	-DLLAMA_BUILD_SERVER=ON
	-DLLAMA_CURL=OFF
)

# Explicitly set all GPU backends to prevent llama.cpp's CMake from auto-detecting
# and failing when dependencies are incomplete (e.g., Vulkan without glslc).
if [[ "$ENABLE_CUDA" -eq 1 ]]; then
	write_step "CUDA backend enabled."
	CMAKE_ARGS+=(-DGGML_CUDA=ON)
else
	CMAKE_ARGS+=(-DGGML_CUDA=OFF)
fi

if [[ "$ENABLE_VULKAN" -eq 1 ]]; then
	write_step "Vulkan backend enabled."
	CMAKE_ARGS+=(-DGGML_VULKAN=ON)
else
	CMAKE_ARGS+=(-DGGML_VULKAN=OFF)
fi

if [[ "$ENABLE_METAL" -eq 1 ]]; then
	write_step "Metal backend enabled."
	CMAKE_ARGS+=(-DGGML_METAL=ON)
else
	CMAKE_ARGS+=(-DGGML_METAL=OFF)
fi

CONFIGURE_LOG="$(mktemp "${TMPDIR:-/tmp}/ofxggml-llama-configure.XXXXXX")"
if ! cmake "$SOURCE_DIR" "${CMAKE_ARGS[@]}" 2>&1 | tee "$CONFIGURE_LOG"; then
	# Auto-detect should be resilient: if Vulkan looked available but CMake
	# cannot resolve full Vulkan SDK requirements, retry CPU/CUDA/Metal only.
	if [[ "$ENABLE_VULKAN" -eq 1 ]] &&
		[[ "$AUTO_DETECT" -eq 1 ]] &&
		[[ "$VULKAN_EXPLICIT" -eq 0 ]] &&
		grep -q "Could NOT find Vulkan" "$CONFIGURE_LOG"; then
		write_step "Warning: Vulkan auto-detected but CMake configure failed; retrying with Vulkan disabled."
		ENABLE_VULKAN=0
		FALLBACK_ARGS=()
		for arg in "${CMAKE_ARGS[@]}"; do
			[[ "$arg" == "-DGGML_VULKAN=ON" ]] && continue
			FALLBACK_ARGS+=("$arg")
		done
		FALLBACK_ARGS+=(-DGGML_VULKAN=OFF)
		> "$CONFIGURE_LOG"
		if ! cmake "$SOURCE_DIR" "${FALLBACK_ARGS[@]}" 2>&1 | tee "$CONFIGURE_LOG"; then
			die "CMake configure failed after retrying with Vulkan disabled."
		fi
		CMAKE_ARGS=("${FALLBACK_ARGS[@]}")
	else
		die "CMake configure failed."
	fi
fi

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

# Build llama-completion (one-shot text completion — used by ofxGgmlGuiExample),
# llama-cli (interactive chat mode), and llama-embedding when available.
write_step "Building llama-server, llama-completion, llama-cli, and llama-embedding with $JOBS parallel jobs..."
if ! cmake --build . --config Release --target llama-server llama-completion llama-cli llama-embedding -j "$JOBS"; then
	write_step "llama-embedding target unavailable, retrying without it..."
	cmake --build . --config Release --target llama-server llama-completion llama-cli -j "$JOBS"
fi

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

write_step "Installing llama tools to $INSTALL_PREFIX/bin..."
EFFECTIVE_INSTALL_PREFIX="$INSTALL_PREFIX"

if [[ -e "$INSTALL_PREFIX" ]] && [[ ! -d "$INSTALL_PREFIX" ]]; then
	die "Install prefix '$INSTALL_PREFIX' exists but is not a directory."
fi

# Locate built binaries.
find_binary() {
	local name="$1"
	for candidate in \
		"$BUILD_DIR/bin/$name" \
		"$BUILD_DIR/bin/${name}.exe" \
		"$BUILD_DIR/bin/Release/${name}.exe" \
		"$BUILD_DIR/$name" \
		"$BUILD_DIR/${name}.exe"; do
		if [[ -f "$candidate" ]]; then
			echo "$candidate"
			return 0
		fi
	done
	return 1
}

BUILT_SERVER=""
BUILT_COMPLETION=""
BUILT_CLI=""
BUILT_EMBEDDING=""
BUILT_SERVER=$(find_binary "llama-server") || true
BUILT_COMPLETION=$(find_binary "llama-completion") || true
BUILT_CLI=$(find_binary "llama-cli") || true
BUILT_EMBEDDING=$(find_binary "llama-embedding") || true

if [[ -z "$BUILT_SERVER" ]] && [[ -z "$BUILT_COMPLETION" ]] && [[ -z "$BUILT_CLI" ]] && [[ -z "$BUILT_EMBEDDING" ]]; then
	die "Could not find any built llama binaries under $BUILD_DIR"
fi

install_binary() {
	local src="$1"
	local dest_dir="$2/bin"
	local bin_name
	bin_name="$(basename "$src")"
	local dest_file="$dest_dir/$bin_name"
	mkdir -p "$dest_dir"
	cp "$src" "$dest_file"
	chmod +x "$dest_file"
	write_step "Installed: $dest_file"
}

install_binary_sudo() {
	local src="$1"
	local dest_dir="$2/bin"
	local bin_name
	bin_name="$(basename "$src")"
	sudo mkdir -p "$dest_dir"
	sudo cp "$src" "$dest_dir/"
	sudo chmod +x "$dest_dir/$bin_name"
	write_step "Installed: $dest_dir/$bin_name"
}

do_install() {
	local prefix="$1"
	local use_sudo="${2:-false}"
	for binary in "$BUILT_SERVER" "$BUILT_COMPLETION" "$BUILT_CLI" "$BUILT_EMBEDDING"; do
		if [[ -n "$binary" ]]; then
			if [[ "$use_sudo" == "true" ]]; then
				install_binary_sudo "$binary" "$prefix"
			else
				install_binary "$binary" "$prefix"
			fi
		fi
	done
}

if [[ ! -d "$INSTALL_PREFIX" ]]; then
	INSTALL_PREFIX_PARENT="$(dirname "$INSTALL_PREFIX")"
	if [[ -w "$INSTALL_PREFIX_PARENT" ]]; then
		mkdir -p "$INSTALL_PREFIX"
	fi
fi

if [[ -d "$INSTALL_PREFIX" ]] && [[ -w "$INSTALL_PREFIX" ]]; then
	do_install "$INSTALL_PREFIX"
elif command -v sudo >/dev/null 2>&1 && ! is_windows_like; then
	write_step "Requires elevated permissions for $INSTALL_PREFIX — using sudo."
	do_install "$INSTALL_PREFIX" true
elif [[ "$INSTALL_PREFIX" == "$DEFAULT_INSTALL_PREFIX" ]] && [[ -n "${HOME:-}" ]]; then
	EFFECTIVE_INSTALL_PREFIX="${HOME}/.local"
	write_step "Install prefix '$INSTALL_PREFIX' is not writable; falling back to '$EFFECTIVE_INSTALL_PREFIX'."
	do_install "$EFFECTIVE_INSTALL_PREFIX"
else
	die "Install prefix '$INSTALL_PREFIX' is not writable. Use --prefix to a writable location."
fi

# ---------------------------------------------------------------------------
# Verify
# ---------------------------------------------------------------------------

write_step "Verifying installation..."
INSTALLED_TOOLS=0
for tool_name in llama-server llama-completion llama-cli llama-embedding; do
	tool_path="$EFFECTIVE_INSTALL_PREFIX/bin/$tool_name"
	if is_windows_like; then
		tool_path="${tool_path}.exe"
	fi
	if [[ -x "$tool_path" ]]; then
		write_step "$tool_name found at $tool_path"
		if "$tool_path" --version >/dev/null 2>&1 || "$tool_path" --help >/dev/null 2>&1; then
			write_step "$tool_name runs successfully."
		else
			write_step "Warning: $tool_name exists but did not respond to --version or --help."
		fi
		INSTALLED_TOOLS=$((INSTALLED_TOOLS + 1))
	fi
done

if [[ "$INSTALLED_TOOLS" -eq 0 ]]; then
	write_step "Warning: could not verify any installed tools under $EFFECTIVE_INSTALL_PREFIX/bin."
fi

write_step "Done! llama.cpp runtime tools have been built and installed to $EFFECTIVE_INSTALL_PREFIX/bin."
write_step ""
write_step "Next steps:"
write_step "  1. Ensure $EFFECTIVE_INSTALL_PREFIX/bin is available to your app,"
write_step "     or rely on the addon-local libs/llama/bin default."
write_step "  2. Run scripts/download-model.sh to fetch a GGUF model (if not done)."
write_step "  3. Optionally launch scripts/start-llama-server.sh for a local warm server."
write_step "  4. Build and run your OF project with ofxGgml."
