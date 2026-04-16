#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# setup_linux_macos.sh — One-command setup for the ofxGgml addon (Linux/macOS).
#
# Builds ggml (auto-detect GPU backends by default) and keeps the
# llama.cpp CLI tools optional. Model download is optional and disabled by default.
#
# Use --cpu-only to force CPU-only builds, or --cuda / --vulkan / --metal
# to force a specific backend.
#
# Usage:
#   ./scripts/setup_linux_macos.sh [OPTIONS]
#
# Options:
#   --cpu-only         Build CPU backend only (disable GPU auto-detect)
#   --auto             Auto-detect and enable available GPU backends (default)
#   --gpu, --cuda      Enable CUDA backend
#   --vulkan           Enable Vulkan backend
#   --metal            Enable Metal backend (macOS only)
#   --prefix DIR       Install prefix for llama tools
#                      (default: /usr/local on Linux, /usr/local on macOS)
#   --skip-ggml        Skip building ggml (if already built)
#   --with-llama-cli   Build optional llama.cpp CLI fallback tools
#   --skip-llama       Skip building llama.cpp CLI tools (legacy no-op)
#   --skip-model       Skip downloading the text model file (default)
#   --download-model   Download the text model file(s)
#   --model-preset N   Download a specific text model preset (default: both)
#   --jobs N           Parallel build jobs (default: auto-detect)
#   --clean            Remove previous build directories before building
#   --help             Show this help message
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADDON_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

SKIP_GGML=0
SKIP_LLAMA=1
SKIP_MODEL=1
GPU_FLAG="--auto"
LLAMA_GPU_FLAG="--auto"
PREFIX_VALUE=""
JOBS_VALUE=""
CLEAN=0
MODEL_PRESET_VALUE=""

if [[ -z "$JOBS_VALUE" ]]; then
	if command -v nproc >/dev/null 2>&1; then
		JOBS_VALUE="$(nproc)"
	elif command -v sysctl >/dev/null 2>&1; then
		JOBS_VALUE="$(sysctl -n hw.ncpu 2>/dev/null || true)"
	fi
fi

write_step() {
	printf '\n\033[1;34m==> %s\033[0m\n' "$1"
}

write_ok() {
	printf '\033[1;32m✓ %s\033[0m\n' "$1"
}

write_warn() {
	printf '\033[1;33m⚠ %s\033[0m\n' "$1"
}

die() {
	printf '\033[1;31mError: %s\033[0m\n' "$1" >&2
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
		--gpu|--cuda)
			GPU_FLAG="--cuda"
			LLAMA_GPU_FLAG="--gpu"
			shift
			;;
		--auto)
			GPU_FLAG="--auto"
			LLAMA_GPU_FLAG="--auto"
			shift
			;;
		--vulkan)
			GPU_FLAG="--vulkan"
			LLAMA_GPU_FLAG="--vulkan"
			shift
			;;
		--metal)
			GPU_FLAG="--metal"
			LLAMA_GPU_FLAG="--metal"
			shift
			;;
		--cpu-only)
			GPU_FLAG="--cpu-only"
			LLAMA_GPU_FLAG=""
			shift
			;;
		--prefix)
			[[ $# -ge 2 ]] || die "--prefix requires a value"
			PREFIX_VALUE="$2"
			shift 2
			;;
		--skip-ggml)
			SKIP_GGML=1
			shift
			;;
		--with-llama-cli)
			SKIP_LLAMA=0
			shift
			;;
		--skip-llama)
			SKIP_LLAMA=1
			shift
			;;
		--skip-model)
			SKIP_MODEL=1
			shift
			;;
		--download-model)
			SKIP_MODEL=0
			shift
			;;
		--model-preset)
			[[ $# -ge 2 ]] || die "--model-preset requires a value"
			MODEL_PRESET_VALUE="$2"
			shift 2
			;;
		--jobs)
			[[ $# -ge 2 ]] || die "--jobs requires a value"
			JOBS_VALUE="$2"
			shift 2
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

# ---------------------------------------------------------------------------
# Banner
# ---------------------------------------------------------------------------

echo ""
echo "  ┌──────────────────────────────────────┐"
echo "  │          ofxGgml Setup v0.2           │"
echo "  └──────────────────────────────────────┘"
echo ""

# ---------------------------------------------------------------------------
# Step 1 — Build ggml (GPU auto-detect by default)
# ---------------------------------------------------------------------------

if [[ "$SKIP_GGML" -eq 0 ]]; then
	write_step "Step 1/3: Building ggml tensor library..."
	GGML_ARGS=("$GPU_FLAG")
	if [[ -n "$JOBS_VALUE" ]]; then
		GGML_ARGS+=(--jobs "$JOBS_VALUE")
	fi
	if [[ "$CLEAN" -eq 1 ]]; then
		GGML_ARGS+=(--clean)
	fi
	"$SCRIPT_DIR/build-ggml.sh" "${GGML_ARGS[@]}"
	write_ok "ggml built successfully."
else
	write_warn "Skipping ggml build (--skip-ggml)."
fi

# ---------------------------------------------------------------------------
# Step 2 — Build llama.cpp CLI
# ---------------------------------------------------------------------------

if [[ "$SKIP_LLAMA" -eq 0 ]]; then
	write_step "Step 2/3: Building optional llama.cpp CLI fallback tools..."
	LLAMA_ARGS=()
	if [[ -n "$LLAMA_GPU_FLAG" ]]; then
		LLAMA_ARGS+=("$LLAMA_GPU_FLAG")
	fi
	if [[ -n "$PREFIX_VALUE" ]]; then
		LLAMA_ARGS+=(--prefix "$PREFIX_VALUE")
	fi
	if [[ -n "$JOBS_VALUE" ]]; then
		LLAMA_ARGS+=(--jobs "$JOBS_VALUE")
	fi
	if [[ "$CLEAN" -eq 1 ]]; then
		LLAMA_ARGS+=(--clean)
	fi
	"$SCRIPT_DIR/build-llama-cli.sh" "${LLAMA_ARGS[@]}"
	write_ok "Optional llama.cpp CLI tools built successfully."
else
	write_warn "Skipping optional llama.cpp CLI tools (server-first default)."
fi

# ---------------------------------------------------------------------------
# Step 3 — Download model
# ---------------------------------------------------------------------------

if [[ "$SKIP_MODEL" -eq 0 ]]; then
	write_step "Step 3/3: Downloading model file(s)..."
	if [[ -n "$MODEL_PRESET_VALUE" ]]; then
		"$SCRIPT_DIR/download-model.sh" --preset "$MODEL_PRESET_VALUE"
	else
		"$SCRIPT_DIR/download-model.sh" --both
	fi
	write_ok "Model download complete."
else
	write_warn "Skipping model download (default)."
fi

# ---------------------------------------------------------------------------
# Done
# ---------------------------------------------------------------------------

echo ""
echo "  ┌──────────────────────────────────────┐"
echo "  │           Setup complete!             │"
echo "  └──────────────────────────────────────┘"
echo ""
echo "  Next steps:"
echo "    1. Open your OF project and add ofxGgml to addons.make"
echo "    2. Include <ofxGgml.h> in your source files"
echo "    3. Build and run!"
echo "    4. Use --with-llama-cli if you want legacy local CLI fallback tools"
echo "    5. Build llama-server separately when you want a local persistent server"
echo ""
echo "  Examples:"
echo "    ofxGgmlBasicExample/   — matrix multiplication"
echo "    ofxGgmlNeuralExample/  — feedforward neural network"
echo "    ofxGgmlGuiExample/     — full AI Studio with llama.cpp"
echo ""
