#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADDON_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

MODEL_PATH="${OFXGGML_TEXT_MODEL:-}"
SERVER_EXE="${OFXGGML_LLAMA_SERVER:-}"
HOST="127.0.0.1"
PORT="8080"
GPU_LAYERS="28"
CONTEXT_SIZE="4096"
DETACHED=0
FORCE_NEW=0
NO_HEALTH_CHECK=0
DRY_RUN=0
STARTUP_TIMEOUT=30
LOG_DIR=""

usage() {
  cat <<'EOF'
Usage: bash scripts/start-llama-server.sh [options]

Options:
  --model PATH          GGUF model path
  --server-exe PATH     llama-server executable
  --host HOST           bind host (default: 127.0.0.1)
  --port PORT           port (default: 8080)
  --gpu-layers N        GPU layers passed as -ngl (default: 28)
  --context N           context size passed as -c (default: 4096)
  --detached            run server in background
  --force-new           do not reuse a reachable server
  --no-health-check     skip health checks
  --startup-timeout N   seconds to wait for readiness (default: 30)
  --log-dir PATH        detached logs directory
  --dry-run             print command only
  -h, --help            show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --model) MODEL_PATH="$2"; shift 2 ;;
    --server-exe) SERVER_EXE="$2"; shift 2 ;;
    --host) HOST="$2"; shift 2 ;;
    --port) PORT="$2"; shift 2 ;;
    --gpu-layers) GPU_LAYERS="$2"; shift 2 ;;
    --context) CONTEXT_SIZE="$2"; shift 2 ;;
    --detached) DETACHED=1; shift ;;
    --force-new) FORCE_NEW=1; shift ;;
    --no-health-check) NO_HEALTH_CHECK=1; shift ;;
    --startup-timeout) STARTUP_TIMEOUT="$2"; shift 2 ;;
    --log-dir) LOG_DIR="$2"; shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

find_server() {
  local p
  for p in \
    "$ADDON_ROOT/libs/llama/bin/llama-server" \
    "$ADDON_ROOT/libs/llama.cpp/build/bin/Release/llama-server" \
    "$ADDON_ROOT/libs/llama.cpp/build/bin/llama-server" \
    "$ADDON_ROOT/libs/llama.cpp/build/llama-server"
  do
    if [[ -f "$p" ]]; then printf '%s\n' "$p"; return 0; fi
  done
  return 1
}

find_model() {
  local dir model
  for dir in \
    "$ADDON_ROOT/../models" \
    "$ADDON_ROOT/models" \
    "$ADDON_ROOT/ofxGgmlTextExample/bin/data" \
    "$ADDON_ROOT/ofxGgmlTextExample/bin/data/models" \
    "$ADDON_ROOT/ofxGgmlTextExample/models" \
    "$ADDON_ROOT/ofxGgmlChatExample/bin/data" \
    "$ADDON_ROOT/ofxGgmlChatExample/bin/data/models" \
    "$ADDON_ROOT/ofxGgmlChatExample/models"
  do
    [[ -d "$dir" ]] || continue
    model="$(find "$dir" -maxdepth 1 -type f -name '*.gguf' -print -quit 2>/dev/null || true)"
    if [[ -n "$model" ]]; then printf '%s\n' "$model"; return 0; fi
  done
  return 1
}

health_ready() {
  curl --silent --fail --max-time 2 "http://$HOST:$PORT/health" >/dev/null 2>&1
}

[[ -n "$SERVER_EXE" ]] || SERVER_EXE="$(find_server || true)"
if [[ -z "$SERVER_EXE" || ! -f "$SERVER_EXE" ]]; then
  echo "Could not find llama-server." >&2
  echo "Build llama.cpp first or pass --server-exe /path/to/llama-server." >&2
  exit 1
fi

[[ -n "$MODEL_PATH" ]] || MODEL_PATH="$(find_model || true)"
if [[ -z "$MODEL_PATH" || ! -f "$MODEL_PATH" ]]; then
  echo "Could not find a GGUF model." >&2
  echo "Pass --model /path/to/model.gguf or set OFXGGML_TEXT_MODEL." >&2
  exit 1
fi

SERVER_URL="http://$HOST:$PORT"
if [[ $NO_HEALTH_CHECK -eq 0 && $DRY_RUN -eq 0 && $FORCE_NEW -eq 0 ]] && health_ready; then
  echo "llama-server is already ready at $SERVER_URL"
  exit 0
fi

ARGS=(-m "$MODEL_PATH" --host "$HOST" --port "$PORT" -ngl "$GPU_LAYERS" -c "$CONTEXT_SIZE")
printf 'Starting llama-server\n  exe:   %s\n  model: %s\n  url:   %s\n' "$SERVER_EXE" "$MODEL_PATH" "$SERVER_URL"
printf 'Command: '; printf '%q ' "$SERVER_EXE" "${ARGS[@]}"; printf '\n'

[[ $DRY_RUN -eq 0 ]] || exit 0

if [[ $DETACHED -eq 1 ]]; then
  [[ -n "$LOG_DIR" ]] || LOG_DIR="$ADDON_ROOT/build/llama-server"
  mkdir -p "$LOG_DIR"
  nohup "$SERVER_EXE" "${ARGS[@]}" >"$LOG_DIR/llama-server.out.log" 2>"$LOG_DIR/llama-server.err.log" &
  PID=$!
  printf '%s\n' "$PID" >"$LOG_DIR/llama-server.pid"
  echo "llama-server started in background (PID $PID)"

  if [[ $NO_HEALTH_CHECK -eq 0 ]]; then
    deadline=$((SECONDS + STARTUP_TIMEOUT))
    until health_ready; do
      if ! kill -0 "$PID" 2>/dev/null; then
        echo "llama-server exited before becoming ready." >&2
        echo "See $LOG_DIR/llama-server.err.log" >&2
        exit 1
      fi
      if (( SECONDS >= deadline )); then
        echo "llama-server did not become ready within ${STARTUP_TIMEOUT}s." >&2
        echo "Check $SERVER_URL/health and $LOG_DIR/llama-server.err.log" >&2
        exit 1
      fi
      sleep 0.5
    done
    echo "llama-server is ready at $SERVER_URL"
  fi
else
  echo "llama-server is running in this terminal. Press Ctrl+C to stop it."
  exec "$SERVER_EXE" "${ARGS[@]}"
fi
