# Quick Start Guide

Get up and running with ofxGgml in 5 minutes.

## Installation

1. Clone the addon into your openFrameworks `addons` folder:

```bash
cd ~/openFrameworks/addons
git clone https://github.com/Jonathhhan/ofxGgml.git
```

2. Run the setup script:

**Linux/macOS:**
```bash
cd ofxGgml
./scripts/setup_linux_macos.sh
```

**Windows:**
```bat
cd ofxGgml
scripts\setup_windows.bat
```

This will:
- Build the ggml library with auto-detected GPU backends
- Download a recommended starter model (~1.5GB)
- Set up the local llama-server runtime

## Your First Project

### Option 1: Use an Example

The easiest way to get started is to open an existing example:

1. Use the openFrameworks Project Generator
2. Import `ofxGgmlBasicExample`, `ofxGgmlChatExample`, or `ofxGgmlWebScrapingExample`
3. Build and run

### Option 2: Create a New Project

1. Create a new project with the Project Generator
2. Add `ofxGgml` to `addons.make`
3. Include the header and write your first inference:

```cpp
// main.cpp or ofApp.cpp
#include "ofxGgmlBasic.h"

class ofApp : public ofBaseApp {
public:
    ofxGgmlEasy ai;

    void setup() {
        // Configure text inference
        ofxGgmlEasyTextConfig config;
        config.modelPath = ofToDataPath("models/qwen2.5-1.5b-instruct-q4_k_m.gguf");
        config.serverUrl = "http://127.0.0.1:8080";
        ai.configureText(config);

        // Run your first inference
        auto result = ai.chat("Explain machine learning in simple terms.", "English");

        if (result.inference.success) {
            ofLogNotice() << "AI Response:\n" << result.inference.text;
        } else {
            ofLogError() << "Error: " << result.inference.error;
        }
    }
};
```

## What Header to Include?

ofxGgml provides layered headers for incremental adoption:

| Header | What You Get | Use When |
|--------|--------------|----------|
| `ofxGgmlBasic.h` | Core + text inference | **You only need text/chat AI (recommended start)** |
| `ofxGgmlModalities.h` | Basic + speech/vision/TTS/images | You need multimodal AI |
| `ofxGgmlWorkflows.h` | Modalities + specialized pipelines | You need video/montage/research workflows |
| `ofxGgmlAssistants.h` | Basic + code/chat/review assistants | You need AI coding assistance |
| `ofxGgmlCore.h` | Runtime, tensors, models | You need low-level tensor operations |

**Recommended:** Start with `ofxGgmlBasic.h` for text-only projects.

**Need multiple features?** Include multiple headers:
```cpp
#include "ofxGgmlBasic.h"
#include "ofxGgmlModalities.h"  // Also get speech/vision
```

## Next Steps

- **Text-only workflows**: See [BASIC_INFERENCE.md](BASIC_INFERENCE.md)
- **Choose features**: See [CHOOSING_FEATURES.md](CHOOSING_FEATURES.md)
- **Speech/Vision**: See [../features/MODALITIES.md](../features/MODALITIES.md)
- **Full capabilities**: See [../README.md](../README.md)

## Common Issues

### "llama-server not found"
The server auto-starts if installed locally. To install:
```bash
./scripts/build-llama-server.sh  # Linux/macOS
scripts\build-llama-server.bat   # Windows
```

### "Model not found"
Download a model:
```bash
./scripts/download-model.sh 1    # Downloads recommended Qwen model
```

### "Backend initialization failed"
Try CPU-only mode:
```bash
./scripts/build-ggml.sh --cpu-only
```

## Getting Help

- **Examples**: Check the `ofxGgmlBasicExample`, `ofxGgmlChatExample`, `ofxGgmlWebScrapingExample`, `ofxGgmlGuiExample`
- **Documentation**: See `docs/` folder
- **Issues**: https://github.com/Jonathhhan/ofxGgml/issues
