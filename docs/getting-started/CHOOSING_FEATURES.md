# Choosing Features Guide

ofxGgml uses a **layered architecture** - you include only the features you need. This reduces compile times and complexity.

## Quick Decision

**Most users should start with `#include "ofxGgmlBasic.h"`** - it provides text inference which covers 80% of use cases.

## The Five Layers

### Layer 1: Core
**Header:** `#include "ofxGgmlCore.h"`
**Size:** ~5,000 LOC

What you get:
- Runtime and backend management (CPU/CUDA/Vulkan/Metal)
- Tensor operations (30+ ops)
- Graph building and execution
- GGUF model loading

**Use when:** You only need low-level tensor operations without AI inference.

### Layer 2: Basic (Recommended Start)
**Header:** `#include "ofxGgmlBasic.h"`
**Size:** Core + ~8,000 LOC

What you get (includes Core plus):
- LLM text inference (llama-server and CLI backends)
- Streaming with backpressure control
- Batch processing
- `ofxGgmlEasy` facade
- Chat and text assistants
- Prompt templates

**Use when:** You need chat, summarization, translation, or any text AI.

### Layer 3: Modalities
**Header:** `#include "ofxGgmlModalities.h"`
**Size:** Basic + ~12,000 LOC

What you get (includes Basic plus):
- Speech-to-text (Whisper)
- Text-to-speech (Piper, OuteTTS)
- Vision (image understanding)
- Video (frame analysis)
- Image generation (Stable Diffusion integration)
- CLIP embeddings

**Use when:** You need audio or visual AI capabilities.

### Layer 4: Workflows
**Header:** `#include "ofxGgmlWorkflows.h"`
**Size:** Modalities + ~10,000 LOC

What you get (includes Modalities plus):
- Video planning and editing
- Montage planning with EDL export
- Citation search
- Video essay pipeline
- Music generation
- MilkDrop presets
- Web crawling for RAG

**Use when:** You need domain-specific creative or research tools.

### Layer 5: Assistants
**Header:** `#include "ofxGgmlAssistants.h"`
**Size:** Basic + ~6,000 LOC

What you get (includes Basic plus):
- Chat assistant (multi-turn conversations)
- Code assistant (semantic search, completions)
- Workspace assistant (patch validation)
- Coding agent (orchestration)
- Code review

**Use when:** You need AI coding or review tools.

### Complete Set (Not Recommended)
**Header:** `#include "ofxGgml.h"`
**Size:** All layers (~41,000 LOC)

**⚠️ Warning:** This includes everything and will slow down compilation significantly.

**Only use when:** You're exploring all features or building the comprehensive GUI example.

## Project Examples

### Example 1: Simple Chatbot

**Goal:** Local chat interface

**Include:** `ofxGgmlBasic.h`

**You get:**
- Text inference
- `ofxGgmlEasy::chat()`
- Conversation memory

**You skip:**
- Speech, vision
- Video workflows
- Code assistants

### Example 2: Audio Transcription Tool

**Goal:** Transcribe audio files

**Include:** `ofxGgmlModalities.h`

**You get:**
- Core + basic + speech
- Whisper integration
- Optional TTS for playback

**You skip:**
- Video workflows
- Music generation
- Code assistants

### Example 3: Video Essay Creator

**Goal:** Research → script → narration → video

**Include:** `ofxGgmlWorkflows.h`

**You get:**
- Everything up to workflows
- Citation search
- Video essay pipeline
- Text, speech, vision

**You skip:**
- Code assistants
- MilkDrop generation

### Example 4: AI Code Helper

**Goal:** Semantic code search and editing

**Include:** `ofxGgmlAssistants.h`

**You get:**
- Core + basic + code assistants
- Semantic retrieval
- Patch validation

**You skip:**
- Speech, vision
- Video workflows
- Music generation

### Example 5: Everything

**Goal:** Comprehensive AI toolkit

**Include:** `ofxGgml.h`

**You get:** All features

**Use when:** You want maximum flexibility or are building the GUI example

## External Dependencies

Some features require companion addons:

| Feature | Requires | Optional? |
|---------|----------|-----------|
| Image generation | `ofxStableDiffusion` | Yes |
| CLIP embeddings | `clip.cpp` binaries | Yes |
| AceStep music | `acestep.cpp` server | Yes |
| VLC preview | `ofxVlc4` | Yes |
| Holoscan bridge | Holoscan runtime | Yes |

**All are optional** - core features work standalone.

## Compile Time Impact

Approximate build times (Release, 8-core machine):

| Header | Additional Compile Time |
|--------|------------------------|
| `ofxGgmlCore.h` | +2 min |
| `ofxGgmlBasic.h` | +3 min |
| `ofxGgmlModalities.h` | +5 min |
| `ofxGgmlWorkflows.h` | +7 min |
| `ofxGgmlAssistants.h` | +4 min |
| `ofxGgml.h` (all) | +8 min |

**Tip:** Use the smallest header that meets your needs to reduce build times during development.

## Runtime Memory Usage

Approximate memory overhead (excluding models):

| Layer | Memory Overhead |
|-------|----------------|
| Core | ~10 MB |
| Basic | +5 MB |
| Modalities | +15 MB |
| Workflows | +10 MB |
| Assistants | +8 MB |

**Note:** Model sizes dominate memory usage (1-8 GB typical).

## Migration Path

Start small and add features as needed:

1. **Start:** `ofxGgmlBasic.h` for text
2. **Add audio:** Change to `ofxGgmlModalities.h`
3. **Add workflows:** Change to `ofxGgmlWorkflows.h`
4. **Add code tools:** Also include `ofxGgmlAssistants.h`

Each layer includes the previous layers, so migration is just changing the include.

## Best Practices

### For Beginners
1. Start with `ofxGgmlBasic.h`
2. Use `ofxGgmlEasy` facade
3. Run `ofxGgmlChatExample`
4. Read [BASIC_INFERENCE.md](BASIC_INFERENCE.md)

### For Production
1. Include only needed layers
2. Use server backend for performance
3. Monitor with `ofxGgmlMetrics`
4. Review [../PERFORMANCE.md](../PERFORMANCE.md)

### For Exploration
1. Run `ofxGgmlGuiExample`
2. Try different modes
3. Include `ofxGgml.h` to experiment
4. Narrow down to specific layer for production

## Getting Help

**Can't decide?** Start with `ofxGgmlBasic.h` - it covers 80% of use cases.

**Need multiple features?** Look at which header includes all of them:
- Text + Vision → `ofxGgmlModalities.h`
- Text + Code assistant → `ofxGgmlAssistants.h`
- Text + Video planning → `ofxGgmlWorkflows.h`

**Still unsure?** Open an issue with your use case: https://github.com/Jonathhhan/ofxGgml/issues
