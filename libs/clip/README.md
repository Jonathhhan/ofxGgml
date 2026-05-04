# Bundled CLIP Implementation

This directory contains a bundled version of [clip.cpp](https://github.com/monatis/clip.cpp) - a C++ implementation of OpenAI's CLIP for text and image embeddings.

## What is CLIP?

CLIP (Contrastive Language-Image Pre-training) is a neural network that jointly trains text and image encoders, enabling:
- Text-to-image similarity scoring
- Image ranking by text queries
- Semantic image search
- Zero-shot image classification

## Why Bundled?

The CLIP implementation is bundled directly into ofxGgml to provide out-of-the-box support for:
- `ofxGgmlClipInference` - Text/image embedding and ranking
- Diffusion image reranking via CLIP similarity
- Cross-modal similarity workflows

## Build Integration

CLIP sources are automatically included when you build ofxGgml. The headers are available at:
- `libs/clip/include/clip.h`
- `libs/clip/include/stb_image.h`

## Original Source

- **Project**: https://github.com/monatis/clip.cpp
- **License**: MIT (see LICENSE file)
- **Dependencies**: ggml (already bundled in ofxGgml)

## Usage

CLIP functionality is available through `ofxGgmlClipCppAdapters.h` which provides automatic detection and integration:

```cpp
#include "ofxGgmlModalities.h"

// CLIP headers are automatically found
ofxGgmlClipInference clip;
auto backend = ofxGgmlClipCppAdapters::createBackend("model.gguf");
clip.setBackend(backend);

// Encode text
auto textEmb = clip.embedText("A photo of a cat");

// Encode image
auto imageEmb = clip.embedImage("photo.jpg");

// Compute similarity
float similarity = ofxGgmlClipInference::cosineSimilarity(
    textEmb.embedding,
    imageEmb.embedding
);
```

## Models

CLIP requires a separate model file (e.g., `openai_clip-vit-base-patch32.gguf`). See the main ofxGgml documentation for model download links and usage examples.
