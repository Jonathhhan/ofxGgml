#!/usr/bin/env bash
set -euo pipefail

MODEL=""
PROMPT="Hello"
RUNS=5
TOKENS=128
CONTEXT=2048
THREADS=0
OUTPUT_JSON=""
CLI_BIN="llama-cli"

usage() {
cat <<USAGE
benchmark-llama-cli.sh — quick llama.cpp benchmark helper

Usage:
  ./scripts/benchmark-llama-cli.sh --model <path.gguf> [options]

Options:
  --model PATH       GGUF model path (required)
  --prompt TEXT      Prompt text (default: "Hello")
  --runs N           Number of repeated runs (default: 5)
  --tokens N         Max generated tokens per run (default: 128)
  --context N        Context size (default: 2048)
  --threads N        CPU threads (default: 0 = llama default)
  --cli PATH         llama-cli executable (default: llama-cli)
  --output-json PATH Save full benchmark report as JSON
  --help             Show this help
USAGE
}

while [[ $# -gt 0 ]]; do
case "$1" in
--model) MODEL="$2"; shift 2 ;;
--prompt) PROMPT="$2"; shift 2 ;;
--runs) RUNS="$2"; shift 2 ;;
--tokens) TOKENS="$2"; shift 2 ;;
--context) CONTEXT="$2"; shift 2 ;;
--threads) THREADS="$2"; shift 2 ;;
--cli) CLI_BIN="$2"; shift 2 ;;
--output-json) OUTPUT_JSON="$2"; shift 2 ;;
--help|-h) usage; exit 0 ;;
*) echo "Unknown option: $1" >&2; usage; exit 1 ;;
esac
done

[[ -n "$MODEL" ]] || { echo "Error: --model is required" >&2; exit 1; }
[[ -f "$MODEL" ]] || { echo "Error: model not found: $MODEL" >&2; exit 1; }
[[ "$RUNS" =~ ^[0-9]+$ ]] || { echo "Error: --runs must be an integer" >&2; exit 1; }
[[ "$TOKENS" =~ ^[0-9]+$ ]] || { echo "Error: --tokens must be an integer" >&2; exit 1; }
[[ "$CONTEXT" =~ ^[0-9]+$ ]] || { echo "Error: --context must be an integer" >&2; exit 1; }
[[ "$THREADS" =~ ^[0-9]+$ ]] || { echo "Error: --threads must be an integer" >&2; exit 1; }

prompt_file="$(mktemp "${TMPDIR:-/tmp}/ofxggml-bench-prompt.XXXXXX.txt")"
results_file="$(mktemp "${TMPDIR:-/tmp}/ofxggml-bench-result.XXXXXX.txt")"
cleanup() {
rm -f "$prompt_file" "$results_file"
}
trap cleanup EXIT
printf '%s' "$PROMPT" > "$prompt_file"

for ((i = 1; i <= RUNS; i++)); do
start_ns="$(date +%s%N)"
cmd=("$CLI_BIN" -m "$MODEL" --file "$prompt_file" -n "$TOKENS" -c "$CONTEXT" --simple-io --no-display-prompt)
if [[ "$THREADS" -gt 0 ]]; then
cmd+=(--threads "$THREADS")
fi
raw="$("${cmd[@]}" 2>&1)"
status=$?
end_ns="$(date +%s%N)"
dur_ms=$(( (end_ns - start_ns) / 1000000 ))
if [[ $status -ne 0 ]]; then
echo "Run $i failed:" >&2
echo "$raw" >&2
exit $status
fi
word_count="$(printf '%s' "$raw" | wc -w | tr -d ' ')"
echo "$dur_ms,$word_count" >> "$results_file"
echo "Run $i/$RUNS: ${dur_ms}ms, approx_tokens=${word_count}"
done

python3 - "$results_file" "$RUNS" "$OUTPUT_JSON" <<'PY'
import json
import math
import statistics
import sys
from pathlib import Path

rows_path = Path(sys.argv[1])
runs = int(sys.argv[2])
out_json = sys.argv[3]
rows = []
for line in rows_path.read_text(encoding="utf-8").splitlines():
    if not line.strip():
        continue
    ms_s, tok_s = line.strip().split(",", 1)
    ms = float(ms_s)
    tok = float(tok_s)
    tps = (tok / (ms / 1000.0)) if ms > 0 else 0.0
    rows.append({"latency_ms": ms, "approx_tokens": tok, "approx_tokens_per_sec": tps})

lat = [r["latency_ms"] for r in rows]
tps = [r["approx_tokens_per_sec"] for r in rows]

lat_sorted = sorted(lat)

def percentile(values, p):
    if not values:
        return 0.0
    k = (len(values) - 1) * p
    f = math.floor(k)
    c = math.ceil(k)
    if f == c:
        return values[int(k)]
    return values[f] * (c - k) + values[c] * (k - f)

report = {
    "runs": runs,
    "latency_ms": {
        "mean": statistics.mean(lat) if lat else 0.0,
        "p50": percentile(lat_sorted, 0.50),
        "p90": percentile(lat_sorted, 0.90),
        "p95": percentile(lat_sorted, 0.95),
        "max": max(lat) if lat else 0.0,
    },
    "approx_tokens_per_sec": {
        "mean": statistics.mean(tps) if tps else 0.0,
        "max": max(tps) if tps else 0.0,
    },
    "samples": rows,
}

print("\nBenchmark summary")
print(f"  mean latency: {report['latency_ms']['mean']:.2f} ms")
print(f"  p50 latency:  {report['latency_ms']['p50']:.2f} ms")
print(f"  p95 latency:  {report['latency_ms']['p95']:.2f} ms")
print(f"  mean tok/s:   {report['approx_tokens_per_sec']['mean']:.2f}")

if out_json:
    Path(out_json).write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"  report json:  {out_json}")
PY
