# ofxGgml

`ofxGgml` is being restarted as a small, planned openFrameworks addon for
ggml-based local compute and inference.

The previous full framework has been frozen on the `legacy-full` branch. That
branch keeps the broad experimental surface: assistants, web/RAG helpers,
speech/TTS, vision, SAM/SAM3, diffusion, music/video workflows, and the large
GUI example. New `main` is the clean core line.

## Goal

Keep the default addon boring, dependable, and easy to embed:

- ggml dependency setup
- backend/runtime ownership
- tensor and graph wrappers
- GGUF/model metadata inspection
- basic text inference as a later explicit layer
- stable result/error types
- focused tests and examples

Everything else should become a companion addon or an optional layer.

## Planned Layers

| Layer | Header | Scope |
| --- | --- | --- |
| Core | `ofxGgmlCore.h` | runtime, tensors, graphs, models, results |
| Text | `ofxGgmlText.h` | small text request/result API with pluggable llama.cpp / server adapters |
| Embeddings | `ofxGgmlEmbedding.h` | embedding requests, llama-server adapter, vector helpers |
| Optional companions | separate addons | vision, speech, SAM, assistants, workflows |

The text layer has explicit llama.cpp adapters for a warm `llama-server` and a
CLI fallback. The server runtime is optional and installed only through the
dedicated scripts, so core builds and tests do not require llama.cpp.

## Status

This is the first rewrite commit. It intentionally favors a clear skeleton over
feature volume. Use `legacy-full` if you need the prior broad implementation
while this branch is rebuilt.

## Setup Direction

The addon will not vendor large generated binaries by default. Build scripts
will populate:

```text
libs/ggml/include
libs/ggml/lib
```

from the pinned upstream ggml `v0.11.0` revision.

```powershell
.\scripts\setup-ggml.ps1
.\scripts\setup-ggml.bat
.\scripts\setup-ggml.ps1 -CpuOnly
.\scripts\setup-ggml.ps1 -Cuda
.\scripts\setup-ggml.ps1 -Vulkan
.\scripts\setup-ggml.ps1 -AllBackends
```

On macOS and Linux, use the shell wrapper:

```sh
./scripts/setup-ggml.sh
./scripts/setup-ggml.sh -CpuOnly
./scripts/setup-ggml.sh -Vulkan
```

Running without backend switches is the default `-Auto` mode. Explicit backend
switches keep the build narrow, and adding `-Auto` to an explicit backend also
enables any other locally detected backend.

`-Auto` and `-AllBackends` are intentionally different:

| Option | Behavior |
| --- | --- |
| `-Auto` | Detects local SDKs/tools and enables only available backends. |
| `-AllBackends` | Requires every supported backend SDK/toolchain and fails early if one is missing. |

Explicit backend switches are requirements. For example, `-Cuda` fails before
configuration if the CUDA Toolkit cannot be found. Use default `-Auto` when you
want the script to skip unavailable GPU backends.

On Windows, the setup script locates Visual Studio C++ build tools and runs
CMake through `VsDevCmd.bat`. CUDA builds use the Visual Studio CUDA toolset;
when CUDA is combined with Vulkan or OpenCL, the script builds CUDA separately
from the native backends so ggml's nested Vulkan shader build does not inherit
the CUDA toolset.

Backend binaries remain generated local artifacts. `addon_config.mk` is updated
by the setup script to reference the ggml libraries that were actually built.
The runtime default is `ofxGgmlBackend::Auto`, which tries built GPU backends in
priority order (`CUDA`, then `Vulkan`) before falling back to CPU. Use
`ofxGgmlRuntimeSettings::preferredBackend` when an app needs to force CPU or a
specific GPU backend.

SAM3 support is optional and generated locally:

```powershell
scripts\install-sam3-cpp.bat
scripts\build-sam3-cpp.bat -Cuda -SkipExamples
```

The install script patches `sam3.cpp` with a `SAM3_CUDA` CMake option and a CUDA
backend initialization path. The build script exports `libs/sam3/lib/sam3.lib`
and enables `OFXGGML_ENABLE_SAM3_ADAPTER` in the local `addon_config.mk` SAM3
marker. By default, SAM3 builds against the addon ggml source under
`libs/ggml/.source` so the addon links a single ggml version; pass
`-BundledGgml` only when intentionally testing upstream sam3.cpp in isolation.

## Validation

Run the addon unit tests:

```powershell
scripts\test-addon.bat
scripts\test-addon.ps1
```

Run the fast local validation suite:

```bat
scripts\validate-local.bat
```

This runs the headless addon tests, generated-project repair checks, and launch
dry-run smoke checks. It does not open example windows or start long-running
servers.

Build the generated openFrameworks simple example:

```powershell
scripts\build-simple-example.bat
scripts\build-simple-example.ps1 -Configuration Release
```

The examples use `ofxImGui` for their control/status panels. Keep `ofxImGui`
installed beside this addon and regenerate the project files if `addons.make`
changes are not picked up by your IDE. The Windows build helper repairs stale
generated projects enough for local smoke builds.

Check generated Visual Studio project repair without running a full build:

```bat
scripts\test-example-project-repair.bat
```

This runs the repair pass for Simple, Text, Chat, and Embedding examples, then
verifies that stale generated dependency sources are absent and required addon
sources/includes are present.

The simple example reports the selected runtime backend, enumerates available
devices, and runs a tiny F32 add graph as a smoke test.

Build the generated text example:

```powershell
scripts\build-text-example.bat
scripts\build-text-example.ps1 -Configuration Release
```

`ofxGgmlTextExample` defaults to a warm local `llama-server` at
`http://127.0.0.1:8080` and runs one OpenAI-compatible chat completion through
`ofxGgmlTextGenerator`. Set `OFXGGML_TEXT_SERVER_URL` or
`OFXGGML_TEXT_SERVER_MODEL` to override routing, or edit those values in the
example runtime panel. The request runs on a worker thread and streams server
output into the UI; use the ImGui buttons or press `R` to run again and `C` to
cancel the active transport.

The optional local llama.cpp runtime is explicit:

```powershell
scripts\build-llama-server.bat
scripts\build-llama-server.bat -Cuda
scripts\build-llama-server.bat -Cuda -CudaArchitectures 86
scripts\build-llama-server.bat -CpuOnly
scripts\build-llama-server.bat -WithCompletionTool
scripts\start-llama-server.bat -ModelPath C:\path\to\model.gguf
scripts\start-llama-server.bat -ModelPath C:\path\to\embedding-model.gguf -Embeddings
scripts\start-llama-server.bat -Detached -LogDir logs\llama-server
scripts\start-llama-server.bat -DryRun
```

When CUDA is enabled, the build script asks `nvidia-smi` for compute
capabilities and passes `CMAKE_CUDA_ARCHITECTURES` explicitly. Use
`-CudaArchitectures` to override that detection. On Windows CUDA builds default
to one build job to avoid MSVC/CUDA parallel PDB races; pass `-Jobs` if you want
to force a wider build.

On macOS/Linux, the matching wrappers call the same PowerShell implementation:

```sh
./scripts/build-llama-server.sh
./scripts/start-llama-server.sh -ModelPath /path/to/model.gguf
```

Those scripts clone/build upstream `llama.cpp` under `libs/llama.cpp` and
install `llama-server`, `llama-cli`, and `llama-embedding` into
`libs/llama/bin`. The examples use `llama-server` by default; `llama-cli` is an
explicit fallback only when you launch with `-Backend cli`.
`llama-completion` is intentionally excluded from the default runtime bundle;
pass `-WithCompletionTool` only when you need that upstream helper executable.

Run a local embedding smoke test:

```powershell
scripts\run-embedding.bat -Prompt "openFrameworks local inference"
scripts\run-embedding.bat -ModelPath C:\path\to\embedding-model.gguf -Format json
scripts\run-embedding.bat -ModelPath C:\path\to\embedding-model.gguf -Raw
```

The embedding runner uses `libs\llama\bin\llama-embedding.exe` by default and
searches the same local model folders as the text/chat scripts. Set
`OFXGGML_EMBEDDING_MODEL` or pass `-ModelPath` to use an embedding-tuned GGUF;
general instruct models are useful only as smoke tests. The default output is
OpenAI-style JSON. Use `-Format array`, `-Format json+`, or `-Raw` for other
`llama-embedding` output formats.

The addon also exposes `ofxGgmlEmbeddingGenerator` and
`ofxGgmlLlamaServerEmbeddingBackend` for OpenAI-compatible `/v1/embeddings`
requests. `llama-server --embeddings` is embedding-only, so the launcher uses
port `8081` by default when `-Embeddings` is set. Keep the normal chat server on
`8080` and run a dedicated embedding server on `8081`. Embedding server launches
default to OpenAI-compatible `--pooling mean`; pass `-EmbeddingPooling` to
override it for a specific model.

If `-ModelPath` is omitted, the server launcher searches the text example data
folders, this addon's `models` folder, and the sibling shared `addons/models`
folder. Detached server launches emit `OFXGGML_LLAMA_SERVER_PID=...`; pass
`-LogDir` when you want stdout/stderr logs for background startup failures.
The launcher checks `http://host:port/health` before starting; if a server is
already reachable, it prints the health status and reuses it instead of
starting a duplicate process. Pass `-ForceNew` to intentionally start another
server, `-NoHealthCheck` to skip probing, or `-StartupTimeoutSeconds` to tune
the detached startup wait.

You can launch it without setting global environment variables:

```powershell
scripts\run-text-example.bat -ServerUrl http://127.0.0.1:8080
scripts\run-text-example.ps1 -ServerUrl http://127.0.0.1:8080 -ServerModel local-model
```

The run scripts check the local server URL first. If no local `llama-server`
responds and a bundled server/model can be found, they start
`libs\llama\bin\llama-server.exe` detached before launching the example. Pass
`-NoAutoServer` to only connect to an already-running server.
Pass `-DryRun` to `run-text-example`, `run-chat-example`, `run-embedding-example`,
or `start-llama-server` to inspect the resolved executable, model, and server
settings without starting the UI or server process.

Run the launch dry-run smoke checks:

```bat
scripts\test-launch-dry-run.bat
```

The old CLI fallback is still available when you explicitly request it:

```powershell
scripts\run-text-example.bat -Backend cli -LlamaCli C:\path\to\llama-cli.exe -Model C:\path\to\model.gguf
```

In CLI mode, omitted paths are auto-discovered beside the app and under local
`bin`, `data`, `tools`, `models`, `addons/models`, and
`libs/llama.cpp/build/bin` folders.

Build the interactive chat example:

```powershell
scripts\build-chat-example.bat
scripts\run-chat-example.bat -ServerUrl http://127.0.0.1:8080
```

`ofxGgmlChatExample` uses `ofxImGui` for an editable system prompt, chat
history, sampling controls, and a message composer. It defaults to the same
warm `llama-server` flow as the text example and keeps `llama-cli` as an
explicit fallback for local smoke testing. Both text examples include a runtime
panel with editable server URL/model fields, a local GGUF picker, and a refresh
button for rescanning local model folders.

Build the embedding example:

```powershell
scripts\build-embedding-example.bat
scripts\run-embedding-example.bat -ServerUrl http://127.0.0.1:8081
```

`ofxGgmlEmbeddingExample` uses `ofxImGui` for two editable input fields,
embedding server settings, request status, compact previews of both returned
vectors, and their cosine similarity. The run script starts a dedicated
`llama-server --embeddings` process on `8081` when one is not already reachable.
It uses OpenAI-compatible `--pooling mean` by default. Use an embedding-tuned
GGUF for meaningful vectors; general instruct models are only useful for smoke
testing.

The `.sh` wrappers call the same PowerShell scripts on macOS/Linux when
PowerShell 7+ is installed. Platform project files still need to be generated by
the openFrameworks projectGenerator before building examples on each platform.
