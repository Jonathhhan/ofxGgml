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
- `ofxGgmlGuiExample` chat internet controls simplified:
  - removed redundant "Use internet context" toggle
  - removed "All modes" toggle from chat panel
  - `Offline mode` is now the primary control for chat internet grounding.
- Session persistence updated for new/removed GUI settings.

### Documentation
- `README.md` updated with inference examples for `autoPromptCache` and token-count caching behavior.
- `OPTIMIZATION_SUMMARY.md` updated with addon-level runtime improvements section.
