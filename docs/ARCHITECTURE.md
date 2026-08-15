# V2 Architecture

## Product path

```text
openFrameworks app
  -> ofxGgml::ChatSession
  -> ofxGgml::Server
  -> OpenAI-compatible HTTP endpoint
  -> external llama-server
```

The process boundary is architectural. It prevents unrelated native runtimes
from forcing one ggml version, one CUDA build, or one release cycle on the
addon.

## Public surface

- `Server` owns endpoint configuration and HTTP transport.
- `ChatSession` owns conversation history.
- `ChatRequest`, `ChatOptions`, and `ChatResult` are explicit value types.
- `HttpTransport` is injectable so protocol behavior can be tested without a
  model or network service.

## Deliberately absent

- No Core addon or common native runtime.
- No tensor, graph, or universal model abstraction.
- No automatic backend discovery beyond the configured server endpoint.
- No agent framework before a real tool-using workflow exists.
- No ecosystem manifest or cross-repository control plane.

## Growth rule

A public class must be used by an example. A shared abstraction needs two real
consumers. A new addon boundary must solve an observed installation, build,
link, or independent-release problem.
