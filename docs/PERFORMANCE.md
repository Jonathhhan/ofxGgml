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

## Inference Performance

### Server Mode (Recommended)

For text generation and LLM inference, **server mode is strongly recommended** for production use:

**Enable server mode:**
```cpp
ofxGgmlInferenceSettings settings;
settings.useServerBackend = true;
settings.serverUrl = "http://127.0.0.1:8080";
```

**Benefits of server mode:**
- **10-50ms faster** per request (eliminates process spawn overhead)
- **Persistent model loading** - model stays in memory between requests
- **Connection pooling** - reuses HTTP connections
- **Streaming support** - real-time token generation
- **Better GPU utilization** - single process manages GPU memory

**CLI mode fallback:**
The CLI mode (process spawn per request) remains available as a fallback but has significant overhead:
- Process creation: ~10-50ms per request
- Model reload on every request
- No connection reuse
- Higher memory fragmentation

### Prompt Caching

**Prompt caching is now enabled by default** (`promptCacheAll = true`) providing:
- **2-5x speedup** for multi-turn conversations
- Cached context reuse for repeated prompts
- Automatic cache path generation

To customize caching:
```cpp
settings.promptCacheAll = true;           // Default: enabled
settings.autoPromptCache = true;          // Auto-generate paths
settings.promptCachePath = "custom.bin";  // Optional custom path
```

**Best for:**
- Chat applications with conversation history
- Code assistants with repository context
- Multi-turn workflows with stable prefixes

### Batch Processing

For multiple independent requests, use the batch API:

```cpp
std::vector<ofxGgmlBatchRequest> requests = {
    {prompt1, settings1},
    {prompt2, settings2},
    {prompt3, settings3}
};

ofxGgmlBatchSettings batchSettings;
batchSettings.allowParallelProcessing = true;  // Server mode only
batchSettings.maxConcurrent = 4;

auto result = inference.generateBatch(modelPath, requests, batchSettings);
```

**Benefits:**
- **2-4x speedup** for parallel requests (server mode)
- Concurrent processing with configurable parallelism
- Automatic sequential fallback for CLI mode
- Built-in metrics tracking

**Best for:**
- Video scene planning (multiple scenes in parallel)
- Batch summarization
- Multi-language translation

### Inference Performance Checklist

1. ✅ **Use server mode** for production inference
2. ✅ **Enable prompt caching** (default: enabled)
3. ✅ **Use batch API** for multiple requests
4. ✅ **Reuse settings objects** to avoid reconstruction
5. ✅ **Profile with metrics** (`ofxGgmlMetrics::getInstance()`)

### Expected Performance

**Text Generation (server mode + prompt caching):**
- First request: 100-500ms (model warm-up)
- Subsequent requests: 20-100ms (cached context)
- Batch processing: 2-4x improvement over sequential

**Text Generation (CLI mode):**
- Every request: 100-600ms (process spawn + model load)
- No caching benefit
- 10-30x slower than server mode for multi-turn workflows

