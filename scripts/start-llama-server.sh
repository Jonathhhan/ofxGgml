#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADDON_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

MODEL_PATH="${MODEL_PATH:-}"
SERVER_EXE="${SERVER_EXE:-}"
BIND_HOST="${BIND_HOST:-127.0.0.1}"
PORT="${PORT:-8080}"
GPU_LAYERS="${GPU_LAYERS:-28}"
CONTEXT_SIZE="${CONTEXT_SIZE:-6144}"
DETACHED=0
DRY_RUN=0

usage() {
	cat <<'EOF'
Usage: ./scripts/start-llama-server.sh [options]

Options:
  --model PATH        GGUF model path
  --server-exe PATH   Explicit llama-server binary path
  --host HOST         Bind host (default: 127.0.0.1)
  --port PORT         Bind port (default: 8080)
  --gpu-layers N      GPU layers / -ngl value (default: 28)
  --context N         Context size / -c value (default: 6144)
  --detached          Launch in background
  --dry-run           Print the command without launching
  --help              Show this message
EOF
}

resolve_first_existing_path() {
	for candidate in "$@"; do
		[[ -n "$candidate" ]] || continue
		if [[ -f "$candidate" ]]; then
			printf '%s\n' "$candidate"
			return 0
		fi
	done
	return 1
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--model)
			MODEL_PATH="${2:-}"
			shift 2
			;;
		--server-exe)
			SERVER_EXE="${2:-}"
			shift 2
			;;
		--host)
			BIND_HOST="${2:-}"
			shift 2
			;;
		--port)
			PORT="${2:-}"
			shift 2
			;;
		--gpu-layers)
			GPU_LAYERS="${2:-}"
			shift 2
			;;
		--context)
			CONTEXT_SIZE="${2:-}"
			shift 2
			;;
		--detached)
			DETACHED=1
			shift
			;;
		--dry-run)
			DRY_RUN=1
			shift
			;;
		--help|-h)
			usage
			exit 0
			;;
		*)
			echo "Error: unknown option: $1" >&2
			exit 1
			;;
	esac
done

if [[ -z "$SERVER_EXE" ]]; then
	SERVER_EXE="$(resolve_first_existing_path \
		"$ADDON_ROOT/libs/llama/bin/llama-server" \
		"$ADDON_ROOT/build/llama.cpp-build/bin/llama-server" \
		"$ADDON_ROOT/build/llama.cpp-build/bin/Release/llama-server" \
		"")" || true
fi

if [[ -z "$SERVER_EXE" ]]; then
	echo "Error: could not find llama-server. Expected it in libs/llama/bin or build/llama.cpp-build/bin." >&2
	exit 1
fi

if [[ -z "$MODEL_PATH" ]]; then
	MODEL_PATH="$(resolve_first_existing_path \
		"$ADDON_ROOT/ofxGgmlGuiExample/bin/data/models/qwen2.5-coder-7b-instruct-q4_k_m.gguf" \
		"$ADDON_ROOT/ofxGgmlGuiExample/bin/data/models/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf" \
		"$ADDON_ROOT/ofxGgmlGuiExample/bin/data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf" \
		"")" || true
fi

if [[ -z "$MODEL_PATH" ]]; then
	echo "Error: could not find a default GGUF model. Pass --model explicitly." >&2
	exit 1
fi

if [[ ! -f "$MODEL_PATH" ]]; then
	echo "Error: model file not found: $MODEL_PATH" >&2
	exit 1
fi

ARGS=(
	-m "$MODEL_PATH"
	--host "$BIND_HOST"
	--port "$PORT"
	-ngl "$GPU_LAYERS"
	-c "$CONTEXT_SIZE"
)

echo "Starting llama-server with:"
echo "  exe:    $SERVER_EXE"
echo "  model:  $MODEL_PATH"
echo "  host:   $BIND_HOST"
echo "  port:   $PORT"
echo "  ngl:    $GPU_LAYERS"
echo "  ctx:    $CONTEXT_SIZE"
echo "  mode:   $([[ "$DETACHED" -eq 1 ]] && echo detached || echo foreground)"
echo
printf '"%s"' "$SERVER_EXE"
for arg in "${ARGS[@]}"; do
	printf ' "%s"' "$arg"
done
printf '\n'

if [[ "$DRY_RUN" -eq 1 ]]; then
	exit 0
fi

if [[ "$DETACHED" -eq 1 ]]; then
	(
		cd "$(dirname "$SERVER_EXE")"
		nohup "$SERVER_EXE" "${ARGS[@]}" >/dev/null 2>&1 &
	)
	echo
	echo "llama-server started in the background."
	echo "Use the GUI with Server URL: http://$BIND_HOST:$PORT"
else
	echo
	echo "llama-server is starting in the current console. Press Ctrl+C to stop it."
	cd "$(dirname "$SERVER_EXE")"
	exec "$SERVER_EXE" "${ARGS[@]}"
fi
