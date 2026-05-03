# API Stability Policy

ofxGgml uses semantic versioning for the supported addon tier.

## Version guards

Include `src/core/ofxGgmlVersion.h` (or any public umbrella header) to access:

- `OFXGGML_VERSION_MAJOR`
- `OFXGGML_VERSION_MINOR`
- `OFXGGML_VERSION_PATCH`
- `OFXGGML_VERSION_STRING`
- `OFXGGML_VERSION_CODE`
- `OFXGGML_VERSION_ENCODE(major, minor, patch)`
- `OFXGGML_VERSION_AT_LEAST(major, minor, patch)`

The historical `OFX_GGML_VERSION_*` macros remain available during the 1.x series for source compatibility.

## Stable APIs

Stable APIs are expected to remain source-compatible for the current major version:

- `ofxGgmlCore.h`
- `ofxGgmlBasic.h`
- core runtime, tensor, graph, model, and result types
- `ofxGgmlInference` settings/result structures and non-experimental text inference methods
- documented model onboarding and health helpers in `ofxGgmlEasy`

Backward-compatible additions may ship in minor releases. Patch releases should limit changes to fixes, documentation, and behavior-preserving improvements.

## Experimental APIs

Experimental APIs may change in minor releases and should not be treated as compatibility anchors:

- companion/example-tier workflows behind `OFXGGML_ENABLE_COMPANION_WORKFLOWS`
- `ofxGgmlCompanionWorkflows.h`
- video essay, music/AceStep, MilkDrop, and Holoscan bridge surfaces
- GUI-only workflows and prototype panels
- APIs documented as proposed, draft, experimental, or roadmap-only

When an experimental API becomes stable, it should be documented here and covered by tests before a release.

## Compatibility rules

For stable APIs within the same major version:

- do not remove public symbols without a deprecation period
- do not rename public types, methods, fields, headers, or macros without aliases
- do not change default behavior in a way that breaks documented examples unless guarded or clearly migrated
- add tests for stable API contracts when fixing regressions or adding compatibility guards

Breaking stable APIs requires a major version bump and migration notes.
