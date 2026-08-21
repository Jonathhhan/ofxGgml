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
Run `scripts\list-models.bat` when you want to see every discovered GGUF and the
folders that were searched.
For a fresh checkout, `scripts\first-run.bat` runs setup, builds llama.cpp
tools, and then runs doctor.

## Build And Run

Simple runtime smoke test:

```powershell
scripts\run-simple-example.bat -Build
```

Text:

```powershell
scripts\run-text-example.bat -Build -Model C:\path\to\model.gguf
scripts\run-text-example.bat -Build -Model C:\path\to\model.gguf -StrictModel
```

Chat:

```powershell
scripts\run-chat-example.bat -Build -Model C:\path\to\model.gguf
scripts\run-chat-example.bat -Build -Model C:\path\to\model.gguf -StrictModel
```

Embeddings:

```powershell
scripts\run-embedding-example.bat -Build -Model C:\path\to\embedding-model.gguf
scripts\run-embedding-example.bat -Build -Model C:\path\to\embedding-model.gguf -StrictModel
```

The run scripts set the environment variables that the examples read, start a
bundled server when possible, and reuse an already healthy local server instead
of starting duplicates.

### macOS / Linux

The text example has a native shell launcher and does not require PowerShell:

```bash
bash scripts/run-text-example.sh --model /path/to/model.gguf
```

It checks `http://127.0.0.1:8080/health`, reuses an already healthy server, or
starts the bundled `llama-server` in the background when one is available.
You can also start only the server:

```bash
bash scripts/start-llama-server.sh --model /path/to/model.gguf
```

On macOS the text launcher discovers the openFrameworks app executable under
`ofxGgmlTextExample/bin/*.app/Contents/MacOS/` as well as a plain binary under
`bin/`.

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

Passing `-Model C:\path\to\model.gguf` is the most explicit option on Windows.
On macOS/Linux use `--model /path/to/model.gguf`.
Use `scripts\list-models.bat` on Windows to print the same search folders and
discovered models.

## Environment Overrides

| Variable | Purpose |
| --- | --- |
| `OFXGGML_TEXT_MODEL` | Text/chat model path |
| `OFXGGML_TEXT_SERVER_URL` | Text/chat server URL |
| `OFXGGML_TEXT_SERVER_MODEL` | Optional server-side model id |
| `OFXGGML_TEXT_BACKEND` | `server` or `cli` |
| `OFXGGML_LLAMA_CLI` | Explicit `llama-cli` path for CLI fallback |
| `OFXGGML_TEXT_STRICT_MODEL` | Set to `1/true` for strict text model filtering |
| `OFXGGML_EMBEDDING_MODEL` | Embedding model path |
| `OFXGGML_EMBEDDING_SERVER_URL` | Embedding server URL |
| `OFXGGML_EMBEDDING_SERVER_MODEL` | Optional embedding server model id |
| `OFXGGML_EMBEDDING_STRICT_MODEL` | Set to `1/true` for strict embedding model filtering |

Prefer run-script parameters for normal use. Environment variables are useful
when launching examples directly from an IDE.

## Dry Runs

Dry runs show what would launch without opening an example window.

Windows:

```powershell
scripts\run-simple-example.bat -DryRun
scripts\run-text-example.bat -DryRun
scripts\run-chat-example.bat -DryRun
scripts\run-embedding-example.bat -DryRun
scripts\start-llama-server.bat -DryRun -ModelPath C:\path\to\model.gguf
scripts\status-llama-server.bat
scripts\stop-llama-server.bat -DryRun -IncludeExamples
```

macOS/Linux:

```bash
bash scripts/run-text-example.sh --dry-run --model /path/to/model.gguf
bash scripts/start-llama-server.sh --dry-run --model /path/to/model.gguf
```

## Common Fixes

- If a GUI example cannot find `ofxImGui.h`, install `ofxImGui` beside
  `ofxGgml`, then rebuild or regenerate the project.
- If a model is missing, pass `-Model` on Windows or `--model` on macOS/Linux,
  or put a GGUF under `addons/models`.
- If a server request fails, launch through the run scripts instead of opening
  the example executable directly.
- If rebuilding llama.cpp cannot replace DLLs, close running examples and
  servers, or run `scripts\build-llama-server.bat -StopRunningRuntime`.
- If a detached Windows server is still running, use `scripts\stop-llama-server.bat`.
- If you are not sure what is running on macOS/Linux, check
  `curl http://127.0.0.1:8080/health`.
