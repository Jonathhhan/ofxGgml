# API Facades And Backend Boundaries

This addon works best when it presents a stable API over concrete upstream libraries without pretending those libraries are interchangeable.

## Short version

- Keep the addon API generic enough to call text, speech, TTS, vision, and media backends consistently.
- Keep backend truth specific: model families, unsupported tasks, required sidecar files, and raw loader errors belong to the backend adapter.
- Avoid rewriting upstream behavior just to fit a generic abstraction unless there is a strong compatibility reason.

## Practical rule

Use generic APIs for call shape, not for backend identity.

Good:

- `ofxGgmlTtsInference::synthesize(request)` provides one stable entry point.
- `chatllm.cpp`, Whisper, AceStep, and other integrations keep their own capability checks and runtime diagnostics.
- UI profiles expose backend-specific expectations such as required model family or `speaker.json`.

Bad:

- treating every GGUF TTS model as interchangeable
- replacing backend-native errors with vague generic failures
- forcing unsupported backend features into generic flags
- patching upstream library source to behave like an imaginary common engine

## Adapter responsibilities

Each backend adapter should own:

- capability checks before launch
- backend-specific model and file expectations
- translation from addon requests into backend CLI or API arguments
- preservation of raw backend diagnostics

The generic layer should own:

- shared request/result structs
- backend selection
- common UI plumbing
- high-level workflow orchestration

## TTS example

The `chatllm.cpp` TTS path is not "any GGUF TTS backend." It is a concrete adapter with concrete rules:

- it is wired for `chatllm.cpp`
- it expects OuteTTS-compatible GGUF models
- clone voice currently expects a prepared `speaker.json`
- continue-speech is not wired yet
- loader errors from `chatllm.cpp` should be shown directly

Those are not leaks in the abstraction. They are the useful truth of the integration.

## Upstream policy

Prefer wrapper code over upstream source edits.

When backend customization is needed, prefer this order:

1. addon-side adapter logic
2. runtime flags or configuration
3. wrapper scripts or isolated compatibility shims
4. minimal, documented upstream patches only when unavoidable

This keeps upgrades easier and debugging more honest.
