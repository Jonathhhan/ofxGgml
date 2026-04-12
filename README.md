# ofxGgml

An [openFrameworks](https://openframeworks.cc) addon wrapping the [ggml](https://github.com/ggml-org/ggml) tensor library for machine-learning computation.

> **Version 0.2** — restructured addon layout with flat `src/`, standard oF example naming, and a one-command setup script.

---

## Quick Start

```bash
# 1. Clone into your oF addons directory
cd openFrameworks/addons
git clone https://github.com/Jonathhhan/ofxGgml.git

# 2. One-command setup: build ggml, llama.cpp CLI, and download models
cd ofxGgml
./scripts/setup.sh            # CPU-only
./scripts/setup.sh --gpu      # with CUDA
./scripts/setup.sh --auto     # auto-detect GPU backends

# 3. Add ofxGgml to your project's addons.make and build
```

## Features

| Category | Capabilities |
|----------|-------------|
| **Backend management** | Automatic discovery of CPU, CUDA, Metal, Vulkan backends; runtime selection |
| **GGUF model loading** | Load GGUF files, inspect metadata and tensors, upload weights to GPU |
| **Tensor wrapper** | Non-owning handle with OF-friendly data access (read/write floats, fill) |
| **Graph builder** | Fluent API for computation graphs — see [Operations](#supported-operations) |
| **Scheduled execution** | Multi-backend scheduler with automatic tensor placement and fallback |
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
- [ggml](https://github.com/ggml-org/ggml) (built from source — see below)
- C++17 compiler

## Installing ggml

### Automated (recommended)

```bash
./scripts/setup.sh              # builds ggml + llama CLI + downloads models
./scripts/setup.sh --auto       # auto-detect and enable GPU backends
./scripts/setup.sh --skip-llama --skip-model  # ggml only
```

### Manual — Linux / macOS (pkg-config)

```bash
git clone https://github.com/ggml-org/ggml
cd ggml && mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build . --config Release -j$(nproc)
sudo cmake --install .
```

### Manual — Windows (Visual Studio)

```powershell
git clone https://github.com/ggml-org/ggml
cd ggml && mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=C:/openFrameworks/addons/ofxGgml/libs/ggml `
  -DBUILD_SHARED_LIBS=ON -DGGML_BUILD_EXAMPLES=OFF -DGGML_BUILD_TESTS=OFF
cmake --build . --config Release -j 8
cmake --install . --config Release
```

Expected files after install:

| Path | Contents |
|------|----------|
| `libs/ggml/include/` | ggml headers |
| `libs/ggml/lib/ggml.lib` | Import library |
| `libs/ggml/bin/*.dll` | Runtime DLLs (`ggml.dll`, `ggml-base.dll`, `ggml-cpu.dll`) |

Copy the DLLs into your project's `bin/` directory (next to the `.exe`).

## Addon Structure

```
ofxGgml/
├── addon_config.mk          # oF project generator configuration
├── src/
│   ├── ofxGgml.h             # umbrella header — include this
│   ├── ofxGgmlCore.h/.cpp    # backend init, compute, model weight loading
│   ├── ofxGgmlGraph.h/.cpp   # computation graph builder
│   ├── ofxGgmlModel.h/.cpp   # GGUF model loader
│   ├── ofxGgmlTensor.h/.cpp  # non-owning tensor wrapper
│   ├── ofxGgmlTypes.h        # enums, settings, result structs
│   ├── ofxGgmlHelpers.h      # utility functions
│   └── ofxGgmlVersion.h      # version macros
├── libs/                     # third-party libs (ggml headers/binaries)
├── scripts/
│   ├── setup.sh              # one-command full setup
│   ├── build-ggml.sh         # build & install ggml
│   ├── build-llama-cli.sh    # build & install llama.cpp CLI tools
│   └── download-model.sh     # download GGUF model presets
├── example-basic/            # matrix multiplication demo
├── example-neural/           # feedforward neural network demo
└── example-gui/              # full ImGui AI Studio
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

## API Reference

| Class | Purpose |
|-------|---------|
| `ofxGgml` | Backend init, device enumeration, compute scheduling, model weight loading |
| `ofxGgmlGraph` | Build computation graphs (tensor creation + operations) |
| `ofxGgmlModel` | Load GGUF model files, inspect metadata and tensor information |
| `ofxGgmlTensor` | Non-owning tensor handle with metadata and data access |
| `ofxGgmlTypes` | Enums and settings (`ofxGgmlType`, `ofxGgmlBackendType`, …) |
| `ofxGgmlHelpers` | Utility functions (type names, byte formatting, …) |
| `ofxGgmlVersion` | Version macros (`OFX_GGML_VERSION_STRING`, etc.) |

## Examples

| Example | Description |
|---------|-------------|
| **example-basic/** | Matrix multiplication with console output |
| **example-neural/** | Simple feedforward neural network visualized in the OF window |
| **example-gui/** | Full ImGui-based AI Studio with six modes (Chat, Script, Summarize, Write, Translate, Custom) |

### example-gui features

- **Model preselection** — choose from recommended GGUF models via sidebar
- **Script language selector** — 8 language presets (C++, Python, JS, Rust, GLSL, Go, Bash, TS)
- **Script source browser** — connect to a local folder or GitHub repository
- **Session persistence** — auto-save/load, File → Save/Load Session

### Real inference in example-gui

The GUI example uses [llama.cpp](https://github.com/ggml-org/llama.cpp) CLI tools for text generation:

- Expected model location: `example-gui/bin/data/models/<preset>.gguf`
- Runtime requirement: `llama-completion` or `llama-cli` in `PATH`, a common install directory, or `libs/llama/bin/`

Build the tools with:

```bash
./scripts/build-llama-cli.sh                     # system install
./scripts/build-llama-cli.sh --prefix ./libs/llama  # addon-local install
```

## Build Scripts

| Script | Purpose |
|--------|---------|
| `scripts/setup.sh` | **One-command setup**: builds ggml + llama CLI + downloads models |
| `scripts/build-ggml.sh` | Clone, compile, and install the ggml library |
| `scripts/build-llama-cli.sh` | Clone, compile, and install llama.cpp CLI tools |
| `scripts/download-model.sh` | Download GGUF model presets |

All scripts support `--gpu`, `--vulkan`, `--metal`, `--auto`, `--prefix`, `--jobs`, `--clean`, and `--help`.

### Model presets

| # | Model | Size | Best for |
|---|-------|------|----------|
| 1 | Qwen2.5-1.5B Instruct Q4_K_M | ~1.0 GB | chat, general |
| 2 | Qwen2.5-Coder-1.5B Instruct Q4_K_M | ~1.0 GB | scripting, code generation |

```bash
./scripts/download-model.sh                    # download both presets
./scripts/download-model.sh --preset 2         # coder model only
./scripts/download-model.sh --task script      # same as --preset 2
./scripts/download-model.sh --list             # show all presets
```

## Troubleshooting

### GGML_ASSERT at ggml.cpp:22

This assertion means ggml's static initializer ran twice. Common causes:

- **Duplicate linking**: linking `ggml`, `ggml-base`, and `ggml-cpu` simultaneously.  The addon links only `ggml.lib` on VS to avoid this.
- **Multiple installations**: system-wide + local copies both loaded at runtime.
- **Dynamic backend loading**: the addon sets `GGML_NO_BACKTRACE=1` automatically.

If the assertion still occurs, set `GGML_NO_BACKTRACE=1` before launching.

### llama-completion not found

The GUI example searches for `llama-completion`, `llama-cli`, or `llama` in:

1. Custom path from GUI settings
2. System `PATH`
3. Running executable's directory
4. `libs/llama/bin/` (addon-local)
5. Common directories (`/usr/local/bin`, `~/.local/bin`)

Build with: `./scripts/build-llama-cli.sh`

## Extending the Addon

The addon is structured for easy extension:

- **New operations**: Add methods to `ofxGgmlGraph` following the existing pattern.
- **New model formats**: Extend or subclass `ofxGgmlModel`.
- **Custom backends**: Use `ofxGgml::getBackend()` for low-level access.
- **New tensor types**: Add values to `ofxGgmlType` enum in `ofxGgmlTypes.h`.

## License

MIT — see [LICENSE](LICENSE).
