# Changelog

All notable changes to `ofxGgml` are documented in this file.

## [Unreleased]

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
