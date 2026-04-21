# ofxGgml

`ofxGgml` is an openFrameworks wrapper around [ggml](https://github.com/ggml-org/ggml) with backend selection, graph execution, GGUF model loading, server-first `llama-server` plus optional llama.cpp CLI inference helpers, prompt-memory utilities, and a GUI example aimed at local AI workflows.

## Layered API Architecture

ofxGgml uses **layered headers** - include only what you need:

| Header | What You Get | Use When |
|--------|--------------|----------|
| **`ofxGgmlBasic.h`** | **Core + text inference** | **Text/chat AI (start here!)** |
| `ofxGgmlModalities.h` | Basic + speech/vision/TTS/images | Multimodal AI workflows |
| `ofxGgmlWorkflows.h` | Modalities + video/montage/research | Specialized creative pipelines |
| `ofxGgmlAssistants.h` | Basic + code/chat assistants | AI coding assistance |
| `ofxGgmlCore.h` | Runtime, tensors, models | Low-level tensor ops only |
| `ofxGgml.h` | All layers combined | ⚠️ Everything (slower builds) |

**Recommended:** Start with `#include "ofxGgmlBasic.h"` for text-only projects.

**Why layered?** Faster compile times, clearer APIs, simpler examples. Each layer builds on previous ones.

See [docs/getting-started/CHOOSING_FEATURES.md](docs/getting-started/CHOOSING_FEATURES.md) for detailed guidance.

It is aimed at local-first AI tools, lightweight inference utilities, prompt-driven creative apps, and openFrameworks projects that want ggml runtime access without wiring the low-level backend API by hand.

## Note

Parts of this addon, its examples, GUI structure, and documentation were developed with AI-assisted help during implementation and refinement.

## License

This addon is released under the [MIT License](LICENSE).

## Release

- addon release version: `1.0.4`
- changelog: `CHANGELOG.md`

## Highlights

- bundled `ggml` source with CPU, CUDA, Vulkan, and Metal-aware build paths
- runtime backend discovery and selection with CPU fallback
- `ofxGgmlGraph` fluent graph builder for common ggml operations
- `ofxGgmlModel` GGUF inspection and backend weight upload
- `ofxGgmlInference` llama.cpp helper for CLI and persistent `llama-server` generation, embeddings, cache reuse, capability probing, cutoff continuation, and source-grounded prompt building
- **Batched Inference API** for efficient multi-request processing with concurrent execution, automatic fallback, and comprehensive metrics
- **Quick Wins** for developer experience:
  - `ofxGgmlStreamingContext` - streaming API with backpressure control, pause/resume/cancel
  - `ofxGgmlLogger` - comprehensive logging with multiple levels and file/console output
  - `ofxGgmlMetrics` - performance tracking for tokens/sec, cache rates, memory usage, and batch efficiency
  - `ofxGgmlModelRegistry` - model version management and hot-swapping
  - `ofxGgmlPromptTemplates` - 30+ reusable templates for common AI tasks
- server-streamed text output now uses delta-based chunk handling so Chat and Script mode no longer duplicate partial text while `llama-server` replies are still arriving
- addon-level `Live context` support for loaded sources, domain-provider grounding, generic search fallback, and stricter citation-oriented response modes
- `ofxGgmlSpeechInference` for local speech-to-text workflows via pluggable speech backends, with ready-to-use Whisper CLI profiles
- `ofxGgmlTtsInference` as a lightweight text-to-speech bridge layer for Piper and optional `chatllm.cpp`-backed OuteTTS workflows
- `ofxGgmlClipInference` as a lightweight CLIP-style embedding and ranking bridge layer for text/image similarity workflows, with an optional `clip.cpp` adapter path
- `ofxGgmlDiffusionInference` as a lightweight image-generation bridge layer that can host an `ofxStableDiffusion` adapter without coupling diffusion internals into the core addon, now with structured image modes, CLIP-friendly rerank selection, and richer per-image metadata
- `ofxGgmlVisionInference` for multimodal image-to-text requests against `llama-server`-style OpenAI-compatible endpoints
- `ofxGgmlVideoInference` for backend-driven video understanding, starting with sampled-frame analysis and room for future specialized video backends
- `ofxGgmlVideoPlanner` for beat planning, multi-scene sequencing, and AI-assisted edit-plan generation that can feed video, diffusion, or writing workflows
  - the planner remains generation-agnostic so apps can pair those plans with `ofxStableDiffusion`, `ofxVlc4`, or external renderers while keeping one shared planning/export manifest
- `ofxGgmlMontagePlanner` for subtitle-driven montage planning, ranked clip selection, editor briefs, and CMX-style EDL export
- `ofxGgmlMontagePreviewBridge` as a playback-facing bridge surface that exposes source-timed and montage-timed subtitle tracks, cue lookup, and playlist-oriented preview data for companions such as `ofxVlc4`
- `ofxGgmlHoloscanBridge` as an optional live `frame -> vision -> preview/result` bridge for Holoscan-style media pipelines, with a native Holoscan runtime path on Linux and the addon fallback lane kept for other platforms for now
- `ofxGgmlImageSearch` for internet reference-image lookup through pluggable providers, with a working Wikimedia Commons backend
- `ofxGgmlWebCrawler` as an optional website-ingestion bridge layer, with a `Mojo` CLI adapter for local website-to-Markdown crawling workflows
- `ofxGgmlCitationSearch` for topic-oriented source-grounded quote extraction, with structured citation items built from loaded URLs or crawler-ingested website content
- `ofxGgmlVideoEssayWorkflow` for local-first `topic -> cited outline -> narrated script -> voice cues -> SRT -> visual concept -> scene/edit planning` orchestration on top of citation search, the text assistant, and the shared video planner
- `ofxGgmlVideoEssayWorkflow` stays intentionally staged: research, outline, script, voice cues, and SRT/cue-sheet generation remain the core path, while scene/edit planning is layered on as a handoff-friendly Phase 3 instead of forcing one renderer or export path
  - the workflow now also exposes request validation plus a reusable JSON manifest that captures topic, sources, cues, planning summaries, and warnings for downstream render/export tools
- `ofxGgmlMediaPromptGenerator` for local-first cross-media prompt translation, starting with `Music -> Image` prompt generation that can reuse transcripts, lyrics, and existing text backends before handing the result to diffusion workflows
- `ofxGgmlMusicGenerator` for general music-prompt generation, local-first ABC notation sketch generation, prompt sanitization/validation, and future pluggable rendered-audio backend bridges
- `ofxGgmlAceStepBridge` for optional rendered-music and audio-understanding workflows against an external `acestep.cpp`-compatible server
- `ofxGgmlMilkDropGenerator` for MilkDrop / projectM preset generation and editing through the existing text-inference backend, with prompt preparation, preset sanitization, and `.milk` file saving helpers
- `ofxGgmlMilkDropGenerator` now also supports basic preset validation, conservative repair prompts, and multi-variant preset generation for quicker visual iteration
- `ofxGgmlEasy` as a small high-level facade for common text, chat, translation, vision, and speech tasks without wiring the lower-level assistants by hand
- `ofxGgmlEasy` now also covers crawler-backed citation research, subtitle-montage planning/export, AI-assisted video-edit planning, and MilkDrop preset generation/editing so apps can reuse the higher-level workflow helpers without depending on the full GUI example
- `ofxGgmlEasy` now also covers cross-media prompt translation plus general music-prompt / ABC-sketch generation so apps can build `Music -> Image` and `Image -> Music` flows without wiring the lower-level helpers directly
- `ofxGgmlEasy` now keeps text inference, crawling, and citation search on one persistent helper path, so `configureText()`, `configureWebCrawler()`, `getWebCrawler()`, `getCitationSearch()`, and `findCitations()` operate on the same configured pipeline
  - `ofxGgmlEasy` now also exposes `planVideoEssay(...)` so apps can reuse the new citation-to-script workflow, including visual concept plus scene/edit handoff data, without copying the GUI example glue
  - the `Video Essay` workflow can now also hand that Phase 3 output into an optional `ofxVlc4` preview/render lane in the GUI example, including source-video subtitle preview, inline playback controls, and texture-recorded essay renders muxed with the generated narration track
- `ofxGgmlChatAssistant` for reusable chat prompts, response-language control, and UI-thin conversation flows
- `ofxGgmlCodeAssistant` for coding-oriented prompts, structured task plans, unified diff output, compile-database-aware semantic retrieval, inline completion, repo context, focused-file assistance, and follow-up scripting actions
- `ofxGgmlCodeAssistant` now also exposes lightweight assistant sessions, a typed tool registry, approval callbacks for risky proposals, and streamed assistant events so apps can build safer IDE-style coding flows without reimplementing orchestration
- `ofxGgmlCodingAgent` as a thin orchestration layer on top of the code and workspace assistants, with persistent session memory, a read-only `Plan` mode, optional patch application, and verification-aware coding runs
- the GUI example Script mode now surfaces that assistant runtime directly with `Build` / `Plan` agent switching, `@` references including read-oriented `@general`, quick slash/intents chips, streamed tool/approval status, and explicit approve/deny handling for risky proposals
- `ofxGgmlWorkspaceAssistant` for validated patch application, allow-listed edit enforcement, unified-diff transactions with rollback, shadow-workspace safe apply, auto-selected verification commands, and retry-oriented coding loops on top of structured assistant output
- coding workflows now carry lightweight task memory such as active mode, selected backend, recent files, and last failure reason so retries and follow-up prompts stay more grounded
- structured coding prompts now push a clearer inspect -> patch -> verify loop, stronger self-check instructions, and recovery from weak unstructured model replies
- `ofxGgmlTextAssistant` for translation, summarization, rewriting, and reusable text-task prompts
- `ofxGgmlCodeReview`, `ofxGgmlProjectMemory`, and `ofxGgmlScriptSource` helpers for local coding and multi-pass review workflows
- `ofxGgmlScriptSource` now accepts local folders, Visual Studio `.sln` / `.vcxproj` workspaces, GitHub `owner/repo` values, full GitHub URLs, and branch-aware repo URLs
- assistant eval coverage for retrieval quality, dry-run safety, and structured workspace execution
- async graph submission and explicit synchronization for frame-friendly compute
- Windows build scripts that refresh Visual Studio linking automatically
- GUI example for local chat, review, and script-assisted workflows built mostly on addon helpers
  - GUI example `Easy` mode now demonstrates the high-level `ofxGgmlEasy` facade directly, including one-click chat, summarize, translate, citation search, `Video Essay`, and coding-agent `Plan` flows using the currently selected backend/model
    - GUI example sidebar loading is now more centralized: `Video` now separates its text planner model from a dedicated video-render model preset/override lane, text modes can override the selected preset with any local GGUF path, and `Image` mode now keeps diffusion model / VAE / init / mask loading together in one shared sidebar section
  - GUI example Vision mode now also includes a small `Holoscan Bridge` section for live frame submission and inline preview.
    - The native Holoscan runtime path is Linux-only for now; Windows and other platforms stay on the addon fallback lane until that runtime is validated there.
  - GUI example Translate mode with auto-detect source language, natural vs. literal translation shortcuts, detect-and-translate flow, and more reliable prompt/input handoff buttons
  - GUI example Chat and Translate modes now each keep a dedicated lightweight TTS preview lane, so spoken replies and translated voice output can be played, restarted, and stopped inline without bouncing through the main TTS panel
  - GUI example `Video Essay` mode now also supports optional `ofxVlc4` preview/render handoff, so a source video plus the generated essay SRT can be previewed live and texture-recorded into a muxed narration render without leaving the example
  - GUI example `Montage` mode now also includes a clip-playlist export lane for generated or curated clip paths, with JSON playlist-manifest export, optional `ofxVlc4` playlist preview, and texture-recorded render export with optional external-audio mux
  - that same Montage playlist lane can now auto-collect generated video outputs such as rendered essay videos or previous montage renders, merge them into the current clip list, and start preview/record export as soon as the first VLC frame is ready
  - GUI example Montage mode can now preview restructured subtitle cues live, copy generated SRT/VTT exports, and keep a playback-facing subtitle track ready for external preview layers such as `ofxVlc4`
  - when the GUI example is regenerated with `ofxVlc4` enabled in `addons.make`, Montage mode can also load the active subtitle track directly into an optional `ofxVlc4` preview with subtitle delay / scale controls
  - that same Montage area can now also collect clip paths into a small playlist manifest, preview the sequence through an optional second `ofxVlc4` lane, and record the playlist back out to a video file with optional audio mux
  - `ofxVlc4` is no longer included in the default `ofxGgmlGuiExample/addons.make`; add it back manually only if you want those optional VLC preview and texture-record export lanes
- GUI example Summarize mode now includes a dedicated citation-research section that can extract quoted evidence from loaded URLs or crawl a seed website before building a cited summary
- GUI example Diffusion mode now includes a `Music -> Image` helper that can turn a music caption plus optional lyrics/transcript into a reusable visual prompt for the existing diffusion flow
- GUI example Vision mode now also includes a dedicated `Music Video` workflow section that can turn song text into a visual concept, apply music-video planning defaults, and hand the result directly into video planning, diffusion, and edit-plan generation
- the shared video planner now also supports music-video-aware section planning, including intro / verse / chorus / bridge style sections, section-level energy and cut-density hints, and section summaries that feed prompt generation and Music Video planning review
- GUI example Vision mode now also includes an `Image / Prompt -> Music` helper that can turn a scene description into a reusable music-generation prompt and a local ABC notation sketch, with direct handoff into Custom mode or `.abc` saving
- GUI example MilkDrop mode can generate `.milk` preset text from prompts, save presets, and optionally preview the result live through `ofxProjectM` when the example is regenerated with that addon enabled
- GUI example MilkDrop mode now also includes validation, preset repair, quick variants, and projectM preview controls for beat sensitivity, preset duration, and microphone-driven reactive preview while Speech recording is active

## Source layout

The main public API stays in:

- `src/ofxGgml.h`

Core implementation is split by concern:

- `src/core/` for runtime entry points, shared types, helpers, and version metadata
- `src/compute/` for tensors and graph building
- `src/model/` for GGUF model loading
- `src/inference/` for completion execution, grounded prompt assembly, and speech / vision / video inference helpers
- `src/inference/` also now includes bridge scaffolds for optional CLIP-style ranking, TTS, diffusion/image-generation, and music-generation backends such as `clip.cpp`, OuteTTS, `ofxStableDiffusion`, and future rendered-audio generators, plus higher-level planners and preview bridges for video, montage, media-prompt translation, and image search workflows
- `src/inference/` also now includes optional web-ingestion helpers such as `ofxGgmlWebCrawler` plus topic-oriented quote extraction via `ofxGgmlCitationSearch` for local crawler-backed RAG/document pipelines
- `src/bridges/` for optional companion runtime bridges such as the Holoscan-backed live vision lane
- `src/assistants/` for chat, code, workspace, review, and text-task helpers
- `src/support/` for script sources, project memory, and the high-level `ofxGgmlEasy` facade

Supporting areas:

- `libs/ggml/`
- `scripts/` for user-facing setup, build, download, and benchmark entry points
  - includes `scripts/install-acestep.ps1` / `scripts/install-acestep.bat` for building a local AceStep runtime while keeping only the final launcher/runtime binaries under `libs/acestep/bin`
- `scripts/dev/` for maintainer update and patching helpers
- `docs/`
- `tests/`
- `ofxGgmlBasicExample/`
- `ofxGgmlGuiExample/`
- `ofxGgmlNeuralExample/`

Developer tooling:

- repo-level `.clang-tidy` for addon/example static-analysis defaults
- `scripts/run-clang-tidy.ps1` for Windows / Visual Studio workflows
- `scripts/run-clang-tidy.sh` for Linux/macOS compile-database workflows
- `docs/CLANG_TIDY.md` for usage and recommended scope
- `docs/OFXIMGUI_ASSISTANT_SPEC.md` for the proposed specialized `ofxImGui` / openFrameworks GUI assistant design

## Compatibility policy

`ofxGgml` can work alongside optional companion addons such as `ofxStableDiffusion`, but the recommended policy is:

- pin tested upstream revisions instead of following `main`
- keep `ofxGgml` and `ofxStableDiffusion` as separate integrations
- avoid manual copying of stale `ggml*.dll` files into example `bin` folders
- treat "tested together" as an explicit release note, not an assumption

The full versioning and runtime-packaging guidance lives in:

- `docs/COMPATIBILITY.md`

Short version: prefer a compatibility matrix over "latest everywhere", and only consider one shared `ggml` build when both upstreams are verified against the same revision and you are ready to maintain that coupling.

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

By default, helper scripts now prefer a shared addon-level model folder:

- `models/`

That keeps large GGUF / Whisper model files out of per-example `bin/data/models` copies during development. The GUI example still falls back to bundled `bin/data/models` when you package a standalone app.

In the current GUI example:

  - `Video` mode now keeps planner and render model selection separate:
  - the shared catalog still provides an optional text planner preset for chunk/continuity planning
  - the sidebar also exposes a dedicated `Video Render Model` section with its own optional video-generation presets, custom local model override, and download link
- the shared sidebar can override the selected text-model preset with any local GGUF file through `Browse GGUF...`
- `Image` mode keeps diffusion asset loading centralized in the sidebar under `Image Assets`, including diffusion model, optional VAE, init image, and inpaint mask image

On Windows, the GUI example also adds central runtime folders such as:

- `libs/llama/bin`
- `libs/whisper/bin`
- `libs/chatllm/bin`
- `libs/piper/bin`
- `libs/ggml/build/src/Release`

to the process DLL search path at startup, so development builds do not need duplicate runtime DLL copies in `bin/` unless you are distributing a standalone bundle.

## Easy API

If you want a shorter setup path than wiring `ofxGgmlInference`, `ofxGgmlTextAssistant`, `ofxGgmlVisionInference`, and `ofxGgmlSpeechInference` manually, use `ofxGgmlEasy`.

`ofxGgmlEasy` keeps one owned crawler/citation path internally. Configuration changes made through `configureText()`, `configureWebCrawler()`, `getWebCrawler()`, or `getCitationSearch()` are reused by later `crawlWebsite()` and `findCitations()` calls instead of being rebuilt on a separate temporary helper.

For coding workflows that need more than a single prompt, `ofxGgmlCodingAgent` now sits between `ofxGgmlCodeAssistant` and `ofxGgmlWorkspaceAssistant`. It keeps a reusable coding session, can stay read-only in `Plan` mode, and can optionally apply and verify structured edits in one run.

Minimal text setup:

```cpp
#include "ofxGgml.h"

ofxGgmlEasy ai;

ofxGgmlEasyTextConfig text;
text.modelPath = "data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf";
text.completionExecutable = "llama-completion";
ai.configureText(text);

auto summary = ai.summarize("Summarize this paragraph in one sentence.");
if (summary.inference.success) {
    ofLogNotice() << summary.inference.text;
}
```

Chat and translation:

```cpp
auto chat = ai.chat("Explain ggml in simple terms.", "English");
auto translated = ai.translate("Guten Morgen", "English", "German");
```

Vision:

```cpp
ofxGgmlEasyVisionConfig vision;
vision.modelPath = "data/models/LFM2.5-VL-1.6B-Q8_0.gguf";
vision.mmprojPath = "data/models/mmproj-LFM2.5-VL-1.6b-Q8_0.gguf";
vision.serverUrl = "http://127.0.0.1:8080";
ai.configureVision(vision);

auto imageResult = ai.describeImage("data/images/test.png");
```

Speech:

```cpp
ofxGgmlEasySpeechConfig speech;
speech.modelPath = "data/models/ggml-base.en.bin";
speech.cliExecutable = "whisper-cli";
ai.configureSpeech(speech);

auto transcript = ai.transcribeAudio("data/audio/test.wav");
```

Research, montage, and edit workflows:

```cpp
ofxGgmlEasyCrawlerConfig crawler;
crawler.executablePath = "mojo";
ai.configureWebCrawler(crawler);

auto citations = ai.findCitations(
    "Berlin airport winter disruption",
    {},
    "https://example.com/weather",
    4);

auto montage = ai.planMontageFromSrt(
    "data/subtitles/scene.srt",
    "Build a concise recap montage.");

auto editPlan = ai.planVideoEdit(
    "Berlin city footage",
    "Turn this into a fast social recap.",
    "Opening skyline, transit, crowd reaction.");

auto musicPrompt = ai.generateMusicPrompt(
    "dreamy rainy neon city at night",
    "ambient electronica",
    "soft analog synths, sub bass, light vinyl crackle",
    45);

auto imageToMusic = ai.generateImageToMusicPrompt(
    "orange dusk over a harbor with slow boats and reflected lights",
    "gentle movement, reflective mood",
    "cinematic ambient",
    "warm piano and textured pads");

auto abc = ai.generateMusicNotation(
    "playful hand-drawn city chase",
    "quirky chamber pop",
    "pizzicato strings and clarinet",
    16);
if (abc.success) {
    ai.saveMusicNotation(
        abc.notationText,
        "data/generated/music/city-chase.abc");
}

auto milkdrop = ai.generateMilkDropPreset(
    "neon kaleidoscope tunnel with bass-reactive zoom pulses",
    "Geometric",
    0.65f);
if (milkdrop.success) {
    ai.getMilkDropGenerator().savePreset(
        milkdrop.presetText,
        "data/generated/milkdrop/neon-tunnel.milk");
}

auto variants = ai.generateMilkDropVariants(
    "liquid neon lattice with soft beat pulses",
    "Liquid",
    0.6f,
    3);

auto validation = ai.validateMilkDropPreset(milkdrop.presetText);
if (!validation.valid) {
    auto repaired = ai.repairMilkDropPreset(milkdrop.presetText);
}
```

**Workflow Presets** (v1.1.0+):

Chain common AI workflows with a single method call:

```cpp
// Summarize then translate
auto result = ai.summarizeAndTranslate(
    longArticleText,
    "Spanish",  // target language
    "English",  // source language (or "Auto detect")
    150);       // max summary words

cout << "Summary: " << result.getIntermediateResult(0) << endl;
cout << "Translation: " << result.finalOutput << endl;
cout << "Total time: " << result.totalElapsedMs << "ms" << endl;

// Transcribe audio then summarize transcript
auto audioResult = ai.transcribeAndSummarize("interview.mp3", 100);

// Describe image then analyze with text model
auto visionResult = ai.describeAndAnalyze(
    "scene.jpg",
    "Analyze the artistic style and composition");

// Crawl website then summarize findings
auto webResult = ai.crawlAndSummarize("https://example.com", 2, 200);
```

Workflow results include:
- `intermediateResults` - Outputs from each step
- `finalOutput` - Final result
- `totalElapsedMs` - End-to-end timing
- `success` / `error` - Status information

`ofxGgmlEasy` is intentionally small. If you need deeper control, it still exposes the owned lower-level helpers:

- `getInference()`
- `getChatAssistant()`
- `getTextAssistant()`
- `getVisionInference()`
- `getSpeechInference()`
- `getWebCrawler()`
- `getCitationSearch()`
- `getMediaPromptGenerator()`
- `getVideoEssayWorkflow()`
- `getVideoPlanner()`
- `getMusicGenerator()`
- `getMilkDropGenerator()`

## Quick Wins: Developer Experience Features

ofxGgml includes high-impact features for improved developer experience. See `docs/QUICK_WINS.md` for detailed documentation.

### Batched Inference API

Process multiple inference requests efficiently with automatic parallelization and fallback:

```cpp
ofxGgmlInference inference;
std::vector<std::string> prompts = {
    "Summarize quantum computing",
    "Explain neural networks",
    "What is machine learning?"
};

ofxGgmlInferenceSettings settings;
settings.maxTokens = 256;
settings.useServerBackend = true;

auto result = inference.generateBatchSimple("model.gguf", prompts, settings);
std::cout << "Processed: " << result.processedCount << " requests in "
          << result.totalElapsedMs << "ms" << std::endl;
```

Features:
- Concurrent processing for server backends (configurable parallelism)
- Sequential fallback for CLI backends
- Per-request settings and streaming callbacks
- Built-in metrics tracking via `ofxGgmlMetrics`
- Batch embeddings support, now with bounded concurrent server requests instead of a purely sequential loop

See `docs/BATCH_INFERENCE.md` for comprehensive documentation.

### Streaming API with Backpressure Control

`ofxGgmlStreamingContext` provides pause/resume/cancel capabilities and flow control:

```cpp
auto ctx = std::make_shared<ofxGgmlStreamingContext>();
ctx->setBackpressureThreshold(1000);

inference.generate(modelPath, prompt, settings, [ctx](const std::string& chunk) {
    if (ctx->shouldPause()) ctx->waitForResume(5000);
    if (ctx->isCancelled()) return false;
    processChunk(chunk);
    return true;
});
```

**Enhanced Progress Tracking** (v1.1.0+):

Track detailed streaming progress with token counts, speed, and completion percentage:

```cpp
auto ctx = std::make_shared<ofxGgmlStreamingContext>();
ctx->setEstimatedTotal(settings.maxTokens);

inference.generate(modelPath, prompt, settings, [ctx](const std::string& chunk) {
    ctx->addTokens(1); // Increment for each token/chunk

    auto progress = ctx->getProgress(chunk);
    ofLogNotice() << "Progress: " << (progress.percentComplete * 100.0f) << "% "
                  << "Speed: " << progress.tokensPerSecond << " tok/s "
                  << "Elapsed: " << progress.elapsedMs << "ms";

    if (ctx->isCancelled()) return false;
    return true;
});
```

Progress information includes:
- `tokensGenerated` - Current token count
- `estimatedTotal` - Expected total (from maxTokens)
- `percentComplete` - Progress as 0.0 to 1.0
- `tokensPerSecond` - Generation speed
- `elapsedMs` - Time since start

### Logging and Metrics

`ofxGgmlLogger` provides configurable logging, and `ofxGgmlMetrics` tracks performance:

```cpp
auto& logger = ofxGgmlLogger::getInstance();
logger.setLevel(ofxGgmlLogger::Level::Debug);
logger.info("Inference", "Starting generation");

auto& metrics = ofxGgmlMetrics::getInstance();
metrics.recordInferenceStart("llama-7b");
// ... inference ...
metrics.recordInferenceEnd("llama-7b", tokens, elapsedMs);
std::cout << metrics.getSummary();
```

### Memory Usage and Server Monitoring

Monitor resource consumption and server status for production deployments:

```cpp
// Check memory usage
ofxGgml runtime;
runtime.setup();
auto memory = runtime.getMemoryUsage();
std::cout << "Model weights: " << (memory.modelWeightBytes / 1024 / 1024) << " MB" << std::endl;
std::cout << "Total allocated: " << (memory.totalAllocatedBytes / 1024 / 1024) << " MB" << std::endl;
std::cout << "Backend: " << memory.backendName << std::endl;

// Monitor server queue
ofxGgmlInference inference;
auto queueStatus = ofxGgmlInference::getServerQueueStatus("http://127.0.0.1:8080");
if (queueStatus.available) {
    std::cout << "Queue length: " << queueStatus.queueLength << std::endl;
    std::cout << "Processing: " << queueStatus.processingCount << std::endl;
    std::cout << "Completed: " << queueStatus.completedCount << std::endl;
}
```

These monitoring APIs are essential for:
- Diagnosing memory issues and optimizing resource usage
- Load balancing across multiple server instances
- Detecting server overload conditions
- Performance profiling and capacity planning

### Model Version Management

`ofxGgmlModelRegistry` enables hot-swapping between model versions:

```cpp
auto& registry = ofxGgmlModelRegistry::getInstance();
registry.registerModel(metadata_v1);
registry.registerModel(metadata_v2);
registry.setActiveVersion("llama-7b", "v2-q5"); // Hot-swap
std::string path = registry.getActiveModelPath("llama-7b");
```

### Prompt Template Library

`ofxGgmlPromptTemplates` provides 30+ reusable templates:

```cpp
auto& templates = ofxGgmlPromptTemplates::getInstance();
std::string prompt = templates.fill("summarize", {
    {"text", content},
    {"max_length", "3 sentences"}
});
```

## clang-tidy

`ofxGgml` now ships with a repo-level `clang-tidy` configuration and helper scripts.

Windows / Visual Studio:

```powershell
./scripts/run-clang-tidy.ps1
./scripts/run-clang-tidy.ps1 -UseMsBuild
./scripts/run-clang-tidy.ps1 -Files src/core/ofxGgmlCore.cpp,src/inference/ofxGgmlInference.cpp
```

Linux / macOS:

```bash
./scripts/run-clang-tidy.sh
./scripts/run-clang-tidy.sh src/core/ofxGgmlCore.cpp src/inference/ofxGgmlInference.cpp
```

The scripts prefer a `compile_commands.json` database when available and keep analysis scoped to addon/example code rather than vendored `libs/`.

More detailed guidance lives in:

- `docs/CLANG_TIDY.md`

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
./scripts/setup_linux_macos.sh --skip-model
./scripts/setup_linux_macos.sh --with-llama-cli --skip-model
./scripts/setup_linux_macos.sh --skip-model
./scripts/setup_linux_macos.sh --skip-ggml --model-preset 2
```

Windows:

```bat
scripts\setup_windows.bat
scripts\setup_windows.bat --cuda
scripts\setup_windows.bat --skip-model
scripts\setup_windows.bat --with-llama-cli --skip-model
scripts\setup_windows.bat --skip-ggml --model-preset 2
```

`download-model` covers the text GGUF presets used by chat/script/write flows. Speech (`Whisper`) and multimodal `Vision` models are configured separately in the addon and GUI example because they use different runtimes and file layouts. The current Vision defaults favor EU-safe llama-server profiles such as `LFM2.5-VL` for general image understanding and `GLM-OCR` for OCR-heavy work.

### AceStep music backend

The music tools are split intentionally:

- `Image -> Music` prompt generation and ABC sketch generation stay local-first and do not require AceStep
- rendered audio generation and audio understanding do require an external AceStep-compatible server

To install a local AceStep checkout into the addon:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install-acestep.ps1
```

```bat
scripts\install-acestep.bat
```

The installer now auto-detects the remote default branch when possible, falls back across common branch names (`main`, `master`, `dev`, `trunk`), and initializes required submodules so upstream branch/layout changes fail less often.

By default the helper now keeps the heavy checkout/build tree outside the addon under `%LOCALAPPDATA%\ofxGgml\acestep\`, copies the final runtime files into:

- `libs/acestep/bin`

and prunes the temporary source/build artifacts after a successful install. Pass `-KeepArtifacts` if you intentionally want to keep the checkout/build tree for debugging.

After starting the AceStep server, open the GUI example's `Vision -> AceStep Music Backend` section and use `Check AceStep server` to verify that the configured URL is reachable.

`setup_windows.bat` and `setup_linux_macos.sh` now follow the server-first path by default: they build `ggml`, leave the local `llama.cpp` runtime optional, and let you opt into addon-local `llama-server` plus CLI fallback tools with `--with-llama-cli` only when you want them.

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

Use the dedicated PowerShell helper to clone `ggml-org/llama.cpp`, build the local text runtime, and copy the server plus CLI fallback tools into `libs/llama/bin`.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-llama-server.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\build-llama-server.ps1 -Cuda
powershell -ExecutionPolicy Bypass -File .\scripts\build-llama-server.ps1 -CpuOnly
```

```bat
scripts\build-llama-server.bat
scripts\build-llama-server.bat -Cuda
scripts\build-llama-server.bat -CpuOnly
```

By default the script:

- uses `build\llama-src` for the upstream source checkout
- uses `build\llama-bld` for the CMake build tree
- installs `llama-server.exe`, `llama-completion.exe`, `llama-cli.exe`, and the required DLLs into `libs\llama\bin`
- installs `llama-embedding.exe` too when that target is available in the upstream checkout
- forces `GGML_VULKAN=OFF` in this Windows helper to avoid long-path MSBuild failures inside the Vulkan shader generator subtree

That install location matches the GUI example's local server discovery and CLI fallback probing, so server-backed text modes can auto-launch the local server during app setup and still fall back to addon-local `llama-completion` / `llama-cli` from the same `llama.cpp` checkout.

For Linux and macOS, the shell helper now follows the same addon-local runtime pattern:

```bash
./scripts/build-llama-cli.sh --auto
./scripts/start-llama-server.sh
```

To launch the local server manually after building it:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\start-llama-server.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\start-llama-server.ps1 -Detached
```

The helpers default to `http://127.0.0.1:8080`, reuse the recommended local GGUF model when possible, and expose GPU layers / context size flags so the server path matches the GUI example's defaults more closely.

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

## Getting Started

### Quick Start (5 minutes)

See [docs/getting-started/QUICKSTART.md](docs/getting-started/QUICKSTART.md)

### Learning Path

1. **Text-only AI**: [docs/getting-started/BASIC_INFERENCE.md](docs/getting-started/BASIC_INFERENCE.md)
2. **Speech/Vision**: [docs/features/MODALITIES.md](docs/features/MODALITIES.md)
3. **Video/Research**: [docs/features/WORKFLOWS.md](docs/features/WORKFLOWS.md)
4. **Code Assistants**: [docs/features/ASSISTANTS.md](docs/features/ASSISTANTS.md)

### Which Features Do I Need?

See [docs/getting-started/CHOOSING_FEATURES.md](docs/getting-started/CHOOSING_FEATURES.md) to pick the right subset.

## Examples

### Focused Examples (Recommended for Learning)

- `ofxGgmlChatExample`: **Simple chat application** (~200 lines, uses `ofxGgmlBasic.h`)
- `ofxGgmlBasicExample`: Interactive matrix demo plus steady-state matmul benchmark
- `ofxGgmlNeuralExample`: Reusable inference graph with live class bars and latency view

### Comprehensive Example

- `ofxGgmlGuiExample`: All features in one GUI demo - local chat, review, script, speech, multimodal, and `Live context` workflow UI backed by addon helpers

The lightweight examples are keyboard-driven so you can rerun compute and benchmark paths without restarting the app.

## Tests

The test suite lives in `tests/` and covers core runtime behavior, model loading, inference helpers, chat/code/text assistants, and project memory support. When you change backend setup, Windows linking, or inference command assembly, it is worth rerunning the tests or at least rebuilding one example project.

Windows quick check:

```powershell
cmake -S tests -B tests/build
cmake --build tests/build --config Release
./tests/build/Release/ofxGgml-tests.exe
```

Recent coverage additions include the montage preview/export bridge, newer split inference text-cleanup paths, and the updated speech / translate assistant flows.

## Persistent Server Backend

`ofxGgmlInference` can now target a warm `llama-server` process for both text generation and embeddings. When `serverModel` is left empty, the addon probes `/v1/models`, caches the active model briefly, and reuses that information across nearby requests. Review and retrieval flows can also use the same server, which keeps hierarchical review fast while preserving semantic ranking.

The GUI example couples a preferred text backend to each text-capable mode, stores that preference with the session, and now defaults every text mode to `llama-server`.

When the server backend is selected, the GUI:

- auto-applies server-friendly defaults for the active text mode
- auto-probes the configured server URL
- auto-launches a local `llama-server` during app setup when the URL points at the default local endpoint and a local server binary is available
- keeps the sidebar focused on passive status and configuration rather than manual server-management buttons

When the CLI backend is selected, it is treated as an optional fallback path rather than a required default. The sidebar now makes that explicit and keeps the missing-CLI state informational instead of presenting it as a broken primary setup.

Text-heavy modes also ship with server-friendly quick actions so the warm backend is useful outside of coding:

- `Chat`: `Summarize Chat`
- `Summarize`: `Executive Brief`, `Action Items`, `Meeting Notes`, `Source Brief`
- `Write`: `Shorten`, `Email Reply`, `Release Notes`, `Commit Message`
- `Translate`: `Natural`, `Literal`, `Detect + Translate`
- `Custom`: `JSON Reply`, `Professional Tone`

## Performance

`ofxGgml` now ships with explicit benchmark entry points and performance optimizations.

### Quick Performance Tips

**For best inference performance:**
1. **Use server mode** - `settings.useServerBackend = true` (10-50ms faster per request)
2. **Prompt caching** - Enabled by default for 2-5x speedup in multi-turn workflows
3. **Batch API** - Process multiple requests in parallel with `generateBatch()`

**Run benchmarks:**

```bash
./scripts/benchmark-addon.sh
```

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\benchmark-addon.ps1
```

These wrappers configure the test suite with `OFXGGML_ENABLE_BENCHMARK_TESTS=ON`, build `ofxGgml-tests`, and run the stable benchmark set (`[benchmark]~[manual]`) by default.

**For detailed performance tuning, optimization strategies, and expected performance numbers, see `docs/PERFORMANCE.md`.**

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

The scripting-oriented assistant path has also been tightened so it behaves more like an editing agent than a one-shot prompt wrapper:

- likely edit targets now prefer compiler-reported files, allow-listed files, focused files, and recently touched files
- symbol retrieval gives extra weight to recently touched files instead of treating all workspace files equally
- structured prompts ask for inspect -> patch -> verify plans with grounded file paths and symbol names
- when the model replies in weak freeform prose even though structured output was requested, the assistant now performs one recovery pass to convert that answer into the expected structured tags
- workspace retries now carry forward the last failure reason and touched files so remediation passes are narrower and more actionable

Repository-specific instructions can now shape review prompts as well. For local workspaces, the addon reads:

- `.github/copilot-instructions.md`
- path-specific `.github/instructions/*.instructions.md`
- nearest `AGENTS.md`

Those instruction files are read directly from the workspace, so they still work even when hidden folders such as `.github` would otherwise be excluded from a normal file listing.

## Chat Assistant Helpers

`ofxGgmlChatAssistant` lifts the generic chat prompt path out of the `GuiExample`. It prepares reusable conversation prompts with optional system instructions and response-language hints, so apps can keep chat UIs thin and consistent.

Use it when an app wants local chat behavior without duplicating prompt assembly or keeping a second language-preset list in UI code.

## Code Assistant Helpers

`ofxGgmlCodeAssistant` lifts the scripting workflow out of the `GuiExample`. It builds coding prompts with language presets, project memory, repo/file context, focused-file snippets, semantic symbol retrieval, and reusable actions such as `Generate`, `Edit`, `Refactor`, `Review`, `FixBuild`, `GroundedDocs`, `ContinueTask`, and `ContinueCutoff`.

It can also request structured task output so apps receive a machine-readable plan instead of only free-form prose. Structured responses can include acceptance criteria, file intents, patch operations, unified diffs, synthesized test ideas, verification commands, review findings with severity/confidence, reviewer-simulation passes, risk scoring, open questions, and parsed build-error context.

Use it when an app wants Copilot-style local coding assistance without duplicating prompt assembly, retrieval, and follow-up logic in UI code.

The `GuiExample` now layers higher-level scripting workflows on top of those assistant actions:

- slash commands such as `/review`, `/reviewfix`, `/nextedit`, `/summary`, `/tests`, `/fix`, `/explain`, and `/docs`
- one-click `Next Edit`, `Review Fix Plan`, and local `Change Summary` actions
- server-first code generation with automatic CLI fallback when the server is unavailable
- clearer Script-mode guidance in the UI, including backend/workspace context, suggested next steps, cached verification commands, and reuse of recent touched files / last failure reason for follow-up prompts

For app-side orchestration, the code assistant now also exposes a lighter agent runtime surface:

- `ofxGgmlCodeAssistantSession` keeps active mode, backend, touched files, last failure, and short prompt/result history
- `defaultToolRegistry()`, `registerTool()`, and `getToolRegistry()` expose a typed assistant tool catalog for actions such as repo context reads, symbol retrieval, patch application, and verification
- `runWithSession(...)` adds streamed events plus approval callbacks for risky proposals such as `apply_patch` and `run_verification`
- `ofxGgmlCodeAssistantEvent` emits phases such as prompt prepared, output chunk, structured result ready, tool proposed, approval requested, and completed/error

That keeps the addon aligned with editor-style assistant flows without forcing every openFrameworks app to rebuild session memory, tool summaries, and approval UX from scratch.

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

On top of those core prompts, the GUI example now includes professional one-click flows for executive briefs, action extraction, meeting notes, email replies, release notes, commit messages, natural vs. literal translations, and structured JSON replies.

Use it when an app wants translation or writing-assistant features without hardcoding task prompts in its UI layer.

## Speech Helpers

`ofxGgmlSpeechInference` adds addon-level speech-to-text support through a pluggable backend interface. The default backend targets upstream `whisper.cpp`, prefers a local `whisper-cli` build in `libs/whisper/bin` or `build/whisper.cpp-build/bin` when available, and ships with ready-to-use profile hints for common Whisper model families such as `Tiny.en`, `Base.en`, `Small`, and `Large-v3 Turbo`.

Use it when an app wants local `Transcribe` / `Translate` audio workflows without hardcoding command-line assembly in its UI layer. The `GuiExample` exposes executable path, model path, profile selection, language hint, prompt, and transcript output as a first-class panel.

The speech path can now also target an optional OpenAI-compatible speech server through `Server URL` and `Server model` in the GUI. When a speech server is configured, the Speech panel prefers the warm server backend first and falls back to `whisper-cli` automatically if the server request fails and a local CLI path is still available.

For a local `whisper.cpp` speech workflow on Windows, use:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-whisper-server.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\build-whisper-server.ps1 -Cuda
powershell -ExecutionPolicy Bypass -File .\scripts\start-whisper-server.ps1
```

Or use the matching batch wrappers:

```bat
scripts\build-whisper-server.bat
scripts\build-whisper-server.bat -Cuda
scripts\start-whisper-server.bat
```

`scripts/build-whisper-server.ps1` now builds and installs both `whisper-cli.exe` and `whisper-server.exe` into `libs/whisper/bin`, so the CLI fallback and the warm speech server come from the same local `whisper.cpp` checkout. The helper defaults to `http://127.0.0.1:8081`, which avoids colliding with the addon's default `llama-server` text backend on `8080`. The GUI now defaults the Speech server URL to that local endpoint, auto-detects the local `whisper-cli` / `whisper-server` runtime, and can auto-start a local `whisper-server` during setup when the configured URL points at localhost and a local server binary is available.

When the backend supports it, the speech path now keeps richer artifacts:

- detected language hints from Whisper output
- `.srt` / `.vtt` subtitle artifacts when timestamp mode is enabled
- parsed timestamp segments surfaced back into the GUI through the same lightweight SRT parser style already used in `ofxVlc4`
- temporary microphone capture to WAV, so the Speech panel can record and immediately transcribe or translate from the default input device

The Speech panel now supports a simple microphone workflow directly in the GUI:

- `Start Mic Recording` captures from the default input device
- `Stop + Run` writes a temporary WAV and runs the current speech task immediately
- `Use Last Recording` lets you retry the same captured audio after changing prompt, language hint, or `Transcribe` / `Translate` task settings

## TTS Helpers

`ofxGgmlTtsInference` adds a parallel addon-level text-to-speech layer for synthesis workflows that should stay separate from Whisper-style speech transcription. The current addon exposes two backend families:

- Piper for direct `.onnx` voice models with a matching `.onnx.json`
- `chatllm.cpp` for converted OuteTTS artifacts such as `.bin` / `.ggmm`

Use it when an app wants local speech generation without baking one TTS runtime into its UI layer. The bridge exposes `Synthesize`, `Clone Voice`, and `Continue Speech` task labels, and the GUI example now routes the selected TTS profile to the matching backend instead of pretending all TTS models are interchangeable.

### Piper

The first built-in TTS profile is now Piper. Leave `Executable` blank to auto-discover the addon-local launcher at `libs/piper/bin/piper.bat` first, then fall back to `piper` on `PATH`.

The maintained upstream Piper repository is now:

- `https://github.com/OHF-Voice/piper1-gpl`

The older `rhasspy/piper` repository is archived, so prefer the maintained `OHF-Voice` upstream for documentation and project status.

On Windows, you can install the local Piper runtime and a recommended starter voice with:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install-piper.ps1
```

That helper creates a local Python-backed Piper runtime under `%LOCALAPPDATA%\ofxGgml\piper-runtime`, writes an addon-local launcher into `libs/piper/bin`, and downloads `en_US-lessac-medium` into `models/piper` by default.

You can also use the one-command Windows setup flow:

```bat
scripts\setup_windows.bat --with-piper --download-piper-voice
```

To fetch another Piper voice later:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\download-piper-voice.ps1 -VoiceName en_GB-alan-medium
```

### chatllm.cpp OuteTTS

`chatllm.cpp` remains available as a backend-specific TTS option for converted OuteTTS models. The GUI example treats `Executable` as an optional override: leave it blank to auto-discover `chatllm.cpp`, or point it at a specific runtime if you have one already. The preferred addon-local runtime path is still `libs/chatllm/bin/chatllm(.exe)`.

On Windows, you can populate that runtime path with:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-chatllm.ps1
```

That helper clones `foldl/chatllm.cpp`, builds the local runtime outside the addon tree under `%LOCALAPPDATA%\ofxGgml\chatllm-runtime`, then copies only the final executable and nearby DLLs into `libs/chatllm/bin`.

## Diffusion Helpers

`ofxGgmlDiffusionInference` stays intentionally thin at the native layer, but the public bridge now carries more of the creative workflow state that matters in apps.

The current bridge surface supports:

- image tasks such as `TextToImage`, `ImageToImage`, `InstructImage`, `Variation`, `Restyle`, `Inpaint`, and `Upscale`
- selection policies such as `KeepOrder`, `Rerank`, and `BestOnly`
- optional CLIP-oriented ranking inputs like `rankingPrompt` and normalized text/image embedding control
- richer generated-image metadata including `sourceIndex`, selected-best flags, score values, scorer labels, and score summaries

That design keeps `ofxStableDiffusion` standalone while giving `ofxGgmlClipInference` a clean seam for prompt-image reranking. When a runtime attaches both a diffusion backend and a CLIP backend, apps can generate batches, score them semantically, and keep either the original order or the best-scoring result without pulling low-level `ggml` or diffusion internals across addon boundaries.

The GUI example now goes one step further for local workflows: `ofxGgmlGuiExample/addons.make` includes `ofxStableDiffusion`, and the Diffusion panel auto-attaches a local `ofxStableDiffusion` engine when that addon is available in the same openFrameworks `addons` folder. It also exposes quicker handoff actions such as reusing the current Vision image as an init image, sending generated outputs back into Vision, and copying generated image paths straight into the CLIP panel for reranking.

## Vision Helpers

`ofxGgmlVisionInference` adds multimodal image-to-text support for `llama-server`-compatible endpoints. It prepares task-specific prompts for `Describe`, `OCR`, and `Ask`, handles local image encoding as data URLs, and includes curated profile hints for families such as `LFM2.5-VL`, `Qwen VL`, `GLM OCR`, and `Llama 3.2 Vision`.

The GUI example now recommends `LFM2.5-VL` first for general vision tasks, keeps `GLM-OCR` available for OCR-focused flows, and labels Meta `Llama 3.2 Vision` as EU-restricted because the official Hugging Face download is currently blocked from the European Union.

The default `LFM2.5-VL` profile now points at the exact upstream GGUF download URL for `LFM2.5-VL-1.6B-Q4_0.gguf`, so the Vision panel’s model action goes straight to the correct file instead of a broken guessed path.

The vision path is now more purpose-built too:

- better task-specific prompt defaults for scene description, OCR, and grounded image Q&A
- broader OpenAI-compatible response parsing for more server response shapes
- image labels are injected into multimodal payloads, which improves grounding when a request uses more than one image
- OCR requests now ask for higher-detail image handling when the server understands that hint

The GUI example’s Vision panel also includes one-click quick actions such as `Scene Describe`, `Screenshot Review`, and `Document OCR` so common workflows feel less like raw prompting.

Use it when an app wants OCR, screenshot understanding, document extraction, or image-grounded prompting without rebuilding OpenAI-style request payloads manually.

## Video Helpers

`ofxGgmlVideoInference` adds a backend-driven video layer on top of the vision stack. The default backend samples frames and reuses the multimodal image path, while keeping the API open for future specialized backends such as dedicated video-language servers.

The sampled-frame workflow is now more structured:

- frame labels distinguish opening, middle, and closing samples
- prompts include sample count, clip-window hints, and clearer timestamp-aware instructions
- summaries and OCR requests are phrased to be more professional and to call out unsampled gaps when they may matter

The GUI example now exposes this path directly from the Vision panel with an optional video file input and configurable sampled-frame count, so you can route short local clips through the same stable `llama-server` vision profile.

The Vision panel now also includes dedicated `Action Analysis` and `Emotion Analysis` presets. Those tasks can still run through the regular sampled-frame vision path, but they can optionally target a stronger temporal sidecar service through `Sidecar URL` and `Sidecar model`.

### Temporal Sidecar Contract

For `Action` and `Emotion` video tasks, `ofxGgmlVideoInference` can call an optional sidecar endpoint. By default it normalizes the configured URL to `http://127.0.0.1:8090/analyze`.

The addon sends a JSON payload shaped like:

```json
{
  "task": "Action",
  "model": "temporal-action-v1",
  "video_path": "clip.mp4",
  "prompt": "...",
  "system_prompt": "...",
  "response_language": "en",
  "max_tokens": 512,
  "temperature": 0.2,
  "sampled_frames": [
    {
      "path": "frame0.png",
      "label": "Opening frame at 0:00",
      "timestamp_seconds": 0.0
    }
  ],
  "output_schema": {
    "primary_label": "string",
    "confidence": "number_0_to_1",
    "secondary_labels": "string[]",
    "timeline": "string[]",
    "evidence": "string[]",
    "valence": "string_optional",
    "arousal": "string_optional",
    "notes": "string_optional"
  }
}
```

The sidecar can respond either with those fields at the top level or under a top-level `result` object. The addon maps that into a professional report with:

- primary label and confidence
- secondary labels
- evidence bullets
- timeline bullets
- optional `valence` / `arousal` for emotion-style models
- optional free-form notes

Use it when an app wants practical local video understanding today, but still wants a clean path to stronger temporal backends later.

## Video Planning Helpers

`ofxGgmlVideoPlanner` adds a higher-level planning layer on top of text, vision, and video analysis. It can build single-scene beat plans, multi-scene generation plans, and AI-assisted editing briefs from a plain-language goal.

The planner currently supports:

- beat-oriented prompt planning for one clip
- recurring entities and scene continuity across multi-scene plans
- selected-scene versus full-sequence prompt assembly
- AI-assisted edit-plan generation with structured actions, durations, and editorial notes
- editor-workflow generation that turns edit plans into actionable next steps and direct handoff prompts for Vision, Write, Diffusion, Custom, and Montage flows
- lightweight workflow-state support in the GUI example, including active-step focus, done/undone tracking, and session restore for longer editing passes
- editor presets in the GUI example for `Trailer`, `Montage`, `Recap`, `Music Video`, `Social Short`, and `Product Teaser`, so common edit goals can prefill clip count, target duration, and grounding strategy in one click

The GUI example exposes that planner directly in the Vision / Video area with:

- `Plan Video` for beat planning
- `Plan Multi-Scene` for scene scripts and continuity-aware prompts
- `Plan Edit` for AI-assisted editing briefs
- handoff actions into Write and Diffusion for scene-level reuse

Use it when an app wants an LLM to structure video generation or editing before handing work off to a diffusion backend, a multimodal model, or a human editor.

## Montage Helpers

`ofxGgmlMontagePlanner` turns subtitle or transcript cues into a ranked montage plan. It can parse SRT-style cues, score them against a montage goal, keep the result in timeline order, and export a CMX-style EDL alongside a human-readable editor brief.

The planner supports:
- **Drop-frame timecode** for NTSC 29.97fps workflows
- **Audio track export** for combined video/audio EDL files
- **Source file path references** for media management
- **Transition metadata** including duration and suggestions
- **Custom audio track routing** per clip

For detailed EDL export documentation, see `docs/EDL_EXPORT.md`.

The GUI example includes a `Subtitle montage automat` workflow with:

- local `.srt` selection or reuse of speech-generated subtitle output
- montage-goal scoring
- ranked clip suggestions
- inline editor brief preview
- `Copy EDL` for timeline handoff
- `Export active SRT` / `Export active VTT` for subtitle-slave handoff into `ofxVlc4`
- optional direct `ofxVlc4` preview when the example is regenerated with the companion addon available

Use it when an app wants transcript-driven rough-cut planning without building a full NLE integration first.

## Image Search Helpers

`ofxGgmlImageSearch` provides a lightweight provider-agnostic interface for internet image search. The addon currently ships with a working Wikimedia Commons backend, and the example uses it for prompt-driven reference gathering in both Vision and Diffusion workflows.

The GUI example can:

- search for internet reference images from a prompt
- reuse the current Vision or Diffusion prompt as the search query
- preview a selected result
- cache a chosen result locally for Vision image analysis or Diffusion init-image reuse

Use it when an app wants lightweight reference gathering without hardwiring a specific commercial image-search API into the rest of the workflow.

## Web Crawler Helpers

`ofxGgmlWebCrawler` provides a lightweight optional bridge for website ingestion. The default backend wraps the external `Mojo` CLI crawler, runs a local crawl, and normalizes discovered Markdown files into structured addon result objects.

On Windows, the recommended local setup is the bundled installer script:

`scripts/install-mojo.ps1`

That script installs Mojo into a local WSL-backed environment under `%LOCALAPPDATA%\ofxGgml\mojo\project` and creates a small Windows launcher at `libs/mojo/bin/mojo.bat`, which the crawler discovers automatically without leaving the full WSL venv inside the addon tree.

The default `Mojo` adapter currently supports:

- start URL
- crawl depth
- optional JavaScript rendering
- explicit output directory or temporary crawl directories
- normalized command output and discovered Markdown documents
- canonical source URL recovery from markdown metadata/frontmatter when available, with fallback to the crawl start URL
- `allowedDomains` enforcement on the requested start URL plus post-crawl filtering of normalized documents

Use it when an app wants to turn a website into local Markdown/documents for later embedding, retrieval, citation scraping, or RAG-style prompt grounding without coupling the addon to one crawler implementation.

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
