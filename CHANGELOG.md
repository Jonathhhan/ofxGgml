# Changelog

All notable changes to `ofxGgml` are documented in this file.

## [Unreleased]

### Added
- `ofxGgmlInference` live grounding now groups specialized sources under domain providers and keeps generic search as a separate fallback path.
- `ofxGgmlClipInference` as a new bridge scaffold for optional CLIP-style text/image embedding and ranking backends, now with a generic bridge surface plus an optional `clip.cpp` adapter path and compatibility helpers for older `ofxStableDiffusion`-style naming.
- `ofxGgmlDiffusionInference` as a new bridge scaffold for optional image-generation backends, including a callback-friendly `ofxStableDiffusion` adapter surface.
- The diffusion bridge now carries structured image tasks for `InstructImage`, `Variation`, and `Restyle`, plus batch-selection modes for `KeepOrder`, `Rerank`, and `BestOnly`.
- Generated-image metadata now keeps selection/ranking fields such as `sourceIndex`, selected-best state, score, scorer, and score-summary text so CLIP-aware callers can reason about best-of-N runs directly from addon result objects.
- `ofxGgmlTtsInference` as a new bridge scaffold for optional text-to-speech backends, now with a `chatllm.cpp` adapter path for OuteTTS-style models, task-oriented request/result types, and speaker-profile handling.
- The GUI example TTS mode now defaults its executable field to the addon-local `libs/chatllm/bin` runtime path and uses a cleaner one-backend-per-request flow.
- Script mode now supports higher-level slash commands and quick actions such as `/review`, `/reviewfix`, `/nextedit`, `/summary`, and `Change Summary`.
- Text-focused GUI modes now expose additional professional one-click actions, including executive briefs, action items, meeting notes, email replies, release notes, commit messages, and structured JSON replies.
- The Speech panel now supports temporary microphone capture, including `Start Mic Recording`, `Stop + Run`, and `Use Last Recording` for direct transcribe / translate workflows from the default input device.
- Speech inference now supports an optional OpenAI-compatible speech-server backend for `/v1/audio/transcriptions` and `/v1/audio/translations`, with automatic `whisper-cli` fallback when the server path fails.
- Windows helpers now cover `whisper-server` too, including `scripts/build-whisper-server.ps1` and `scripts/start-whisper-server.ps1` for a local warm speech backend on port `8081`.
- Local `whisper.cpp` runtime detection now covers both `whisper-cli` and `whisper-server`, and the Windows build helper installs both binaries into `libs/whisper/bin` so the GUI's CLI fallback and managed server share the same upstream runtime.
- `llama.cpp` runtime helpers now install `llama-server`, `llama-completion`, and `llama-cli` together into `libs/llama/bin` on Windows and Linux/macOS, so the text server path and addon-local CLI fallback can share one local upstream runtime in the same way as Whisper.
- The Vision panel now includes quick actions such as `Scene Describe`, `Screenshot Review`, and `Document OCR`, plus an optional sampled-video path that reuses the stable multimodal server backend directly from the GUI.
- Video `Action` and `Emotion` tasks can now optionally call a temporal sidecar service, with structured request/response plumbing and dedicated Vision-panel presets for those workflows.

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

### Documentation
- `README.md` now documents the `Live context` policies, server-first mode actions, script slash commands, repository instruction-file support, the new optional CLI build behavior, the microphone-driven speech workflow, the optional speech-server backend, the shared local `whisper.cpp` runtime helper path, the upgraded vision / sampled-video workflows, and the optional temporal sidecar contract for video action / emotion analysis.

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
