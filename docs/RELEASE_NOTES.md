# Release Notes

## v2.0.0-rewrite.1 — candidate

This is the first release candidate for the reduced endpoint-first V2. The name
continues the existing `v2.0.0-rewrite.0` prerelease sequence; it does not claim
API compatibility with that earlier, broader runtime design.

The addon is renamed from `ofxGgml` to `ofxIC`, short for **Inference
Connector**. The previous name implied an embedded ggml runtime even though the
new architecture deliberately owns only the client side of a process boundary.
The public namespace is now `ofxIC`, `Server` is now `Endpoint`, and environment
variables use the `OFXIC_` prefix.

### Included

- one openFrameworks addon with no embedded ggml or model runtime;
- OpenAI-compatible endpoint inspection and chat completions;
- conversation history and optional response streaming;
- explicitly loaded local documents and one allowlisted search tool;
- a bounded tool loop that returns source identifiers;
- one `ofxImGui` openFrameworks example with selectable endpoint profiles;
- injectable-transport tests on Linux and Windows;
- compilation and GUI launch against the current openFrameworks Linux nightly;
- an explicitly triggered Hugging Face tool-path smoke test.

### Breaking boundary

The tensor, graph, embedded-runtime, generic model, embedding, segmentation,
SAM, and multi-addon APIs from `v2.0.0-rewrite.0` are intentionally absent.
Local `llama-server` and hosted providers use the same endpoint-facing API.

### Candidate validation

Pushes and `v2.*` tags run deterministic tests and the openFrameworks nightly
GUI smoke. Live provider inference remains separately gated so publishing a
candidate cannot consume Hugging Face credit accidentally.

### Deferred

- an embedded native runtime;
- Whisper, SAM, Stable Diffusion, music, and video addons;
- generic agents or unrestricted tool execution;
- automatic model downloads or provider selection.
