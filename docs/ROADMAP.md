# Roadmap

This roadmap starts after `v2.0.0-rewrite.0`. The goal is to keep `main`
small, testable, and useful for openFrameworks projects without rebuilding the
old all-in-one framework.

## Next Checkpoint

Target: `v2.0.0-rewrite.1`

Purpose: stabilize the tagged rewrite by improving confidence and ergonomics
without widening the core surface.

## Workstreams

### 1. Public API Stability

- Done after `v2.0.0-rewrite.0`: align `ofxGgmlBackend` with the setup backend
  switches by exposing OpenCL and wiring Metal/OpenCL initialization when built.
- Done after `v2.0.0-rewrite.0`: add `ofxGgmlGetBackendName()` so examples and
  downstream projects can use one stable backend label spelling.
- Done after `v2.0.0-rewrite.0`: add result-style helpers to
  `ofxGgmlComputeResult` without changing the runtime compute return type.
- Done after `v2.0.0-rewrite.0`: split optional SAM3 adapter includes into
  `ofxGgmlSam3.h` so `ofxGgmlSegmentation.h` stays backend-neutral.
- Done after `v2.0.0-rewrite.0`: add result-style helpers to text, embedding,
  and segmentation result structs.
- Done after `v2.0.0-rewrite.0`: add status helpers to public llama CLI/server
  adapter helper result structs.
- Done after `v2.0.0-rewrite.0`: move example prompt/output logging to `ofLog`
  and model result checks with the bool-style helpers.
- Done after `v2.0.0-rewrite.0`: add standalone compile coverage for every
  public umbrella header.
- Done after `v2.0.0-rewrite.0`: remove `m_` private member prefixes from the
  inference layer to better match openFrameworks implementation style.
- Done after `v2.0.0-rewrite.0`: rename public backend label accessors to
  `getBackendName()` for openFrameworks-style getter naming.
- Done after `v2.0.0-rewrite.0`: rename the free backend label helper to
  `ofxGgmlGetBackendName()` for openFrameworks-style helper naming.
- Done after `v2.0.0-rewrite.0`: update tests to assert result values through
  bool-style helpers instead of direct `.success` reads where possible.
- Done after `v2.0.0-rewrite.0`: update the core contract and architecture
  notes to describe the current OF-style public backend naming.
- Done after `v2.0.0-rewrite.0`: rename CPU/CUDA backend enum values to keep
  acronym spelling consistent with OpenCL.
- Review every public header for naming consistency and avoidable churn.
- Add focused tests before changing any public type.
- Keep `ofxGgml.h` as a small umbrella; avoid workflow-specific includes.
- Document every intentional breaking change in `docs/RELEASE_NOTES.md`.

### 2. Build And Backend Confidence

- Keep CPU as the required baseline.
- Add explicit smoke notes for CUDA and Vulkan machines when those paths are
  exercised locally.
- Keep `-Auto` as the default setup behavior and preserve early SDK failures for
  explicit backend switches.
- Avoid committing generated ggml, llama.cpp, SAM3, model, or project artifacts.

### 3. Examples

- Keep examples focused: simple runtime, text, chat, embeddings.
- Improve example UX only when it clarifies the focused workflow.
- Do not add an all-in-one example to core.
- Keep `ofxImGui` usage optional to examples, not to the public addon API.

### 4. Segmentation Boundary

- Keep SAM3 behind optional scripts and adapter hooks.
- Add a real openFrameworks segmentation example only after there is a known
  compatible model and repeatable sample image workflow.
- Keep `scripts\test-sam3-smoke.*` as the gate until then.

### 5. Companion Addons

These should not enter core by default:

- assistants and coding workflows
- RAG, web crawling, citations, or project memory
- speech, TTS, diffusion, CLIP, YOLO, broad vision workflows
- montage, music, video, or product-level GUI workflows

If one becomes important, start it as a companion addon that depends on
`ofxGgml` instead of expanding the core contract.

## Release Rule

Before each rewrite checkpoint:

- run `scripts\validate-local.ps1`
- build changed examples explicitly
- update `docs\RELEASE_NOTES.md`
- check `git status --ignored` for generated artifacts
- keep `docs\CORE_CONTRACT.md` current with the next milestone
