# Changelog

All notable changes to `ofxGgml` are documented in this file.

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
