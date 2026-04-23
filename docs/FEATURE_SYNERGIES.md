# Feature Synergies in ofxGgml

This document catalogs the synergies between features in ofxGgml, including both realized integrations and opportunities for enhancement.

## Overview

ofxGgml contains extensive cross-feature integration, with strong shared infrastructure enabling multiple AI capabilities to work together. This analysis identifies:

1. **Existing Strong Synergies** - Features that already collaborate effectively
2. **Newly Implemented Synergies** - Recent additions that bridge features
3. **Future Opportunities** - Potential enhancements identified but not yet implemented

---

## 1. CORE SHARED INFRASTRUCTURE

### 1.1 Universal Text Inference Engine

**Component:** `ofxGgmlInference`
**Impact:** Foundation for 10+ features

**Shared by:**
- Citation search (source-grounded quote extraction)
- Code assistants (repository context, structured patches)
- Video planning (beat planning, scene generation)
- Music generation (ABC notation, prompt generation)
- MilkDrop presets (procedural generation)
- Text assistant (translation, summarization, rewriting)
- Chat assistant (conversation management)
- Media prompt generator (cross-modal translation)

**Key Capabilities:**
- `generateWithSources()` - source-grounded generation
- `generateWithUrls()` - URL-backed context
- `generateWithScriptSource()` - code repository context
- `embed()` / `embedBatch()` - semantic embeddings
- `countPromptTokens()` - context management

**Synergy Mechanism:** Single configuration point via `ofxGgmlEasy::syncTextBackends()` propagates settings to all consumers.

---

### 1.2 Embedding Infrastructure

**Component:** `ofxGgmlEmbeddingIndex` in `ofxGgmlInference`
**Use Cases:**
- **Code Review** - Semantic file ranking by query relevance
- **Citation Search** - RAG-style source chunk selection (top-16 most relevant)
- **General RAG** - Document retrieval for source-grounded generation

**Implementation:** Cosine similarity search over text embeddings

**Recent Improvement:** Citation search upgraded from top-8 to top-16 chunks for broader coverage (commit 905bf3d).

---

### 1.3 CLIP Embeddings

**Component:** `ofxGgmlClipInference`
**Modalities:** Text and image embeddings

**Existing Integration:**
- **Diffusion Reranking** - `ofxGgmlStableDiffusionAdapters` uses CLIP to score generated images against prompts

**Newly Implemented (commit 3374df3):**
- **Image Search Semantic Ranking** - `ofxGgmlImageSearch` now supports CLIP-based reranking via `useSemanticRanking` flag
  - Wikimedia keyword results reranked by semantic similarity
  - Automatic fallback if CLIP not configured
  - Opt-in for backward compatibility

**Future Opportunities:**
- Video planning scene validation
- Media prompt translation quality verification
- Vision task pre-filtering

---

### 1.4 Subtitle/Segment Infrastructure

**Component:** `ofxGgmlSpeechSegment`
**Format:** Timestamped text segments with start/end times

**Pipeline Flow:**
```
Speech Transcription → Segments → Montage Planning → Subtitle Tracks → Preview/Playback
```

**Integration Points:**
1. **Speech → Montage**: `ofxGgmlMontagePlanner::segmentsFromSpeechSegments()` converts formats
2. **Montage → Preview**: `ofxGgmlMontagePreviewBridge` exposes dual subtitle tracks:
   - Montage-timed (selected clips only)
   - Source-timed (original positions)
3. **Preview → Playback**: ofxVlc4 integration for live preview with subtitles

**Standardization:** SRT/VTT format universally supported across all components.

---

### 1.5 Source-Grounded Generation System

**Component:** `ofxGgmlPromptSource` + fetch/build utilities
**Location:** `src/inference/ofxGgmlInferenceSourceInternals.h`

**Capabilities:**
- `fetchUrlSources()` - Fetch and normalize web content
- `collectScriptSourceDocuments()` - Aggregate code repository files
- `buildPromptWithSources()` - Assemble citation-ready prompts

**Shared By:**
- Citation search (extracts quotes from fetched sources)
- Code assistants (repository context injection)
- Video essay workflow (grounds outlines in research sources)

**Data Structure:**
```cpp
struct ofxGgmlPromptSource {
    std::string label;
    std::string uri;
    std::string content;
    bool isWebSource;
    bool wasTruncated;
};
```

---

## 2. REALIZED END-TO-END PIPELINES

### 2.1 Video Essay Workflow ✅

**Component:** `ofxGgmlVideoEssayWorkflow`

**Complete Pipeline:**
```
Topic → Citation Search → Outline → Script → Voice Cues → SRT → Scene Plans
```

**Integrated Components:**
- `ofxGgmlCitationSearch` (m_citationSearch)
- `ofxGgmlTextAssistant` (m_textAssistant)
- `ofxGgmlVideoPlanner` (m_videoPlanner)

**Output:** Unified JSON manifest containing:
- Citations with source URLs
- Outline with `[Source N]` references
- Script derived from outline
- SRT subtitles from voice cues
- Scene plan JSON
- Edit plan JSON

**Coordination:** All phases reference previous outputs (outline uses citations, script uses outline, etc.).

---

### 2.2 Speech-to-Montage Pipeline ✅

**Flow:**
```
Audio → Whisper STT → Segments → Montage Scoring → Clip Selection → EDL Export
```

**Format Conversions:**
1. Whisper produces `ofxGgmlSpeechSegment[]`
2. Montage planner converts via `segmentsFromSpeechSegments()`
3. Scoring ranks segments against goal
4. Outputs CMX EDL + SRT/VTT exports

**Preview Integration:** `ofxGgmlMontagePreviewBridge` packages:
- Playlist clips (sequential playback order)
- Dual subtitle tracks
- Duration calculations
- Cue lookup by timestamp

---

### 2.3 Media Prompt Translation ✅

**Component:** `ofxGgmlMediaPromptGenerator`

**Bidirectional Cross-Modal Translation:**
1. **Music → Image**: Music description + lyrics → visual diffusion prompt
2. **Image → Music**: Scene description → music generation prompt

**Integration:**
- Uses `ofxGgmlInference` for LLM-based translation
- Outputs feed directly to:
  - Diffusion inference (visual prompts)
  - Music generator (music prompts)

**Use Case:** Music video workflow uses Music→Image to generate visuals synchronized to audio.

---

## 3. NEWLY IMPLEMENTED SYNERGIES

### 3.1 CLIP + Image Search (commit 3374df3) ✨

**Problem:** Image search used keyword-only Wikimedia results with no semantic understanding.

**Solution:** Added optional CLIP-based semantic reranking.

**Implementation:**
- New `useSemanticRanking` flag in `ofxGgmlImageSearchRequest`
- New `semanticScore` field in `ofxGgmlImageSearchItem`
- `setClipInference()` method to configure CLIP backend
- `searchWithSemanticRanking()` method ranks results by cosine similarity

**Benefits:**
- Results ranked by semantic relevance to query, not just keyword match
- Leverages existing CLIP infrastructure (was only used for diffusion)
- Backward compatible (opt-in flag)

**Usage:**
```cpp
ofxGgmlImageSearch imageSearch;
imageSearch.setClipInference(&clipInference);

ofxGgmlImageSearchRequest request;
request.prompt = "neon city at night";
request.useSemanticRanking = true;

auto result = imageSearch.search(request);
// Results sorted by semanticScore (high to low)
```

---

### 3.2 Enhanced Citation Search Coverage (commit 905bf3d) ✨

**Changes:**
- Increased RAG retrieval from top-8 to top-16 chunks
- Increased realtime source fetching from 4 to 12 minimum sources
- Enhanced prompt with explicit source attribution requirements
- Added source diversity prioritization instruction

**Impact:**
- 2x broader chunk coverage for citation extraction
- 3x more diverse sources in realtime mode
- Stronger enforcement of exact quotes with source indices

---

### 3.3 CLIP Scene Coherence Validation (2026-04-23) ✨

**Implementation:** `ofxGgmlVideoPlanner::validateSceneCoherence()`

**Problem:** Video planning generated multi-scene plans without verifying that scenes aligned with the overall vision.

**Solution:** Use CLIP text embeddings to validate scene coherence.

**How it works:**
1. Embed the overall scene description/prompt as reference
2. For each scene, embed its event prompt or title+summary
3. Compute cosine similarity between overall and scene embeddings
4. Warn about scenes with low coherence (<0.5 similarity)
5. Return average coherence score and per-scene warnings

**Usage:**
```cpp
auto planResult = videoPlanner.plan(modelPath, request, settings, inference);
if (planResult.success && clipInference) {
    auto coherence = ofxGgmlVideoPlanner::validateSceneCoherence(
        planResult.plan, &clipInference);
    if (coherence.success && coherence.averageScore < 0.6f) {
        // Warn user about low overall coherence
        for (const auto& warning : coherence.warnings) {
            ofLogWarning() << warning;
        }
    }
}
```

**Benefits:**
- Early detection of off-topic scenes before generation
- Quantitative coherence metrics for plan quality
- Leverages existing CLIP infrastructure

---

### 3.4 Vision-Based Diffusion Validation (2026-04-23) ✨

**Implementation:** `ofxGgmlDiffusionInference::validateWithVision()`

**Problem:** Diffusion generates images but doesn't verify they match the prompt.

**Solution:** Use vision inference to describe generated images, then compute semantic alignment.

**How it works:**
1. For each generated image, use vision model to create description
2. If text inference available, embed both original prompt and description
3. Compute cosine similarity for alignment score
4. Return per-image scores and descriptions

**Usage:**
```cpp
auto genResult = diffusion.generate(request);
if (genResult.success && visionInference && textInference) {
    auto validation = ofxGgmlDiffusionInference::validateWithVision(
        genResult, request.prompt, &visionInference,
        visionProfile, &textInference);

    for (const auto& [idx, score] : validation.imageScores) {
        if (score < 0.6f) {
            // This image poorly matches the prompt
            ofLogWarning() << "Image " << idx << " alignment: " << score;
        }
    }
}
```

**Benefits:**
- Automated quality checking for generated images
- Close the generate→analyze→verify loop
- Enable prompt refinement based on analysis

---

### 3.5 Video Analysis Hints for Planning (2026-04-23) ✨

**Implementation:** `ofxGgmlVideoPlanner::extractHintsFromVideoAnalysis()` + `enrichPlanningPromptWithHints()`

**Problem:** Video analysis produces insights (emotion, action, pacing) but scene planning doesn't use them.

**Solution:** Extract structured hints from video analysis and inject them into planning prompts.

**How it works:**
1. `extractHintsFromVideoAnalysis()` extracts:
   - Primary emotion/action with confidence
   - Secondary action labels
   - Suggested pacing (dynamic, slow, moderate)
   - Suggested tone (reflective, intense, balanced)
   - Timeline progression
2. `enrichPlanningPromptWithHints()` augments planning prompt with these hints
3. Planner uses hints to inform scene tone, pacing, and progression

**Usage:**
```cpp
// Analyze source video first
auto videoResult = videoInference.runTemporalSidecarRequest(videoRequest);
if (videoResult.success && !videoResult.structured.primaryLabel.empty()) {
    // Extract hints from analysis
    auto hints = ofxGgmlVideoPlanner::extractHintsFromVideoAnalysis(
        videoResult.structured);

    // Build base planning prompt
    std::string basePrompt = ofxGgmlVideoPlanner::buildPlanningPrompt(planRequest);

    // Enrich with analysis hints
    std::string enrichedPrompt =
        ofxGgmlVideoPlanner::enrichPlanningPromptWithHints(basePrompt, hints);

    // Use enriched prompt for planning (modifies request prompt)
}
```

**Benefits:**
- Context-aware planning based on actual video content
- Better pacing and tone decisions informed by analysis
- Connects analytical and generative pipelines

---

## 4. ARCHITECTURAL PATTERNS ENABLING SYNERGY

### 4.1 Bridge Backend Pattern ✅

**Examples:**
- `ofxGgmlClipBridgeBackend` - CLIP embeddings
- `ofxGgmlStableDiffusionBridgeBackend` - Image generation
- `ofxGgmlMusicGenerationBridgeBackend` - Audio generation
- `ofxGgmlTtsBridgeBackend` - Speech synthesis
- `ofxGgmlWebCrawlerBridgeBackend` - Website ingestion

**Design:** Adapter pattern with `std::function<>` callbacks

**Benefit:** Features can share backend instances (e.g., multiple consumers use same CLIP backend).

---

### 4.2 Validation Loop Framework ✅ (2026-04-23) ✨

**Implementation:** `src/inference/ofxGgmlValidationLoop.h` (~200 lines)

**Design:** Generic template-based validation loop pattern for generate→validate→refine cycles.

**Core Components:**
```cpp
template<typename TGenerated, typename TAnalysis>
class ofxGgmlValidationLoop {
public:
    using GenerateFunction = std::function<TGenerated(int attemptNumber)>;
    using ValidateFunction = std::function<TAnalysis(const TGenerated&)>;
    using ScoreFunction = std::function<float(const TGenerated&, const TAnalysis&)>;
    using RefineFunction = std::function<void(TGenerated&, const TAnalysis&, float score)>;

    void setGenerator(GenerateFunction fn);
    void setValidator(ValidateFunction fn);
    void setScorer(ScoreFunction fn);
    void setRefiner(RefineFunction fn);

    ofxGgmlValidationLoopResult<TGenerated, TAnalysis> run();
};

struct ofxGgmlValidationLoopConfig {
    int maxAttempts = 3;
    float qualityThreshold = 0.6f;
    bool enableRefinement = true;
    bool collectAllAttempts = false;
    float improvementThreshold = 0.1f;
};
```

**Features:**
- **Configurable retry logic**: Set max attempts and quality threshold
- **Progress monitoring**: Optional callbacks with cancellation support
- **Refinement loop**: Automatic retry with improvements based on feedback
- **Result aggregation**: Collects all attempts or just the best one
- **Improvement tracking**: Stops early if refinement isn't helping

**Concrete Helpers:**
```cpp
namespace ofxGgmlValidationLoops {
    // Diffusion → Vision validation loop
    ofxGgmlValidationLoopResult<...> validateDiffusionWithVision(
        const ofxGgmlImageGenerationRequest& request,
        ofxGgmlDiffusionInference* diffusion,
        ofxGgmlVisionInference* vision,
        const ofxGgmlVisionModelProfile& visionProfile,
        ofxGgmlInference* textInference,
        const ofxGgmlValidationLoopConfig& config);

    // Video Planning → CLIP validation loop
    ofxGgmlValidationLoopResult<...> validateVideoPlanWithCLIP(
        const ofxGgmlVideoPlannerRequest& request,
        const std::string& modelPath,
        const ofxGgmlInferenceSettings& settings,
        const ofxGgmlInference& inference,
        const ofxGgmlVideoPlanner& planner,
        ofxGgmlClipInference* clip,
        const ofxGgmlValidationLoopConfig& config);
}
```

**Usage Example:**
```cpp
// Custom validation loop
ofxGgmlValidationLoop<ImageResult, AnalysisResult> loop;

loop.setGenerator([&](int attempt) {
    auto request = baseRequest;
    if (attempt > 1) request.seed = -1; // Random seed for retry
    return diffusion.generate(request);
});

loop.setValidator([&](const ImageResult& result) {
    return vision.analyze(result.images[0].path);
});

loop.setScorer([](const ImageResult&, const AnalysisResult& analysis) {
    return analysis.alignmentScore;
});

loop.setRefiner([](ImageResult& result, const AnalysisResult& analysis, float score) {
    // Optionally modify parameters based on feedback
    result.refinementNotes = analysis.suggestions;
});

ofxGgmlValidationLoopConfig config;
config.maxAttempts = 5;
config.qualityThreshold = 0.8f;
loop.setConfig(config);

auto result = loop.run();
if (result.success) {
    // Use result.bestGenerated and result.bestAnalysis
}
```

**Benefit:**
- Transforms ofxGgml from "generate and hope" to "generate, verify, refine"
- Reusable pattern for all generative→analytical workflows
- Automated quality assurance with configurable thresholds
- Enables multi-attempt refinement strategies

---

### 4.3 Easy API Facade ✅

**Component:** `ofxGgmlEasy`

**Centralization:** Owns instances of:
- Inference, chat, text, vision, speech assistants
- Citation search (which owns web crawler)
- Video planner, media prompt generator, music generator
- Video essay workflow, long video planner, coding agent

**Synergy Mechanism:** `syncTextBackends()` propagates text config to 10+ components in one call.

**Gap:** No equivalent for vision/CLIP backends (each feature configures independently).

---

### 4.4 Consistent Result Types ✅

**Pattern:** All features return structured results with:
- `bool success`
- `float elapsedMs`
- `std::string error`
- Feature-specific data

**Benefit:** Easy pipeline chaining with consistent error propagation.

**Example:**
```cpp
auto citationResult = citationSearch.search(request);
if (!citationResult.success) return propagateError(citationResult);

auto scriptResult = textAssistant.custom(
    buildScriptPrompt(citationResult.citations));
if (!scriptResult.success) return propagateError(scriptResult);
```

---

## 5. IDENTIFIED OPPORTUNITIES (NOT YET IMPLEMENTED)

### 5.1 Video/Audio Analysis Not Used for Generation ⚠️

**Gap:** Analysis features produce insights but don't fully feed generation pipelines.

**Specific Opportunities:**
- Video emotion analysis → video planner scene tone hints (**✅ Complete - see Section 3.5**)
- Speech transcript keywords → music generator mood/style
- Subtitle prosody features → montage beat planning

**Note:** The video analysis hints integration (item 3.5) is now complete, addressing one key opportunity. Remaining work focuses on audio-to-generation bridges.

**Effort:** 2-4 hours per integration
**Value:** Medium (better contextual generation)

---

### 5.2 Code Assistants Isolated from Multi-Modal Features ⚠️

**Gap:** Code assistants don't leverage vision, speech, or citation capabilities.

**Missed Opportunities:**
1. **Vision + Code**: Analyze UI screenshots when reviewing React components
2. **Speech + Code**: Transcribe code review meetings → extract action items
3. **Citations + Code**: Ground code suggestions in official API documentation

**Example:**
```cpp
// Vision-enhanced code review
auto reviewResult = codeReview.review(repoPath, modelPath);
for (const auto& file : reviewResult.findings) {
    if (isUICode(file.path)) {
        auto screenshots = findRelatedScreenshots(file.path);
        for (const auto& shot : screenshots) {
            auto analysis = vision.describeImage(shot);
            // Compare to code comments/docs
        }
    }
}
```

**Effort:** 8-12 hours
**Value:** Medium (enhanced code review capabilities)

---

### 5.3 CLIP Integration Expansion ⚠️

**Current:** CLIP used for diffusion reranking, image search, and (**✅ Complete**) video scene coherence validation.

**Additional Opportunities:**
1. **Video Planning** - Validate scene coherence across multi-scene plans (**✅ Complete - see Section 3.3**)
2. **Media Translation** - Verify Music→Image translation quality (~40 lines)
3. **Vision Tasks** - Pre-filter images before expensive multimodal analysis (~30 lines)

**Note:** Video scene coherence validation (item 3.3) is now complete.

**Effort:** 1-2 hours for remaining integrations
**Value:** High (quality improvements using existing infrastructure)

---

### 5.4 Fragmented Embedding Indices ⚠️

**Problem:** Multiple features build separate embedding indices:
- Code review embeds file snippets
- Text inference embeds for RAG
- CLIP embeds images independently

**Opportunity:** Create `ofxGgmlUnifiedSemanticIndex` consolidating:
- Text embeddings (documents, code)
- CLIP embeddings (images, video frames)
- Cross-modal search (text query → both document and image results)

**Example:**
```cpp
class ofxGgmlUnifiedSemanticIndex {
    ofxGgmlEmbeddingIndex textIndex;
    ofxGgmlClipInference clipIndex;

    vector<Result> search(string query, SearchMode mode) {
        if (mode == TextOnly) return textIndex.search(query);
        if (mode == VisualOnly) return clipIndex.rankImagesForText(query);
        if (mode == MultiModal) {
            // Combine both and re-rank
        }
    }
};
```

**Effort:** 6-8 hours
**Value:** High (architectural improvement, enables new features)

---

## 6. SYNERGY IMPACT MATRIX

| Feature A | Feature B | Status | Lines Shared | Evidence |
|-----------|-----------|--------|--------------|----------|
| Text Inference | All Text Features | ✅ Complete | ~2000 (entire engine) | Universal backend |
| Speech | Montage | ✅ Complete | ~150 | Format conversion |
| Citation | Video Essay | ✅ Complete | ~300 | Workflow orchestration |
| CLIP | Diffusion | ✅ Complete | ~100 | Reranking logic |
| **CLIP** | **Image Search** | **✅ Complete (3374df3)** | **~85** | **Semantic ranking** |
| **CLIP** | **Video Planning** | **✅ Complete (new)** | **~70** | **Scene coherence validation** |
| **Vision** | **Diffusion** | **✅ Complete (new)** | **~65** | **Quality validation loop** |
| **Video Analysis** | **Video Planning** | **✅ Complete (new)** | **~90** | **Context hints for generation** |
| Inference Embeddings | Citation | ✅ Complete | ~200 | RAG retrieval |
| Web Crawler | Citation | ✅ Complete | ~150 | Source ingestion |
| Media Prompt | Diffusion | ✅ Complete | ~80 | Prompt translation |
| Speech | Music Gen | ⚠️ Missing | N/A | Emotion-driven prompts |
| Vision | Code Review | ⚠️ Missing | N/A | Screenshot analysis |

**Legend:**
- ✅ Complete: Fully integrated with active data flow
- ⚠️ Missing: Clear opportunity but not implemented

---

## 7. RECOMMENDATIONS

### Immediate (Completed) ✅
1. ✅ Add CLIP reranking to image search (~85 lines, 1-2h) - **DONE: commit 3374df3**
2. ✅ Enhance citation search coverage (~13 lines, 30m) - **DONE: commit 905bf3d**

### Near-Term (Completed in this session) ✅
3. ✅ Expand CLIP to video planning scene validation (~50 lines) - **DONE: validateSceneCoherence()**
4. ✅ Add vision validation to diffusion outputs (~40 lines) - **DONE: validateWithVision()**
5. ✅ Connect video analysis to scene planning (~60 lines) - **DONE: extractHintsFromVideoAnalysis() + enrichPlanningPromptWithHints()**

### Medium-Term (High Value, 6-12 hours each)
6. Create `ofxGgmlUnifiedSemanticIndex` (~200 lines, 6-8h)
7. ✅ Build validation loop framework (~200 lines, 4-6h) - **DONE: ofxGgmlValidationLoop template class**
8. Extend code assistants with vision/speech (~200 lines, 8-12h)

### Long-Term (Strategic)
9. Unify JSON manifest formats across workflows
10. Create cross-modal example gallery in documentation
11. Add `syncVisionBackends()` equivalent to Easy API

---

## 8. CONCLUSION

ofxGgml demonstrates **strong architectural foundations** with excellent shared infrastructure. Key strengths:

**Realized Synergies:**
- Universal text inference engine (10+ consumers)
- Complete speech-montage-preview pipeline
- End-to-end video essay orchestration
- CLIP + diffusion semantic scoring
- **CLIP + image search semantic ranking (commit 3374df3)**
- **CLIP + video planning scene coherence validation (NEW)**
- **Vision + diffusion quality validation loops (NEW)**
- **Video analysis → scene planning context hints (NEW)**
- **Validation loop framework for generative→analytical workflows (NEW)**

**Architectural Enablers:**
- Bridge backend pattern (flexible adapters)
- Consistent result types (easy chaining)
- Source grounding system (URL/repo context)
- SRT standardization (universal subtitle format)
- **Generic validation loop templates (NEW)**

**Highest Value Remaining Opportunities:**
1. **Unified semantic index** - Share embeddings across features
2. **Code assistant multimodal extensions** - Vision for UI review, speech for meetings
3. **Audio-to-generation bridges** - Speech analysis hints for music/montage

**Strategic Priority:** The four synergies implemented in this session (CLIP scene coherence, vision-based diffusion validation, video analysis hints, and the reusable validation loop framework) demonstrate the value of closing generative→analytical loops. The validation patterns established here transform ofxGgml from "generate and hope" to "generate, verify, refine" with configurable retry logic and automated quality checking.

---

## Change Log

- **2026-04-23**: Implemented four synergies (CLIP video validation, vision diffusion validation, video analysis hints, validation loop framework)
- **2026-04-21**: Initial synergy analysis and documentation
- **2026-04-21**: Implemented CLIP semantic ranking for image search (commit 3374df3)
- **Earlier**: Enhanced citation search with broader source coverage (commit 905bf3d)
