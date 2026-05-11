# Release Notes

## Unreleased

- Documented `ofxGgmlSam`, `ofxGgmlMusic`, and `ofxGgmlSpeech` as planned
  companion addon lanes for SAM/SAM3, music/audio, and speech/voice workflows.
- Added a roadmap list for later companion candidates: vision, diffusion, RAG,
  agents, video, and larger UI tooling.
- Added `docs/COMPANIONS.md` as the central split rule for core versus
  companion addon work.
- Added `docs/README.md` as a compact map of the documentation set.

## v1.0.1

- Patch release metadata to make `1.0.1` the active release target.
- Added `docs/QUICKSTART.md` with a direct clone/setup/run path for text, chat,
  and embedding examples.
- Added a short first-run command sequence to the README.
- Clarified run-script errors and model hints so missing example builds or GGUF
  files point to the next useful command.
- Clarified llama.cpp install failures when runtime DLLs are locked by running
  server/example processes.
- Added `build-llama-server -StopRunningRuntime` for explicitly stopping local
  addon llama/example processes before reinstalling runtime DLLs.
- Added `scripts/doctor.*` to report first-run readiness for tools,
  dependencies, models, examples, and local llama-server endpoints.
- Added `docs/EXAMPLES.md` as a compact guide to choosing, building, and
  launching the focused examples.
- Added `run-simple-example.*` so all focused examples have consistent launch
  wrappers and dry-run coverage.
- Added `first-run.*` to run ggml setup, llama.cpp tool builds, and doctor as one
  first-checkout workflow.
- Added `stop-llama-server.*` to inspect or stop detached local llama-server
  processes launched from the addon.
- Added `list-models.*` to show model search directories and discovered GGUF
  files before launching examples.
- Added `status-llama-server.*` to inspect default text/embedding health
  endpoints and running local llama-server processes.
- Keep the narrowed rewritten addon scope unchanged while preserving the
  existing `1.0.0` foundation notes for history.

## v1.0.0

This is the first stable release for the rewritten `main` line. It is a
breaking reset from the old broad framework and should be treated as a new,
small addon foundation.

Use the `legacy-full` branch for the previous experimental implementation with
assistants, RAG, speech/TTS, broad vision workflows, diffusion, music/video
experiments, and the large GUI example.

### Changes Since Legacy

- Breaking: renamed tensor accessors to explicit getter names: `getType()`,
  `getNumDims()`, `getExtent()`, `getByteSize()`, and `getElementCount()`.
- Breaking: renamed graph/tensor low-level accessors to explicit getter names:
  `getRaw()`, `getContext()`, and `getNodeCount()`.
- Breaking: renamed `ofxGgmlRuntime::state()` to `getState()` for
  openFrameworks-style getter naming.
- Breaking: renamed `ofxGgmlRuntime::listDevices()` to `getDevices()` for
  openFrameworks-style getter naming.
- Updated the core contract and roadmap after the public header naming audit.
- Added `setup-ggml -DryRun` plus smoke coverage so backend setup plans can be
  checked without fetching, cleaning, configuring, or compiling ggml.
- Added optional explicit backend setup dry-run smoke scripts for CUDA, Vulkan,
  CUDA+Vulkan, OpenCL, and AllBackends plans.
- Added setup dry-run coverage for invalid option combinations so setup failures
  stay early and clear.
- Added launch dry-run coverage for the explicit text/chat llama.cpp CLI
  fallback path.
- Added launch dry-run coverage for the standalone `llama-embedding` runner.
- Added generated-artifact hygiene checks to local validation.
- Updated the roadmap and core contract to make `1.0.0` a final confidence-pass
  checkpoint rather than an open-ended refactor bucket.
- Breaking: renamed `ofxGgmlBackend::Cpu` and `ofxGgmlBackend::Cuda` to
  `ofxGgmlBackend::CPU` and `ofxGgmlBackend::CUDA` so acronym values use the
  same spelling as `OpenCL`.
- Breaking: renamed `ofxGgmlBackendName()` to `ofxGgmlGetBackendName()` for
  openFrameworks-style free-function getter naming.
- Breaking: renamed public backend label accessors from `backendName()` to
  `getBackendName()` to better match openFrameworks getter style.
- Added standalone compile coverage for each public umbrella header so hidden
  include dependencies are caught before API refactors.
- Renamed private inference-layer members away from `m_` prefixes to better
  match openFrameworks-style implementation code.
- Updated tests to assert text and segmentation results through bool-style
  helpers instead of reading `.success` directly.
- Updated the core contract and architecture notes to match the current
  openFrameworks-style backend naming.
- Switched example prompt/output console prints to the openFrameworks `ofLog`
  system and updated examples to use the result-style bool helpers.
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
- Added `ofxGgmlGetBackendName()` for stable public backend labels.
- Wired Metal and OpenCL runtime initialization behind `OFXGGML_WITH_METAL` and
  `OFXGGML_WITH_OPENCL`; unbuilt or unavailable backends still fall back to CPU
  when allowed, or fail clearly in strict mode.
- Expanded runtime tests to cover strict/fallback behavior for CUDA, Vulkan,
  Metal, and OpenCL selections.

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
