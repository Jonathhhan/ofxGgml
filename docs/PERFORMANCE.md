# Performance Guide

`ofxGgml` performs best when you treat setup, graph allocation, tensor upload, and compute as separate phases.

## Quick start

Run the addon benchmarks with:

```bash
./scripts/benchmark-addon.sh
```

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\benchmark-addon.ps1
```

Both wrappers configure `tests/` with `OFXGGML_ENABLE_BENCHMARK_TESTS=ON`, build the Catch2 test runner, and execute the stable benchmark suite (`[benchmark]~[manual]`).

## What to optimize first

- Reuse graphs whenever the tensor layout is stable. Allocating once and refreshing tensor data is much cheaper than rebuilding the graph every frame.
- Separate setup cost from steady-state cost. Measure `setup()`, `allocGraph()`, tensor upload, and `computeGraph()` independently.
- Keep host-to-device transfers small. Upload only the tensors that actually changed.
- Prefer async submission only when the app has useful work to do before `synchronize()`.
- Clamp runtime settings to sane values. A `graphSize` of zero or invalid thread counts should never silently poison performance.

## Current examples

- `ofxGgmlBasicExample` shows a reusable matrix benchmark for the active backend.
- `ofxGgmlNeuralExample` keeps a tiny feedforward graph alive and reports steady-state inference latency.
- `tests/test_benchmark.cpp` covers tensor ops, reductions, transfers, sync-vs-async, and backend comparisons.

## Recommended workflow

1. Build ggml for the backends you care about.
2. Run `scripts/benchmark-addon.*` to establish a baseline.
3. Compare setup, allocation, and steady-state inference separately.
4. Re-run the same filter after every backend or memory-path change.

## Notes

- GPU wins depend heavily on tensor size and transfer overhead.
- Very small graphs often benchmark better on CPU because launch overhead dominates.
- If you are benchmarking UI-facing code, prefer repeated runs with preallocated graphs so the numbers match real app behavior.
