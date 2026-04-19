# Changelog

All notable changes to `ofxGgml` are documented in this file.

## [Unreleased]

### Added
- The GUI example AceStep panel now includes an explicit server setup check plus a one-click install-command helper, making it clearer that prompt/ABC workflows stay local-first while rendered audio requires an external AceStep server.
- `scripts/install-acestep.ps1` and `scripts/install-acestep.bat` as addon-local helpers for checking out and building AceStep into `libs/acestep/`.
- The AceStep installer now validates external command failures properly, auto-detects or falls back across common branch names, initializes required submodules such as `ggml`, and reports checkout-layout issues more clearly when upstream changes break the expected build root.
- `ofxGgmlCodeAssistantSession`, typed assistant tool definitions/calls, and streamed assistant-event callbacks so apps can keep lightweight coding-session state without rebuilding the whole GUI example workflow.
- `ofxGgmlCodeAssistant::runWithSession(...)` as a higher-level coding-assistant entry point with approval callbacks for risky tool proposals such as patch application and verification commands.
- `ofxGgmlCodingAgent` as a new orchestration layer that keeps coding-session memory, supports a read-only `Plan` mode, and can hand structured edits into workspace apply/verify flows without rebuilding that glue in apps.
- `ofxGgmlMusicGenerator` as a new local-first music helper for reusable music-prompt generation, ABC notation sketch generation, notation sanitization/validation, file saving, and future pluggable rendered-audio backends.
- `ofxGgmlEasy` now also exposes music-prompt generation, `Image -> Music` prompt generation, and ABC notation helpers through the same high-level facade used by text, speech, citation, montage, and MilkDrop workflows.
- `ofxGgmlVideoEssayWorkflow` as a new citation-grounded wrapper for `topic -> outline -> narrated script -> voice cues -> SRT`, built from the existing citation-search and text-assistant layers instead of a separate backend stack.
- `ofxGgmlHoloscanBridge` as an optional live `frame -> vision -> preview/result` bridge, plus a small Vision-panel example lane for feeding frames into the current vision stack.
  - The native Holoscan runtime path is Linux-only for now; other platforms stay on the addon fallback lane until that runtime is validated there.
- The GUI example now includes a dedicated `Easy` mode that exercises the high-level `ofxGgmlEasy` facade directly for chat, summarize, translate, citation search, `Video Essay`, and coding-agent planning flows against the current backend/model selection.
- `ofxGgmlVideoEssayWorkflow` now also exposes request validation plus a reusable JSON manifest for downstream render/export tools, so source-backed essay planning can travel more cleanly into `ofxVlc4`, diffusion, or external clip renderers.

### Changed
- `ofxGgmlCodeAssistant` now derives a first-pass tool plan from structured results, emits prompt/chunk/tool/approval/completion events during runs, and can seed/update task memory directly from a reusable session object.
- Assistant test coverage now includes session seeding, streamed events, tool proposal generation, and approval gating for risky coding actions.
- The GUI example Script mode now exposes the newer coding-assistant runtime directly through streamed workflow events, explicit approve/deny handling for risky tool proposals, `Build` / `Plan` agent switching, IBM-style `@` references including broad read-oriented `@general`, quick intent chips, and a separate inline-completion lane.
- The GUI example Vision panel now also exposes a dedicated `Music Video` workflow section that reuses the shared `Music -> Image` state, applies music-video planning defaults, and hands the generated visual concept directly into video planning, diffusion, and edit-plan generation.
- `ofxGgmlVideoPlanner` now also supports music-video-aware section planning, with optional intro / verse / chorus / bridge style sections, section-level cut-density hints, and section summaries that carry through prompt generation and plan review.
- The GUI example Vision panel now includes an `Image / Prompt -> Music` workflow that can turn scene descriptions or existing text outputs into reusable music-generation prompts and local `.abc` sketch files, complementing the earlier Diffusion-side `Music -> Image` helper.
- Headless test coverage now also covers the new media-prompt and music-generator helpers so the local-first cross-media workflow stays buildable outside the GUI example.
  - `ofxGgmlVideoEssayWorkflow` now also derives a reusable visual concept plus shared `ofxGgmlVideoPlanner` scene/edit outputs, so the essay pipeline can hand off into Vision, Diffusion, and editorial planning without duplicating prompt assembly in the example.
  - The GUI example now includes a dedicated `Video Essay` mode that can turn a topic plus loaded URLs or a crawler seed into source-backed research notes, a cited outline, a spoken script, voice cues, an SRT-ready cue sheet, and Phase 3 visual/scene/edit handoff output with one-click TTS, Vision, Diffusion, and Write reuse.
  - `Video Essay` can now also reuse the existing optional `ofxVlc4` lane in the GUI example for source-video subtitle preview plus texture-recorded render export, including muxing the generated narration track into the final essay video when voiceover audio is available.
  - Montage now also exposes a clip-playlist export lane in the GUI example, including JSON manifest export, optional `ofxVlc4` playlist preview, and texture-recorded playlist renders with optional external-audio mux.
  - Montage clip-playlist export can now auto-collect generated video outputs such as essay renders or previous playlist renders, merge them into the current clip list, and defer recording until the `ofxVlc4` preview texture is actually ready.
  - `ofxGgmlInference::generateBatch(...)` now honors `allowParallelProcessing` directly and uses a small worker-pool style server-batch fallback instead of repeatedly spawning per-chunk thread bursts.
  - Chat and Translate now share the same lightweight TTS-preview pattern in the GUI example, with dedicated inline playback controls and a small shared preview/request state instead of duplicated per-mode wiring.

## [1.0.4] - 2026-04-19

### Added
- `docs/OFXIMGUI_ASSISTANT_SPEC.md` as a concrete design spec for a specialized `ofxImGui` / openFrameworks GUI coding assistant, including retrieval rules, prompt contract, UI-specific review heuristics, and a staged integration path on top of the existing assistant stack.
- Repo-level `.clang-tidy` defaults for addon and GUI-example sources, tuned for practical `bugprone`, `performance`, `portability`, `readability`, and `modernize` coverage without enabling the noisiest style-only checks.
- `scripts/run-clang-tidy.ps1` for Windows / Visual Studio analysis runs, with compile-database preference and an optional MSBuild Clang-Tidy fallback.
- `scripts/run-clang-tidy.sh` for Linux/macOS compile-database workflows.
- `docs/CLANG_TIDY.md` to document supported workflows, compile-database discovery, and recommended scope.
- `ofxGgmlEasy` as a new high-level facade for common text, chat, translation, vision, and speech tasks, with short `configure...()` entry points and convenience methods such as `summarize()`, `chat()`, `describeImage()`, and `transcribeAudio()`.
- `ofxGgmlEasy` now also exposes higher-level helpers for website crawling, citation extraction, subtitle montage planning/export, and AI-assisted video-edit planning/workflow generation.
- `ofxGgmlEasy` now initializes its default crawler / speech backends more predictably, documents the newer workflow helpers directly in the README, and avoids mutating internal citation-search state through a `const_cast` during `findCitations()`.
- `ofxGgmlMilkDropGenerator` as a new text-backend-driven helper for generating, editing, sanitizing, and saving MilkDrop / projectM preset files.
- `ofxGgmlMilkDropGenerator` now also exposes basic preset validation, conservative repair flows, and multi-variant generation for faster prompt iteration.
- `ofxGgmlEasy` now also exposes MilkDrop helpers so apps can generate, edit, validate, repair, save, and generate preset variants without wiring the lower-level generator directly.
- `ofxGgmlMediaPromptGenerator` as a local-first cross-media prompt helper, starting with `Music -> Image` prompt generation from music captions plus optional lyrics or transcripts.
- Headless test coverage for the montage preview/export bridge, including bundle assembly, cue lookup, SRT/VTT text generation, and file export.
- `ofxGgmlMontagePreviewBridge` as a new playback-facing bridge API for source-timed versus montage-timed subtitle tracks, cue lookup by time, and playlist-oriented montage preview bundles that can be consumed by companions such as `ofxVlc4`.
- GUI-example montage subtitle preview/export updates, including generated montage-timed and source-timed subtitle tracks, inline cue preview, live preview playback text, and one-click SRT/VTT copying.
- The GUI example montage workflow now exposes an optional direct `ofxVlc4` preview path when regenerated with `ofxVlc4` in `addons.make`, including subtitle-slave loading plus subtitle delay / scale controls for the active preview track.
- The GUI example `ofxVlc4` montage preview now reloads when preview timing changes, can jump directly to selected subtitle cues, and can sync individual video-edit workflow steps into the live preview.
- `ofxGgmlWebCrawler` as a new optional website-ingestion bridge, with a default `Mojo` CLI adapter for local website-to-Markdown crawling and normalized crawled-document results.
- `ofxGgmlCitationSearch` as a new source-grounded citation helper that can extract structured quote lists and cited summaries from either loaded URLs or crawler-ingested website documents.
- `ofxGgmlVideoPlanner` now also emits a lightweight editor-workflow layer with actionable next steps, preview hints, and direct handoff targets for existing modes such as Vision, Write, Diffusion, Custom, and Montage.

### Changed
- `.gitignore` now ignores generated `compile_commands.json` files so local clang-tidy workflows do not pollute the worktree.
- Citation-search source collection now normalizes and deduplicates explicit URLs plus crawler-returned markdown documents before extraction, reducing repeated citations and empty-source noise.
- `ofxGgmlEasy` now routes `findCitations()` and `crawlWebsite()` through the same owned crawler/citation helpers that `configureText()`, `configureWebCrawler()`, `getWebCrawler()`, and `getCitationSearch()` expose, so facade-level configuration stays consistent across calls.
- The default `Mojo` crawler adapter now preserves canonical source URLs from markdown metadata when possible, falls back to the crawl start URL when needed, validates the requested start URL against `allowedDomains`, and filters normalized crawl results by the same domain policy.
- The GUI example and helper scripts now prefer a shared addon-level `models/` folder instead of per-example `bin/data/models` copies, with `bin/data/models` kept as a standalone-app fallback.
- On Windows, the GUI example now registers central runtime directories such as `libs/llama/bin`, `libs/whisper/bin`, `libs/chatllm/bin`, and the built `ggml` output folders as DLL search paths at startup, reducing duplicate development-time DLL copies in `bin/`.
- Prompt-echo cleanup in `ofxGgmlInference` is more conservative now, so legitimate greeting-style replies such as `hello! ...` are preserved instead of being clipped as false positive prompt echoes.
- Translate mode in the GUI example is more usable: source language can be set to `Auto detect`, prompt-copy buttons now update reliably through deferred ImGui buffer writes, and the panel exposes clearer `Detect Language`, `Natural`, `Literal`, and `Detect + Translate` actions.
- Summarize mode in the GUI example now includes a dedicated citation-research workflow with topic input, optional crawler seed URL, structured quote preview, and direct handoff into Write mode.
- The GUI example video-editing section now turns structured edit plans into clickable workflow steps, so Frame-style `analyze -> plan -> handoff` flows can jump directly into the already available Vision, Write, Diffusion, Custom, and Montage tools.
- The GUI example video-editing workflow now keeps an active step, done/undone state, quick `Open next step` actions, and session persistence so longer editing passes can be resumed instead of re-triaged.
- The GUI example video-editing workflow now also includes reusable editor presets such as `Trailer`, `Montage`, `Recap`, `Music Video`, `Social Short`, and `Product Teaser`, with one-click defaults for edit goal, clip count, target duration, and grounding.
- The GUI example now includes a dedicated MilkDrop mode for prompt-driven `.milk` preset generation, save/open actions, validation/repair, quick variants, and optional live preview through `ofxProjectM` with beat-sensitivity, preset-duration, and microphone-reactive preview controls when that addon is available.
- The GUI example Diffusion panel now includes a `Music -> Image` helper that can prepare a visual prompt from a music description and optional speech/lyric text, then hand it directly into the existing diffusion prompt field.
- The GUI example text-heavy modes are now split into a dedicated `TextModes.cpp` translation unit, reducing `ofApp.cpp` concentration and making mode-level maintenance easier.
- The GUI example speech and TTS flow now lives in a dedicated `SpeechTts.cpp` translation unit, further reducing `ofApp.cpp` concentration and keeping microphone, Whisper, and `chatllm.cpp` UI/runtime glue in one place.
- GUI-example session persistence now lives in its own `SessionPersistence.cpp` translation unit, and stale disabled text-mode copies have been removed from `ofApp.cpp` so the remaining example shell is easier to navigate.
- The headless test harness now tracks newer split inference sources and includes the OF-style time helpers needed by logging and cleanup code, so local test runs stay aligned with the current addon structure.
- The GUI example now derives its local mode-count constant from the shared model-preset configuration, preventing future mode-array drift when new modes are added.

## [1.0.3] - 2026-04-18

### Changed
- **BREAKING**: `ofxGgml::setup()`, `ofxGgml::allocGraph()`, and `ofxGgml::loadModelWeights()` now return `Result<void>` instead of `bool` for richer error reporting. Users must update their code from `if (ggml.setup())` to `auto result = ggml.setup(); if (result.isOk())`. Error details are available via `result.error().code`, `result.error().message`, and `result.error().toString()`.

### Added
- **Quick Wins**: Four high-impact, low-effort features for improved developer experience:
  - `ofxGgmlStreamingContext` for streaming API with backpressure control, pause/resume/cancel capabilities, and flow control
  - `ofxGgmlLogger` for comprehensive configurable logging with multiple levels (Trace/Debug/Info/Warn/Error/Critical), console/file output, and custom callbacks
  - `ofxGgmlMetrics` for performance tracking including tokens/sec, cache hit rates, memory usage, custom counters/gauges, and timing histograms
  - `ofxGgmlModelRegistry` for model version management and hot-swapping with rich metadata tracking (architecture, quantization, parameters, etc.)
  - `ofxGgmlPromptTemplates` library with 30+ built-in templates for common tasks (summarize, code review, Q&A, translation, etc.) and variable substitution support
- `ofxGgmlInference` live grounding now groups specialized sources under domain providers and keeps generic search as a separate fallback path.
- `ofxGgmlClipInference` as a new bridge scaffold for optional CLIP-style text/image embedding and ranking backends, now with a generic bridge surface plus an optional `clip.cpp` adapter path and compatibility helpers for older `ofxStableDiffusion`-style naming.
- `ofxGgmlDiffusionInference` as a new bridge scaffold for optional image-generation backends, including a callback-friendly `ofxStableDiffusion` adapter surface.
- The diffusion bridge now carries structured image tasks for `InstructImage`, `Variation`, and `Restyle`, plus batch-selection modes for `KeepOrder`, `Rerank`, and `BestOnly`.
- Generated-image metadata now keeps selection/ranking fields such as `sourceIndex`, selected-best state, score, scorer, and score-summary text so CLIP-aware callers can reason about best-of-N runs directly from addon result objects.
- `ofxGgmlTtsInference` as a new bridge scaffold for optional text-to-speech backends, now with a `chatllm.cpp` adapter path for OuteTTS-style models, task-oriented request/result types, and speaker-profile handling.
- The GUI example TTS mode now defaults its executable field to the addon-local `libs/chatllm/bin` runtime path and uses a cleaner one-backend-per-request flow.
- The GUI example Diffusion mode now adds `ofxStableDiffusion` through `addons.make`, auto-attaches a local `ofxStableDiffusion` engine when present, and includes quicker cross-panel handoff actions for Vision, Diffusion, and CLIP workflows.
- Script mode now supports higher-level slash commands and quick actions such as `/review`, `/reviewfix`, `/nextedit`, `/summary`, and `Change Summary`.
- Text-focused GUI modes now expose additional professional one-click actions, including executive briefs, action items, meeting notes, email replies, release notes, commit messages, and structured JSON replies.
- The Speech panel now supports temporary microphone capture, including `Start Mic Recording`, `Stop + Run`, and `Use Last Recording` for direct transcribe / translate workflows from the default input device.
- Speech inference now supports an optional OpenAI-compatible speech-server backend for `/v1/audio/transcriptions` and `/v1/audio/translations`, with automatic `whisper-cli` fallback when the server path fails.
- Windows helpers now cover `whisper-server` too, including `scripts/build-whisper-server.ps1` and `scripts/start-whisper-server.ps1` for a local warm speech backend on port `8081`.
- Local `whisper.cpp` runtime detection now covers both `whisper-cli` and `whisper-server`, and the Windows build helper installs both binaries into `libs/whisper/bin` so the GUI's CLI fallback and managed server share the same upstream runtime.
- `llama.cpp` runtime helpers now install `llama-server`, `llama-completion`, and `llama-cli` together into `libs/llama/bin` on Windows and Linux/macOS, so the text server path and addon-local CLI fallback can share one local upstream runtime in the same way as Whisper.
- The Vision panel now includes quick actions such as `Scene Describe`, `Screenshot Review`, and `Document OCR`, plus an optional sampled-video path that reuses the stable multimodal server backend directly from the GUI.
- Video `Action` and `Emotion` tasks can now optionally call a temporal sidecar service, with structured request/response plumbing and dedicated Vision-panel presets for those workflows.
- `ofxGgmlLiveSpeechTranscriber` as an addon-level rolling Whisper helper for near-realtime microphone transcription with chunking, overlap, temp WAV handling, and live transcript state.
- `ofxGgmlVideoPlanner` as a reusable planning layer for beat-based video prompts, multi-scene sequence plans, and AI-assisted edit briefs.
- `ofxGgmlMontagePlanner` for subtitle-driven montage planning, ranked clip selection, editor briefs, and CMX-style EDL export.
- `ofxGgmlImageSearch` as a provider-agnostic internet image-search layer, currently shipping with a Wikimedia Commons backend and example-side prompt reuse / local caching.

### Changed
- `ofxGgmlGuiExample` replaces the old online/offline toggle with four `Live context` policies: `Offline`, `LoadedSourcesOnly`, `LiveContext`, and `LiveContextStrictCitations`.
- GUI live-source controls now use the more general `Live context` / `sources` wording instead of mixed `online` / `realtime` labels.
- The AI Studio diffusion panel now exposes the newer bridge surface too, including instruct-image prompting, variation/restyle task selection, CLIP-oriented rerank modes, ranking prompts, and richer generated-image summaries.
- Local workspaces now keep `.github` available during script-source scans so repository instruction files can shape assistant and review prompts.
- `ofxGgmlCodeAssistant` and `ofxGgmlCodeReview` now read local `AGENTS.md` and `.github` instruction files directly from the workspace for server-first coding and review flows.
- Setup scripts now follow the faster server-first path by default: `ggml` still builds automatically, while the local `llama.cpp` runtime remains opt-in via `--with-llama-cli` instead of being built on every `--auto` setup.
- The GUI now presents `llama-server` as the recommended text backend and frames `llama-completion` as an optional local fallback instead of a required default component.
- Server-backed text modes now auto-apply the old low-latency tuning defaults, auto-start the local server during app setup when the configured URL is local, and remove the manual `Check Server` / `Start Local Server` / `Stop Local Server` / `Tune For Server` buttons from the GUI.
- The Speech panel now defaults to a local speech-server URL on `http://127.0.0.1:8081`, prefers a warm speech server first, and can auto-start a managed local `whisper-server` during setup when the configured URL is local.
- Whisper timestamp handling now preserves `.srt` / `.vtt` artifacts, surfaces parsed segments in the GUI, and reuses the same lightweight SRT parsing approach we already trust in `ofxVlc4`.
- Vision response handling now accepts more OpenAI-compatible response shapes, adds stronger task-specific prompting, and labels multimodal image parts more explicitly for better grounding.
- Vision profile download hints can now use explicit direct URLs, and the default `LFM2.5-VL` GUI action now targets the correct `LFM2.5-VL-1.6B-Q4_0.gguf` file instead of a broken guessed link.
- Video analysis now uses more structured sampled-frame prompts with frame-position labels, sample-count context, and clearer timeline guidance.
- Streamed text generation now treats server chunks as deltas consistently across the inference and GUI layers, so live `llama-server` output no longer duplicates partial prefixes in Chat or Script mode.
- The GUI example now adds inline image and video previews across Vision, Diffusion, and analyzed-video flows, plus cleaner cross-panel reuse for Vision, Diffusion, CLIP, and internet image search.
- Diffusion workflows now use the real `ofxStableDiffusion` runtime path when present, clamp width / height to the same known-good size set used by the companion example, and surface clearer disable reasons in the GUI.
- Vision / Video planning now supports selected-scene versus full-sequence generation modes, richer continuity context per scene, and AI-assisted edit-plan handoff into Write mode.
- Subtitle montage planning now extracts goal keywords once per plan, sanitizes EDL titles before export, and exposes the editor brief inline in the GUI.
- `ofxGgmlInference` batch processing now preserves request order without mutex-heavy result appends, and server-backed `embedBatch()` now performs bounded concurrent requests instead of a purely sequential loop.
- The Script panel now exposes clearer assistant context, smarter suggested next steps, and stronger follow-up grounding by reusing recent touched files, cached verification commands, and the last failure reason directly in the coding assistant flow.
- Inference internals are further split so server probing, source grounding, and text cleanup live behind dedicated internal seams instead of one monolithic translation unit.
- `ofxGgmlCodeAssistant` and `ofxGgmlWorkspaceAssistant` now keep lightweight task memory for active mode, selected backend, recent files, and last failure reason; bias retrieval toward recently touched files; ask for a clearer inspect -> patch -> verify loop; and perform a structured-output recovery pass when a model returns weak freeform prose instead of the requested tagged format.

### Documentation
- `README.md` now documents the `Live context` policies, server-first mode actions, script slash commands, repository instruction-file support, the new optional CLI build behavior, the microphone-driven speech workflow, the optional speech-server backend, the shared local `whisper.cpp` runtime helper path, the upgraded vision / sampled-video workflows, the video-planning and montage helpers, internet image search, and the optional temporal sidecar contract for video action / emotion analysis.
- Added `docs/COMPATIBILITY.md` to document the recommended `ggml` / `stable-diffusion.cpp` versioning policy, companion-addon runtime packaging rules, and the tested-matrix workflow for `ofxGgml` plus `ofxStableDiffusion`.

## [1.0.2] - 2026-04-16

### Added
- Windows helpers to build and launch a local `llama-server`, including `scripts/build-llama-server.ps1`, `scripts/start-llama-server.ps1`, and GUI controls to check, start, and stop a managed local server.
- Shared server probing in `ofxGgmlInference` with normalized base URLs, model discovery via `/v1/models`, capability summaries, and server-backed embeddings support through `/v1/embeddings`.
- Per-mode text-backend preferences in `ofxGgmlGuiExample` so chat/script/custom flows can stay on the persistent server path while other text tasks remain switchable.

### Changed
- `ofxGgmlInference` now supports persistent `llama-server` generation and embeddings, automatic active-model resolution, and local embedding fallback when a server embedding request fails.
- `ofxGgmlGuiExample` now treats `llama-server` as a first-class text backend with server reachability feedback, capability hints, managed local startup, and per-mode backend persistence.
- Hierarchical code review now uses improved semantic and lexical ranking, stronger low-signal filtering, and more professional fallback summaries for tiny files, project files, and code-fragment summaries.

### Fixed
- Review generation no longer reports misleading blank-pass success for empty server responses and now preserves real server transmission and HTTP failures in the UI.
- Review summary cleanup now rejects incomplete call fragments such as `ofRunApp(window,` and generic placeholders like `Project file included in the hierarchical review.`.

### Documentation
- `README.md` now documents the `llama-server` build/start workflow, server-backed text generation, and the GUI example's mode-coupled text backend behavior.

## [1.0.1] - 2026-04-16

### Added
- `ofxGgmlSpeechInference` with a pluggable speech backend interface, Whisper CLI integration, recommended Whisper model profiles, and GUI-example speech transcription / translation support.
- `ofxGgmlVisionInference` with OpenAI-compatible multimodal request assembly for `llama-server`-style vision models and GUI-example image workflows.
- `ofxGgmlVideoInference` with backend-based video understanding, including a sampled-frames default backend and addon-level tests.
- `ofxGgmlCodeAssistant` structured task results with file intents, patch operations, verification commands, risks, and follow-up questions.
- `ofxGgmlWorkspaceAssistant` as a public addon module for patch application, verification loops, and retry-driven coding workflows.
- Symbol-aware retrieval in `ofxGgmlCodeAssistant` so coding prompts can surface relevant definitions and references from `ofxGgmlScriptSource`.
- Assistant eval coverage for retrieval ranking, dry-run safety, structured workspace execution, and verification retries.
- Review findings with structured `priority`, `confidence`, `file`, `line`, and fix suggestions in `ofxGgmlCodeAssistant`.
- Specialized assistant modes for constrained `Edit`, invariant-aware `Refactor`, `FixBuild`, and grounded web/doc requests.
- Unified diff output support in structured code-assistant responses, plus workspace diff previews.
- Semantic index building in `ofxGgmlCodeAssistant` for caller-aware symbol lookup across script-source documents.
- Cursor-aware inline completion helpers in `ofxGgmlCodeAssistant` for editor-style coding assistance.
- Compiler-output parsing helpers in `ofxGgmlCodeAssistant` for `FixBuild` workflows driven by raw MSVC, Clang, or GCC errors.
- Transaction and rollback support in `ofxGgmlWorkspaceAssistant`, including backup capture and unified-diff previews.
- Automatic verification command suggestion in `ofxGgmlWorkspaceAssistant` based on changed files and available test targets.
- Compile-database-aware semantic retrieval in `ofxGgmlCodeAssistant`, including symbol ranges, qualified names, and caller metadata for local workspaces that expose `compile_commands.json`.
- Unified-diff parsing and hunk-based apply support in `ofxGgmlWorkspaceAssistant`, with drift-aware validation before edits are written.
- Spec-to-code workflow helpers in `ofxGgmlCodeAssistant`, including acceptance criteria, synthesized test suggestions, reviewer-simulation passes, and patch risk scoring.
- Semantic code-map generation in `ofxGgmlCodeAssistant` for plan-first coding flows and repo-aware prompt assembly.
- Shadow-workspace execution in `ofxGgmlWorkspaceAssistant` so edits can be verified safely before syncing back to the original workspace.

### Changed
- `ofxGgmlGuiExample` now treats speech and vision as first-class addon-backed modes instead of keeping those flows buried inside ad-hoc UI logic.
- Speech workflows can now carry an explicit Whisper model path instead of relying only on a backend executable.
- Structured command parsing is now more tolerant of partially degraded assistant output, which makes Windows-based scripted test and tooling flows more robust.
- C++ symbol extraction now recognizes scoped definitions such as `Type::method()` more reliably, improving retrieval quality for real codebases.
- Symbol-aware context building can now expose likely callers and related references instead of only top matching declarations.
- Workspace patch application can now enforce an allow-list of editable files for constrained edit workflows.
- Build-fix execution can now derive editable files from compiler output and reuse the same verification/retry loop as other workspace tasks.
- Workspace patch application now validates replacement operations before apply and can roll back automatically after failed verification.
- Inline completion prompting now supports a fill-in-the-middle style cursor format for editor integrations.
- Structured coding flows now surface test ideas, reviewer-simulation findings, and risk metadata as first-class addon result types instead of leaving that logic to the GUI layer.

### Documentation
- `README.md` now documents speech, vision, and video helpers together with the GUI example's multimodal workflows and the separation between text-model downloads vs. Whisper / vision runtimes.
- `README.md` now documents semantic symbol retrieval, inline completion, transaction-based workspace editing, and the expanded assistant eval suite as public addon features.
- `README.md` now also documents spec-to-code planning, semantic code maps, and shadow-workspace safe apply as public addon features.

## [1.0.0] - 2026-04-15

### Added
- `ofxGgmlInferenceSettings::autoPromptCache` (default: `true`) to enable automatic per-model prompt-cache path selection when `promptCachePath` is not set.
- Internal token count cache in `ofxGgmlInference::countPromptTokens()` keyed by model path + text hash to reduce repeated tokenizer subprocess calls.
- Chat response language selector in `ofxGgmlGuiExample`.
- `tests/test_project_memory.cpp` with coverage for lifecycle, clamping, and prompt-context behavior.
- `OFXGGML_ENABLE_BENCHMARK_TESTS` CMake option in `tests/CMakeLists.txt` so full functional tests run by default while benchmarks remain opt-in.

### Changed
- Inference generation now uses prompt-cache flags with an auto-derived stable path when enabled.
- Inference executable validation now aligns with process execution semantics: explicit file paths must point to regular files, and command names are accepted when resolvable via `PATH`.
- Nonzero exit handling is now shared across `generate()` and `embed()`: exit `130` is treated as benign, while other nonzero exits only pass when valid output is produced.
- Runtime output cleaning now uses a shared noise-line filter for both warning stripping and leading-noise trimming to reduce drift.
- `ofxGgmlGuiExample` chat internet controls simplified:
  - removed redundant "Use internet context" toggle
  - removed "All modes" toggle from chat panel
  - `Offline mode` is now the primary control for chat internet grounding.
- Session persistence updated for new/removed GUI settings.

### Documentation
- `README.md` updated with inference examples for `autoPromptCache` and token-count caching behavior.
- `OPTIMIZATION_SUMMARY.md` updated with addon-level runtime improvements section.
