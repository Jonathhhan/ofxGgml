#!/usr/bin/env bash
set -euo pipefail

BUILD_PATH=""
FIX=0
CHECKS=""
FILES=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-path)
      BUILD_PATH="${2:-}"
      shift 2
      ;;
    --fix)
      FIX=1
      shift
      ;;
    --checks)
      CHECKS="${2:-}"
      shift 2
      ;;
    --help|-h)
      cat <<'EOF'
Usage: ./scripts/run-clang-tidy.sh [--build-path PATH] [--fix] [--checks CHECKS] [files...]
EOF
      exit 0
      ;;
    *)
      FILES+=("$1")
      shift
      ;;
  esac
done

if ! command -v clang-tidy >/dev/null 2>&1; then
  echo "clang-tidy was not found in PATH." >&2
  exit 1
fi

find_compile_commands() {
  local candidates=()
  if [[ -n "$BUILD_PATH" ]]; then
    if [[ "$(basename "$BUILD_PATH")" == "compile_commands.json" ]]; then
      candidates+=("$BUILD_PATH")
    fi
    candidates+=("$BUILD_PATH/compile_commands.json")
  fi

  candidates+=(
    "compile_commands.json"
    "build/compile_commands.json"
    "ofxGgmlGuiExample/compile_commands.json"
    "ofxGgmlGuiExample/build/compile_commands.json"
  )

  local candidate
  for candidate in "${candidates[@]}"; do
    if [[ -f "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done

  return 1
}

COMPILE_COMMANDS="$(find_compile_commands || true)"
if [[ -z "$COMPILE_COMMANDS" ]]; then
  echo "No compile_commands.json found. Pass --build-path or generate a compile database first." >&2
  exit 1
fi

if [[ ${#FILES[@]} -eq 0 ]]; then
  while IFS= read -r file; do
    FILES+=("$file")
  done < <(find src ofxGgmlGuiExample/src -type f \( -name '*.cpp' -o -name '*.cxx' -o -name '*.cc' -o -name '*.c' \) | sort)
fi

ARGS=()
if [[ -n "$CHECKS" ]]; then
  ARGS+=("-checks=$CHECKS")
fi
if [[ "$FIX" -eq 1 ]]; then
  ARGS+=("-fix")
fi
ARGS+=("-p" "$(dirname "$COMPILE_COMMANDS")")
ARGS+=("${FILES[@]}")

echo "Running clang-tidy with compile database: $COMPILE_COMMANDS"
clang-tidy "${ARGS[@]}"
