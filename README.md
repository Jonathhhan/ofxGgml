# ofxGgml V2

`ofxGgml` connects an openFrameworks application to an external
OpenAI-compatible model endpoint. The V2 branch follows one narrow workflow:
chat, search explicitly loaded documents through one allowlisted tool, and
return a cited answer. The endpoint can be a local `llama-server` or a hosted
provider.

The addon does **not** embed ggml, llama.cpp, CUDA, SAM, or another native model
runtime. Run `llama-server` separately and update it independently.

## Install

Clone the V2 branch into the openFrameworks addons directory:

```sh
cd path/to/openFrameworks/addons
git clone --branch v2 https://github.com/Jonathhhan/ofxGgml.git
```

Open `ofxGgmlChatExample` with the openFrameworks Project Generator, or add
`ofxGgml` to an existing project. No model runtime or model file is installed
with the addon.

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

These objects deliberately use non-owning references: keep `Server` alive while
its `ChatSession` is used, keep both `ChatSession` and `ToolRegistry` alive while
using `ToolLoop`, and keep `DocumentIndex` alive after registering document
search.

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

Proven end to end:

- deterministic protocol tests on Linux and Windows
- compilation and GUI launch against the current openFrameworks Linux nightly
- automated keyboard input through the real example GUI
- a marker-gated Hugging Face run with two model requests, local
  `search_documents` execution, and a cited final answer

The latest recorded live proof used `openai/gpt-oss-120b` and returned
`v2-architecture.md#chunk-1` in the
[successful GUI run](https://github.com/Jonathhhan/ofxGgml/actions/runs/31979160396).

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
- Press `F1` to inspect `/v1/models`.
- Type a message and press Enter to send it.
- Press `F2` to clear the conversation.

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

For a GitHub-hosted run, add a repository Actions secret named `HF_TOKEN`.
Optionally set repository variables `HF_MODEL` and `HF_SERVER_URL`; otherwise
the workflows use `openai/gpt-oss-120b` and the Hugging Face router. On `v2`,
`[hf-smoke]` runs the provider protocol check and `[hf-gui-smoke]` runs the
compiled openFrameworks GUI through the same hosted tool path. Normal pushes
run neither live inference test, so they cannot consume provider credit
accidentally.

## Tests

```sh
cmake -S tests -B tests/build
cmake --build tests/build
ctest --test-dir tests/build --output-on-failure
```

The deterministic tests use an injected transport; live inference remains a
separate, explicitly triggered check so protocol failures and provider costs
cannot be confused with unit-test failures.

GitHub Actions also builds `ofxGgmlChatExample` against the current official
openFrameworks Linux nightly. The workflow records the resolved archive name,
opens the GUI on a virtual display, and uploads its screenshot and log. With
`[hf-gui-smoke]`, it additionally types a question, waits for the model-backed
tool loop, verifies the source identifier, and captures the rendered answer.

## Branch status

The next candidate is `v2.0.0-rewrite.1`. It continues the existing
`v2.0.0-rewrite.0` prerelease line while replacing that broad in-process
runtime design with the smaller server-first addon described here. See the
[release notes](docs/RELEASE_NOTES.md) for its exact boundary. The current
`main` branch and previous addon family remain untouched.
