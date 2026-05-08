# ofxGgmlFoundationExample

A deliberately small example for the clean `ofxGgmlFoundation.h` / `ofxGgmlInference.h` rebuild layer.

It demonstrates the intended openFrameworks shape:

- `setup()` configures the local AI session.
- `keyPressed()` sends a prompt.
- `draw()` renders plain sketch state.

The current backend is deterministic, so the example can compile and run before real `llama-server` or ggml-backed text generation is wired into the new interface.
