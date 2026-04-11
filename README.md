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
  - **Model preselection** — choose from 6 recommended GGUF models (TinyLlama, Phi-2, CodeLlama, DeepSeek Coder, Gemma) via a sidebar combo.
  - **Script language selector** — 8 language presets (C++, Python, JavaScript, Rust, GLSL, Go, Bash, TypeScript) that set language-specific system prompts.
  - **Script source browser** — connect to a **local folder** or **GitHub repository** to browse, load, and save script files directly from the scripting panel.
  - **Session persistence** — auto-saves on exit, auto-loads on startup.  Full File → Save/Load Session support.  Saves all inputs, outputs, chat history, settings, model/language selections, and script source state.

## Build Scripts

Two helper scripts are provided in the repository's `scripts/` directory:

### `scripts/build-ggml.sh`

Clone, compile, and install the ggml library from source:

```bash
# Basic CPU-only build
./scripts/build-ggml.sh

# With CUDA support
./scripts/build-ggml.sh --gpu

# Custom install prefix
./scripts/build-ggml.sh --prefix $HOME/.local --jobs 8
```

### `scripts/download-model.sh`

Download a GGUF model file for inference.  Supports model presets and task-based selection:

```bash
# Download default model (TinyLlama 1.1B Chat Q4_0, ~600 MB)
./scripts/download-model.sh

# Select by preset number
./scripts/download-model.sh --preset 4    # CodeLlama 7B — best for scripting

# Select the preferred model for a task (matches GUI example modes)
./scripts/download-model.sh --task script     # CodeLlama 7B
./scripts/download-model.sh --task chat       # TinyLlama 1.1B
./scripts/download-model.sh --task summarize  # Gemma 2B

# List all presets with details
./scripts/download-model.sh --list

# Download a specific model by URL
./scripts/download-model.sh --model https://huggingface.co/TheBloke/phi-2-GGUF/resolve/main/phi-2.Q4_0.gguf
```

Available presets:
| # | Model | Size | Best for |
|---|-------|------|----------|
| 1 | TinyLlama 1.1B Chat Q4_0 | ~600 MB | chat, general |
| 2 | TinyLlama 1.1B Chat Q8_0 | ~1.1 GB | chat, general (higher quality) |
| 3 | Phi-2 Q4_0 | ~1.6 GB | reasoning, code, chat |
| 4 | CodeLlama 7B Instruct Q4_0 | ~3.8 GB | scripting, code generation |
| 5 | DeepSeek Coder 1.3B Q4_0 | ~0.8 GB | scripting, code |
| 6 | Gemma 2B Instruct Q4_0 | ~1.4 GB | chat, summarize, writing |

Preferred models per example task (`--task NAME`):
| Task | Preset | Model |
|------|--------|-------|
| chat | 1 | TinyLlama 1.1B Chat Q4_0 |
| script | 4 | CodeLlama 7B Instruct Q4_0 |
| summarize | 6 | Gemma 2B Instruct Q4_0 |
| write | 6 | Gemma 2B Instruct Q4_0 |
| translate | 6 | Gemma 2B Instruct Q4_0 |
| custom | 3 | Phi-2 Q4_0 |

## License

MIT — see [LICENSE](LICENSE).
