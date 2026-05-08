# ofxGgml Rebuild Notes

This is the clean rebuild direction for `ofxGgml`, keeping openFrameworks habits in mind: simple includes, sketch-friendly lifecycle methods, small examples, and optional depth for people who need it.

## Shape

The addon should grow around three clear layers:

- `ofxGgmlFoundation.h`: lifecycle, backend discovery, model locations, logging/metrics, and stable request/response types.
- `ofxGgmlInference.h`: text generation, chat, embeddings, streaming, batching, and model catalog helpers.
- `ofxGgmlCreative.h`: optional speech, TTS, vision, diffusion, segmentation, planning, research, and companion workflows.

The first new public header is `src/ofxGgmlFoundation.h`. It is deliberately small and can be included from a normal `ofApp` without pulling every modality and workflow into the compile.

The second public header is `src/ofxGgmlInference.h`. It keeps model catalog, session setup, and text backend selection separate from the lower-level runtime lifecycle.

The third public header is `src/ofxGgmlCreative.h`. It starts as an opt-in capability registry so heavy modalities have a clean place to attach without becoming part of the default include.

## openFrameworks API Style

The high-level object should feel familiar in sketches:

```cpp
#include "ofxGgmlFoundation.h"

ofxGgmlApp ai;

void ofApp::setup() {
    ai.setup();
}

void ofApp::draw() {
    auto response = ai.generate("write a short caption");
}
```

The clean API should prefer:

- `setup()`, `update()`, `drawDebug()`, and `close()` where appropriate.
- Plain structs for settings and results.
- Small public headers and focused examples.
- Runtime backends hidden behind interfaces instead of spread through modality-specific code.

## First Milestone

The first milestone is not full inference. It is a stable foundation:

- Runtime lifecycle.
- Backend/device vocabulary.
- Swappable text backend interface.
- Deterministic test backend.
- One small text example.
- Tests that do not require a full openFrameworks install.

Once this is solid, real `llama-server`, CLI, and in-process ggml backends can plug into the text backend interface without changing sketch code.

## Second Milestone

The second milestone is the clean inference session:

- `ofxGgmlSession` as the high-level inference object.
- `ofxGgmlModelCatalog` for stable model metadata.
- `ofxGgmlTextBackend` implementations selected explicitly.
- `ofxGgmlLlamaServerBackend` declared as the real server-backed slot, even while its transport is still pending.
- `ofxGgmlTextTaskQueue` for `submit()` / `update()` flows that fit openFrameworks sketches.

Sketch code should be able to stay small:

```cpp
#include "ofxGgmlInference.h"

ofxGgmlSession ai;

void ofApp::setup() {
    ai.setup();
    ai.useLlamaServer({"http://127.0.0.1:8080"});
}

void ofApp::update() {
    ai.update();
}
```

## Third Milestone

The third milestone is the creative layer boundary:

- `ofxGgmlCreativeSession` wraps an inference session.
- `ofxGgmlCreativeCapabilityRegistry` tracks which optional tools are present.
- Vision, speech, diffusion, segmentation, video planning, and research integrations register capabilities before they expose heavier APIs.

This keeps the addon discoverable for creative work while preserving a small default compile surface.
