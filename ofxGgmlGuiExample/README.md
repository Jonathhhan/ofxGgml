# ofxGgmlGuiExample

This example is a showcase for the addon API layers and UI integration patterns. It is not the project test harness and should not be the first place new feature logic is added.

## Maintenance policy

- Keep the GUI focused on demonstrating stable addon APIs.
- Put reusable behavior in `src/` and cover it with `tests/`.
- Put complex workflows such as video essay, montage, music/AceStep, MilkDrop, or Holoscan into focused examples, tutorial projects, or companion addons.
- Avoid expanding `ofApp.cpp`; prefer smaller panels/helpers when showcase glue is still needed.

Use `../tests/run-tests.sh` for validation coverage instead of relying on manual GUI flows.
