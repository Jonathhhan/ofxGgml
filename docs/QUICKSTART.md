# Quickstart

This is the boring path: clone the addon, build the local dependencies, and run
the text, chat, and embedding examples without setting global environment
variables.

For a compact guide to what each example is for, see `docs/EXAMPLES.md`.

## 1. Prerequisites

- openFrameworks installed locally
- `ofxGgml` cloned into the openFrameworks `addons` folder
- `ofxImGui` installed beside `ofxGgml`
- Git, CMake, and a C++ compiler
- Windows: Visual Studio C++ build tools
- macOS/Linux: PowerShell 7+ for the wrapper scripts
- one GGUF text model, and preferably one embedding-tuned GGUF model

Models are not downloaded by this addon. Put them in one of these places:

```text
openFrameworks/addons/models
openFrameworks/addons/ofxGgml/models
openFrameworks/addons/ofxGgml/ofxGgmlTextExample/bin/data/models
```

You can also pass a model path directly with `-Model`.

## 2. Clone

From the openFrameworks `addons` folder:

```powershell
git clone https://github.com/Jonathhhan/ofxGgml.git
cd ofxGgml
```

## 3. Run First-Run Setup

The shortest setup command builds ggml, builds llama.cpp tools, and runs the
doctor report:

```powershell
.\scripts\first-run.bat
```

Preview the plan without cloning, cleaning, configuring, or compiling:

```powershell
.\scripts\first-run.bat -DryRun
```

Use backend switches when you want to force a specific plan:

```powershell
.\scripts\first-run.bat -Cuda
.\scripts\first-run.bat -CpuOnly
.\scripts\first-run.bat -Cuda -StopRunningRuntime
```

On macOS/Linux:

```sh
./scripts/first-run.sh
```

## 4. Build ggml Manually

Default setup uses `-Auto`, which enables only locally available backends and
falls back to CPU when GPU SDKs are missing.

```powershell
.\scripts\setup-ggml.bat
```

Use explicit backend switches only when you want the setup to require that SDK:

```powershell
.\scripts\setup-ggml.bat -Cuda
.\scripts\setup-ggml.bat -Vulkan
.\scripts\setup-ggml.bat -DryRun
```

On macOS/Linux:

```sh
./scripts/setup-ggml.sh
```

## 5. Build llama.cpp Tools Manually

The text, chat, and embedding examples use a warm `llama-server` by default.
Build it once:

```powershell
.\scripts\build-llama-server.bat
```

With CUDA:

```powershell
.\scripts\build-llama-server.bat -Cuda
```

This installs `llama-server`, `llama-cli`, and `llama-embedding` under:

```text
libs/llama/bin
```

Check the local install before opening examples:

```powershell
.\scripts\doctor.bat
.\scripts\list-models.bat
```

Warnings are meant to be actionable. For example, a missing model warning means
you can either place a GGUF in `addons/models` or pass `-Model` to the run
script.

## 6. Run Simple

```powershell
.\scripts\run-simple-example.bat -Build
```

The simple example verifies runtime setup, backend selection, device reporting,
and a tiny graph without requiring a model or server.

## 7. Run Text

```powershell
.\scripts\run-text-example.bat -Build -Model C:\path\to\model.gguf
```

Use strict mode when you only want text GGUFs:

```powershell
.\scripts\run-text-example.bat -Build -Model C:\path\to\text-model.gguf -StrictModel
```

The launcher builds the example, starts `llama-server` on `127.0.0.1:8080` if
needed, and opens the openFrameworks example. Use the editable prompt field in
the UI, or press `R` to run again.

## 8. Run Chat

```powershell
.\scripts\run-chat-example.bat -Build -Model C:\path\to\model.gguf
```

Use strict mode when you only want text GGUFs:

```powershell
.\scripts\run-chat-example.bat -Build -Model C:\path\to\text-model.gguf -StrictModel
```

Chat uses the same text server on port `8080`. If the server is already healthy,
the launcher reuses it instead of starting a duplicate.

## 9. Run Embeddings

Use an embedding-tuned GGUF model for meaningful vectors:

```powershell
.\scripts\run-embedding-example.bat -Build -Model C:\path\to\embedding-model.gguf
```

Use strict mode when you only want embedding models:

```powershell
.\scripts\run-embedding-example.bat -Build -Model C:\path\to\embedding-model.gguf -StrictModel
```

The embedding launcher starts a dedicated `llama-server --embeddings` process on
port `8081`, because embedding mode and chat mode should be separate.

## 10. Sanity Checks

Fast validation:

```powershell
.\scripts\validate-local.bat
```

Dry-run launch resolution without opening windows:

```powershell
.\scripts\run-simple-example.bat -DryRun
.\scripts\run-text-example.bat -DryRun
.\scripts\run-chat-example.bat -DryRun
.\scripts\run-embedding-example.bat -DryRun
```

## Common Fixes

- `ofxImGui.h` missing: install `ofxImGui` beside `ofxGgml`, then regenerate or
  rebuild the example project.
- CUDA requested but not found: use default setup or install the CUDA Toolkit.
- No model found: pass `-Model C:\path\to\model.gguf` or place a GGUF in
  `addons/models`.
- Rebuilding llama.cpp fails while installing DLLs: stop running
  `llama-server`, text, chat, or embedding example processes, or rerun
  `build-llama-server.bat -StopRunningRuntime`.
- Detached server keeps running: use `scripts\stop-llama-server.bat`, or
  `scripts\stop-llama-server.bat -IncludeExamples` when examples are still open.
- Server state is unclear: use `scripts\status-llama-server.bat`.
- Server request failed: start from the run scripts instead of opening the `.exe`
  directly; they set the needed environment variables and can start the server.
- Embeddings fail on port `8080`: use the embedding launcher or start
  `llama-server` with `-Embeddings`, which defaults to port `8081`.
