# ofxGgml V2

`ofxGgml` connects an openFrameworks application to a local
OpenAI-compatible model server. The V2 branch deliberately starts with one
small capability: inspect a server and keep a chat session.

The addon does **not** embed ggml, llama.cpp, CUDA, SAM, or another native model
runtime. Run `llama-server` separately and update it independently.

## Current API

```cpp
#include "ofxGgml.h"

ofxGgml::Server server("http://127.0.0.1:8080");
ofxGgml::ChatSession chat(server);

chat.setSystemPrompt("Be concise.");
const auto result = chat.send("Hello");

if (result) {
	ofLogNotice() << result.text;
} else {
	ofLogError() << result.error;
}
```

Inspect the server and advertised model identity before use:

```cpp
const auto status = server.inspect();
if (status && !status.models.empty()) {
	ofxGgml::ChatOptions options;
	options.model = status.models.front();
	chat.setOptions(options);
}
```

## Scope

Implemented:

- `/v1/models` server inspection
- `/v1/chat/completions`
- conversational history
- optional streaming transport when openFrameworks exposes curl
- injected HTTP transport for deterministic tests

Next vertical slice:

- local document index
- one allowlisted `search_documents` tool
- one model-backed document answer with citations

Not part of the supported V2 surface yet:

- embedded native model runtimes
- generic tensor, graph, or model abstractions
- multi-agent orchestration
- Whisper, SAM, Stable Diffusion, music, or video APIs

## Example

`ofxGgmlChatExample` is a minimal keyboard-driven openFrameworks example.

- Set `OFXGGML_SERVER_URL` to override `http://127.0.0.1:8080`.
- Press `I` to inspect `/v1/models`.
- Type a message and press Enter to send it.
- Press `C` to clear the conversation.

## Tests

```sh
cmake -S tests -B tests/build
cmake --build tests/build
ctest --test-dir tests/build --output-on-failure
```

The tests use an injected transport and do not claim real inference. A real
model-backed smoke will be added with the document-tool vertical slice.

## Branch status

V2 is under development on an isolated branch. The current `main` branch and
the previous addon family remain available until this smaller path is proven.
