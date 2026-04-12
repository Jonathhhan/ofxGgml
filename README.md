# ofxGgml

An [openFrameworks](https://openframeworks.cc) addon wrapping the [ggml](https://github.com/ggml-org/ggml) tensor library for machine-learning computation.

## Features

- **Backend management** — Automatic discovery and initialization of CPU, CUDA, Metal, Vulkan, and other ggml backends.
- **GGUF model loading** (`ofxGgmlModel`) — Load GGUF model files, inspect metadata and tensor information, and upload weights to backend buffers for inference.
- **Tensor wrapper** (`ofxGgmlTensor`) — Lightweight, non-owning wrapper with OF-friendly data access (read/write float vectors, fill, etc.).
- **Computation graph builder** (`ofxGgmlGraph`) — Fluent API for building ggml computation graphs:
  - Element-wise arithmetic: add, sub, mul, div, scale, clamp, sqr, sqrt
  - Matrix operations: matMul (A × B^T), transpose, permute, reshape, view
  - Reductions: sum, mean, argmax
  - Normalization: norm, rmsNorm, layerNorm
  - Activations: relu, gelu, silu, sigmoid, tanh, softmax
  - Transformer helpers: flashAttn, rope
  - Convolution / pooling: conv1d, pool1d, pool2d, upscale
  - Loss functions: crossEntropyLoss
- **Scheduled execution** — Multi-backend scheduler with automatic tensor placement and fallback.
- **Device enumeration** — Query available devices, memory, and capabilities at runtime.
- **Runtime device selection** — Switch between CPU, GPU, or a specific device at runtime via `ofxGgmlSettings::deviceIndex` or the GUI device selector.

## Requirements

- openFrameworks 0.12+
- [ggml](https://github.com/ggml-org/ggml) built and installed (headers in `libs/ggml/include`, library linked via `addon_config.mk`).

### Installing ggml

#### Linux (pkg-config)

```bash
git clone https://github.com/ggml-org/ggml
cd ggml
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build . --config Release -j 8
sudo cmake --install .
```

On Linux the addon uses `pkg-config` to locate the library automatically.

#### Windows (Visual Studio)

Build ggml with CMake and install it into the addon's `libs/ggml` directory:

```powershell
git clone https://github.com/ggml-org/ggml
cd ggml
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=C:/openFrameworks/addons/ofxGgml/libs/ggml -DBUILD_SHARED_LIBS=ON -DGGML_BUILD_EXAMPLES=OFF -DGGML_BUILD_TESTS=OFF
cmake --build . --config Release -j 8
cmake --install . --config Release
```

The addon expects the following files after installation:

- `libs/ggml/include/` — ggml headers
- `libs/ggml/lib/ggml.lib`, `ggml-base.lib`, `ggml-cpu.lib` — import libraries
- `libs/ggml/bin/ggml.dll`, `ggml-base.dll`, `ggml-cpu.dll` — runtime DLLs

Copy the DLLs from `libs/ggml/bin/` into your project's `bin/` directory (next to the .exe) so they can be found at runtime.

## Usage

### Loading a GGUF model

```cpp
#include "ofxGgml.h"

ofxGgml ggml;
ggml.setup();

// Load a GGUF model file.
ofxGgmlModel model;
if (!model.load("path/to/model.gguf")) {
    // handle error — file not found, corrupt, etc.
}

// Inspect metadata.
std::string arch = model.getMetadataString("general.architecture");
int32_t ctxLen   = model.getMetadataInt32("llama.context_length");

// List tensors in the model.
for (int64_t i = 0; i < model.getNumTensors(); i++) {
    std::string name = model.getTensorName(i);
    auto tensor = model.getTensor(name);
    // tensor.getType(), tensor.getDimSize(0), etc.
}

// Upload weights to the backend (CPU, CUDA, Metal, …).
ggml.loadModelWeights(model);
```

### Basic matrix multiplication

```cpp
#include "ofxGgml.h"

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
ggml.setTensorData(a, dataA, sizeof(dataA));
ggml.setTensorData(b, dataB, sizeof(dataB));

auto r = ggml.compute(graph);
if (r.success) {
    std::vector<float> out(result.getNumElements());
    ggml.getTensorData(result, out.data(), out.size() * sizeof(float));
    // out = { 60, 90, 42, 55, 54, 29, 50, 54, 28, 110, 126, 64 }
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

auto hidden = graph.matMul(weights, input);  // [3 x 1]
auto biased = graph.add(hidden, bias);
auto output = graph.relu(biased);

graph.setOutput(output);
graph.build(output);

auto r = ggml.compute(graph);
```

## API overview

| Class | Purpose |
|---|---|
| `ofxGgml` | Backend init, device enumeration, compute scheduling, model weight loading |
| `ofxGgmlModel` | Load GGUF model files, inspect metadata and tensor information |
| `ofxGgmlGraph` | Build computation graphs (tensor creation + operations) |
| `ofxGgmlTensor` | Non-owning tensor handle with metadata + data access |
| `ofxGgmlTypes.h` | Enums and settings (`ofxGgmlType`, `ofxGgmlBackendType`, …) |
| `ofxGgmlHelpers.h` | Utility functions (type names, byte formatting, …) |

## Examples

- **ofxGgmlExample** — Matrix multiplication with console output.
- **ofxGgmlNeuralExample** — Simple feedforward neural network visualized in the OF window.
- **ofxGgmlGuiExample** — Full ImGui-based AI Studio with six modes (Chat, Script, Summarize, Write, Translate, Custom).  Features include:
  - **Model preselection** — choose from 2 recommended GGUF models (Qwen2.5-1.5B, Qwen2.5-Coder-1.5B) via a sidebar combo.
  - **Script language selector** — 8 language presets (C++, Python, JavaScript, Rust, GLSL, Go, Bash, TypeScript) that set language-specific system prompts.
  - **Script source browser** — connect to a **local folder** or **GitHub repository** to browse, load, and save script files directly from the scripting panel.
  - **Session persistence** — auto-saves on exit, auto-loads on startup.  Full File → Save/Load Session support.  Saves all inputs, outputs, chat history, settings, model/language selections, and script source state.

## Build Scripts

Three helper scripts are provided in the repository's `scripts/` directory:

### `scripts/build-ggml.sh`

Clone, compile, and install the ggml library from source:

```bash
# Basic CPU-only system install
./scripts/build-ggml.sh

# System install with explicit CUDA support
./scripts/build-ggml.sh --gpu

# Custom install prefix
./scripts/build-ggml.sh --prefix $HOME/.local --jobs 8
```

### `scripts/build-llama-cli.sh`

Clone, compile, and install the [llama.cpp](https://github.com/ggml-org/llama.cpp) CLI tools (`llama-completion` and `llama-cli`).  `ofxGgmlGuiExample` uses these tools for real text generation:

```bash
# Basic CPU-only system install
./scripts/build-llama-cli.sh

# System install with CUDA support
./scripts/build-llama-cli.sh --gpu

# Install into the addon's libs directory (auto-detected by the GUI example)
./scripts/build-llama-cli.sh --prefix ./libs/llama

# Custom install prefix
./scripts/build-llama-cli.sh --prefix $HOME/.local --jobs 8
```

### `scripts/download-model.sh`

Download a GGUF model file for inference.  Supports model presets and task-based selection:

```bash
# Download both recommended presets (chat + coder, default behavior)
./scripts/download-model.sh

# Explicitly request both recommended presets
./scripts/download-model.sh --both

# Select by preset number
./scripts/download-model.sh --preset 2    # Qwen2.5-Coder — best for scripting

# Select the preferred model for a task (matches GUI example modes)
./scripts/download-model.sh --task script     # Qwen2.5-Coder-1.5B
./scripts/download-model.sh --task chat       # Qwen2.5-1.5B

# List all presets with details
./scripts/download-model.sh --list

# Download a specific model by URL
./scripts/download-model.sh --model https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf
```

Available presets:
| # | Model | Size | Best for |
|---|-------|------|----------|
| 1 | Qwen2.5-1.5B Instruct Q4_K_M | ~1.0 GB | chat, general |
| 2 | Qwen2.5-Coder-1.5B Instruct Q4_K_M | ~1.0 GB | scripting, code generation |

Preferred models per example task (`--task NAME`):
| Task | Preset | Model |
|------|--------|-------|
| chat | 1 | Qwen2.5-1.5B Instruct Q4_K_M |
| script | 2 | Qwen2.5-Coder-1.5B Instruct Q4_K_M |
| summarize | 1 | Qwen2.5-1.5B Instruct Q4_K_M |
| write | 1 | Qwen2.5-1.5B Instruct Q4_K_M |
| translate | 1 | Qwen2.5-1.5B Instruct Q4_K_M |
| custom | 1 | Qwen2.5-1.5B Instruct Q4_K_M |

### Real inference in `ofxGgmlGuiExample`

`ofxGgmlGuiExample` now attempts real text generation using [llama.cpp](https://github.com/ggml-org/llama.cpp) CLI tools with the selected preset model file:

- Expected model location: `ofxGgmlGuiExample/bin/data/models/<preset>.gguf`
- Runtime requirement: `llama-completion` (preferred) or `llama-cli` available in your `PATH`, a common install directory, or the addon-local `libs/llama/bin/` directory

The app prefers `llama-completion` (one-shot text completion) over `llama-cli` (interactive chat mode since [llama.cpp PR #17824](https://github.com/ggml-org/llama.cpp/pull/17824)).  Both tools are built by the build script.

You can build the llama.cpp CLI tools locally with:

```bash
./scripts/build-llama-cli.sh
```

Or install them into the addon tree for automatic detection:

```bash
./scripts/build-llama-cli.sh --prefix ./libs/llama
```

If either prerequisite is missing, the app shows an error with setup instructions.

## License

MIT — see [LICENSE](LICENSE).
