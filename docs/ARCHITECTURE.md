# Architecture

`main` is now the core rewrite line. The design rule is simple: if a feature is
not needed by most openFrameworks projects that want ggml access, it does not
belong in the default addon.

## Core Responsibilities

- Own and release ggml backends safely.
- Provide small tensor and graph wrappers with explicit lifetimes.
- Surface errors through `ofxGgmlResult<T>` rather than process-level aborts.
- Keep runtime compute status result-like while preserving elapsed-time data in
  `ofxGgmlComputeResult`.
- Keep inference result structs result-like (`isOk()`, `isError()`, bool
  conversion) while preserving their simple data fields.
- Keep public adapter transport result structs similarly status-checkable.
- Prefer openFrameworks-style getter names for public addon API where that makes
  call sites clearer, such as `getBackendName()`.
- Keep binary dependency setup reproducible.
- Provide focused examples that each prove one concept.

## Non-Core Responsibilities

These should live in companion addons or optional layers:

- assistants and coding agents
- RAG, web crawling, citation search
- llama.cpp-specific server/CLI tooling through the planned `ofxGgmlLlama`
- SAM/SAM3 through `ofxGgmlSam`, plus vision, TTS, diffusion
- music, audio analysis, and generation workflows through `ofxGgmlMusic`
- speech recognition, transcription, and voice workflows through `ofxGgmlSpeech`
- video essay, montage, MilkDrop, Holoscan workflows
- large all-in-one GUI experiments

## Public Header Plan

- `ofxGgmlCore.h`: low-level stable foundation
- `ofxGgmlText.h`: small text request/result API with pluggable backends
- `ofxGgmlEmbedding.h`: embedding request/result API and vector helpers
- `ofxGgmlSegmentation.h`: point-prompt segmentation API with optional adapters
- `ofxGgmlSam3.h`: temporary optional SAM3 adapter boundary
- `ofxGgml.h`: default umbrella for core, text, embeddings, and stable optional bridge APIs

## Optional Runtime Layers

The default addon API can expose small optional layers when their boundaries stay
plain C++ and testable. The llama.cpp server tools are installed only through
explicit scripts and are treated as a local runtime, not as a required core
dependency. `ofxGgmlLlama` is the planned future home for that llama-specific
tooling, but the `v1.0.1` workflow stays here until the split can preserve the
current setup path. Chat and embedding examples may use `ofxImGui`, but the
public API must not depend on GUI code.

SAM/SAM3 should live in the companion addon `ofxGgmlSam`. Until that exists,
this repo keeps a small adapter boundary only. Its generated native integration
can be enabled locally, but projects that only include the core, text, or
embedding headers should compile without a SAM checkout. Segmentation callers
should use `ofxGgmlSegmentation.h`; concrete SAM3 code stays behind
`ofxGgmlSam3.h` and `ofxGgmlSam3Adapters`.

## Compatibility

The frozen full implementation is on `legacy-full`. New `main` can make
breaking changes freely until the first rewritten release tag.
