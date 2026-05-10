# Core Contract

This document is the working contract for the `main` rewrite of `ofxGgml`.
It should be updated before large implementation changes, not after the addon
has drifted.

## Purpose

`ofxGgml` core is the narrow foundation for openFrameworks projects that need
local ggml compute. It owns ggml runtime setup, graph execution, model metadata,
and a small C++ API that can be depended on by optional layers.

The core is not a product workflow framework. The frozen `legacy-full` branch is
the archive of the previous broad experiment and should be mined for useful
code, tests, and build lessons.

## Non-Goals

These are not allowed in core by default:

- chat/coding assistants
- RAG, web crawling, citation search, project memory, or workflow manifests
- speech, TTS, vision, diffusion, YOLO, CLIP, SAM, or SAM3 adapters
- video essay, montage, music, MilkDrop, Holoscan, or GUI workflow experiments
- model downloads or large generated binaries committed to the repository

Those features can become companion addons or optional layers that depend on
core.

## Public Surface

The core public surface is:

- `ofxGgmlResult<T>` and `ofxGgmlError`
- `ofxGgmlRuntime`
- `ofxGgmlTensor`
- `ofxGgmlGraph`
- `ofxGgmlModel` and `ofxGgmlModelInfo`
- `ofxGgmlTextGenerator` and text request/result types
- `ofxGgmlLlamaCliTextBackend` and `ofxGgmlLlamaServerTextBackend`
- `ofxGgmlEmbeddingGenerator`, embedding request/result types, and vector helpers
- `ofxGgmlSegmentationInference`, segmentation request/result types, and SAM3 adapter hooks
- `ofxGgmlCore.h`
- `ofxGgmlText.h`
- `ofxGgmlEmbedding.h`
- `ofxGgmlSegmentation.h`
- `ofxGgml.h`

`ofxGgml.h` should remain a boring umbrella. Text belongs there only as a small
backend-neutral API. Embeddings belong there only as a small request/result API
and math helpers. Segmentation belongs there only as a small prompt/result API;
SAM3 remains an optional adapter. Concrete llama.cpp CLI/server adapters must
keep process execution or HTTP transport behind a replaceable boundary, provide
a default runner only when it is small and testable, and carry focused tests.

## Runtime Contract

`ofxGgmlRuntime` owns backend resources. It must:

- be movable but not copyable
- release all backend resources in `close()` and the destructor
- make `setup()` idempotent by closing previous state first
- default to `Auto`, trying built GPU backends before CPU fallback
- report backend setup failures through `ofxGgmlResult<void>`
- avoid process aborts where recovery is practical
- keep async execution out of the first stable core release

CPU support is required. GPU support is optional and must be detected or
explicitly requested by setup scripts.

## Tensor And Graph Contract

`ofxGgmlTensor` is a lightweight handle. It does not own storage.

`ofxGgmlGraph` owns the ggml context used to create graph tensors. It must:

- be movable but not copyable
- keep tensor handles valid only while the graph exists
- reject invalid dimensions before calling ggml where possible
- require an explicit `build()` before allocation or compute
- keep operation names small and conventional: `add`, `mul`, `matmul`, etc.

Data upload and download belong on `ofxGgmlRuntime`, because backend memory is a
runtime concern.

## Model Contract

`ofxGgmlModel` starts as metadata inspection only. Loading model weights into
backend buffers is a later milestone.

The first supported format is GGUF metadata via gguf headers. Missing gguf
support should produce a clear error, not a compile failure for projects that
have not run dependency setup.

## Dependency Contract

Core dependency setup must be reproducible:

- pin ggml to `v0.11.0` before the first rewrite release
- populate `libs/ggml/include`
- populate `libs/ggml/lib`
- do not commit generated ggml binaries by default
- support CPU by default
- keep CUDA, Vulkan, Metal, and OpenCL as explicit setup options
- use auto-detection by default when no backend switch is supplied
- fail before CMake when an explicitly requested backend SDK is unavailable
- provide Windows batch and POSIX shell wrappers for setup
- run Windows CMake builds through Visual Studio's native compiler environment

The setup script should be safe to rerun and should not delete files outside the
addon.

## Testing Contract

Every public type needs a focused test before it is treated as stable.

Minimum test categories:

- result/error behavior
- public header compilation
- graph lifecycle
- tensor shape and byte-size reporting
- runtime setup failure when ggml is absent
- runtime setup/compute when ggml is present
- model metadata failure and success paths

The headless test target should compile without openFrameworks. openFrameworks
examples are smoke checks, not the only tests.

## Example Contract

Each example should demonstrate one idea:

- `ofxGgmlSimpleExample`: runtime setup and one tiny graph
- `ofxGgmlTextExample`: one editable local text-generation prompt
- `ofxGgmlChatExample`: interactive chat against a warm server, with CLI fallback
- `ofxGgmlEmbeddingExample`: compare two texts through an embedding server

Segmentation remains script-tested through `scripts/test-sam3-smoke.*` until
the example gate in `docs/SEGMENTATION.md` is met.

No all-in-one GUI example belongs in core.

## Expansion Gate

A feature may enter core only if all are true:

- it is needed by most downstream companion layers
- it has a small public API
- it has a focused test
- it has a focused example or documented smoke check
- it does not require large committed binaries
- it does not add workflow/product assumptions

If any answer is no, the feature belongs in a companion addon or an optional
layer.

## Immediate Milestones

1. Done: make `scripts/setup-ggml.ps1` build ggml `v0.11.0` for CPU and optional local backends.
2. Done: add headless tests for `ofxGgmlGraph` and `ofxGgmlTensor`.
3. Done: make `ofxGgmlRuntime` execute a CPU graph end to end.
4. Done: add GGUF metadata tests with a tiny fixture or generated test file.
5. Done: build `ofxGgmlSimpleExample` through openFrameworks and run a tiny graph.
6. Done: design the backend-neutral `ofxGgmlText.h` surface.
7. Done: add the llama.cpp CLI adapter boundary.
8. Done: add the real platform process runner.
9. Done: add `ofxGgmlTextExample`.
10. Done: add llama-server text transport with true streaming/cancel support.
11. Done: add an interactive chat example.
12. Done: add embedding runner, server API, and embedding similarity example.
13. Done: add generated-project repair coverage for multiple examples.
14. Done: tighten example launch scripts and model/server discovery consistency.
15. Done: add focused smoke coverage for launch-script dry-run behavior.
16. Done: make launch-script smoke checks part of a single local validation entrypoint.
17. Done: decide the next optional layer boundary before adding more runtime surface.
18. Done: add a focused segmentation example or documented SAM3 smoke path.
19. Done: decide whether segmentation needs an openFrameworks example or should remain script-tested until a real SAM3 model workflow is available.
20. Done: tighten release/readiness docs around what belongs in `main` before the first rewrite tag.
21. Done: review release readiness gaps and either close them or intentionally defer them.
22. Done: run a full generated-example build confidence pass before considering a rewrite tag.
23. Done: confirm README setup, validation, llama-server, embedding, and SAM3 smoke commands match the scripts.
24. Next: confirm no generated project files, binaries, caches, or model files are staged before the rewrite tag.
