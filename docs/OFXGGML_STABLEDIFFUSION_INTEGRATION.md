# ofxGgml and ofxStableDiffusion Integration Guide

This guide documents the integration improvements between ofxGgml and ofxStableDiffusion, including enhanced error handling, capability detection, progress callbacks, and smart context caching.

## Overview

The integration between ofxGgml and ofxStableDiffusion follows a bridge pattern that keeps both addons independent while providing seamless interoperability. The integration has been significantly improved with:

1. **Enhanced Error Handling** - Structured error types with detailed diagnostics
2. **Model Capability Detection** - Runtime capability queries for loaded models
3. **Progress Callbacks** - Real-time generation progress monitoring
4. **Smart Context Caching** - Reduced model reload overhead
5. **Diagnostics & Metrics** - Detailed performance tracking

## Enhanced Error Handling

### Error Types

The integration now uses structured error types instead of generic error messages:

```cpp
enum class ofxGgmlImageGenerationErrorType {
    None = 0,
    ConfigurationError,  // Backend not configured, invalid settings
    ModelLoadError,      // Model file not found or failed to load
    ValidationError,     // Invalid request parameters
    GenerationError,     // Generation failed during processing
    ResourceError,       // File system, memory, or resource issues
    TimeoutError,        // Operation exceeded timeout limit
    BackendError         // Backend-specific error
};
```

### Using Error Types

```cpp
ofxGgmlDiffusionInference diffusion;
// ... configure backend ...

ofxGgmlImageGenerationRequest request;
request.prompt = "A beautiful landscape";
request.modelPath = "models/sd15.safetensors";

auto result = diffusion.generate(request);

if (!result.success) {
    ofLogError() << "Generation failed: " << result.error;
    ofLogError() << "Error type: "
                 << ofxGgmlDiffusionInference::errorTypeLabel(result.errorType);

    switch (result.errorType) {
    case ofxGgmlImageGenerationErrorType::ModelLoadError:
        // Handle model loading issues
        break;
    case ofxGgmlImageGenerationErrorType::ValidationError:
        // Handle validation errors
        break;
    case ofxGgmlImageGenerationErrorType::TimeoutError:
        // Handle timeout errors
        break;
    default:
        break;
    }
}
```

### Diagnostics

The result now includes detailed diagnostic information:

```cpp
struct ofxGgmlImageGenerationDiagnostics {
    float modelLoadTimeMs = 0.0f;         // Time to load/initialize model
    float generationTimeMs = 0.0f;        // Time for actual generation
    float postProcessTimeMs = 0.0f;       // Time for post-processing
    size_t peakMemoryMB = 0;              // Peak memory usage
    size_t contextReloads = 0;            // Number of context reloads
    std::string modelArchitecture;        // Detected architecture (SD1.5, SDXL, FLUX)
    std::string backendVersion;           // Backend version info
    std::vector<std::pair<std::string, std::string>> timingBreakdown;
};

// Access diagnostics
auto result = diffusion.generate(request);
if (result.success) {
    ofLogNotice() << "Generation time: " << result.diagnostics.generationTimeMs << "ms";
    ofLogNotice() << "Context reloads: " << result.diagnostics.contextReloads;
    ofLogNotice() << "Model architecture: " << result.diagnostics.modelArchitecture;
}
```

## Model Capability Detection

### Query Capabilities

Backends can now report their capabilities:

```cpp
struct ofxGgmlImageGenerationCapabilities {
    bool supportsTextToImage = false;
    bool supportsImageToImage = false;
    bool supportsInstructImage = false;
    bool supportsVariation = false;
    bool supportsRestyle = false;
    bool supportsInpaint = false;
    bool supportsUpscale = false;
    bool supportsControlNet = false;
    bool supportsLoRA = false;
    bool supportsProgressCallbacks = false;
    bool supportsBatchGeneration = false;
    int maxBatchSize = 1;
    std::vector<std::string> supportedSamplers;
    std::string modelArchitecture;
    std::string backendVersion;
};
```

### Usage Example

```cpp
ofxGgmlDiffusionInference diffusion;
// ... attach ofxStableDiffusion backend ...

auto capabilities = diffusion.getCapabilities();

if (capabilities.supportsInpaint) {
    ofLogNotice() << "Inpaint is supported";
}

if (capabilities.supportsProgressCallbacks) {
    ofLogNotice() << "Progress callbacks supported";
}

ofLogNotice() << "Max batch size: " << capabilities.maxBatchSize;
ofLogNotice() << "Supported samplers:";
for (const auto & sampler : capabilities.supportedSamplers) {
    ofLogNotice() << "  - " << sampler;
}
```

### Setting Capabilities in Adapter

When creating an ofxStableDiffusion backend, provide a capabilities callback:

```cpp
auto backend = std::dynamic_pointer_cast<ofxGgmlStableDiffusionBridgeBackend>(
    ofxGgmlDiffusionInference::createStableDiffusionBridgeBackend());

// Set capabilities function
backend->setGetCapabilitiesFunction([]() {
    ofxGgmlImageGenerationCapabilities caps;
    caps.supportsTextToImage = true;
    caps.supportsImageToImage = true;
    caps.supportsInstructImage = true;
    caps.supportsVariation = true;
    caps.supportsRestyle = true;
    caps.supportsUpscale = true;
    caps.supportsControlNet = true;
    caps.supportsLoRA = true;
    caps.supportsProgressCallbacks = true;
    caps.supportsBatchGeneration = true;
    caps.maxBatchSize = 4;
    caps.supportedSamplers = {"euler_a", "euler", "heun", "dpm2", "dpmpp2m", "lcm"};
    caps.modelArchitecture = "SD 1.5";
    caps.backendVersion = "0.1.0";
    return caps;
});

diffusion.setBackend(backend);
```

## Progress Callbacks

### Progress Structure

```cpp
struct ofxGgmlImageGenerationProgress {
    float progress = 0.0f;          // Overall progress 0.0 to 1.0
    int currentStep = 0;            // Current diffusion step
    int totalSteps = 0;             // Total diffusion steps
    int currentBatch = 0;           // Current batch being processed
    int totalBatches = 0;           // Total batches
    std::string currentPhase;       // e.g., "loading", "diffusion", "saving"
    float elapsedMs = 0.0f;         // Elapsed time in milliseconds
    bool cancelled = false;         // Set to true to request cancellation
};
```

### Using Progress Callbacks

```cpp
ofxGgmlImageGenerationRequest request;
request.prompt = "A futuristic cityscape";
request.steps = 30;

// Set progress callback
request.progressCallback = [](const ofxGgmlImageGenerationProgress & progress) {
    ofLogNotice() << "Progress: " << (progress.progress * 100.0f) << "%";
    ofLogNotice() << "Step: " << progress.currentStep << "/" << progress.totalSteps;
    ofLogNotice() << "Phase: " << progress.currentPhase;

    // Return false to cancel generation
    if (userRequestedCancel) {
        return false;
    }
    return true;  // Continue
};

auto result = diffusion.generate(request);
```

### Progress Callback in GUI

```cpp
class ofApp : public ofBaseApp {
public:
    float generationProgress = 0.0f;
    std::string currentPhase;

    void generateImage() {
        ofxGgmlImageGenerationRequest request;
        request.prompt = promptText;

        request.progressCallback = [this](const ofxGgmlImageGenerationProgress & progress) {
            generationProgress = progress.progress;
            currentPhase = progress.currentPhase;
            return true;
        };

        auto result = diffusion.generate(request);
    }

    void draw() {
        // Draw progress bar
        ofDrawRectangle(10, 10, generationProgress * 780, 30);
        ofDrawBitmapString("Phase: " + currentPhase, 10, 60);
    }
};
```

## Smart Context Caching

### Overview

Context caching reduces model reload overhead by tracking which model/settings are currently loaded and only reloading when necessary.

### Enabling Context Caching

```cpp
using namespace ofxGgmlStableDiffusionAdapters;

RuntimeOptions options;
options.enableContextCaching = true;  // Enabled by default
options.threads = 8;
options.weightType = SD_TYPE_F16;

auto backend = createImageBackend(stableDiffusionEngine, options);
diffusion.setBackend(backend);
```

### Cache Key Components

The context cache tracks:
- Model path
- VAE path
- TAESD path
- ControlNet path
- LoRA model directory
- Thread count
- Weight type

Changes to these parameters trigger a context reload. Changes to other parameters (steps, CFG scale, etc.) do not require reloading.

### Monitoring Cache Performance

```cpp
auto result = diffusion.generate(request);
if (result.success) {
    ofLogNotice() << "Context reloads: " << result.diagnostics.contextReloads;
    ofLogNotice() << "Model load time: " << result.diagnostics.modelLoadTimeMs << "ms";
}
```

## Integration Patterns

### Basic Integration

```cpp
#include "ofxGgml.h"

#if OFXGGML_HAS_OFXSTABLEDIFFUSION
#include "ofxGgmlStableDiffusionAdapters.h"
#endif

class ofApp : public ofBaseApp {
public:
    void setup() {
        #if OFXGGML_HAS_OFXSTABLEDIFFUSION
        // Create ofxStableDiffusion engine
        stableDiffusionEngine = std::make_shared<ofxStableDiffusion>();

        // Configure runtime options
        using namespace ofxGgmlStableDiffusionAdapters;
        RuntimeOptions options;
        options.threads = 8;
        options.enableContextCaching = true;

        // Create and attach backend
        auto backend = createImageBackend(stableDiffusionEngine, options);
        diffusion.setBackend(backend);
        #endif
    }

    ofxGgmlDiffusionInference diffusion;

    #if OFXGGML_HAS_OFXSTABLEDIFFUSION
    std::shared_ptr<ofxStableDiffusion> stableDiffusionEngine;
    #endif
};
```

### Advanced Integration with CLIP Reranking

```cpp
#if OFXGGML_HAS_OFXSTABLEDIFFUSION

// Setup CLIP inference for reranking
ofxGgmlClipInference clipInference;
// ... configure CLIP ...

// Create runtime options with CLIP
using namespace ofxGgmlStableDiffusionAdapters;
RuntimeOptions options;
options.clipInference = std::make_shared<ofxGgmlClipInference>(clipInference);
options.clipScorerName = "ofxGgmlClip";
options.enableContextCaching = true;

auto backend = createImageBackend(stableDiffusionEngine, options);
diffusion.setBackend(backend);

// Generate with reranking
ofxGgmlImageGenerationRequest request;
request.prompt = "A serene mountain lake";
request.selectionMode = ofxGgmlImageSelectionMode::Rerank;
request.batchCount = 4;
request.rankingPrompt = "photorealistic, high quality";

auto result = diffusion.generate(request);
if (result.success) {
    for (const auto & image : result.images) {
        if (image.selected) {
            ofLogNotice() << "Best image: " << image.path;
            ofLogNotice() << "Score: " << image.score;
        }
    }
}

#endif
```

## Best Practices

### 1. Check Capabilities Before Use

```cpp
auto caps = diffusion.getCapabilities();
if (caps.supportsInpaint) {
    // Use inpaint
} else {
    ofLogWarning() << "Inpaint not supported by current backend";
}
```

### 2. Handle Errors Gracefully

```cpp
auto result = diffusion.generate(request);
if (!result.success) {
    switch (result.errorType) {
    case ofxGgmlImageGenerationErrorType::ModelLoadError:
        ofSystemAlertDialog("Model file not found. Please check model path.");
        break;
    case ofxGgmlImageGenerationErrorType::ResourceError:
        ofSystemAlertDialog("Insufficient disk space or memory.");
        break;
    default:
        ofSystemAlertDialog("Generation failed: " + result.error);
        break;
    }
}
```

### 3. Use Progress Callbacks for Long Operations

```cpp
request.progressCallback = [](const ofxGgmlImageGenerationProgress & progress) {
    if (progress.currentStep % 5 == 0) {
        ofLogNotice() << "Step " << progress.currentStep << "/" << progress.totalSteps;
    }
    return !shouldCancel;
};
```

### 4. Enable Context Caching for Multiple Generations

```cpp
RuntimeOptions options;
options.enableContextCaching = true;  // Keep model loaded between generations
```

### 5. Monitor Diagnostics in Production

```cpp
if (result.success) {
    // Log performance metrics
    metricsLogger.record("generation_time_ms", result.diagnostics.generationTimeMs);
    metricsLogger.record("context_reloads", result.diagnostics.contextReloads);
    metricsLogger.record("peak_memory_mb", result.diagnostics.peakMemoryMB);
}
```

## Troubleshooting

### High Context Reload Count

If you see many context reloads:

1. Check if you're changing model paths between requests
2. Ensure `enableContextCaching` is true
3. Keep model-related settings consistent

### Memory Issues

```cpp
// Reduce memory usage
RuntimeOptions options;
options.vaeTiling = true;
options.freeParamsImmediately = true;
options.keepVaeOnCpu = true;  // For low VRAM systems
```

### Timeout Errors

```cpp
// Increase timeout for complex generations
RuntimeOptions options;
options.timeout = std::chrono::seconds(600);  // 10 minutes
```

## Version Compatibility

Follow the compatibility matrix in `docs/COMPATIBILITY.md`:

| ofxGgml | ggml revision | ofxStableDiffusion | stable-diffusion.cpp | Status |
|---------|---------------|--------------------|--------------------|--------|
| 1.0.4+  | TBD           | latest             | TBD                | Testing |

## See Also

- `docs/COMPATIBILITY.md` - Addon versioning and compatibility
- `docs/API_BACKEND_BOUNDARIES.md` - Backend integration principles
- `docs/FEATURE_SYNERGIES.md` - Cross-feature integration patterns
- `tests/test_diffusion_inference.cpp` - Test examples
