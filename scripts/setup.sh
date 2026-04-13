#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# setup.sh — One-command setup for the ofxGgml addon.
#
# Builds ggml (CPU-only by default), installs the llama.cpp CLI
# tools, and downloads a recommended model so you can run the examples
# right away.
#
# GPU backends (CUDA, Vulkan, Metal) must be explicitly enabled via
# flags.  Use --auto to auto-detect available GPU backends, or
# --cuda / --vulkan / --metal to enable a specific one.
#
# Usage:
#   ./scripts/setup.sh [OPTIONS]
#
# Options:
#   --cpu-only         Build CPU backend only (default)
#   --auto             Auto-detect and enable available GPU backends
#   --gpu, --cuda      Enable CUDA backend
#   --prefix DIR       Install prefix for llama tools
#                      (default: /usr/local on Linux, addon-local libs/ on Windows)
#   --skip-ggml        Skip building ggml (if already built)
#   --skip-llama       Skip building llama.cpp CLI tools
#   --skip-model       Skip downloading the model file
#   --model-preset N   Download a specific model preset (default: both)
#   --jobs N           Parallel build jobs (default: auto-detect)
#   --clean            Remove previous build directories before building
#   --help             Show this help message
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADDON_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

SKIP_GGML=0
SKIP_LLAMA=0
SKIP_MODEL=0
GPU_FLAG=""
LLAMA_GPU_FLAG=""
PREFIX_FLAG=""
JOBS_FLAG=""
CLEAN_FLAG=""
MODEL_PRESET_FLAG=""

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
			shift
			;;
		--prefix)
			[[ $# -ge 2 ]] || die "--prefix requires a value"
			PREFIX_FLAG="--prefix $2"
			shift 2
			;;
		--skip-ggml)
			SKIP_GGML=1
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
		--model-preset)
			[[ $# -ge 2 ]] || die "--model-preset requires a value"
			MODEL_PRESET_FLAG="--preset $2"
			shift 2
			;;
		--jobs)
			[[ $# -ge 2 ]] || die "--jobs requires a value"
			JOBS_FLAG="--jobs $2"
			shift 2
			;;
		--clean)
			CLEAN_FLAG="--clean"
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
# Step 1 — Build ggml (CPU-only by default)
# ---------------------------------------------------------------------------

if [[ "$SKIP_GGML" -eq 0 ]]; then
	write_step "Step 1/3: Building ggml tensor library..."
	# shellcheck disable=SC2086
	"$SCRIPT_DIR/build-ggml.sh" $GPU_FLAG $JOBS_FLAG $CLEAN_FLAG
	write_ok "ggml built successfully."
else
	write_warn "Skipping ggml build (--skip-ggml)."
fi

# ---------------------------------------------------------------------------
# Step 2 — Build llama.cpp CLI
# ---------------------------------------------------------------------------

if [[ "$SKIP_LLAMA" -eq 0 ]]; then
	write_step "Step 2/3: Building llama.cpp CLI tools..."
	# shellcheck disable=SC2086
	"$SCRIPT_DIR/build-llama-cli.sh" $LLAMA_GPU_FLAG $PREFIX_FLAG $JOBS_FLAG $CLEAN_FLAG
	write_ok "llama.cpp tools built successfully."
else
	write_warn "Skipping llama.cpp build (--skip-llama)."
fi

# ---------------------------------------------------------------------------
# Step 3 — Download model
# ---------------------------------------------------------------------------

if [[ "$SKIP_MODEL" -eq 0 ]]; then
	write_step "Step 3/3: Downloading model file(s)..."
	if [[ -n "$MODEL_PRESET_FLAG" ]]; then
		# shellcheck disable=SC2086
		"$SCRIPT_DIR/download-model.sh" $MODEL_PRESET_FLAG
	else
		"$SCRIPT_DIR/download-model.sh" --both
	fi
	write_ok "Model download complete."
else
	write_warn "Skipping model download (--skip-model)."
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
echo ""
echo "  Examples:"
echo "    ofxGgmlBasicExample/   — matrix multiplication"
echo "    ofxGgmlNeuralExample/  — feedforward neural network"
echo "    ofxGgmlGuiExample/     — full AI Studio with llama.cpp"
echo ""
