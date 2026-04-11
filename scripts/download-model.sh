#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# download-model.sh — Download a GGUF model for use with ofxGgml.
#
# Usage:
#   ./scripts/download-model.sh [--model URL] [--preset N] [--task NAME]
#                               [--output DIR] [--name FILE]
#
# Options:
#   --model  URL   Direct URL to a GGUF model file.
#                  Default: Qwen2.5-1.5B Instruct Q4_K_M
#   --preset N     Select a model by preset number (see --list)
#   --task   NAME  Select the preferred model for a task: chat, script,
#                  summarize, write, translate, custom  (matches the GUI
#                  example modes)
#   --output DIR   Directory to save the model (default: bin/data/models/)
#   --name   FILE  Output file name (default: derived from URL)
#   --list         List recommended models with preset numbers and exit
#   --help         Show this help message
#
# Recommended models (small enough for development):
#   1. Qwen2.5-1.5B Instruct Q4_K_M       (~1.0 GB) — chat, general
#   2. Qwen2.5-Coder-1.5B Instruct Q4_K_M (~1.0 GB) — scripting, code generation
#
# Preferred models per example task:
#   chat       → preset 1  Qwen2.5-1.5B Instruct Q4_K_M
#   script     → preset 2  Qwen2.5-Coder-1.5B Instruct Q4_K_M
#   summarize  → preset 1  Qwen2.5-1.5B Instruct Q4_K_M
#   write      → preset 1  Qwen2.5-1.5B Instruct Q4_K_M
#   translate  → preset 1  Qwen2.5-1.5B Instruct Q4_K_M
#   custom     → preset 1  Qwen2.5-1.5B Instruct Q4_K_M
# ---------------------------------------------------------------------------
set -euo pipefail

# ---------------------------------------------------------------------------
# Model presets — same list as the GUI example
# ---------------------------------------------------------------------------

PRESET_NAMES=(
	"Qwen2.5-1.5B Instruct Q4_K_M"
	"Qwen2.5-Coder-1.5B Instruct Q4_K_M"
)
PRESET_URLS=(
	"https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf"
	"https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/main/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf"
)
PRESET_SIZES=(
	"~1.0 GB"
	"~1.0 GB"
)
PRESET_BESTFOR=(
	"chat, general"
	"scripting, code generation"
)

# ---------------------------------------------------------------------------
# Task → preferred preset mapping (matches GUI example AiMode enum)
# ---------------------------------------------------------------------------

declare -A TASK_PRESET
TASK_PRESET[chat]=1
TASK_PRESET[script]=2
TASK_PRESET[summarize]=1
TASK_PRESET[write]=1
TASK_PRESET[translate]=1
TASK_PRESET[custom]=1

MODEL_URL=""
OUTPUT_DIR=""
OUTPUT_NAME=""
PRESET_INDEX=""
TASK_NAME=""

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

list_models() {
	echo "Recommended GGUF models for development / testing:"
	echo ""
	for i in "${!PRESET_NAMES[@]}"; do
		local n=$((i + 1))
		printf "  %d. %-40s %s\n" "$n" "${PRESET_NAMES[$i]}" "${PRESET_SIZES[$i]}"
		printf "     Best for: %s\n" "${PRESET_BESTFOR[$i]}"
		printf "     %s\n\n" "${PRESET_URLS[$i]}"
	done
	echo "Preferred models per example task (--task NAME):"
	echo ""
	for task in chat script summarize write translate custom; do
		local p="${TASK_PRESET[$task]}"
		local idx=$((p - 1))
		printf "  %-12s → preset %d  %s\n" "$task" "$p" "${PRESET_NAMES[$idx]}"
	done
	echo ""
	echo "Usage:"
	echo "  ./scripts/download-model.sh --preset 1      # Qwen2.5-1.5B (default)"
	echo "  ./scripts/download-model.sh --preset 2      # Qwen2.5-Coder for scripting"
	echo "  ./scripts/download-model.sh --task script   # same as --preset 2"
	echo "  ./scripts/download-model.sh --task chat     # Qwen2.5-1.5B for chat"
	echo "  ./scripts/download-model.sh --model <URL>   # custom URL"
	exit 0
}

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------

while [[ $# -gt 0 ]]; do
	case "$1" in
		--model)
			MODEL_URL="$2"
			shift 2
			;;
		--preset)
			PRESET_INDEX="$2"
			shift 2
			;;
		--task)
			TASK_NAME="$2"
			shift 2
			;;
		--output)
			OUTPUT_DIR="$2"
			shift 2
			;;
		--name)
			OUTPUT_NAME="$2"
			shift 2
			;;
		--list)
			list_models
			;;
		--help|-h)
			usage
			;;
		*)
			die "Unknown option: $1"
			;;
	esac
done

# Resolve --task to a preset number.
if [[ -n "$TASK_NAME" ]]; then
	TASK_NAME="${TASK_NAME,,}"   # lowercase
	if [[ -z "${TASK_PRESET[$TASK_NAME]+x}" ]]; then
		die "Unknown task: $TASK_NAME (valid: chat, script, summarize, write, translate, custom)"
	fi
	if [[ -n "$PRESET_INDEX" ]]; then
		die "Cannot use both --task and --preset"
	fi
	PRESET_INDEX="${TASK_PRESET[$TASK_NAME]}"
	write_step "Task '$TASK_NAME' → preset $PRESET_INDEX"
fi

# Resolve preset.
if [[ -n "$PRESET_INDEX" ]]; then
	idx=$((PRESET_INDEX - 1))
	if [[ $idx -lt 0 ]] || [[ $idx -ge ${#PRESET_URLS[@]} ]]; then
		die "Invalid preset number: $PRESET_INDEX (valid: 1-${#PRESET_URLS[@]})"
	fi
	MODEL_URL="${PRESET_URLS[$idx]}"
	write_step "Preset $PRESET_INDEX selected: ${PRESET_NAMES[$idx]} (${PRESET_SIZES[$idx]})"
fi

# Defaults.
if [[ -z "$MODEL_URL" ]]; then
	MODEL_URL="${PRESET_URLS[0]}"
	write_step "No --model or --preset specified, using default: ${PRESET_NAMES[0]}"
fi

if [[ -z "$OUTPUT_DIR" ]]; then
	# Try to find the example's bin/data directory.
	SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
	ADDON_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
	GUI_EXAMPLE="$ADDON_ROOT/ofxGgmlGuiExample/bin/data"
	if [[ -d "$(dirname "$GUI_EXAMPLE")" ]]; then
		OUTPUT_DIR="$GUI_EXAMPLE/models"
	else
		OUTPUT_DIR="$(pwd)/models"
	fi
fi

if [[ -z "$OUTPUT_NAME" ]]; then
	OUTPUT_NAME="$(basename "$MODEL_URL")"
fi

# ---------------------------------------------------------------------------
# Prerequisites
# ---------------------------------------------------------------------------

DOWNLOAD_CMD=""
if command -v curl >/dev/null 2>&1; then
	DOWNLOAD_CMD="curl"
elif command -v wget >/dev/null 2>&1; then
	DOWNLOAD_CMD="wget"
else
	die "Neither curl nor wget found. Please install one."
fi

# ---------------------------------------------------------------------------
# Download
# ---------------------------------------------------------------------------

mkdir -p "$OUTPUT_DIR"
OUTPUT_PATH="$OUTPUT_DIR/$OUTPUT_NAME"

if [[ -f "$OUTPUT_PATH" ]]; then
	write_step "Model already exists at $OUTPUT_PATH"
	write_step "Delete it first if you want to re-download."
	exit 0
fi

write_step "Downloading model..."
write_step "  URL:  $MODEL_URL"
write_step "  Dest: $OUTPUT_PATH"
write_step ""

if [[ "$DOWNLOAD_CMD" == "curl" ]]; then
	curl -L --progress-bar -o "$OUTPUT_PATH" "$MODEL_URL"
elif [[ "$DOWNLOAD_CMD" == "wget" ]]; then
	wget --show-progress -O "$OUTPUT_PATH" "$MODEL_URL"
fi

# ---------------------------------------------------------------------------
# Verify
# ---------------------------------------------------------------------------

if [[ ! -s "$OUTPUT_PATH" ]]; then
	rm -f "$OUTPUT_PATH"
	die "Downloaded file is empty. Check the URL and try again."
fi

FILE_SIZE=$(wc -c < "$OUTPUT_PATH" 2>/dev/null || echo 0)
write_step "Download complete!  Size: $(numfmt --to=iec "$FILE_SIZE" 2>/dev/null || echo "$FILE_SIZE bytes")"
write_step "Model saved to: $OUTPUT_PATH"
write_step ""
write_step "Next steps:"
write_step "  1. Build ggml with scripts/build-ggml.sh (if not done)."
write_step "  2. Build and run your OF project with ofxGgml."
write_step "  3. Point the model path to: $OUTPUT_PATH"
