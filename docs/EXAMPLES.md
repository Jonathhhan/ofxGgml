# Examples

The examples are intentionally small. Use them as separate smoke tests and
copyable starting points, not as one big application.

## Which Example?

| Example | Use it when | Needs a model | Server |
| --- | --- | --- | --- |
| `ofxGgmlSimpleExample` | You want to verify ggml setup and runtime/device selection. | No | No |
| `ofxGgmlTextExample` | You want one editable prompt and one generated response. | Text GGUF | `llama-server` on `8080` by default |
| `ofxGgmlChatExample` | You want an interactive chat UI with history and sampling controls. | Text GGUF | `llama-server` on `8080` by default |
| `ofxGgmlEmbeddingExample` | You want to compare two texts with cosine similarity. | Embedding GGUF recommended | `llama-server --embeddings` on `8081` by default |

Run `scripts\doctor.bat` first when something feels unclear. It reports missing
tools, addon neighbors, runtime files, models, built examples, and reachable
servers.

## Build And Run

Simple runtime smoke test:

```powershell
scripts\build-simple-example.bat
ofxGgmlSimpleExample\bin\ofxGgmlSimpleExample.exe
```

Text:

```powershell
scripts\run-text-example.bat -Build -Model C:\path\to\model.gguf
```

Chat:

```powershell
scripts\run-chat-example.bat -Build -Model C:\path\to\model.gguf
```

Embeddings:

```powershell
scripts\run-embedding-example.bat -Build -Model C:\path\to\embedding-model.gguf
```

The run scripts set the environment variables that the examples read, start a
bundled server when possible, and reuse an already healthy local server instead
of starting duplicates.

## Ports

| Port | Purpose |
| --- | --- |
| `8080` | Text and chat completions |
| `8081` | Embeddings |

Keep chat/text and embeddings separate. `llama-server --embeddings` is an
embedding server, not a chat server.

## Model Search

The run scripts and examples look for `.gguf` models in these places:

```text
openFrameworks/addons/models
openFrameworks/addons/ofxGgml/models
openFrameworks/addons/ofxGgml/<Example>/bin/data
openFrameworks/addons/ofxGgml/<Example>/bin/data/models
openFrameworks/addons/ofxGgml/<Example>/models
```

Passing `-Model C:\path\to\model.gguf` is the most explicit option and is
recommended for first runs.

## Environment Overrides

| Variable | Purpose |
| --- | --- |
| `OFXGGML_TEXT_MODEL` | Text/chat model path |
| `OFXGGML_TEXT_SERVER_URL` | Text/chat server URL |
| `OFXGGML_TEXT_SERVER_MODEL` | Optional server-side model id |
| `OFXGGML_TEXT_BACKEND` | `server` or `cli` |
| `OFXGGML_LLAMA_CLI` | Explicit `llama-cli` path for CLI fallback |
| `OFXGGML_EMBEDDING_MODEL` | Embedding model path |
| `OFXGGML_EMBEDDING_SERVER_URL` | Embedding server URL |
| `OFXGGML_EMBEDDING_SERVER_MODEL` | Optional embedding server model id |

Prefer run-script parameters for normal use. Environment variables are useful
when launching examples directly from an IDE.

## Dry Runs

Dry runs show what would launch without opening an example window:

```powershell
scripts\run-text-example.bat -DryRun
scripts\run-chat-example.bat -DryRun
scripts\run-embedding-example.bat -DryRun
scripts\start-llama-server.bat -DryRun -ModelPath C:\path\to\model.gguf
```

## Common Fixes

- If a GUI example cannot find `ofxImGui.h`, install `ofxImGui` beside
  `ofxGgml`, then rebuild or regenerate the project.
- If a model is missing, pass `-Model` or put a GGUF under `addons/models`.
- If a server request fails, launch through the run scripts instead of opening
  the `.exe` directly.
- If rebuilding llama.cpp cannot replace DLLs, close running examples and
  servers, or run `scripts\build-llama-server.bat -StopRunningRuntime`.
