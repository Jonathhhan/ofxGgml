# Modalities Guide

Learn how to use ofxGgml for multimodal AI: speech, vision, video, image generation, and CLIP embeddings.

## Overview

Include `ofxGgmlModalities.h` to get:
- All basic text inference features
- Speech-to-text (Whisper)
- Text-to-speech (Piper, OuteTTS)
- Vision and video understanding
- Image generation (Stable Diffusion)
- CLIP text/image embeddings

## Speech-to-Text

### Setup

```cpp
#include "ofxGgmlModalities.h"

ofxGgmlEasy ai;

// Configure speech backend
ofxGgmlEasySpeechConfig speechConfig;
speechConfig.modelPath = "models/ggml-base.en.bin";
speechConfig.cliExecutable = "whisper-cli";
speechConfig.serverUrl = "http://127.0.0.1:8081";  // Optional server

ai.configureSpeech(speechConfig);
```

### Transcribe Audio

```cpp
auto result = ai.transcribeAudio("recording.wav");

if (result.success) {
    cout << "Transcript: " << result.text << endl;
    cout << "Language: " << result.detectedLanguage << endl;
}
```

### Translate Audio

Translate speech to English:

```cpp
ofxGgmlSpeechInference speech;
ofxGgmlSpeechSettings settings;
settings.task = ofxGgmlSpeechTask::Translate;  // Not Transcribe
settings.language = "es";  // Source is Spanish

auto result = speech.process("spanish-audio.wav", settings);
// Returns English translation
```

### Live Microphone Transcription

```cpp
ofxGgmlLiveSpeechTranscriber transcriber;
transcriber.setup(&speechInference);

// Start recording
transcriber.startRecording();

// ... speak into microphone ...

// Stop and transcribe
transcriber.stopRecording();
auto transcript = transcriber.getLatestTranscript();
```

## Text-to-Speech

### Piper TTS (Recommended)

```cpp
ofxGgmlTtsInference tts;

ofxGgmlTtsSettings settings;
settings.backend = ofxGgmlTtsBackend::Piper;
settings.voiceModelPath = "models/piper/en_US-lessac-medium.onnx";
settings.executablePath = "piper";  // Auto-discovered

auto result = tts.synthesize("Hello world!", settings);

if (result.success) {
    // result.audioData contains WAV file bytes
    ofSaveBuffer("output.wav", result.audioData);
}
```

### OuteTTS via chatllm.cpp

```cpp
settings.backend = ofxGgmlTtsBackend::ChatLlm;
settings.voiceModelPath = "models/outetts.bin";
settings.executablePath = "chatllm";

auto result = tts.synthesize("Welcome to ofxGgml!", settings);
```

## Vision (Image Understanding)

### Setup

```cpp
ofxGgmlEasyVisionConfig visionConfig;
visionConfig.modelPath = "models/LFM2.5-VL-1.6B-Q4_0.gguf";
visionConfig.mmprojPath = "models/mmproj-LFM2.5-VL-1.6b-Q8_0.gguf";
visionConfig.serverUrl = "http://127.0.0.1:8080";

ai.configureVision(visionConfig);
```

### Describe Image

```cpp
auto result = ai.describeImage("photo.jpg");

if (result.success) {
    cout << result.description << endl;
}
```

### OCR (Extract Text)

```cpp
ofxGgmlVisionInference vision;
ofxGgmlVisionSettings settings;
settings.task = ofxGgmlVisionTask::OCR;
settings.profileHint = "GLM-OCR";  // OCR-optimized model

auto result = vision.processImage("document.jpg", "", settings);
// Extracts visible text from image
```

### Ask Questions About Images

```cpp
auto result = ai.describeImage(
    "scene.jpg",
    "What time of day is this? What's the weather like?"
);
```

### Process Multiple Images

```cpp
ofxGgmlVisionInference vision;

vector<string> imagePaths = {"img1.jpg", "img2.jpg", "img3.jpg"};
auto result = vision.processImages(
    imagePaths,
    "Compare these three images and describe the differences.",
    settings
);
```

## Video Understanding

### Sampled Frame Analysis

```cpp
ofxGgmlVideoInference video;
video.setVisionInference(&visionInference);

ofxGgmlVideoSettings settings;
settings.sampledFrameCount = 5;  // Extract 5 frames

auto result = video.processVideo(
    "clip.mp4",
    "Summarize what happens in this video.",
    settings
);
```

### Action/Emotion Analysis (Optional Sidecar)

For temporal understanding, configure a specialized video server:

```cpp
settings.task = ofxGgmlVideoTask::Action;
settings.sidecarUrl = "http://127.0.0.1:8090";
settings.sidecarModel = "temporal-action-v1";

auto result = video.processVideo("action-clip.mp4", "", settings);
```

## Image Generation

### Setup with ofxStableDiffusion

```cpp
#include "ofxGgmlModalities.h"

ofxGgmlDiffusionInference diffusion;

// Attach external Stable Diffusion engine
ofxStableDiffusion sdEngine;
diffusion.attachBackend(&sdEngine);

ofxGgmlDiffusionSettings settings;
settings.modelPath = "models/sd-v1.5.gguf";
settings.width = 512;
settings.height = 512;
settings.steps = 20;
```

### Text-to-Image

```cpp
settings.task = ofxGgmlDiffusionTask::TextToImage;

auto result = diffusion.generate(
    "A serene mountain landscape at sunset",
    settings
);

if (result.success && !result.images.empty()) {
    result.images[0].image.save("output.png");
}
```

### Image-to-Image

```cpp
settings.task = ofxGgmlDiffusionTask::ImageToImage;
settings.initImagePath = "input.jpg";
settings.strength = 0.7;  // How much to change

auto result = diffusion.generate(
    "Transform into an oil painting style",
    settings
);
```

### Inpainting

```cpp
settings.task = ofxGgmlDiffusionTask::Inpaint;
settings.initImagePath = "photo.jpg";
settings.maskImagePath = "mask.png";  // White = inpaint area

auto result = diffusion.generate(
    "A red sports car",
    settings
);
```

## CLIP Embeddings

### Text/Image Similarity

```cpp
ofxGgmlClipInference clip;

// Get text embedding
auto textEmb = clip.encodeText("A cat on a mat");

// Get image embedding
auto imageEmb = clip.encodeImage("photo.jpg");

// Compute similarity
float similarity = clip.cosineSimilarity(textEmb.embedding, imageEmb.embedding);
cout << "Similarity: " << similarity << endl;  // 0.0 to 1.0
```

### Rerank Images by Prompt

```cpp
vector<string> imagePaths = {"img1.jpg", "img2.jpg", "img3.jpg"};

auto ranked = clip.rankImages(
    "A sunny beach scene",
    imagePaths
);

// ranked[0] is the best match
cout << "Best match: " << ranked[0].path << " (score: " << ranked[0].score << ")" << endl;
```

### Semantic Image Search

```cpp
// Index a collection
vector<string> collection = {"cat1.jpg", "dog1.jpg", "cat2.jpg"};
auto embeddings = clip.encodeImages(collection);

// Search with text query
auto textQuery = clip.encodeText("A fluffy cat");
auto results = clip.findSimilar(textQuery.embedding, embeddings, 2);  // Top 2

for (auto& match : results) {
    cout << collection[match.index] << " - " << match.score << endl;
}
```

## Performance Tips

### Speech
- Use server mode for faster processing
- Base models are 5-10x faster than Large models
- Enable timestamp mode only when needed

### Vision
- LFM2.5-VL is faster and EU-legal vs Llama 3.2 Vision
- Use GLM-OCR profile for text-heavy images
- Reduce max_tokens for faster responses

### Image Generation
- Lower steps (15-20) for preview, higher (30-50) for final
- Smaller resolutions (512x512) are much faster
- Use CLIP reranking to select best from multiple generations

### CLIP
- Batch encode multiple images at once
- Cache embeddings for large collections
- Use lower precision models for speed

## Model Downloads

### Speech Models (Whisper)
```bash
# Download base English model (~150MB)
./scripts/download-whisper-model.sh base.en

# Or manually:
wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin
```

### TTS Models (Piper)
```bash
# Install Piper and download voice
./scripts/install-piper.ps1  # Windows
# Or download manually from https://github.com/OHF-Voice/piper1-gpl
```

### Vision Models
```bash
# LFM2.5-VL (recommended, EU-safe)
wget https://huggingface.co/LFM/LFM2.5-VL-1.6B-Q4_0-GGUF/resolve/main/lfm2.5-vl-1.6b-q4_0.gguf
wget https://huggingface.co/LFM/LFM2.5-VL-1.6B-Q4_0-GGUF/resolve/main/mmproj-lfm2.5-vl-1.6b-q8_0.gguf
```

### Diffusion Models
See ofxStableDiffusion documentation for model downloads.

## Next Steps

- **Video workflows**: [WORKFLOWS.md](WORKFLOWS.md)
- **Code assistants**: [ASSISTANTS.md](ASSISTANTS.md)
- **Performance tuning**: [../PERFORMANCE.md](../PERFORMANCE.md)

## Examples

- `ofxGgmlVisionExample` - Image understanding
- `ofxGgmlSpeechExample` - Audio transcription
- `ofxGgmlGuiExample` - All modalities in GUI
