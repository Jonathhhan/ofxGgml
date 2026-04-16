# ofxGgml

`ofxGgml` is an openFrameworks wrapper around [ggml](https://github.com/ggml-org/ggml) with backend selection, graph execution, GGUF model loading, llama.cpp CLI and `llama-server` inference helpers, prompt-memory utilities, and a GUI example aimed at local AI workflows.

The main public header is:

- `src/ofxGgml.h`

It is aimed at local-first AI tools, lightweight inference utilities, prompt-driven creative apps, and openFrameworks projects that want ggml runtime access without wiring the low-level backend API by hand.

## Note

Parts of this addon, its examples, GUI structure, and documentation were developed with AI-assisted help during implementation and refinement.

## License

This addon is released under the [MIT License](LICENSE).

## Release

- addon release version: `1.0.2`
- changelog: `CHANGELOG.md`

## Highlights

- bundled `ggml` source with CPU, CUDA, Vulkan, and Metal-aware build paths
- runtime backend discovery and selection with CPU fallback
- `ofxGgmlGraph` fluent graph builder for common ggml operations
- `ofxGgmlModel` GGUF inspection and backend weight upload
- `ofxGgmlInference` llama.cpp helper for CLI and persistent `llama-server` generation, embeddings, cache reuse, capability probing, cutoff continuation, and source-grounded prompt building
- addon-level `Live context` support for loaded sources, domain-provider grounding, generic search fallback, and stricter citation-oriented response modes
- `ofxGgmlSpeechInference` for local speech-to-text workflows via pluggable speech backends, with ready-to-use Whisper CLI profiles
- `ofxGgmlVisionInference` for multimodal image-to-text requests against `llama-server`-style OpenAI-compatible endpoints
- `ofxGgmlVideoInference` for backend-driven video understanding, starting with sampled-frame analysis and room for future specialized video backends
- `ofxGgmlChatAssistant` for reusable chat prompts, response-language control, and UI-thin conversation flows
- `ofxGgmlCodeAssistant` for coding-oriented prompts, structured task plans, unified diff output, compile-database-aware semantic retrieval, inline completion, repo context, focused-file assistance, and follow-up scripting actions
- `ofxGgmlWorkspaceAssistant` for validated patch application, allow-listed edit enforcement, unified-diff transactions with rollback, shadow-workspace safe apply, auto-selected verification commands, and retry-oriented coding loops on top of structured assistant output
- `ofxGgmlTextAssistant` for translation, summarization, rewriting, and reusable text-task prompts
- `ofxGgmlCodeReview`, `ofxGgmlProjectMemory`, and `ofxGgmlScriptSource` helpers for local coding and multi-pass review workflows
- `ofxGgmlScriptSource` now accepts local folders, Visual Studio `.sln` / `.vcxproj` workspaces, GitHub `owner/repo` values, full GitHub URLs, and branch-aware repo URLs
- assistant eval coverage for retrieval quality, dry-run safety, and structured workspace execution
- async graph submission and explicit synchronization for frame-friendly compute
- Windows build scripts that refresh Visual Studio linking automatically
- GUI example for local chat, review, and script-assisted workflows built mostly on addon helpers

## Source layout

The main public API stays in:

- `src/ofxGgml.h`

Core implementation is split by concern:

- `src/core/` for runtime entry points, shared types, helpers, and version metadata
- `src/compute/` for tensors and graph building
- `src/model/` for GGUF model loading
- `src/inference/` for completion execution, grounded prompt assembly, and speech / vision / video inference helpers
- `src/assistants/` for chat, code, workspace, review, and text-task helpers
- `src/support/` for script sources and project memory

Supporting areas:

- `libs/ggml/`
- `scripts/` for user-facing setup, build, download, and benchmark entry points
- `scripts/dev/` for maintainer update and patching helpers
- `docs/`
- `tests/`
- `ofxGgmlBasicExample/`
- `ofxGgmlGuiExample/`
- `ofxGgmlNeuralExample/`

## Clone / install quick start

Clone the addon into your openFrameworks `addons` folder:

```bash
git clone https://github.com/Jonathhhan/ofxGgml.git
```

Then run the setup script for your platform:

```bash
cd ofxGgml
./scripts/setup_linux_macos.sh
```

```bat
cd ofxGgml
scripts\setup_windows.bat
```

After setup, add `ofxGgml` to your project's `addons.make`, regenerate with the openFrameworks Project Generator when needed, and build normally.

## Supported operations

- element-wise: add, sub, mul, div, scale, clamp, sqr, sqrt
- matrix: matMul, transpose, permute, reshape, view
- reductions: sum, mean, argmax
- normalization: norm, rmsNorm
- activations: relu, gelu, silu, sigmoid, tanh, softmax
- transformer: flashAttn, rope
- convolution and pooling: conv1d, convTranspose1d, pool1d, pool2d, upscale
- loss: crossEntropyLoss

## Requirements

- openFrameworks `0.12+`
- CMake `3.18+` for building bundled ggml
- C++17 compiler
- optional GPU toolchains:
  - CUDA Toolkit
  - Vulkan SDK
  - Metal framework on macOS
- `ofxGgmlGuiExample` additionally needs [ofxImGui](https://github.com/jvcleave/ofxImGui)

## Building ggml

Bundled ggml lives in `libs/ggml/` and is built as a static library. By default the setup scripts auto-detect available GPU backends. Use `--cpu-only` to force a CPU-only build, or explicit flags such as `--cuda`, `--vulkan`, or `--metal` when you want a fixed backend set.

> Windows / Visual Studio users should build ggml before opening a generated OF project, otherwise the linker will fail on missing `ggml.lib`.

### Automated setup

Linux and macOS:

```bash
./scripts/setup_linux_macos.sh
./scripts/setup_linux_macos.sh --cuda
./scripts/setup_linux_macos.sh --skip-llama --skip-model
./scripts/setup_linux_macos.sh --skip-model
./scripts/setup_linux_macos.sh --skip-ggml --skip-llama --model-preset 2
```

Windows:

```bat
scripts\setup_windows.bat
scripts\setup_windows.bat --cuda
scripts\setup_windows.bat --skip-model
scripts\setup_windows.bat --skip-ggml --model-preset 2
```

`download-model` covers the text GGUF presets used by chat/script/write flows. Speech (`Whisper`) and multimodal `Vision` models are configured separately in the addon and GUI example because they use different runtimes and file layouts. The current Vision defaults favor EU-safe llama-server profiles such as `LFM2.5-VL` for general image understanding and `GLM-OCR` for OCR-heavy work.

### ggml only

Linux and macOS:

```bash
./scripts/build-ggml.sh
./scripts/build-ggml.sh --auto
./scripts/build-ggml.sh --cuda
./scripts/build-ggml.sh --vulkan
./scripts/build-ggml.sh --cpu-only
```

Windows:

```bat
scripts\build-ggml.bat
scripts\build-ggml.bat --cuda
scripts\build-ggml.bat --vulkan
scripts\build-ggml.bat --cpu-only
```

By default the Windows script builds `Release`. Pass `--with-debug` when you also want `Debug`.

If a parallel Windows build hits a transient CUDA or MSBuild object-link race, the script automatically retries that configuration with a single job.

After building ggml, regenerate your project with the openFrameworks Project Generator so generated Visual Studio projects pick up the latest addon library list.

`scripts\build-ggml.bat` also refreshes `addon_config.mk` for the `vs` section so Visual Studio links the exact ggml libraries you just built. When CUDA is enabled, it injects the CUDA Toolkit dependencies using `$(CUDA_PATH)`. Vulkan linking uses `$(VULKAN_SDK)`.

### llama-server on Windows

Use the dedicated PowerShell helper to clone `ggml-org/llama.cpp`, build `llama-server.exe`, and copy the server runtime into `libs/llama/bin`.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-llama-server.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\build-llama-server.ps1 -Cuda
powershell -ExecutionPolicy Bypass -File .\scripts\build-llama-server.ps1 -CpuOnly
```

By default the script:

- uses `build\llama.cpp-src` for the upstream source checkout
- uses `build\llama.cpp-build` for the CMake build tree
- installs `llama-server.exe` and the required DLLs into `libs\llama\bin`

That install location matches the GUI example's local server discovery, so `Start Local Server` can reuse the freshly built binary without extra configuration.

To launch the local server manually after building it:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\start-llama-server.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\start-llama-server.ps1 -Detached
```

The helper defaults to `http://127.0.0.1:8080`, reuses the recommended local GGUF model when possible, and exposes GPU layers / context size flags so the server path matches the GUI example's defaults more closely.

### Manual CMake build

```bash
cmake -B libs/ggml/build libs/ggml -DCMAKE_BUILD_TYPE=Release
cmake --build libs/ggml/build --config Release
```

Common options:

| Option | Default | Description |
| --- | --- | --- |
| `OFXGGML_GPU_AUTODETECT` | `ON` | Auto-detect and enable available GPU backends |
| `OFXGGML_CUDA` | `OFF` | Force CUDA backend on or off |
| `OFXGGML_VULKAN` | `OFF` | Force Vulkan backend on or off |
| `OFXGGML_METAL` | `OFF` | Force Metal backend on or off |

## Examples

- `ofxGgmlBasicExample`: interactive matrix demo plus steady-state matmul benchmark
- `ofxGgmlGuiExample`: local chat, review, script, speech, multimodal, and `Live context` workflow UI backed by addon helpers
- `ofxGgmlNeuralExample`: reusable inference graph with live class bars and latency view

Both lightweight examples are now keyboard-driven so you can rerun compute and benchmark paths without restarting the app.

## Tests

The test suite lives in `tests/` and covers core runtime behavior, model loading, inference helpers, chat/code/text assistants, and project memory support. When you change backend setup, Windows linking, or inference command assembly, it is worth rerunning the tests or at least rebuilding one example project.

## Persistent Server Backend

`ofxGgmlInference` can now target a warm `llama-server` process for both text generation and embeddings. When `serverModel` is left empty, the addon probes `/v1/models`, caches the active model briefly, and reuses that information across nearby requests. Review and retrieval flows can also use the same server, which keeps hierarchical review fast while preserving semantic ranking.

The GUI example couples a preferred text backend to each text-capable mode:

- `Chat`, `Script`, and `Custom` default to `llama-server`
- `Summarize`, `Write`, and `Translate` default to CLI
- the backend is still switchable per mode and stored with the session

When the server backend is selected, the GUI exposes:

- `Check Server` to probe reachability and fetch model/capability hints
- `Start Local Server` / `Stop Local Server` for an app-managed local `llama-server`
- `Tune For Server` to apply lower-latency settings for the active mode

## Performance

`ofxGgml` now ships with explicit benchmark entry points instead of burying performance checks only inside `tests/`.

Run the benchmark suite with:

```bash
./scripts/benchmark-addon.sh
```

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\benchmark-addon.ps1
```

These wrappers configure the test suite with `OFXGGML_ENABLE_BENCHMARK_TESTS=ON`, build `ofxGgml-tests`, and run the stable benchmark set (`[benchmark]~[manual]`) by default. For tuning guidance and recommended measurement workflow, see `docs/PERFORMANCE.md`.

## Source-grounded generation

`ofxGgmlInference` can now build source-aware prompts directly from URLs or an `ofxGgmlScriptSource` instance, so apps do not need to hand-roll HTML fetching and context assembly on top of the addon.

`ofxGgmlGuiExample` now exposes this through `Live context` policies:

- `Offline`
- `LoadedSourcesOnly`
- `LiveContext`
- `LiveContextStrictCitations`

These modes let you decide whether the assistant should stay fully local, rely only on explicitly loaded source URLs, or use broader live grounding such as loaded sources, domain providers, and generic search. The strict-citation mode keeps the same live lookup behavior but biases responses toward grounded source usage.

```cpp
ofxGgmlInference inference;
ofxGgmlPromptSourceSettings sourceSettings;
sourceSettings.maxSources = 3;
sourceSettings.maxCharsPerSource = 1800;
sourceSettings.maxTotalChars = 5000;

auto result = inference.generateWithUrls(
    modelPath,
    "Summarize the linked material and cite the supporting sources.",
    {
        "https://example.com/post-1",
        "https://example.com/post-2"
    },
    {},
    sourceSettings);
```

The helper normalizes HTML-heavy pages into cleaner text, clips oversized source bodies deterministically, and can ask the model to cite sources as `[Source N]`. For local folders, GitHub repos, or internet-backed script sources, use `generateWithScriptSource(...)` or `collectScriptSourceDocuments(...)`.

## Code Review Helpers

`ofxGgmlCodeReview` lifts the `GuiExample` multi-pass repository review workflow into the addon. It ranks files with lightweight heuristics plus embeddings, produces first-pass file summaries, then aggregates architecture and integration findings through `ofxGgmlInference`.

Use it when an app wants a reusable local code-review pipeline instead of wiring `ofxGgmlScriptSource`, embedding calls, and prompt choreography by hand.

## Chat Assistant Helpers

`ofxGgmlChatAssistant` lifts the generic chat prompt path out of the `GuiExample`. It prepares reusable conversation prompts with optional system instructions and response-language hints, so apps can keep chat UIs thin and consistent.

Use it when an app wants local chat behavior without duplicating prompt assembly or keeping a second language-preset list in UI code.

## Code Assistant Helpers

`ofxGgmlCodeAssistant` lifts the scripting workflow out of the `GuiExample`. It builds coding prompts with language presets, project memory, repo/file context, focused-file snippets, semantic symbol retrieval, and reusable actions such as `Generate`, `Edit`, `Refactor`, `Review`, `FixBuild`, `GroundedDocs`, `ContinueTask`, and `ContinueCutoff`.

It can also request structured task output so apps receive a machine-readable plan instead of only free-form prose. Structured responses can include acceptance criteria, file intents, patch operations, unified diffs, synthesized test ideas, verification commands, review findings with severity/confidence, reviewer-simulation passes, risk scoring, open questions, and parsed build-error context.

Use it when an app wants Copilot-style local coding assistance without duplicating prompt assembly, retrieval, and follow-up logic in UI code.

Symbol context is no longer limited to file snippets. Apps can build a semantic index, query relevant definitions of `runInference` and likely callers, and feed that directly into coding or review prompts. When a local workspace exposes `compile_commands.json`, the assistant upgrades retrieval with compile-database-aware file coverage and range-based caller tracking. For planning-heavy flows, `buildCodeMap(...)` exposes a compact semantic code map, while `runSpecToCode(...)` turns a feature specification into a structured implementation plan with tests, review passes, and risk metadata.

For editor-style integrations, `prepareInlineCompletion(...)` and `runInlineCompletion(...)` provide cursor-aware completion prompts built from the text before and after the insertion point.

## Workspace Assistant Helpers

`ofxGgmlWorkspaceAssistant` wraps `ofxGgmlCodeAssistant` with a workspace execution loop. It can validate structured patch operations inside a workspace root, validate and apply unified diffs with hunk matching, enforce an allow-list for edit mode, preview unified diffs, apply transactions with rollback data, run edits inside a shadow workspace before syncing them back, auto-select verification commands from changed files, and request an updated remediation plan when verification fails.

Use it when an app wants a local coding assistant that can move beyond "suggest code" into "plan, edit, verify, retry" without hardcoding file operations or command orchestration in UI code.

The public result types make that loop inspectable:

- `ofxGgmlCodeAssistantStructuredResult` for plans, patches, and verification commands
- `ofxGgmlWorkspacePatchValidationResult` for pre-apply safety checks
- `ofxGgmlWorkspaceUnifiedDiffFile` and `ofxGgmlWorkspaceUnifiedDiffHunk` for parsed diff structure
- `ofxGgmlWorkspaceTransaction` for transaction state, backups, and rollback-ready previews
- `ofxGgmlWorkspaceApplyResult` for touched files and apply messages
- `ofxGgmlWorkspaceVerificationResult` for command-by-command outcomes
- `ofxGgmlWorkspaceResult` for end-to-end attempts across build/test/retry cycles

## Text Assistant Helpers

`ofxGgmlTextAssistant` lifts translation and general text-workflow prompting out of the `GuiExample`. It prepares reusable prompts for `Summarize`, `KeyPoints`, `TlDr`, `Rewrite`, `Expand`, `Translate`, `DetectLanguage`, and `Custom` tasks.

Use it when an app wants translation or writing-assistant features without hardcoding task prompts in its UI layer.

## Speech Helpers

`ofxGgmlSpeechInference` adds addon-level speech-to-text support through a pluggable backend interface. The default backend targets `whisper-cli`, and the addon now ships with ready-to-use profile hints for common Whisper model families such as `Tiny.en`, `Base.en`, `Small`, and `Large-v3 Turbo`.

Use it when an app wants local `Transcribe` / `Translate` audio workflows without hardcoding command-line assembly in its UI layer. The `GuiExample` exposes executable path, model path, profile selection, language hint, prompt, and transcript output as a first-class panel.

## Vision Helpers

`ofxGgmlVisionInference` adds multimodal image-to-text support for `llama-server`-compatible endpoints. It prepares task-specific prompts for `Describe`, `OCR`, and `Ask`, handles local image encoding as data URLs, and includes curated profile hints for families such as `LFM2.5-VL`, `Qwen VL`, `GLM OCR`, and `Llama 3.2 Vision`.

The GUI example now recommends `LFM2.5-VL` first for general vision tasks, keeps `GLM-OCR` available for OCR-focused flows, and labels Meta `Llama 3.2 Vision` as EU-restricted because the official Hugging Face download is currently blocked from the European Union.

Use it when an app wants OCR, screenshot understanding, document extraction, or image-grounded prompting without rebuilding OpenAI-style request payloads manually.

## Video Helpers

`ofxGgmlVideoInference` adds a backend-driven video layer on top of the vision stack. The default backend samples frames and reuses the multimodal image path, while keeping the API open for future specialized backends such as dedicated video-language servers.

Use it when an app wants practical local video understanding today, but still wants a clean path to stronger temporal backends later.

## Versioning

Version macros live in `src/core/ofxGgmlVersion.h`. Runtime-facing version metadata is available through `ofxGgml::getAddonVersionInfo()`.

## Eval Coverage

The addon test suite now includes assistant-focused eval coverage for:

- symbol-aware retrieval quality
- symbol-aware caller/definition context building
- inline completion prompt assembly
- compiler-output parsing for fix-build flows
- compile-database-aware semantic indexing
- structured code-task parsing
- unified diff generation
- workspace dry-run safety
- workspace allow-list enforcement
- patch validation and transaction rollback behavior
- unified-diff hunk validation and apply behavior
- automatic verification command selection from changed files
- verification retry loops

That keeps the scripting assistant features regression-tested as first-class addon APIs instead of GUI-only behavior.
