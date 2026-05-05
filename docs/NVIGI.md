# Optional NVIDIA NVIGI bridge helpers

`ofxGgml` does not vendor the NVIDIA NVIGI SDK, plugin binaries, or NVIGI
model data. Instead, the addon exposes small callback bridges that let an
application owning an NVIGI SDK integration plug those calls into the existing
ofxGgml request/result types.

Define `OFXGGML_ENABLE_NVIGI=1` in the application or project that wires the
SDK callbacks. When the flag is not defined, all NVIGI helpers remain available
for construction in direct includes, but calls return explicit disabled errors.

## Supported bridge surfaces

- `ofxGgmlNvigiGptBackend` for GPT/text generation callbacks.
- `ofxGgmlNvigiAsrSpeechBackend` for ASR/speech transcription callbacks.
- `ofxGgmlNvigiTtsBackend` for TTS/speech synthesis callbacks.
- `ofxGgmlNvigiRagBackend` for SDK-owned RAG retrieval and RAG generation.
- `ofxGgmlNvigiReloadController` for app-owned load, unload, reload, and
  refresh controls.

The callback approach keeps SDK lifetime, plugin selection, device selection,
model data paths, and reload semantics under application control while allowing
the rest of the addon to consume normal ofxGgml results.

## Layered includes

- `ofxGgmlBasic.h` exposes NVIGI GPT and reload helpers when
  `OFXGGML_ENABLE_NVIGI=1`.
- `ofxGgmlModalities.h` exposes NVIGI ASR and TTS helpers when
  `OFXGGML_ENABLE_NVIGI=1`.
- `ofxGgmlWorkflows.h` exposes the NVIGI RAG helper when
  `OFXGGML_ENABLE_NVIGI=1`.

Direct includes are also supported:

```cpp
#include "inference/ofxGgmlNvigiGptBackend.h"
#include "inference/ofxGgmlNvigiSpeechBackend.h"
#include "inference/ofxGgmlNvigiTtsBackend.h"
#include "inference/ofxGgmlNvigiRagBackend.h"
#include "inference/ofxGgmlNvigiReloadController.h"
```

## Notes

- The bridges intentionally avoid including NVIGI headers so the default addon
  build remains portable.
- Applications should initialize, validate, and shut down the NVIGI runtime
  outside these bridge objects.
- Applications should map SDK-specific errors into the `error` fields on the
  corresponding ofxGgml result types.
- Reload callbacks should decide whether a component reload preserves session
  state, refreshes model data, unloads plugin state, or rebuilds the SDK
  pipeline.
