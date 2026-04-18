# Batch Inference API

The batch inference API allows you to process multiple inference requests efficiently, either in parallel or sequentially, with automatic fallback and comprehensive metrics tracking.

## Overview

Batch inference provides:

- **Parallel Processing**: Process multiple requests concurrently when using server backends
- **Sequential Fallback**: Automatic fallback to sequential processing for CLI backends
- **Flexible Settings**: Support for both homogeneous (same settings) and heterogeneous (different settings) batches
- **Metrics Integration**: Built-in tracking of batch performance via `ofxGgmlMetrics`
- **Error Handling**: Configurable error handling with stop-on-first-error support
- **Resource Control**: Configurable maximum concurrent requests to manage system resources

## Basic Usage

### Simple Batch Inference

Process multiple prompts with shared settings:

```cpp
#include "ofxGgml.h"

ofxGgmlInference inference;

// Prepare prompts
std::vector<std::string> prompts = {
    "Summarize quantum computing",
    "Explain neural networks",
    "What is machine learning?"
};

// Configure settings
ofxGgmlInferenceSettings settings;
settings.maxTokens = 256;
settings.temperature = 0.7f;
settings.useServerBackend = true;
settings.serverUrl = "http://127.0.0.1:8080";

// Process batch
ofxGgmlBatchResult result = inference.generateBatchSimple(
    "models/llama-7b.gguf",
    prompts,
    settings
);

// Check results
if (result.success) {
    std::cout << "Processed: " << result.processedCount << " requests" << std::endl;
    std::cout << "Total time: " << result.totalElapsedMs << "ms" << std::endl;

    for (const auto& item : result.results) {
        std::cout << "Request " << item.id << ": " << item.result.text << std::endl;
    }
}
```

### Advanced Batch with Custom Requests

For more control, use individual batch requests with different settings:

```cpp
std::vector<ofxGgmlBatchRequest> requests;

// Request 1: Short summary
ofxGgmlInferenceSettings settings1;
settings1.maxTokens = 100;
settings1.temperature = 0.5f;
requests.emplace_back("summary_task", "Summarize this article", settings1);

// Request 2: Creative writing
ofxGgmlInferenceSettings settings2;
settings2.maxTokens = 500;
settings2.temperature = 0.9f;
requests.emplace_back("creative_task", "Write a poem about AI", settings2);

// Request 3: Code generation
ofxGgmlInferenceSettings settings3;
settings3.maxTokens = 300;
settings3.temperature = 0.3f;
requests.emplace_back("code_task", "Generate a Python function", settings3);

// Configure batch behavior
ofxGgmlBatchSettings batchSettings;
batchSettings.maxConcurrentRequests = 2;
batchSettings.stopOnFirstError = false;

// Process batch
auto result = inference.generateBatch("models/llama-7b.gguf", requests, batchSettings);
```

### Batch Embeddings

Process multiple texts for embeddings:

```cpp
std::vector<std::string> texts = {
    "First document to embed",
    "Second document to embed",
    "Third document to embed"
};

ofxGgmlEmbeddingSettings embSettings;
embSettings.normalize = true;
embSettings.useServerBackend = true;

std::vector<ofxGgmlEmbeddingResult> embeddings =
    inference.embedBatch("models/embedding-model.gguf", texts, embSettings);

for (size_t i = 0; i < embeddings.size(); ++i) {
    if (embeddings[i].success) {
        std::cout << "Embedding " << i << " dimension: "
                  << embeddings[i].embedding.size() << std::endl;
    }
}
```

## Structures

### ofxGgmlBatchRequest

Represents a single request in a batch:

```cpp
struct ofxGgmlBatchRequest {
    std::string id;                              // Unique identifier for this request
    std::string prompt;                          // The prompt text
    ofxGgmlInferenceSettings settings;           // Settings for this request
    std::function<bool(const std::string&)> onChunk;  // Optional streaming callback
};
```

### ofxGgmlBatchItemResult

Result for a single item in the batch:

```cpp
struct ofxGgmlBatchItemResult {
    std::string id;                    // Request ID
    ofxGgmlInferenceResult result;     // Inference result
    size_t batchIndex;                 // Position in original batch
};
```

### ofxGgmlBatchResult

Overall batch processing result:

```cpp
struct ofxGgmlBatchResult {
    bool success;                              // True if all requests succeeded
    float totalElapsedMs;                      // Total batch processing time
    std::vector<ofxGgmlBatchItemResult> results;  // Individual results
    std::string error;                         // Error message if failed
    size_t processedCount;                     // Number of successful requests
    size_t failedCount;                        // Number of failed requests
};
```

### ofxGgmlBatchSettings

Controls batch processing behavior:

```cpp
struct ofxGgmlBatchSettings {
    bool allowParallelProcessing = true;    // Enable concurrent processing
    bool stopOnFirstError = false;          // Stop batch on first failure
    size_t maxConcurrentRequests = 4;       // Max parallel requests
    bool preferServerBatch = true;          // Prefer server backend for batching
    bool fallbackToSequential = true;       // Fall back to sequential if batch fails
};
```

## Streaming Support

Batch requests support per-request streaming callbacks:

```cpp
std::vector<ofxGgmlBatchRequest> requests;

ofxGgmlInferenceSettings settings;
settings.maxTokens = 200;

// Add request with streaming callback
requests.emplace_back(
    "stream_request",
    "Tell me about AI",
    settings,
    [](const std::string& chunk) -> bool {
        std::cout << chunk << std::flush;
        return true;  // Continue streaming
    }
);

auto result = inference.generateBatch("model.gguf", requests);
```

## Metrics Tracking

Batch operations are automatically tracked via `ofxGgmlMetrics`:

```cpp
auto& metrics = ofxGgmlMetrics::getInstance();

// Process some batches
inference.generateBatchSimple("model.gguf", prompts);

// Get batch statistics
auto stats = metrics.getBatchStats("model.gguf");

std::cout << "Total batches: " << stats.totalBatches << std::endl;
std::cout << "Total requests: " << stats.totalRequests << std::endl;
std::cout << "Processed: " << stats.processedRequests << std::endl;
std::cout << "Failed: " << stats.failedRequests << std::endl;
std::cout << "Avg batch time: "
          << (stats.totalBatchTimeMs / stats.totalBatches) << "ms" << std::endl;
```

## Backend Behavior

### Server Backend

When using a server backend (`useServerBackend = true`):

- Requests are processed concurrently up to `maxConcurrentRequests`
- Compatible requests (same key settings) are grouped together
- Provides best performance for parallel processing
- Automatically manages request queuing

### CLI Backend

When using CLI executables:

- Requests are processed sequentially
- Each request spawns a separate process
- Slower but doesn't require a running server
- Automatic fallback when server is unavailable

## Best Practices

### 1. Group Compatible Requests

For optimal performance, group requests with similar settings:

```cpp
// Good: All requests have same maxTokens and temperature
std::vector<std::string> prompts = {"Q1", "Q2", "Q3"};
ofxGgmlInferenceSettings settings;
settings.maxTokens = 200;
settings.temperature = 0.7f;
inference.generateBatchSimple("model.gguf", prompts, settings);
```

### 2. Tune Concurrency

Adjust `maxConcurrentRequests` based on your hardware:

```cpp
ofxGgmlBatchSettings batchSettings;
// For high-end GPU
batchSettings.maxConcurrentRequests = 8;
// For CPU-only
batchSettings.maxConcurrentRequests = 2;
```

### 3. Handle Partial Failures

Don't stop on first error if you want all results:

```cpp
ofxGgmlBatchSettings batchSettings;
batchSettings.stopOnFirstError = false;  // Continue on errors

auto result = inference.generateBatch("model.gguf", requests, batchSettings);

// Process successful results
for (const auto& item : result.results) {
    if (item.result.success) {
        // Use successful result
    } else {
        // Log or retry failed result
    }
}
```

### 4. Monitor Performance

Use metrics to optimize batch sizes:

```cpp
auto& metrics = ofxGgmlMetrics::getInstance();

// Try different batch sizes
for (size_t batchSize : {5, 10, 20, 50}) {
    metrics.reset();

    std::vector<std::string> batch(batchSize, "Test prompt");
    inference.generateBatchSimple("model.gguf", batch);

    auto stats = metrics.getBatchStats("model.gguf");
    double avgTimePerRequest = stats.totalBatchTimeMs / stats.totalRequests;
    std::cout << "Batch size " << batchSize
              << ": " << avgTimePerRequest << "ms per request" << std::endl;
}
```

## Examples

### Parallel Document Summarization

```cpp
std::vector<std::string> documents = loadDocuments();
std::vector<std::string> prompts;

for (const auto& doc : documents) {
    prompts.push_back("Summarize this document:\n\n" + doc);
}

ofxGgmlInferenceSettings settings;
settings.maxTokens = 200;
settings.temperature = 0.5f;
settings.useServerBackend = true;

ofxGgmlBatchSettings batchSettings;
batchSettings.maxConcurrentRequests = 4;

auto result = inference.generateBatchSimple(
    "models/llama-7b.gguf",
    prompts,
    settings,
    batchSettings
);

std::vector<std::string> summaries;
for (const auto& item : result.results) {
    if (item.result.success) {
        summaries.push_back(item.result.text);
    }
}
```

### Multi-Language Translation

```cpp
std::vector<ofxGgmlBatchRequest> requests;

std::vector<std::string> languages = {"Spanish", "French", "German"};
std::string text = "Hello, how are you?";

for (const auto& lang : languages) {
    ofxGgmlInferenceSettings settings;
    settings.maxTokens = 100;

    std::string prompt = "Translate to " + lang + ": " + text;
    requests.emplace_back(lang + "_translation", prompt, settings);
}

auto result = inference.generateBatch("models/translator.gguf", requests);

for (const auto& item : result.results) {
    std::cout << item.id << ": " << item.result.text << std::endl;
}
```

### Semantic Search with Batch Embeddings

```cpp
// Embed query
ofxGgmlEmbeddingSettings embSettings;
embSettings.normalize = true;
auto queryResult = inference.embed("model.gguf", "search query", embSettings);

// Embed document corpus in batch
std::vector<std::string> documents = loadDocumentCorpus();
auto docEmbeddings = inference.embedBatch("model.gguf", documents, embSettings);

// Find most similar documents
std::vector<float> scores;
for (const auto& docEmb : docEmbeddings) {
    if (docEmb.success) {
        float similarity = ofxGgmlEmbeddingIndex::cosineSimilarity(
            queryResult.embedding,
            docEmb.embedding
        );
        scores.push_back(similarity);
    }
}
```

## Performance Considerations

- **Batch Size**: Larger batches amortize overhead but may increase latency
- **Concurrency**: More concurrent requests can saturate resources
- **Memory**: Each concurrent request holds context in memory
- **Server Warmup**: First batch may be slower due to model loading
- **Network Latency**: Server backend adds network round-trip time

## See Also

- [Quick Wins Documentation](QUICK_WINS.md) - Streaming API and metrics
- [Performance Guide](PERFORMANCE.md) - Performance tuning
- Main API: `src/inference/ofxGgmlInference.h`
- Metrics API: `src/core/ofxGgmlMetrics.h`
