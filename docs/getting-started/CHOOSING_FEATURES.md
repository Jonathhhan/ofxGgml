# Choosing Features Guide

ofxGgml is a comprehensive AI toolkit with many features. This guide helps you choose the right subset for your project.

## Decision Tree

### 1. What do you want to do?

**Just text AI (chat, summarize, translate)**
→ Use `ofxGgmlBasic.h`
→ See [BASIC_INFERENCE.md](BASIC_INFERENCE.md)

**Audio processing (transcription, TTS)**
→ Use `ofxGgmlModalities.h`
→ See [../features/MODALITIES.md](../features/MODALITIES.md#speech)

**Image understanding or generation**
→ Use `ofxGgmlModalities.h`
→ See [../features/MODALITIES.md](../features/MODALITIES.md#vision)

**Video editing/planning workflows**
→ Use `ofxGgmlWorkflows.h`
→ See [../features/WORKFLOWS.md](../features/WORKFLOWS.md#video)

**Code assistant features**
→ Use `ofxGgmlAssistants.h`
→ See [../features/ASSISTANTS.md](../features/ASSISTANTS.md)

**Everything**
→ Use `ofxGgml.h`
→ See [../README.md](../README.md)

## Feature Layers

### Layer 1: Core (Always Included)

When you include any ofxGgml header, you get:
- Runtime and backend management (CPU/CUDA/Vulkan/Metal)
- Tensor operations (30+ ops)
- Graph building and execution
- GGUF model loading
- Logging and metrics

**Include:** `ofxGgmlCore.h`
**Size:** ~5,000 LOC
**Use when:** You only need low-level tensor operations

### Layer 2: Basic Inference

Adds text-only LLM inference:
- llama-server backend (recommended)
- CLI fallback backend
- Streaming with backpressure
- Batch processing
- `ofxGgmlEasy` facade
- Chat and text assistants

**Include:** `ofxGgmlBasic.h`
**Size:** +8,000 LOC
**Use when:** You need chat, summarization, translation

**Recommended starting point for most projects.**

### Layer 3: Modalities

Adds multimodal AI:
- **Speech**: Whisper transcription, translation
- **TTS**: Piper and OuteTTS synthesis
- **Vision**: Image understanding (LLaVA-style)
- **Video**: Frame sampling analysis
- **Diffusion**: Stable Diffusion integration
- **CLIP**: Text/image embeddings

**Include:** `ofxGgmlModalities.h`
**Size:** +12,000 LOC
**Use when:** You need audio/visual AI

### Layer 4: Workflows

Adds specialized creative pipelines:
- **Video planning**: Beat planning, multi-scene scripts
- **Montage**: Subtitle-driven editing, EDL export
- **Citation search**: Source-grounded research
- **Video essay**: Topic → script → narration pipeline
- **Music**: Prompt generation, ABC notation
- **MilkDrop**: Visualization presets
- **Web crawling**: Site ingestion for RAG
- **Media translation**: Music ↔ Image prompts

**Include:** `ofxGgmlWorkflows.h`
**Size:** +10,000 LOC
**Use when:** You need domain-specific creative tools

### Layer 5: Assistants

Adds task-oriented AI helpers:
- **Chat assistant**: Multi-turn conversations
- **Code assistant**: Semantic search, completions
- **Workspace assistant**: Patch validation, diffs
- **Coding agent**: Orchestration with approvals
- **Code review**: Hierarchical analysis

**Include:** `ofxGgmlAssistants.h`
**Size:** +6,000 LOC
**Use when:** You need AI coding/review tools

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
