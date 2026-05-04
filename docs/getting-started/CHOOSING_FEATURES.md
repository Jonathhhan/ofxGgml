# Choosing Features Guide

ofxGgml uses a **layered architecture** - you include only the features you need. This reduces compile times and complexity.

## Quick Decision

**Most users should start with `#include "ofxGgmlBasic.h"`** - it provides text inference which covers 80% of use cases.

## The Core Layers

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
- Streaming with backpressure control (live server-side token streaming uses built-in HTTP transports; Linux/macOS `http://` streaming is covered in headless CI)
- Batch processing
- Chat and text assistants
- Prompt templates

**Use when:** You need chat, summarization, translation, or any text AI.

### Layer 3: Modalities (Optional Adapter Layer)
**Header:** `#include "ofxGgmlModalities.h"`
**Size:** Basic + ~12,000 LOC

What you get (includes Basic plus):
- Speech-to-text (Whisper)
- Text-to-speech (Piper, OuteTTS)
- Vision (image understanding)
- Video (frame analysis)
- Image generation (Stable Diffusion integration)
- CLIP embeddings

**Use when:** You explicitly need audio or visual AI capabilities. These APIs are split from the default `ofxGgml.h` boundary so text-only projects do not inherit speech/TTS/vision/diffusion surfaces.

### Layer 4: Workflows
**Header:** `#include "ofxGgmlWorkflows.h"`
**Size:** Basic + ~10,000 LOC

What you get (includes Basic plus):
- Text/metadata video planning and editing manifests
- Citation search
- Web crawling for RAG

**Use when:** You need optional video planning, citation, crawler, or RAG helpers without adopting multimodal adapters or companion media-application prototypes.

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

## Companion / Example-Tier Workflows

Video essay orchestration, subtitle montage planning/export, music generation, AceStep bridging, MilkDrop preset generation, and Holoscan bridge code are no longer default addon-tier APIs. For existing examples or experiments that intentionally use them, define `OFXGGML_ENABLE_COMPANION_WORKFLOWS=1` before including `ofxGgmlEasy.h`, or include `ofxGgmlCompanionWorkflows.h`. Treat those surfaces as candidates for standalone companion addons or example-level code.

## Need Multiple Layers?

You can include multiple layered headers in the same project:

```cpp
#include "ofxGgmlBasic.h"      // For text inference
#include "ofxGgmlModalities.h"  // Also get speech/vision
#include "ofxGgmlAssistants.h"  // Also get code assistants
```

Or for a comprehensive application (like the GUI example), include all layers:

```cpp
#include "ofxGgmlCore.h"
#include "ofxGgmlBasic.h"
#include "ofxGgmlModalities.h"  // Optional speech/vision/TTS/diffusion adapters
#include "ofxGgmlWorkflows.h"   // Optional planning/research helpers
#include "ofxGgmlAssistants.h"
// Optional companion/example tier:
#include "ofxGgmlCompanionWorkflows.h"
```

## Project Examples

### Example 1: Simple Chatbot

**Goal:** Local chat interface

**Include:** `ofxGgmlBasic.h`

**You get:**
- Text inference
- Low-level text generation helpers; add `support/ofxGgmlEasy.h` for `ofxGgmlEasy::chat()`
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
- Companion-tier media prototypes
- Code assistants

### Example 3: Citation-Grounded Research Tool

**Goal:** Crawl sources, find citations, and produce grounded summaries

**Include:** `ofxGgmlWorkflows.h`

**You get:**
- Everything up to workflows
- Citation search
- Web crawling / RAG helpers
- Text, speech, vision

**You skip:**
- Code assistants
- Companion-tier video essay, music, MilkDrop, AceStep, and Holoscan prototypes

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
- Companion-tier media prototypes

### Example 5: Comprehensive Application

**Goal:** Build an application-style example with companion prototypes (like GUI example)

**Include:** All layers
```cpp
#include "ofxGgmlCore.h"
#include "ofxGgmlBasic.h"
#include "ofxGgmlModalities.h"
#include "ofxGgmlWorkflows.h"
#include "ofxGgmlAssistants.h"
// Optional companion/example tier:
#include "ofxGgmlCompanionWorkflows.h"
```

**You get:** Core addon layers plus companion/example-tier prototypes

**Use when:** Building comprehensive examples that intentionally opt into companion media workflows

## External Dependencies

Some features require companion addons:

| Feature | Requires | Optional? |
|---------|----------|-----------|
| Image generation | `ofxStableDiffusion` | Yes |
| CLIP embeddings | Built-in (bundled) | No - included |
| Montage helpers (companion tier) | `ofxGgmlCompanionWorkflows.h` | No |
| AceStep music (companion tier) | `acestep.cpp` server + `ofxGgmlCompanionWorkflows.h` | Yes |
| VLC preview | `ofxVlc4` | Yes |
| Holoscan bridge (companion tier) | Holoscan runtime + `ofxGgmlCompanionWorkflows.h` | Yes |

**Note**: CLIP support is now bundled directly in `libs/clip/` and automatically available after building ggml.

**All optional addons** can be added as needed - core features work standalone.

## Compile Time Impact

Approximate build times (Release, 8-core machine):

| Header | Additional Compile Time |
|--------|------------------------|
| `ofxGgmlCore.h` | +2 min |
| `ofxGgmlBasic.h` | +3 min |
| `ofxGgmlModalities.h` | +5 min |
| `ofxGgmlWorkflows.h` | +7 min |
| `ofxGgmlAssistants.h` | +4 min |
| All 5 layers | +8 min |

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
3. Start with focused header for your project
4. Narrow down to specific layer for production

## Getting Help

**Can't decide?** Start with `ofxGgmlBasic.h` - it covers 80% of use cases.

**Need multiple features?** Look at which header includes all of them:
- Text + Vision → `ofxGgmlModalities.h`
- Text + Code assistant → `ofxGgmlAssistants.h`
- Text + Video planning → `ofxGgmlWorkflows.h`

**Still unsure?** Open an issue with your use case: https://github.com/Jonathhhan/ofxGgml/issues
