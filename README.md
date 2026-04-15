# ofxGgml

`ofxGgml` is an openFrameworks wrapper around [ggml](https://github.com/ggml-org/ggml) with backend selection, graph execution, GGUF model loading, llama.cpp CLI inference helpers, prompt-memory utilities, and a GUI example aimed at local AI workflows.

The main public header is:

- `src/ofxGgml.h`

It is aimed at local-first AI tools, lightweight inference utilities, prompt-driven creative apps, and openFrameworks projects that want ggml runtime access without wiring the low-level backend API by hand.

## Note

Parts of this addon, its examples, GUI structure, and documentation were developed with AI-assisted help during implementation and refinement.

## License

This addon is released under the [MIT License](LICENSE).

## Release

- addon release version: `1.0.0`
- changelog: `CHANGELOG.md`

## Highlights

- bundled `ggml` source with CPU, CUDA, Vulkan, and Metal-aware build paths
- runtime backend discovery and selection with CPU fallback
- `ofxGgmlGraph` fluent graph builder for common ggml operations
- `ofxGgmlModel` GGUF inspection and backend weight upload
- `ofxGgmlInference` llama.cpp CLI helper for generation, embeddings, cache reuse, CLI capability probing, cutoff continuation, and source-grounded prompt building
- `ofxGgmlChatAssistant` for reusable chat prompts, response-language control, and UI-thin conversation flows
- `ofxGgmlCodeAssistant` for coding-oriented prompts, structured task plans, symbol-aware retrieval, repo context, focused-file assistance, and follow-up scripting actions
- `ofxGgmlWorkspaceAssistant` for patch application, verification commands, and retry-oriented coding loops on top of structured assistant output
- `ofxGgmlTextAssistant` for translation, summarization, rewriting, and reusable text-task prompts
- `ofxGgmlCodeReview`, `ofxGgmlProjectMemory`, and `ofxGgmlScriptSource` helpers for local coding and multi-pass review workflows
- assistant eval coverage for retrieval quality, dry-run safety, and structured workspace execution
- async graph submission and explicit synchronization for frame-friendly compute
- Windows build scripts that refresh Visual Studio linking automatically
- GUI example for local chat, review, and script-assisted workflows built mostly on addon helpers

## Source layout

The main public API stays in:

- `src/ofxGgml.h`

Core implementation is split by concern:

- `src/ofxGgmlCore.*`
- `src/ofxGgmlGraph.*`
- `src/ofxGgmlInference.*`
- `src/ofxGgmlChatAssistant.*`
- `src/ofxGgmlCodeAssistant.*`
- `src/ofxGgmlWorkspaceAssistant.*`
- `src/ofxGgmlTextAssistant.*`
- `src/ofxGgmlCodeReview.*`
- `src/ofxGgmlModel.*`
- `src/ofxGgmlTensor.*`
- `src/ofxGgmlProjectMemory.*`
- `src/ofxGgmlScriptSource.*`
- `src/ofxGgmlTypes.h`
- `src/ofxGgmlResult.h`
- `src/ofxGgmlHelpers.h`
- `src/ofxGgmlVersion.h`

Supporting areas:

- `libs/ggml/`
- `scripts/`
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
- `ofxGgmlGuiExample`: local chat, review, and script workflow UI backed by addon assistants
- `ofxGgmlNeuralExample`: reusable inference graph with live class bars and latency view

Both lightweight examples are now keyboard-driven so you can rerun compute and benchmark paths without restarting the app.

## Tests

The test suite lives in `tests/` and covers core runtime behavior, model loading, inference helpers, chat/code/text assistants, and project memory support. When you change backend setup, Windows linking, or inference command assembly, it is worth rerunning the tests or at least rebuilding one example project.

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

`ofxGgmlCodeAssistant` lifts the scripting workflow out of the `GuiExample`. It builds coding prompts with language presets, project memory, repo/file context, focused-file snippets, symbol-aware retrieval, and reusable actions such as `Generate`, `Refactor`, `Review`, `ContinueTask`, and `ContinueCutoff`.

It can also request structured task output so apps receive a machine-readable plan instead of only free-form prose. Structured responses can include file intents, patch operations, verification commands, risks, and open questions.

Use it when an app wants Copilot-style local coding assistance without duplicating prompt assembly, retrieval, and follow-up logic in UI code.

## Workspace Assistant Helpers

`ofxGgmlWorkspaceAssistant` wraps `ofxGgmlCodeAssistant` with a workspace execution loop. It can apply structured patch operations inside a workspace root, run verification commands, and request an updated remediation plan when verification fails.

Use it when an app wants a local coding assistant that can move beyond "suggest code" into "plan, edit, verify, retry" without hardcoding file operations or command orchestration in UI code.

The public result types make that loop inspectable:

- `ofxGgmlCodeAssistantStructuredResult` for plans, patches, and verification commands
- `ofxGgmlWorkspaceApplyResult` for touched files and apply messages
- `ofxGgmlWorkspaceVerificationResult` for command-by-command outcomes
- `ofxGgmlWorkspaceResult` for end-to-end attempts across build/test/retry cycles

## Text Assistant Helpers

`ofxGgmlTextAssistant` lifts translation and general text-workflow prompting out of the `GuiExample`. It prepares reusable prompts for `Summarize`, `KeyPoints`, `TlDr`, `Rewrite`, `Expand`, `Translate`, `DetectLanguage`, and `Custom` tasks.

Use it when an app wants translation or writing-assistant features without hardcoding task prompts in its UI layer.

## Versioning

Version macros live in `src/ofxGgmlVersion.h`. Runtime-facing version metadata is available through `ofxGgml::getAddonVersionInfo()`.

## Eval Coverage

The addon test suite now includes assistant-focused eval coverage for:

- symbol-aware retrieval quality
- structured code-task parsing
- workspace dry-run safety
- verification retry loops

That keeps the scripting assistant features regression-tested as first-class addon APIs instead of GUI-only behavior.
