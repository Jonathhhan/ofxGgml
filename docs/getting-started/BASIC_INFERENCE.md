# Basic LLM Inference Guide

Learn how to use ofxGgml for text-only AI workflows without the complexity of multimodal features.

## What You'll Learn

- Setting up text inference with llama-server or CLI backends
- Running chat, summarization, and translation tasks
- Using the simple `ofxGgmlEasy` facade
- Understanding streaming and batch inference

## Minimal Setup

### Include the Header

```cpp
#include "ofxGgmlBasic.h"
```

This gives you:
- Core runtime and model loading
- Text inference (llama-server + CLI fallback)
- `ofxGgmlEasy` facade for common tasks
- Chat and text assistants
- Streaming support

### Configure Text Inference

```cpp
ofxGgmlEasy ai;

ofxGgmlEasyTextConfig config;
config.modelPath = "models/qwen2.5-1.5b-instruct-q4_k_m.gguf";
config.serverUrl = "http://127.0.0.1:8080";  // Optional: auto-launches if local
config.completionExecutable = "llama-completion";  // Fallback CLI tool

ai.configureText(config);
```

## Common Tasks

### 1. Chat

```cpp
auto result = ai.chat("What is machine learning?", "English");

if (result.inference.success) {
    cout << result.inference.text << endl;
}
```

### 2. Summarize

```cpp
string longText = "...";
auto summary = ai.summarize(longText, 100);  // Max 100 words
```

### 3. Translate

```cpp
auto translated = ai.translate(
    "Bonjour le monde",
    "English",      // Target language
    "French"        // Source language (or "Auto detect")
);
```

### 4. Custom Prompts

```cpp
ofxGgmlInference inference;

ofxGgmlInferenceSettings settings;
settings.maxTokens = 256;
settings.temperature = 0.7;
settings.useServerBackend = true;

auto result = inference.generate(
    config.modelPath,
    "Write a haiku about programming",
    settings
);
```

## Streaming Inference

For real-time token-by-token output:

```cpp
auto ctx = std::make_shared<ofxGgmlStreamingContext>();

inference.generate(modelPath, prompt, settings,
    [](const std::string& chunk) {
        cout << chunk << flush;  // Print each token as it arrives
        return true;  // Continue streaming
    }
);
```

### With Backpressure Control

```cpp
ctx->setBackpressureThreshold(1000);  // Pause if 1000 chars buffered

inference.generate(modelPath, prompt, settings,
    [ctx](const std::string& chunk) {
        if (ctx->shouldPause()) {
            ctx->waitForResume(5000);  // Wait up to 5 seconds
        }

        if (ctx->isCancelled()) {
            return false;  // Stop streaming
        }

        processChunk(chunk);
        return true;
    }
);

// From another thread:
ctx->cancel();  // Stop the inference
```

## Batch Inference

Process multiple requests efficiently:

```cpp
vector<string> prompts = {
    "Summarize: ...",
    "Translate: ...",
    "Explain: ..."
};

ofxGgmlInferenceSettings settings;
settings.useServerBackend = true;  // Enables parallel processing

auto result = inference.generateBatchSimple(modelPath, prompts, settings);

cout << "Processed " << result.processedCount << " requests\n";
cout << "Total time: " << result.totalElapsedMs << "ms\n";

for (size_t i = 0; i < result.responses.size(); i++) {
    if (result.responses[i].success) {
        cout << "Response " << i << ": " << result.responses[i].text << "\n";
    }
}
```

## Backend Selection

### Server Backend (Recommended)

**Pros:**
- 10-50ms faster per request
- Supports concurrent batch processing
- Keeps model warm between requests
- Enables prompt caching

**Setup:**
```bash
./scripts/build-llama-server.sh    # Build the server
./scripts/start-llama-server.sh    # Start manually, or...
# GUI example auto-starts on http://127.0.0.1:8080
```

```cpp
settings.useServerBackend = true;
settings.serverUrl = "http://127.0.0.1:8080";
```

### CLI Backend (Fallback)

**When to use:**
- Server not available
- Single-shot inference
- Testing/debugging

```cpp
settings.useServerBackend = false;
settings.completionExecutable = "llama-completion";
```

## Chat Assistant

For multi-turn conversations with memory:

```cpp
ofxGgmlChatAssistant chatAssistant;
chatAssistant.setInference(&inference);

// Turn 1
auto response1 = chatAssistant.chat(
    "What's the capital of France?",
    "English",
    "You are a helpful geography tutor."
);

// Turn 2 - remembers previous context
auto response2 = chatAssistant.chat(
    "What's its population?",
    "English"
);
```

## Text Assistant

For specialized text tasks:

```cpp
ofxGgmlTextAssistant textAssistant;
textAssistant.setInference(&inference);

// Summarize
auto summary = textAssistant.summarize(longText, settings);

// Key points
auto keyPoints = textAssistant.extractKeyPoints(text, settings);

// Translate
auto translation = textAssistant.translate(
    text,
    "English",  // target
    "Spanish",  // source
    settings
);

// Detect language
auto detected = textAssistant.detectLanguage(text, settings);
```

## Performance Tips

1. **Use server mode** - 10-50ms faster
2. **Enable prompt caching** - Automatically enabled in server mode
3. **Batch requests** - Process multiple prompts in parallel
4. **Choose appropriate model size** - Smaller models = faster inference
5. **Monitor metrics** - Use `ofxGgmlMetrics::getInstance()` to track performance

## Model Selection

| Model | Size | Speed | Quality | Use Case |
|-------|------|-------|---------|----------|
| Qwen 2.5 1.5B Q4 | 1GB | Fast | Good | Chat, simple tasks |
| Qwen 2.5 7B Q4 | 4GB | Medium | Better | General purpose |
| Llama 3.2 3B | 2GB | Fast | Good | Balanced speed/quality |

Download models:
```bash
./scripts/download-model.sh 1  # Qwen 1.5B (recommended starter)
./scripts/download-model.sh 2  # Qwen 7B
```

## Next Steps

- **Add vision**: See [../features/MODALITIES.md](../features/MODALITIES.md)
- **Code assistant**: See [../features/ASSISTANTS.md](../features/ASSISTANTS.md)
- **Video workflows**: See [../features/WORKFLOWS.md](../features/WORKFLOWS.md)
- **Performance tuning**: See [../PERFORMANCE.md](../PERFORMANCE.md)

## Examples

Check these focused examples:
- `ofxGgmlChatExample` - Simple chat application
- `ofxGgmlBasicExample` - Tensor operations
- `ofxGgmlNeuralExample` - Graph inference
