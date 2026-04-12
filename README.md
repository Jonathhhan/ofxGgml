# ofxGgml

An [openFrameworks](https://openframeworks.cc) addon wrapping the [ggml](https://github.com/ggml-org/ggml) tensor library for machine-learning computation.

## Features

- **Backend management** — Automatic discovery and initialization of CPU, CUDA, Metal, Vulkan, and other ggml backends.
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

## Requirements

- openFrameworks 0.12+
- [ggml](https://github.com/ggml-org/ggml) built and installed (headers in `libs/ggml/include`, library linked via `addon_config.mk`).

### Installing ggml

```bash
git clone https://github.com/ggml-org/ggml
cd ggml
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build . --config Release -j 8
sudo cmake --install .
```

On Linux the addon uses `pkg-config` to locate the library automatically.

## Usage

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
| `ofxGgml` | Backend init, device enumeration, compute scheduling |
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

Two helper scripts are provided in the repository's `scripts/` directory:

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

### `scripts/download-model.sh`

Download a GGUF model file for inference.  Supports model presets and task-based selection:

```bash
# Download default model (Qwen2.5-1.5B Instruct Q4_K_M, ~1.0 GB)
./scripts/download-model.sh

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

## License

MIT — see [LICENSE](LICENSE).
