#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# update-model-checksums.sh — Update model catalog with verified checksums
#
# This script helps maintainers obtain SHA256 checksums for models listed
# in the model catalog and update the catalog with verified values.
#
# Usage:
#   ./scripts/dev/update-model-checksums.sh [--preset N] [--all]
#
# Options:
#   --preset N  Update checksum for preset number N
#   --all       Update all presets with placeholder checksums
#   --help      Show this help message
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADDON_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
MODEL_CATALOG="$ADDON_ROOT/scripts/model-catalog.json"
TEMP_DIR=$(mktemp -d)

cleanup() {
	rm -rf "$TEMP_DIR"
}
trap cleanup EXIT

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

# Check if jq is available for JSON manipulation
if ! command -v jq >/dev/null 2>&1; then
	die "jq is required but not installed. Install with: sudo apt-get install jq"
fi

# Check if sha256sum or shasum is available
CHECKSUM_CMD=""
if command -v sha256sum >/dev/null 2>&1; then
	CHECKSUM_CMD="sha256sum"
elif command -v shasum >/dev/null 2>&1; then
	CHECKSUM_CMD="shasum -a 256"
else
	die "Neither sha256sum nor shasum found. Please install one."
fi

# Parse arguments
PRESET=""
UPDATE_ALL=false

while [[ $# -gt 0 ]]; do
	case "$1" in
		--preset)
			[[ $# -ge 2 ]] || die "--preset requires a value"
			PRESET="$2"
			shift 2
			;;
		--all)
			UPDATE_ALL=true
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

if [[ -z "$PRESET" ]] && [[ "$UPDATE_ALL" != true ]]; then
	die "Either --preset N or --all must be specified"
fi

# Function to update a single preset's checksum
update_preset_checksum() {
	local preset_num="$1"
	local idx=$((preset_num - 1))

	# Extract model info from catalog
	local url=$(jq -r ".models[$idx].url" "$MODEL_CATALOG")
	local filename=$(jq -r ".models[$idx].filename" "$MODEL_CATALOG")
	local name=$(jq -r ".models[$idx].name" "$MODEL_CATALOG")

	if [[ "$url" == "null" ]] || [[ -z "$url" ]]; then
		write_step "Preset $preset_num not found in catalog"
		return 1
	fi

	write_step "Processing preset $preset_num: $name"
	write_step "  Filename: $filename"
	write_step "  URL: $url"

	# Try to find the model in common locations
	local model_path=""
	local search_paths=(
		"$ADDON_ROOT/ofxGgmlGuiExample/bin/data/models/$filename"
		"$ADDON_ROOT/models/$filename"
		"$(pwd)/models/$filename"
		"$HOME/Downloads/$filename"
	)

	for path in "${search_paths[@]}"; do
		if [[ -f "$path" ]]; then
			model_path="$path"
			write_step "  Found model at: $model_path"
			break
		fi
	done

	if [[ -z "$model_path" ]]; then
		write_step "  Model file not found locally. Downloading..."
		write_step "  This may take a while (~1GB download)..."

		# Download to temp directory
		model_path="$TEMP_DIR/$filename"
		if command -v curl >/dev/null 2>&1; then
			curl -L --progress-bar -o "$model_path" "$url"
		elif command -v wget >/dev/null 2>&1; then
			wget --show-progress -O "$model_path" "$url"
		else
			die "Neither curl nor wget found"
		fi
	fi

	# Compute checksum
	write_step "  Computing SHA256 checksum..."
	local checksum=$($CHECKSUM_CMD "$model_path" | awk '{print $1}')

	if [[ ${#checksum} -ne 64 ]]; then
		die "Invalid checksum length: $checksum"
	fi

	write_step "  SHA256: $checksum"

	# Update catalog
	write_step "  Updating model catalog..."

	# Create temporary updated catalog
	jq ".models[$idx].sha256 = \"$checksum\" | del(.models[$idx].note)" \
		"$MODEL_CATALOG" > "$MODEL_CATALOG.tmp"

	# Validate JSON
	if ! jq empty "$MODEL_CATALOG.tmp" 2>/dev/null; then
		rm -f "$MODEL_CATALOG.tmp"
		die "Generated invalid JSON"
	fi

	# Replace original
	mv "$MODEL_CATALOG.tmp" "$MODEL_CATALOG"

	write_step "  ✓ Updated preset $preset_num checksum"
	echo ""
}

# Main logic
if [[ "$UPDATE_ALL" == true ]]; then
	# Count models in catalog
	model_count=$(jq '.models | length' "$MODEL_CATALOG")

	write_step "Updating all presets with placeholder checksums..."
	echo ""

	for ((i=0; i<model_count; i++)); do
		preset_num=$((i + 1))

		# Check if this preset has a placeholder (note field exists or checksum is obviously fake)
		has_note=$(jq -r ".models[$i].note // \"\"" "$MODEL_CATALOG")
		current_checksum=$(jq -r ".models[$i].sha256" "$MODEL_CATALOG")

		if [[ -n "$has_note" ]] || [[ "$current_checksum" =~ ^[a-f0-9c]{64}$ ]] && [[ "$current_checksum" == *"c0c0c0c0"* ]]; then
			update_preset_checksum "$preset_num" || true
		else
			write_step "Preset $preset_num already has a verified checksum, skipping"
		fi
	done
else
	# Update single preset
	update_preset_checksum "$PRESET"
fi

write_step "Done! Model catalog updated with verified checksums."
write_step "Please review $MODEL_CATALOG and commit the changes."
