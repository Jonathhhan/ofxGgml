# ofxGgml

An [openFrameworks](https://openframeworks.cc) addon wrapping the [ggml](https://github.com/ggml-org/ggml) tensor library for machine-learning computation.

> **Version 0.3** — ggml is bundled as a static library, plus native llama.cpp CLI inference helpers for generation, embeddings, and cache-aware workflows.

---

## Quick Start

```bash
# 1. Clone into your oF addons directory
cd openFrameworks/addons
git clone https://github.com/Jonathhhan/ofxGgml.git

# 2. Build ggml (GPU auto-detect by default) + llama.cpp CLI + download models
cd ofxGgml
./scripts/setup_linux_macos.sh              # Linux/macOS full setup (default auto-detect)
./scripts/setup_linux_macos.sh --auto       # explicit auto-detect
./scripts/setup_linux_macos.sh --cpu-only   # force CPU-only
./scripts/setup_linux_macos.sh --cuda       # explicitly enable CUDA

# Windows (Developer Command Prompt)
scripts\setup_windows.bat             # build ggml + download models
scripts\setup_windows.bat --cpu-only  # force CPU-only

# 3. Add ofxGgml to your project's addons.make and build
```

## Features

| Category | Capabilities |
|----------|-------------|
| **Backend management** | Automatic discovery of CPU, CUDA, Metal, Vulkan backends; runtime selection (CUDA → Vulkan → other accelerator → CPU fallback) |
| **GPU support** | GPU backends (CUDA, Vulkan, Metal) can be enabled via build script flags (e.g. `--cuda`, `--auto`) |
| **GGUF model loading** | Load GGUF files, inspect metadata and tensors, upload weights to GPU |
| **Native inference helper** | `ofxGgmlInference` API for llama.cpp CLI generation, embedding, session-cache reuse, and structured outputs |
| **Tensor wrapper** | Non-owning handle with OF-friendly data access (read/write floats, fill) |
| **Graph builder** | Fluent API for computation graphs — see [Operations](#supported-operations) |
| **Scheduled execution** | Multi-backend scheduler with automatic tensor placement and fallback |
| **Async execution** | Async submit + explicit synchronization for frame-friendly compute |
| **Profiling** | Built-in timings for setup, graph alloc, model upload, and compute |
| **Device enumeration** | Query devices, memory, and capabilities at runtime |

### Supported Operations

<details>
<summary>Click to expand full operation list</summary>

- **Element-wise**: add, sub, mul, div, scale, clamp, sqr, sqrt
- **Matrix**: matMul (A × Bᵀ), transpose, permute, reshape, view
- **Reductions**: sum, mean, argmax
- **Normalization**: norm, rmsNorm
- **Activations**: relu, gelu, silu, sigmoid, tanh, softmax
- **Transformer**: flashAttn, rope
- **Convolution / pooling**: conv1d, convTranspose1d, pool1d, pool2d, upscale
- **Loss**: crossEntropyLoss

</details>

## Requirements

- openFrameworks 0.12+
- CMake 3.18+ (for building ggml)
- C++17 compiler
- **Optional**: CUDA toolkit, Vulkan SDK, or Metal framework for GPU acceleration
- **ofxGgmlGuiExample only**: [ofxImGui](https://github.com/jvcleave/ofxImGui) — install it in your `addons/` folder before generating the project with the PG

## Building ggml

ggml source is bundled in `libs/ggml/`.  It is compiled as a static library.  By default, GPU backend auto-detection is enabled (`--auto`).  Use `--cpu-only` to force CPU-only builds, or explicit flags (`--cuda`, `--vulkan`, `--metal`) to force specific backends.

> **Windows / Visual Studio users:** You must build ggml before opening your OF project, otherwise the linker will report `LNK1181: cannot open input file "ggml.lib"`.  See the [Windows](#windows-visual-studio) section below.

### Automated (recommended)

```bash
./scripts/setup_linux_macos.sh              # Linux/macOS: ggml + llama CLI + models (auto-detect)
./scripts/setup_linux_macos.sh --cuda       # Linux/macOS: explicitly enable CUDA
./scripts/setup_linux_macos.sh --skip-llama --skip-model  # Linux/macOS: ggml only
./scripts/setup_linux_macos.sh --skip-model  # Linux/macOS: ggml + llama only (no model download)
./scripts/setup_linux_macos.sh --skip-ggml --skip-llama --model-preset 2  # Linux/macOS: model only
```

```bat
:: Windows: ggml + models
scripts\setup_windows.bat
:: Windows: force CUDA
scripts\setup_windows.bat --cuda
:: Windows: ggml only
scripts\setup_windows.bat --skip-model
:: Windows: model preset 2 only
scripts\setup_windows.bat --skip-ggml --model-preset 2
```

### ggml only

```bash
./scripts/build-ggml.sh           # auto-detect GPU backends (default)
./scripts/build-ggml.sh --auto    # explicit auto-detect
./scripts/build-ggml.sh --cuda  # force CUDA on
./scripts/build-ggml.sh --vulkan  # force Vulkan on
./scripts/build-ggml.sh --cpu-only  # CPU only
```

### Windows (Visual Studio)

Open a **Developer Command Prompt for VS** (or any terminal with CMake in your PATH) and run:

```bat
scripts\build-ggml.bat              &:: auto-detect GPU backends (default)
scripts\build-ggml.bat --cuda       &:: force CUDA on
scripts\build-ggml.bat --vulkan     &:: force Vulkan on
scripts\build-ggml.bat --cpu-only   &:: CPU only
```

This builds both **Debug** and **Release** configurations, producing libraries in `libs\ggml\build\src\Release\` and `libs\ggml\build\src\Debug\`.
If a parallel Windows build hits a transient CUDA/MSBuild object-link race (for example missing `*.obj` in `ggml-cuda`), the script automatically retries that configuration with a single job.

After building ggml, regenerate your project with the openFrameworks Project Generator so the generated VS project picks up the latest addon library list.

`scripts\build-ggml.bat` also refreshes `addon_config.mk` for the `vs` section so Visual Studio links the exact ggml libraries you just built. When CUDA is enabled, it injects the CUDA Toolkit dependencies (`cublas.lib`, `cudart.lib`) using your installed CUDA path—rerun the script if you see `LNK1181` about `cublas.lib`.

> CPU-only builds remain clean by default. CUDA-specific libraries are no longer linked unconditionally.

### Manual — CMake

```bash
cd ofxGgml
cmake -B libs/ggml/build libs/ggml -DCMAKE_BUILD_TYPE=Release
cmake --build libs/ggml/build --config Release -j$(nproc)
```

CMake options:

| Option | Default | Description |
|--------|---------|-------------|
| `OFXGGML_GPU_AUTODETECT` | `ON` | Auto-detect and enable available GPU backends |
| `OFXGGML_CUDA` | `OFF` | Force CUDA backend on/off |
| `OFXGGML_VULKAN` | `OFF` | Force Vulkan backend on/off |
| `OFXGGML_METAL` | `OFF` | Force Metal backend on/off |

### Updating bundled ggml

```bash
./scripts/update-ggml-source.sh          # update to latest master
./scripts/update-ggml-source.sh --commit abc123  # pin to a specific commit
./scripts/build-ggml.sh --clean          # rebuild after updating
```

## Addon Structure

```
ofxGgml/
├── addon_config.mk          # oF project generator configuration
├── src/
│   ├── ofxGgml.h             # umbrella header — include this
│   ├── ofxGgmlCore.h/.cpp    # backend init, compute, model weight loading
│   ├── ofxGgmlGraph.h/.cpp   # computation graph builder
│   ├── ofxGgmlInference.h/.cpp # llama.cpp CLI generation / embeddings helper
│   ├── ofxGgmlModel.h/.cpp   # GGUF model loader
│   ├── ofxGgmlProjectMemory.h/.cpp # prompt memory helper for persistent coding context
│   ├── ofxGgmlScriptSource.h/.cpp # local-folder / GitHub script source browser helper
│   ├── ofxGgmlTensor.h/.cpp  # non-owning tensor wrapper
│   ├── ofxGgmlTypes.h        # enums, settings, result structs
│   ├── ofxGgmlHelpers.h      # utility functions
│   └── ofxGgmlVersion.h      # version macros
├── libs/ggml/                # bundled ggml source + CMake build
│   ├── CMakeLists.txt        # addon's CMake wrapper (GPU opt-in)
│   ├── include/              # ggml headers
│   ├── src/                  # ggml source (core, CPU, CUDA, Vulkan, Metal)
│   ├── cmake/                # ggml cmake helpers
│   └── build/                # build output (created by build-ggml.sh)
├── scripts/
│   ├── setup_linux_macos.sh  # Linux/macOS full setup entry point
│   ├── setup_windows.bat     # Windows setup entry point
│   ├── build-ggml.sh         # build bundled ggml — Linux/macOS
│   ├── build-ggml.bat        # build bundled ggml — Windows/VS
│   ├── update-addon-config.bat # refresh addon_config.mk [vs] with ggml/CUDA libs
│   ├── update-ggml-source.sh # update bundled ggml to latest upstream
│   ├── build-llama-cli.sh    # build & install llama.cpp CLI tools
│   └── download-model.sh     # download GGUF model presets
├── ofxGgmlBasicExample/      # matrix multiplication demo
├── ofxGgmlNeuralExample/     # feedforward neural network demo
└── ofxGgmlGuiExample/        # full ImGui AI Studio
```

## Usage

### Include the addon

```cpp
#include "ofxGgml.h"
```

### Loading a GGUF model

```cpp
ofxGgml ggml;
ggml.setup();

ofxGgmlModel model;
if (!model.load("path/to/model.gguf")) {
    // handle error
}

// Inspect metadata
std::string arch = model.getMetadataString("general.architecture");
int32_t ctxLen   = model.getMetadataInt32("llama.context_length");

// Upload weights to backend (CPU, CUDA, Metal, …)
ggml.loadModelWeights(model);
```

### Basic matrix multiplication

```cpp
ofxGgml ggml;
ggml.setup();

ofxGgmlGraph graph;
auto a = graph.newTensor2d(ofxGgmlType::F32, 2, 4);
auto b = graph.newTensor2d(ofxGgmlType::F32, 2, 3);
graph.setInput(a);
graph.setInput(b);
auto result = graph.matMul(a, b);
graph.setOutput(result);
graph.build(result);

// Load data
float dataA[] = { 2,8, 5,1, 4,2, 8,6 };
float dataB[] = { 10,5, 9,9, 5,4 };
ggml.allocGraph(graph);
ggml.setTensorData(a, dataA, sizeof(dataA));
ggml.setTensorData(b, dataB, sizeof(dataB));

auto r = ggml.computeGraph(graph);
if (r.success) {
    std::vector<float> out(result.getNumElements());
    ggml.getTensorData(result, out.data(), out.size() * sizeof(float));
}
```

### Neural network (single layer + ReLU)

```cpp
ofxGgmlGraph graph;
auto input   = graph.newTensor2d(ofxGgmlType::F32, 4, 1);
auto weights = graph.newTensor2d(ofxGgmlType::F32, 4, 3);
auto bias    = graph.newTensor1d(ofxGgmlType::F32, 3);

graph.setInput(input);
graph.setInput(weights);
graph.setInput(bias);

auto hidden = graph.matMul(weights, input);
auto biased = graph.add(hidden, bias);
auto output = graph.relu(biased);

graph.setOutput(output);
graph.build(output);

ggml.allocGraph(graph);
// ... set tensor data ...
auto r = ggml.computeGraph(graph);
```

### Async execution + explicit synchronization

```cpp
ggml.allocGraph(graph);
auto submit = ggml.computeGraphAsync(graph);
if (submit.success) {
    auto done = ggml.synchronize();
    auto timings = ggml.getLastTimings();
}
```

### Native text generation + KV cache reuse

```cpp
ofxGgmlInference inf;
ofxGgmlInferenceSettings s;
s.maxTokens = 256;
s.promptCachePath = "chat-cache.bin"; // KV/session reuse
s.promptCacheAll = true;
s.jsonSchema = "{\"type\":\"object\",\"properties\":{\"answer\":{\"type\":\"string\"}}}";

auto r = inf.generate("path/to/model.gguf", "Return JSON with an answer.", s);
if (r.success) {
    std::cout << r.text << std::endl;
}
```

### Embeddings + similarity search helper (mini RAG flow)

```cpp
ofxGgmlInference inf;
ofxGgmlEmbeddingIndex index;

auto e1 = inf.embed("path/to/model.gguf", "openFrameworks is a C++ toolkit");
auto e2 = inf.embed("path/to/model.gguf", "ggml is a tensor compute library");
if (e1.success) index.add("doc1", "openFrameworks is a C++ toolkit", e1.embedding);
if (e2.success) index.add("doc2", "ggml is a tensor compute library", e2.embedding);

auto q = inf.embed("path/to/model.gguf", "What library handles tensors?");
if (q.success) {
    auto hits = index.search(q.embedding, 1);
}
```

### Allocation reuse for stable graphs

- Repeated `allocGraph(graph)` calls on the same built graph instance (same graph pointer/object) avoid re-allocation.
- New/different graph instances still use the normal allocation path automatically.

## API Reference

| Class | Purpose |
|-------|---------|
| `ofxGgml` | Backend init, device enumeration, compute scheduling, model weight loading |
| `ofxGgmlGraph` | Build computation graphs (tensor creation + operations) |
| `ofxGgmlInference` | CLI-backed text generation + embeddings with KV cache and structured-output flags |
| `ofxGgmlEmbeddingIndex` | In-memory cosine-similarity retrieval helper for RAG-style lookups |
| `ofxGgmlModel` | Load GGUF model files, inspect metadata and tensor information |
| `ofxGgmlProjectMemory` | Persist request/response memory and prepend it to future prompts |
| `ofxGgmlScriptSource` | Browse script sources from local folders or GitHub repos, load files, and save to local source |
| `ofxGgmlTensor` | Non-owning tensor handle with metadata and data access |
| `Result<T>` | Type-safe result containing either success value or error (similar to C++23 std::expected) |
| `ofxGgmlError` | Detailed error information with error codes and messages |
| `ofxGgmlTypes` | Enums and settings (`ofxGgmlType`, `ofxGgmlBackendType`, …) |
| `ofxGgmlHelpers` | Utility functions (type names, byte formatting, …) |
| `ofxGgmlVersion` | Version macros (`OFX_GGML_VERSION_STRING`, etc.) |

## Examples

| Example | Description |
|---------|-------------|
| **ofxGgmlBasicExample/** | Matrix multiplication with console output |
| **ofxGgmlNeuralExample/** | Simple feedforward neural network visualized in the OF window |
| **ofxGgmlGuiExample/** | Full ImGui-based AI Studio with six modes (Chat, Script, Summarize, Write, Translate, Custom) — requires [ofxImGui](https://github.com/jvcleave/ofxImGui) |

### ofxGgmlGuiExample features

- **Model preselection** — choose from recommended GGUF models via sidebar
- **Script language selector** — 8 language presets (C++, Python, JS, Rust, GLSL, Go, Bash, TS)
- **Script source browser** — powered by core addon class `ofxGgmlScriptSource` for local folder + GitHub repository browsing
- **Hierarchical repository review** — “Review All Files” now uses embeddings + RAG to select relevant files, runs multi-pass summaries (file → architecture → integration), applies smart file filtering (recency, complexity, deps, type), and manages token budgets with sliding windows
- **Project memory (Script mode)** — automatically stores prior coding requests/responses and reuses them in later script prompts
- **Session persistence** — auto-save/load, File → Save/Load Session

### Real inference in ofxGgmlGuiExample

The GUI example uses [llama.cpp](https://github.com/ggml-org/llama.cpp) CLI tools for text generation:

- Expected model location: `ofxGgmlGuiExample/bin/data/models/<preset>.gguf`
- Runtime requirement: `llama-completion`, `llama-cli`, or `llama-embedding` in `PATH`, a common install directory, or `libs/llama/bin/`

Build the tools with:

```bash
./scripts/build-llama-cli.sh                     # system install
./scripts/build-llama-cli.sh --prefix ./libs/llama  # addon-local install
```

## Build Scripts

| Script | Purpose |
|--------|---------|
| `scripts/setup_linux_macos.sh` | **Linux/macOS setup**: builds ggml + llama CLI + downloads models |
| `scripts/setup_windows.bat` | **Windows setup**: builds ggml + downloads models |
| `scripts/build-ggml.sh` | Build the bundled ggml library (Linux/macOS, `--auto` by default) |
| `scripts/build-ggml.bat` | Build the bundled ggml library (Windows/VS, `--auto` default) |
| `scripts/update-addon-config.bat` | **Windows only**: Update `addon_config.mk` with built libraries (fixes linker errors) |
| `scripts/update-ggml-source.sh` | Update bundled ggml source to latest upstream |
| `scripts/build-llama-cli.sh` | Clone, compile, and install llama.cpp CLI tools |
| `scripts/download-model.sh` | Download GGUF model presets |
| `scripts/benchmark-llama-cli.sh` | Benchmark llama-cli latency and approximate tokens/sec |

`build-ggml.sh` supports `--cuda`, `--vulkan`, `--metal`, `--auto`, `--cpu-only`, `--jobs`, `--clean`, and `--help`.  By default GPU backend auto-detection is enabled; use `--cpu-only` to force CPU-only builds.

### Model presets

| # | Model | Size | Best for |
|---|-------|------|----------|
| 1 | Qwen2.5-1.5B Instruct Q4_K_M | ~1.0 GB | chat, general |
| 2 | Qwen2.5-Coder-1.5B Instruct Q4_K_M | ~1.0 GB | scripting, code generation |
| 3 | Phi-3.5-mini Instruct Q4_K_M | ~2.4 GB | reasoning, analysis, complex tasks |
| 4 | Llama-3.2-1B Instruct Q4_K_M | ~0.9 GB | lightweight, fast inference |
| 5 | TinyLlama-1.1B Chat Q4_K_M | ~0.6 GB | very lightweight, testing, prototyping |

```bash
./scripts/download-model.sh                    # download default presets (1 and 2)
./scripts/download-model.sh --preset 3         # Phi-3.5-mini for complex reasoning
./scripts/download-model.sh --preset 4         # Llama-3.2-1B for lightweight use
./scripts/download-model.sh --task script      # same as --preset 2
./scripts/download-model.sh --list             # show all presets
./scripts/download-model.sh --model <URL> --checksum <SHA256>  # custom with checksum verification
```

Model preset metadata is stored in `scripts/model-catalog.json`. `download-model.sh` supports resumable downloads and validates SHA256 checksums when provided. If a preset checksum is blank in the catalog, the script prints an explicit warning and skips integrity verification for that download.

**Updating Checksums**: Run `./scripts/update-model-checksums.sh --all` to automatically download models and compute verified SHA256 checksums for the catalog.

## Testing

ofxGgml includes a comprehensive unit test suite using Catch2.

### Running Tests

```bash
# Build ggml first (required)
./scripts/build-ggml.sh --cpu-only

# Run all tests
cd tests
./run-tests.sh

# Run specific test tags
./build/tests/ofxGgml-tests "[tensor]"
./build/tests/ofxGgml-tests "[graph]"
./build/tests/ofxGgml-tests "[result]"
```

### Test Coverage

- **Tensor operations**: Creation, element-wise ops, unary ops
- **Graph building**: Lifecycle, matrix ops, activations, normalization
- **Error handling**: Result<T> type, error codes, exception safety

See `tests/README.md` for detailed testing documentation.

### CI Integration

Tests run automatically on every push and pull request via GitHub Actions. The CI pipeline includes:

- **Smoke tests**: Build ggml, validate scripts and configuration files
- **Unit tests**: Comprehensive test suite using Catch2 (280+ test cases)
- **Static analysis**: cppcheck and clang-tidy for code quality (warnings allowed, not enforced)

Static analysis reports are uploaded as artifacts for each CI run and can be reviewed to improve code quality over time.

## Error Handling

ofxGgml uses a `Result<T>` pattern for robust error handling:

```cpp
#include "ofxGgml.h"

// Functions return Result<T> for operations that can fail
Result<int> myOperation() {
    if (errorCondition) {
        return ofxGgmlError(ofxGgmlErrorCode::InvalidArgument, "description");
    }
    return successValue;
}

// Check success and handle errors
auto result = myOperation();
if (result.isOk()) {
    int value = result.value();
} else {
    std::cout << "Error: " << result.error().toString() << std::endl;
}

// Or use valueOr for a default
int value = result.valueOr(defaultValue);
```

Available error codes: `BackendInitFailed`, `DeviceNotFound`, `OutOfMemory`, `GraphNotBuilt`, `InvalidTensor`, `ComputeFailed`, `ModelLoadFailed`, `InferenceExecutableMissing`, and more. See `src/ofxGgmlResult.h` for the complete list.

## Troubleshooting

### ggml libraries not found

Make sure you've built ggml first:

```bash
# Linux / macOS
./scripts/build-ggml.sh
```

```bat
:: Windows (Developer Command Prompt)
scripts\build-ggml.bat
```

The build output should be in `libs/ggml/build/src/` (e.g. `libggml.a`, `libggml-base.a`, `libggml-cpu.a` on Linux/macOS, or `ggml.lib` etc. in `libs\ggml\build\src\Release\` on Windows).

### llama-completion not found

The GUI example searches for `llama-completion`, `llama-cli`, or `llama` in:

1. Custom path from GUI settings
2. System `PATH`
3. Running executable's directory
4. `libs/llama/bin/` (addon-local)
5. Common directories (`/usr/local/bin`, `~/.local/bin`)

Build with: `./scripts/build-llama-cli.sh`

### Visual Studio linker error: unresolved external symbol "ggml_backend_cuda_reg"

This error occurs when ggml was built with CUDA support, but the `addon_config.mk` file doesn't include the CUDA library in the link list.

**Solution 1 (Automatic):** Run the update script to scan built libraries and update the config:

```bat
scripts\update-addon-config.bat
```

Then regenerate your Visual Studio project with the openFrameworks Project Generator.

**Solution 2 (Rebuild):** Re-run the build script, which now automatically updates the addon config:

```bat
scripts\build-ggml.bat
```

Then regenerate your Visual Studio project with the openFrameworks Project Generator.

**Note:** This issue was fixed in recent versions. If you built ggml before this fix, use one of the solutions above to update your configuration.

### Visual Studio linker error: unresolved external symbol "cublasCreate_v2", "cublasDestroy_v2", etc.

These errors occur when ggml was built with CUDA support, but the Visual Studio project doesn't link against the required CUDA Toolkit libraries (`cublas.lib`, `cudart.lib`).

The `ggml-cuda.lib` library depends on CUBLAS (CUDA Basic Linear Algebra Subroutines) from the CUDA Toolkit. When linking your final application, Visual Studio needs to be told to also link these CUDA Toolkit libraries.

**Solution (Automatic):** Run the update script to scan for CUDA and add the required CUDA Toolkit libraries:

```bat
scripts\update-addon-config.bat
```

Then regenerate your Visual Studio project with the openFrameworks Project Generator.

The script will:
1. Detect if CUDA backend was built (by checking for `ggml-cuda.lib`)
2. Add `cublas.lib` and `cudart.lib` to the `addon_config.mk` file
3. Display a message confirming the CUDA libraries were added

**Requirements:**
- The CUDA Toolkit must be installed and in your system PATH
- Visual Studio must be able to find the CUDA libraries (usually automatic if CUDA Toolkit is installed correctly)
- If you installed CUDA in a custom versioned folder (for example `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2`), set `CUDA_PATH` (or `CUDAToolkit_ROOT`) to that root and rerun `scripts\update-addon-config.bat`. This writes the absolute `cublas.lib`/`cudart.lib` paths into `addon_config.mk`.

**Note:** This fix was added to resolve CUBLAS linker errors. If you built ggml with CUDA before this fix, run the update script above.

## Security

ofxGgml implements several security best practices to protect against common vulnerabilities:

### Input Validation

- **Path validation**: Model paths and executable paths are validated to ensure they exist and are regular files
- **Path traversal protection**: Executable paths are checked for suspicious patterns like `..`
- **Null byte filtering**: All file paths are checked for null bytes to prevent path injection attacks
- **Canonical path resolution**: Paths are normalized to resolve symlinks and detect malicious redirects

### Input Sanitization

- **Prompt sanitization**: User prompts are sanitized to remove null bytes and dangerous control characters
- **Argument sanitization**: Command-line arguments passed to external processes are sanitized
- **JSON schema validation**: JSON schemas for structured output are sanitized before use

### Secure Temporary Files

- **Atomic file creation**: Temporary files are created with exclusive access flags to prevent race conditions
- **Random filenames**: Cryptographically random filenames using `std::random_device` prevent predictable temp file attacks
- **Automatic cleanup**: Thread-local RAII wrappers ensure temp files are cleaned up on thread exit

### Model Integrity

- **SHA256 checksums**: Model catalog includes SHA256 checksums for verification (when available)
- **Checksum verification**: Download script automatically verifies checksums when provided
- **Integrity warnings**: Clear warnings when checksums are missing or verification fails

### Best Practices for Users

When using ofxGgml in production:

1. **Validate model sources**: Only download models from trusted sources
2. **Verify checksums**: Always check SHA256 checksums for downloaded models
3. **Restrict file paths**: Use absolute paths and avoid user-controlled path construction
4. **Sandbox external executables**: Consider running llama-cli in a restricted environment
5. **Limit prompt size**: Implement application-level limits on prompt lengths to prevent DoS
6. **Monitor resource usage**: Track memory and compute usage when processing user inputs

## Extending the Addon

The addon is structured for easy extension:

- **New operations**: Add methods to `ofxGgmlGraph` following the existing pattern.
- **New model formats**: Extend or subclass `ofxGgmlModel`.
- **Custom backends**: Use `ofxGgml::getBackend()` for low-level access.
- **New tensor types**: Add values to `ofxGgmlType` enum in `ofxGgmlTypes.h`.

## License

MIT — see [LICENSE](LICENSE).
