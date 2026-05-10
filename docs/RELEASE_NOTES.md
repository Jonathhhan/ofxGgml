# Release Notes

## Unreleased

- Added result-style status helpers to the llama CLI command result and
  llama-server transport response helper structs.
- Added result-style `isOk()`, `isError()`, and bool-conversion helpers to
  text, embedding, and segmentation result structs.
- Split optional SAM3 adapter includes into `ofxGgmlSam3.h`; generic
  `ofxGgmlSegmentation.h` now exposes only the backend-neutral segmentation API.
- Added `isOk()`, `isError()`, and bool-conversion helpers to
  `ofxGgmlComputeResult`.
- Added `ofxGgmlBackend::OpenCL` so the public runtime backend enum matches the
  setup script backend switches.
- Added `ofxGgmlBackendName()` for stable public backend labels.
- Wired Metal and OpenCL runtime initialization behind `OFXGGML_WITH_METAL` and
  `OFXGGML_WITH_OPENCL`; unbuilt or unavailable backends still fall back to CPU
  when allowed, or fail clearly in strict mode.
- Expanded runtime tests to cover strict/fallback behavior for CUDA, Vulkan,
  Metal, and OpenCL selections.

## v2.0.0-rewrite.0

This is the first checkpoint tag for the rewritten `main` line. It is a
breaking reset from the old broad framework and should be treated as a new,
small addon foundation.

Use the `legacy-full` branch for the previous experimental implementation with
assistants, RAG, speech/TTS, broad vision workflows, diffusion, music/video
experiments, and the large GUI example.

### Scope

- ggml dependency setup pinned to `v0.11.0`.
- backend runtime ownership with CPU baseline and optional CUDA/Vulkan/local
  backend builds.
- small public C++ API for results, runtime setup, tensors, graphs, and GGUF
  metadata.
- backend-neutral text generation API with llama.cpp server and CLI adapters.
- OpenAI-compatible llama-server text streaming with cancellable transport.
- embedding request/result API, llama-server embedding adapter, `llama-embedding`
  runner, and vector helpers.
- segmentation request/result API with optional SAM3 adapter hooks.
- focused examples for runtime/graph smoke, text, chat, and embeddings.
- Windows batch/PowerShell scripts plus POSIX shell wrappers where practical.
- local validation for headless tests, generated project repair, and launch
  dry-runs.

### Validation

Validated locally on 2026-05-10 with Windows x64 and Visual Studio 18:

- `scripts\validate-local.ps1`
- `scripts\build-simple-example.ps1 -Configuration Release -Platform x64`
- `scripts\build-text-example.ps1 -Configuration Release -Platform x64`
- `scripts\build-chat-example.ps1 -Configuration Release -Platform x64`
- `scripts\build-embedding-example.ps1 -Configuration Release -Platform x64`

The full example builds completed with no addon compile errors. Remaining
warnings are from openFrameworks/ofxImGui headers and generated Visual Studio
project integration.

### Intentional Deferrals

- Real SAM3 model/image UX remains script-tested until a known-compatible model
  and sample image workflow are available.
- Model downloads are intentionally not included.
- Full openFrameworks example builds remain explicit release confidence checks,
  not part of the fast `validate-local` command.
- All-backend verification is local-machine dependent; CPU is the required
  baseline, and optional backends are validated through setup switches.
- POSIX shell wrappers are present, but platform projects still depend on local
  openFrameworks project generation.
