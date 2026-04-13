#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# download-model.sh — Download a GGUF model for use with ofxGgml.
#
# Usage:
#   ./scripts/download-model.sh [--model URL] [--preset N] [--task NAME] [--both]
#                               [--output DIR] [--name FILE] [--checksum SHA256]
#
# Options:
#   --model  URL   Direct URL to a GGUF model file.
#                  Default with no selectors: download both recommended presets
#   --preset N     Select a model by preset number (see --list)
#   --task   NAME  Select the preferred model for a task: chat, script,
#                  summarize, write, translate, custom  (matches the GUI
#                  example modes)
#   --output DIR   Directory to save the model (default: bin/data/models/)
#   --name   FILE  Output file name (default: derived from URL)
#   --both         Download both recommended presets (1 and 2)
#   --checksum HEX SHA256 checksum for --model URL downloads
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

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADDON_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MODEL_CATALOG_PATH="$ADDON_ROOT/scripts/model-catalog.json"

# ---------------------------------------------------------------------------
# Model presets (loaded from model-catalog.json, with fallback defaults)
# ---------------------------------------------------------------------------

PRESET_NAMES=()
PRESET_URLS=()
PRESET_SIZES=()
PRESET_BESTFOR=()
PRESET_FILENAMES=()
PRESET_SHA256=()

if command -v python3 >/dev/null 2>&1 && [[ -f "$MODEL_CATALOG_PATH" ]]; then
	MODEL_CATALOG_ROWS="$(python3 - "$MODEL_CATALOG_PATH" <<'PY'
import json, sys
from pathlib import Path

path = Path(sys.argv[1])
data = json.loads(path.read_text(encoding="utf-8"))
for model in data.get("models", []):
    print("\t".join([
        str(model.get("name", "")),
        str(model.get("url", "")),
        str(model.get("size", "")),
        str(model.get("best_for", "")),
        str(model.get("filename", "")),
        str(model.get("sha256", "")),
    ]))
PY
)"
	while IFS=$'\t' read -r name url size bestfor filename sha256; do
		[[ -z "$name" ]] && continue
		PRESET_NAMES+=("$name")
		PRESET_URLS+=("$url")
		PRESET_SIZES+=("$size")
		PRESET_BESTFOR+=("$bestfor")
		PRESET_FILENAMES+=("$filename")
		PRESET_SHA256+=("$sha256")
	done <<< "$MODEL_CATALOG_ROWS"
fi

if [[ ${#PRESET_NAMES[@]} -eq 0 ]]; then
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
	PRESET_FILENAMES=(
		"qwen2.5-1.5b-instruct-q4_k_m.gguf"
		"qwen2.5-coder-1.5b-instruct-q4_k_m.gguf"
	)
	PRESET_SHA256=("" "")
fi

# Zero-based indices of presets downloaded by default / --both.
RECOMMENDED_PRESET_INDICES=(0 1)

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
MODEL_CHECKSUM=""
PRESET_INDEX=""
TASK_NAME=""
DOWNLOAD_BOTH=false

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
		if [[ -n "${PRESET_SHA256[$i]:-}" ]]; then
			printf "     SHA256: %s\n" "${PRESET_SHA256[$i]}"
		fi
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
	echo "  ./scripts/download-model.sh                 # Download presets 1 and 2 (default)"
	echo "  ./scripts/download-model.sh --preset 2      # Qwen2.5-Coder for scripting"
	echo "  ./scripts/download-model.sh --both          # Download presets 1 and 2"
	echo "  ./scripts/download-model.sh --task script   # same as --preset 2"
	echo "  ./scripts/download-model.sh --task chat     # Qwen2.5-1.5B for chat"
	echo "  ./scripts/download-model.sh --model <URL> --checksum <SHA256>"
	echo "  ./scripts/download-model.sh --model <URL>   # custom URL"
	echo ""
	echo "Catalog: $MODEL_CATALOG_PATH"
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
		--checksum)
			MODEL_CHECKSUM="$2"
			shift 2
			;;
		--both)
			DOWNLOAD_BOTH=true
			shift
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

if [[ "$DOWNLOAD_BOTH" != true ]] && [[ -z "$MODEL_URL" ]] && [[ -z "$PRESET_INDEX" ]] && [[ -z "$TASK_NAME" ]]; then
	DOWNLOAD_BOTH=true
	write_step "No --model/--preset/--task specified, defaulting to both recommended presets"
fi

if [[ "$DOWNLOAD_BOTH" == true ]]; then
	if [[ -n "$MODEL_URL" ]] || [[ -n "$PRESET_INDEX" ]] || [[ -n "$TASK_NAME" ]]; then
		die "Cannot combine --both with --model, --preset, or --task"
	fi
	if [[ -n "$OUTPUT_NAME" ]]; then
		die "Cannot use --name with --both"
	fi
	if [[ -n "$MODEL_CHECKSUM" ]]; then
		die "Cannot use --checksum with --both"
	fi
fi

if [[ -z "$OUTPUT_DIR" ]]; then
	# Try to find the example's bin/data directory.
	GUI_EXAMPLE="$ADDON_ROOT/ofxGgmlGuiExample/bin/data"
	if [[ -d "$(dirname "$GUI_EXAMPLE")" ]]; then
		OUTPUT_DIR="$GUI_EXAMPLE/models"
	else
		OUTPUT_DIR="$(pwd)/models"
	fi
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

download_model() {
	local model_url="$1"
	local output_name="$2"
	local expected_sha256="${3:-}"
	local output_path="$OUTPUT_DIR/$output_name"

	compute_sha256() {
		local path="$1"
		if command -v sha256sum >/dev/null 2>&1; then
			sha256sum "$path" | awk '{print $1}'
			return 0
		elif command -v shasum >/dev/null 2>&1; then
			shasum -a 256 "$path" | awk '{print $1}'
			return 0
		fi
		return 1
	}

	if [[ -f "$output_path" ]]; then
		if [[ -n "$expected_sha256" ]]; then
			if actual_sha256="$(compute_sha256 "$output_path" 2>/dev/null)"; then
				if [[ "$actual_sha256" == "$expected_sha256" ]]; then
					write_step "Model already exists and checksum matches: $output_path"
					return 0
				fi
				write_step "Checksum mismatch for existing file, re-downloading: $output_path"
				rm -f "$output_path"
			else
				write_step "Checksum tool unavailable, keeping existing file: $output_path"
				return 0
			fi
		else
			write_step "Model already exists at $output_path (skipping)"
			return 0
		fi
	fi

	write_step "Downloading model..."
	write_step "  URL:  $model_url"
	write_step "  Dest: $output_path"
	if [[ -n "$expected_sha256" ]]; then
		write_step "  SHA256: $expected_sha256"
	else
		write_step "  SHA256: not configured (download will not be integrity-verified)"
	fi
	write_step ""

	if [[ "$DOWNLOAD_CMD" == "curl" ]]; then
		curl -L --progress-bar -C - --retry 3 --retry-delay 2 -o "$output_path" "$model_url"
	elif [[ "$DOWNLOAD_CMD" == "wget" ]]; then
		wget -c --show-progress -O "$output_path" "$model_url"
	fi

	if [[ ! -s "$output_path" ]]; then
		rm -f "$output_path"
		die "Downloaded file is empty. Check the URL and try again."
	fi

	if [[ -n "$expected_sha256" ]]; then
		local got_sha256=""
		if ! got_sha256="$(compute_sha256 "$output_path" 2>/dev/null)"; then
			rm -f "$output_path"
			die "Failed to compute SHA256 checksum. Install sha256sum or shasum."
		fi
		if [[ "$got_sha256" != "$expected_sha256" ]]; then
			write_step "Checksum mismatch:"
			write_step "  expected: $expected_sha256"
			write_step "  actual:   $got_sha256"
			rm -f "$output_path"
			die "Checksum verification failed for $output_path"
		fi
		write_step "Checksum verification passed."
	fi

	local FILE_SIZE
	FILE_SIZE=$(wc -c < "$output_path" 2>/dev/null || echo 0)
	write_step "Download complete!  Size: $(numfmt --to=iec "$FILE_SIZE" 2>/dev/null || echo "$FILE_SIZE bytes")"
	write_step "Model saved to: $output_path"
	write_step ""
}

mkdir -p "$OUTPUT_DIR"

if [[ "$DOWNLOAD_BOTH" == true ]]; then
	write_step "Downloading both recommended presets"
	for i in "${RECOMMENDED_PRESET_INDICES[@]}"; do
		if [[ $i -lt 0 ]] || [[ $i -ge ${#PRESET_URLS[@]} ]]; then
			die "Recommended preset index $i is out of range, but only ${#PRESET_URLS[@]} preset entries are configured"
		fi
		write_step "Preset $((i + 1)): ${PRESET_NAMES[$i]} (${PRESET_SIZES[$i]})"
		local_name="${PRESET_FILENAMES[$i]:-}"
		if [[ -z "$local_name" ]]; then
			local_name="$(basename "${PRESET_URLS[$i]}")"
		fi
		download_model "${PRESET_URLS[$i]}" "$local_name" "${PRESET_SHA256[$i]:-}"
	done
	write_step "Next steps:"
	write_step "  1. Build ggml with scripts/build-ggml.sh (if not done)."
	write_step "  2. Build and run your OF project with ofxGgml."
	write_step "  3. Point the model path to files under: $OUTPUT_DIR"
	exit 0
fi

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
	if [[ -z "$MODEL_CHECKSUM" ]]; then
		MODEL_CHECKSUM="${PRESET_SHA256[$idx]:-}"
	fi
	write_step "Preset $PRESET_INDEX selected: ${PRESET_NAMES[$idx]} (${PRESET_SIZES[$idx]})"
fi

# Defaults.
if [[ -z "$MODEL_URL" ]]; then
	MODEL_URL="${PRESET_URLS[0]}"
	write_step "No --model or --preset specified, using default: ${PRESET_NAMES[0]}"
fi

if [[ -z "$OUTPUT_NAME" ]]; then
	if [[ -n "$PRESET_INDEX" ]]; then
		idx=$((PRESET_INDEX - 1))
		OUTPUT_NAME="${PRESET_FILENAMES[$idx]:-}"
	fi
	if [[ -z "$OUTPUT_NAME" ]]; then
		OUTPUT_NAME="$(basename "$MODEL_URL")"
	fi
fi

download_model "$MODEL_URL" "$OUTPUT_NAME" "$MODEL_CHECKSUM"
write_step "Next steps:"
write_step "  1. Build ggml with scripts/build-ggml.sh (if not done)."
write_step "  2. Build and run your OF project with ofxGgml."
write_step "  3. Point the model path to: $OUTPUT_DIR/$OUTPUT_NAME"
