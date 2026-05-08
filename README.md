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
| Text | `ofxGgmlText.h` | llama.cpp / llama-server text generation |
| Optional companions | separate addons | vision, speech, SAM, assistants, workflows |

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
| `-AllBackends` | Forces every supported backend option on, even if a local SDK may be missing. |

Backend binaries remain generated local artifacts. `addon_config.mk` is updated
by the setup script to reference the ggml libraries that were actually built.
