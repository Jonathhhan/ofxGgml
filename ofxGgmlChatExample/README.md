# ofxGgmlChatExample

A minimal example demonstrating local AI chat using ofxGgml.

## What This Example Shows

- Simple chat interface with text input
- Server-based inference with automatic fallback
- Conversation history management
- Response streaming
- Basic error handling

## Features

- Type messages and get AI responses
- Maintains conversation context
- Press 'c' to clear chat history
- Press 'q' to quit
- Automatic line wrapping for long responses

## Requirements

1. ofxGgml addon (follow main addon setup)
2. A text model (e.g., Qwen 2.5 1.5B)
3. Optional: llama-server for faster inference

## Setup

### 1. Install the Addon

```bash
cd ~/openFrameworks/addons
git clone https://github.com/Jonathhhan/ofxGgml.git
cd ofxGgml
./scripts/setup_linux_macos.sh  # or setup_windows.bat
```

### 2. Download a Model

```bash
./scripts/download-model.sh 1  # Downloads Qwen 2.5 1.5B (~1.5GB)
```

### 3. Generate the Project

1. Open openFrameworks Project Generator
2. Import this example
3. Make sure `ofxGgml` is in the addons list
4. Click "Generate"
5. Build and run

## Usage

1. Run the example
2. Type your message in the text field
3. Press Enter or click "Send"
4. AI response appears in the chat window
5. Continue the conversation - context is maintained

### Keyboard Shortcuts

- `Enter` - Send message
- `c` - Clear chat history
- `q` - Quit application

## Configuration

Edit the model path in `ofApp::setup()`:

```cpp
config.modelPath = ofToDataPath("models/qwen2.5-1.5b-instruct-q4_k_m.gguf");
```

The example uses `ofxGgmlBasic.h` which includes only text inference features, keeping it simple and focused.

## Code Structure

- `ofApp.h/cpp` - Main application (~200 lines)
- Single file example for clarity
- Uses `ofxGgmlEasy` facade for simplicity

## Troubleshooting

**"Model not found"**
- Check the model path in `ofApp::setup()`
- Download a model using `./scripts/download-model.sh`

**"Server not responding"**
- The example auto-falls back to CLI mode
- Server is optional for better performance

**Slow responses**
- First inference loads model (one-time)
- Use server mode for faster subsequent requests
- Try a smaller model

## Next Steps

- Check `ofxGgmlVisionExample` for image understanding
- Check `ofxGgmlSpeechExample` for audio transcription
- Check `ofxGgmlGuiExample` for full feature demo
- Read `docs/getting-started/BASIC_INFERENCE.md`
