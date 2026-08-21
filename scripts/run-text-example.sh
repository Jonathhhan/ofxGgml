#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADDON_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
EXAMPLE_ROOT="$ADDON_ROOT/ofxGgmlTextExample"

MODEL="${OFXGGML_TEXT_MODEL:-}"
SERVER_URL="${OFXGGML_TEXT_SERVER_URL:-http://127.0.0.1:8080}"
BACKEND="${OFXGGML_TEXT_BACKEND:-server}"
NO_AUTO_SERVER=0
DRY_RUN=0
STRICT_MODEL=0

usage() {
  cat <<'EOF'
Usage: bash scripts/run-text-example.sh [options]

Options:
  --model PATH          GGUF model path
  --server-url URL      llama-server URL (default: http://127.0.0.1:8080)
  --backend NAME        server or cli (default: server)
  --no-auto-server      do not start bundled llama-server
  --strict-model        enable strict text model filtering
  --dry-run             print what would run
  -h, --help            show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --model) MODEL="$2"; shift 2 ;;
    --server-url) SERVER_URL="$2"; shift 2 ;;
    --backend) BACKEND="$2"; shift 2 ;;
    --no-auto-server) NO_AUTO_SERVER=1; shift ;;
    --strict-model) STRICT_MODEL=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

find_model() {
  local dir model
  for dir in \
    "$ADDON_ROOT/../models" \
    "$ADDON_ROOT/models" \
    "$EXAMPLE_ROOT/bin/data" \
    "$EXAMPLE_ROOT/bin/data/models" \
    "$EXAMPLE_ROOT/models"
  do
    [[ -d "$dir" ]] || continue
    model="$(find "$dir" -maxdepth 1 -type f -name '*.gguf' -print -quit 2>/dev/null || true)"
    if [[ -n "$model" ]]; then printf '%s\n' "$model"; return 0; fi
  done
  return 1
}

find_example_exe() {
  local p
  for p in \
    "$EXAMPLE_ROOT/bin/ofxGgmlTextExample.app/Contents/MacOS/ofxGgmlTextExample" \
    "$EXAMPLE_ROOT/bin/ofxGgmlTextExample" \
    "$EXAMPLE_ROOT/bin/ofxGgmlTextExample_debug.app/Contents/MacOS/ofxGgmlTextExample_debug" \
    "$EXAMPLE_ROOT/bin/ofxGgmlTextExample_debug"
  do
    if [[ -f "$p" ]]; then printf '%s\n' "$p"; return 0; fi
  done
  return 1
}

health_ready() {
  curl --silent --fail --max-time 2 "$SERVER_URL/health" >/dev/null 2>&1
}

[[ -n "$MODEL" ]] || MODEL="$(find_model || true)"
EXAMPLE_EXE="$(find_example_exe || true)"

if [[ -z "$EXAMPLE_EXE" ]]; then
  echo "Text example executable was not found under $EXAMPLE_ROOT/bin." >&2
  echo "Build ofxGgmlTextExample in Xcode/openFrameworks first." >&2
  exit 1
fi

export OFXGGML_TEXT_BACKEND="$BACKEND"
export OFXGGML_TEXT_SERVER_URL="$SERVER_URL"
export OFXGGML_TEXT_STRICT_MODEL="$STRICT_MODEL"
if [[ -n "$MODEL" ]]; then export OFXGGML_TEXT_MODEL="$MODEL"; fi

if [[ "$BACKEND" == "server" ]]; then
  echo "Using llama-server: $SERVER_URL"
  if [[ -n "$MODEL" ]]; then echo "Using text model: $MODEL"; fi

  if ! health_ready; then
    if [[ $NO_AUTO_SERVER -eq 1 ]]; then
      echo "llama-server is not reachable at $SERVER_URL" >&2
      exit 1
    fi
    if [[ -z "$MODEL" ]]; then
      echo "No GGUF model found, so llama-server cannot be started automatically." >&2
      echo "Pass --model /path/to/model.gguf or put a model under addons/models." >&2
      exit 1
    fi

    echo "llama-server is not responding; starting bundled server"
    if [[ $DRY_RUN -eq 0 ]]; then
      bash "$SCRIPT_DIR/start-llama-server.sh" \
        --model "$MODEL" \
        --detached \
        --log-dir "$ADDON_ROOT/build/llama-server"
    fi
  fi
fi

if [[ $DRY_RUN -eq 1 ]]; then
  echo "Executable: $EXAMPLE_EXE"
  echo "Backend: $BACKEND"
  echo "Server URL: $SERVER_URL"
  exit 0
fi

echo "Starting ofxGgmlTextExample"
exec "$EXAMPLE_EXE"
