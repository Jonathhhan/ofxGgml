# ofxGgmlGuiExample

This example is a curated showcase for addon API layers and UI integration patterns. It is not the project test harness and should not be the first place new feature logic is added.

The default mode picker stays focused on stable addon APIs. Enable **Show advanced workflows** in the sidebar only when you need companion/example-tier demos such as video essay, long-form video, or MilkDrop. Mode labels, summaries, and stable-vs-advanced grouping live in `src/config/GuiModeCatalog.*` so navigation decisions stay separate from `ofApp.cpp` panel rendering.

## Structure

- `src/config/GuiModeCatalog.*` keeps the sidebar mode catalog and mode summaries.
- `src/panels/` contains reusable floating/status panel widgets.
- `src/managers/` wraps long-lived helper servers.
- `src/utils/` contains shared GUI-example helpers.
- Mode files such as `TextModes.cpp`, `VisionVideo.cpp`, `DiffusionClip.cpp`, and `SamSegmentation.cpp` keep panel-specific behavior out of `ofApp.cpp` where practical.

## Maintenance policy

- Keep the GUI focused on demonstrating stable addon APIs.
- Put reusable behavior in `src/` and cover it with `tests/`.
- Put complex workflows such as video essay, montage, music/AceStep, MilkDrop, or Holoscan into focused examples, tutorial projects, or companion addons.
- Avoid expanding `ofApp.cpp`; prefer smaller panels/helpers when showcase glue is still needed.

## SAM mode

The SAM panel follows the upstream `ggml/examples/sam` example defaults where practical: converted ViT-B `ggml-model-f16.bin`, point-prompt inference, CPU thread control, and multi-mask output. Box prompts and direct mask/iou/stability threshold controls are intentionally left out of the GUI until the attached sam.cpp adapter exposes those controls through the addon bridge.

Use `../tests/run-tests.sh` for validation coverage instead of relying on manual GUI flows.
