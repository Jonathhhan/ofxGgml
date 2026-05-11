# Roadmap

This roadmap tracks the `1.0.1` release line. The goal is to keep `main` small,
testable, and useful for openFrameworks projects without rebuilding the old
all-in-one framework.

## Next Checkpoint

Target: `1.0.1`

Purpose: ship the rewritten addon as a narrow, stable starting point with the
current confidence and ergonomics work included.

Status: ready for a final `1.0.1` confidence pass. Do not widen the public API
before this release unless a blocking validation issue requires it.

## Workstreams

### 1. Public API Stability

- Done for `1.0.0`: align `ofxGgmlBackend` with the setup backend
  switches by exposing OpenCL and wiring Metal/OpenCL initialization when built.
- Done for `1.0.0`: add `ofxGgmlGetBackendName()` so examples and
  downstream projects can use one stable backend label spelling.
- Done for `1.0.0`: add result-style helpers to
  `ofxGgmlComputeResult` without changing the runtime compute return type.
- Done for `1.0.0`: split optional SAM3 adapter includes into
  `ofxGgmlSam3.h` so `ofxGgmlSegmentation.h` stays backend-neutral.
- Done for `1.0.0`: add result-style helpers to text, embedding,
  and segmentation result structs.
- Done for `1.0.0`: add status helpers to public llama CLI/server
  adapter helper result structs.
- Done for `1.0.0`: move example prompt/output logging to `ofLog`
  and model result checks with the bool-style helpers.
- Done for `1.0.0`: add standalone compile coverage for every
  public umbrella header.
- Done for `1.0.0`: remove `m_` private member prefixes from the
  inference layer to better match openFrameworks implementation style.
- Done for `1.0.0`: rename public backend label accessors to
  `getBackendName()` for openFrameworks-style getter naming.
- Done for `1.0.0`: rename the free backend label helper to
  `ofxGgmlGetBackendName()` for openFrameworks-style helper naming.
- Done for `1.0.0`: update tests to assert result values through
  bool-style helpers instead of direct `.success` reads where possible.
- Done for `1.0.0`: update the core contract and architecture
  notes to describe the current OF-style public backend naming.
- Done for `1.0.0`: rename CPU/CUDA backend enum values to keep
  acronym spelling consistent with OpenCL.
- Done for `1.0.0`: rename tensor shape/storage accessors to
  explicit openFrameworks-style getter names.
- Done for `1.0.0`: rename graph/tensor low-level accessors to
  explicit getter names.
- Done for `1.0.0`: rename `ofxGgmlRuntime::state()` to
  `getState()` for openFrameworks-style getter naming.
- Done for `1.0.0`: rename `ofxGgmlRuntime::listDevices()` to
  `getDevices()` for openFrameworks-style getter naming.
- Done for `1.0.0`: review every public header for naming
  consistency and avoidable churn.
- Ongoing rule: add focused tests before changing any public type.
- Ongoing rule: keep `ofxGgml.h` as a small umbrella; avoid workflow-specific
  includes.
- Ongoing rule: document every intentional breaking change in
  `docs/RELEASE_NOTES.md`.

### 2. Build And Backend Confidence

- Ongoing rule: keep CPU as the required baseline.
- Done for `1.0.0`: add `setup-ggml -DryRun` and smoke coverage
  for setup plan reporting.
- Done for `1.0.0`: add explicit setup dry-run smoke commands and
  notes for CUDA and Vulkan machines.
- Done for `1.0.0`: keep `-Auto` as the default setup behavior
  and add dry-run smoke coverage for early setup option failures.
- Done for `1.0.0`: add generated-artifact hygiene checks to keep
  ggml, llama.cpp, SAM3, model, and generated project outputs out of commits.

### 3. Examples

- Ongoing rule: keep examples focused: simple runtime, text, chat, embeddings.
- Done for `1.0.0`: add launch dry-run coverage for the explicit
  text/chat llama.cpp CLI fallback path.
- Done for `1.0.0`: add launch dry-run coverage for the standalone
  `llama-embedding` runner.
- Ongoing rule: improve example UX only when it clarifies the focused workflow.
- Ongoing rule: do not add an all-in-one example to core.
- Ongoing rule: keep `ofxImGui` usage optional to examples, not to the public
  addon API.

### 4. Segmentation Boundary

- Ongoing rule: keep SAM3 behind optional scripts and adapter hooks.
- Deferred: add a real openFrameworks segmentation example only after there is
  a known compatible model and repeatable sample image workflow.
- Ongoing rule: keep `scripts\test-sam3-smoke.*` as the gate until then.

### 5. Companion Addons

See `docs/COMPANIONS.md` for the split rule and named companion lanes.

These should not enter core by default:

- assistants and coding workflows
- RAG, web crawling, citations, or project memory
- speech, TTS, diffusion, CLIP, YOLO, broad vision workflows
- montage, video, or product-level GUI workflows

Named companion lanes:

- `ofxGgmlSam` for SAM/SAM2/SAM3 image segmentation workflows.
- `ofxGgmlLlama` for llama.cpp server/CLI tooling plus text, chat, and
  embedding examples.
- `ofxGgmlMusic` for music, audio analysis, and generation workflows.
- `ofxGgmlSpeech` for speech recognition, transcription, and voice workflows.

Candidate companion lanes, only when a real project needs them:

- `ofxGgmlVision` for CLIP, image embeddings, captions, and VLM-style image
  understanding.
- `ofxGgmlDiffusion` for image/video diffusion and generative visual models.
- `ofxGgmlRag` for document indexing, retrieval, citations, and project memory.
- `ofxGgmlAgents` for assistants, tool use, and workflow automation.
- `ofxGgmlVideo` for video understanding, montage, and temporal analysis.
- `ofxGgmlUI` for larger optional ImGui tools, model browsers, and prompt
  workbenches.

If one becomes important, start it as a companion addon that depends on
`ofxGgml` instead of expanding the core contract.

Split `ofxGgmlLlama` after `ofxGgmlSam` has one useful point-prompt example.
Until then, keep the stable `v1.0.1` llama.cpp workflow in core so text/chat and
embedding users keep the current clone/setup/run path.

## Release Rule

Before each release checkpoint:

- run `scripts\validate-local.ps1`
- build changed examples explicitly
- update `docs\RELEASE_NOTES.md`
- check `git status --ignored` for generated artifacts
- keep `docs\CORE_CONTRACT.md` current with the next milestone
