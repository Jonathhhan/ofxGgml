# ofxGgml V2

`ofxGgml` connects an openFrameworks application to a local
OpenAI-compatible model server. The V2 branch follows one narrow workflow:
chat, search explicitly loaded documents through one allowlisted tool, and
return a cited answer.

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

The complete document workflow remains small:

```cpp
ofxGgml::DocumentIndex documents;
documents.addFile("notes/architecture.md");

ofxGgml::ToolRegistry tools;
tools.addDocumentSearch(documents);

ofxGgml::ToolLoop loop(chat, tools);
const auto answer = loop.run("Why is llama-server a separate process?");
```

`search_documents` receives only a query. It cannot choose a file path or run
an arbitrary function; it searches only text the application loaded first.

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
- local document index
- one allowlisted `search_documents` tool
- bounded tool loop with source citations

Still required before V2 is considered proven:

- one real model-backed document answer with citations

Not part of the supported V2 surface yet:

- embedded native model runtimes
- generic tensor, graph, or model abstractions
- multi-agent orchestration
- Whisper, SAM, Stable Diffusion, music, or video APIs

## Example

`ofxGgmlChatExample` is a minimal keyboard-driven openFrameworks example.

- Set `OFXGGML_SERVER_URL` to override `http://127.0.0.1:8080`.
- Set `OFXGGML_MODEL` when the endpoint does not advertise a model via
  `/v1/models`.
- Set `OFXGGML_API_KEY` for endpoints that require Bearer authentication.
- Press `I` to inspect `/v1/models`.
- Type a message and press Enter to send it.
- Press `C` to clear the conversation.

### Testing without a local GPU

Hugging Face Inference Providers exposes an OpenAI-compatible endpoint and
supports tools. Configure the example without putting the token in source:

```sh
export OFXGGML_SERVER_URL=https://router.huggingface.co/v1
export OFXGGML_API_KEY=hf_your_token
export OFXGGML_MODEL=your-tool-capable-model:provider
```

Create a token with the `Inference Providers` permission and choose a currently
available tool-capable model in the
[Hugging Face playground](https://huggingface.co/playground). Provider usage can
consume monthly credit or incur pay-as-you-go charges; see
[HF pricing](https://huggingface.co/docs/inference-providers/pricing).

The same addon code works with local `llama-server`; only environment values
change. Do not commit tokens or paste them into issue logs.

Before building the openFrameworks example, verify that the selected hosted
model supports the required two-request tool protocol:

```powershell
scripts\smoke-huggingface.bat
```

This live smoke asks the model to call `search_documents`, supplies one fixed
read-only result, and requires the final response to contain its citation. It
tests the hosted model/provider contract. The openFrameworks example then tests
the same protocol through the addon itself.

## Tests

```sh
cmake -S tests -B tests/build
cmake --build tests/build
ctest --test-dir tests/build --output-on-failure
```

The tests use an injected transport and do not claim real inference. The
online or local example is the next model-backed smoke; its provider, model,
date, and result should be recorded before stabilizing the API.

## Branch status

V2 is under development on an isolated branch. The current `main` branch and
the previous addon family remain available until this smaller path is proven.
