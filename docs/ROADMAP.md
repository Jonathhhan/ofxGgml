# ofxGgml Development Roadmap

This document tracks planned features and enhancements for ofxGgml, organized by timeframe and priority.

**Last Updated**: 2026-04-21
**Current Version**: 1.0.4

---

## Legend

- ✅ **Complete** - Implemented and merged
- 🚧 **In Progress** - Currently being developed
- 📋 **Planned** - Designed and ready to implement
- 💡 **Proposed** - Under consideration

---

## Immediate (Next 2 Weeks)

### ✅ Complete Model Checksums
**Status**: Complete
**Effort**: 2-4 hours
**Priority**: HIGH - Security

All 6 model presets now have SHA256 checksums populated in `scripts/model-catalog.json`.

### ✅ Enhanced Streaming Progress
**Status**: Complete
**Effort**: 4-6 hours
**Priority**: MEDIUM-HIGH - Developer Experience
**Completed**: 2026-04-21

**What**: Add rich progress metrics to streaming inference callbacks.

**Implementation**:
- ✅ Added `ofxGgmlStreamingProgress` struct with metrics:
  - `tokensGenerated`, `estimatedTotal`, `percentComplete`
  - `tokensPerSecond`, `elapsedMs`, `currentChunk`
- ✅ Enhanced `ofxGgmlStreamingContext` with progress tracking methods
- ✅ Added `getProgress()` to build progress snapshots
- ✅ Updated README with documentation and examples
- ✅ Updated CHANGELOG with feature description

**Benefits**:
- Better UX with progress bars and ETA
- Token/sec performance metrics
- Non-breaking (backward compatible)

### ✅ Preset Workflow Helpers
**Status**: Complete
**Effort**: 4-6 hours
**Priority**: MEDIUM - Developer Experience
**Completed**: 2026-04-21

**What**: Add common workflow presets to `ofxGgmlEasy` API.

**Implemented Methods**:
- ✅ `summarizeAndTranslate()` - Summarize text then translate
- ✅ `transcribeAndSummarize()` - Transcribe audio then summarize
- ✅ `describeAndAnalyze()` - Describe image then analyze with text model
- ✅ `crawlAndSummarize()` - Crawl website then summarize findings

**Why**: Users manually chain these patterns frequently. Presets reduce boilerplate.

**Files Modified**:
- `src/support/ofxGgmlEasy.h` - Added declarations and result struct
- `src/support/ofxGgmlEasy.cpp` - Implemented workflow logic
- `README.md` - Added usage examples
- `CHANGELOG.md` - Documented new feature

---

## Short-Term (v1.1.0 - Next 2 Months)

### 📋 Result<T> Ex Variants
**Status**: Planned (Already in IMPROVEMENTS_ROADMAP.md)
**Effort**: 12-16 hours
**Priority**: MEDIUM - API Quality

**What**: Add `Result<T>` variants alongside existing bool-returning methods.

**Approach**:
- Non-breaking: Add `setupEx()`, `allocGraphEx()`, etc.
- Keep existing APIs unchanged
- Migrate one example to demonstrate usage

### 📋 Semantic Cache
**Status**: Planned
**Effort**: 14-18 hours
**Priority**: MEDIUM-HIGH - Performance

**What**: Cache inference results by semantic similarity.

**Implementation**:
```cpp
class ofxGgmlSemanticCache {
    optional<CachedResult> lookup(string prompt, float threshold = 0.95f);
    void store(string prompt, string result, float ttlSeconds);
};
```

**Mechanism**:
- Compute embedding of incoming prompt
- Search cached embeddings with cosine similarity
- Return cached result if similarity > threshold
- Time-based TTL for simple invalidation

**Benefits**:
- 0ms vs 200ms for similar/repeated questions
- Works across paraphrased prompts
- Leverages existing embedding infrastructure

### ✅ Memory Usage Reporting
**Status**: Complete
**Effort**: 2-3 hours
**Priority**: LOW-MEDIUM - Monitoring
**Completed**: 2026-04-21

**What**: Add `getMemoryUsage()` to report current model memory consumption.

**Implementation**:
- ✅ Added `ofxGgmlMemoryUsage` struct
- ✅ Implemented `ofxGgml::getMemoryUsage()` method
- ✅ Tracks model weights, graph allocations, backend memory stats
- ✅ Updated README and CHANGELOG

### ✅ Server Queue Status API
**Status**: Complete
**Effort**: 2-3 hours
**Priority**: LOW-MEDIUM - Monitoring
**Completed**: 2026-04-21

**What**: Expose `getServerQueueStatus()` to query llama-server queue state.

**Implementation**:
- ✅ Added `ofxGgmlServerQueueStatus` struct
- ✅ Implemented `ofxGgmlInference::getServerQueueStatus()` method
- ✅ Queries llama-server /metrics endpoint
- ✅ Updated README and CHANGELOG

---

## Medium-Term (v1.2.0 - 3-6 Months)

### 📋 Hybrid RAG with Embeddings
**Status**: Planned
**Effort**: 16-20 hours
**Priority**: MEDIUM-HIGH - Quality

**What**: Combine keyword-based (BM25) and semantic (embedding) retrieval in `ofxGgmlRAGPipeline`.

**Enhancement**:
```cpp
struct ofxGgmlHybridRAGSettings {
    float keywordWeight = 0.4f;    // BM25 contribution
    float semanticWeight = 0.6f;   // Embedding contribution
    bool useReranking = true;
};
```

**Benefits**:
- Better retrieval accuracy (proven in research)
- Combines exact match + semantic understanding
- Builds on newly-added `ofxGgmlRAGPipeline`

### 📋 Health Monitoring
**Status**: Planned
**Effort**: 10-14 hours
**Priority**: MEDIUM-HIGH - Production Readiness

**What**: System health and diagnostics API.

**Features**:
```cpp
struct ofxGgmlHealthStatus {
    bool healthy;
    float cpuUsagePercent;
    size_t memoryUsedMB, vramUsedMB;
    float averageLatencyMs;
    size_t totalRequests, failedRequests;
    bool serverReachable;
    int serverQueueLength;
};
```

**Benefits**:
- Critical for production deployments
- Auto-recovery triggers
- GUI status displays

### 📋 Model Hub Download
**Status**: Planned
**Effort**: 10-12 hours (reduced scope)
**Priority**: MEDIUM - User Experience

**What**: Direct download from Hugging Face Model Hub.

**Scope** (Phase 1):
- Download by repo ID + filename
- HTTP resume support
- SHA256 verification from hub
- Defer search/browse to later

**API**:
```cpp
// In ofxGgmlEasy
bool downloadModel(string repoId, string filename, ProgressCallback);
```

### 📋 Complete RAII Integration
**Status**: Planned (Already in IMPROVEMENTS_ROADMAP.md)
**Effort**: 8-12 hours
**Priority**: MEDIUM - Code Quality

**What**: Use RAII guards throughout `ofxGgml::Impl`.

---

## Long-Term (v2.0.0 - 6-12 Months)

### 📋 LoRA Adapter Support
**Status**: Planned
**Effort**: 20-24 hours
**Priority**: HIGH - Customization

**What**: Load/swap LoRA adapters for model customization.

**API**:
```cpp
class ofxGgmlLoRAAdapter {
    bool loadAdapter(string loraPath);
    bool unloadAdapter();
    void setAdapterWeight(float weight);  // Blend strength
};
```

**Benefits**:
- Customize models without full fine-tuning
- Swap styles/behaviors dynamically
- Much smaller files than full models

### 📋 Multi-Agent Framework
**Status**: Planned
**Effort**: 38-51 hours (increased estimate)
**Priority**: MEDIUM-HIGH - Advanced Workflows

**What**: Agents collaborate on complex tasks with delegation.

**Architecture**:
```cpp
class ofxGgmlAgent {
    string role;  // "researcher", "writer", "critic"
    vector<Tool> availableTools;

    AgentResponse execute(AgentTask task);
    void delegateTo(ofxGgmlAgent* other, AgentTask subtask);
};
```

**Use Cases**:
- Research → Write → Critic pipelines
- Planning → Multiple specialists
- Iterative refinement loops

**Note**: Needs careful design phase first. Start with sequential delegation.

### 📋 Plugin System
**Status**: Planned
**Effort**: 30-38 hours (increased estimate)
**Priority**: MEDIUM - Extensibility

**What**: Extensible architecture for third-party features.

**Design**:
```cpp
class ofxGgmlPlugin {
    virtual string getName() = 0;
    virtual bool initialize(ofxGgmlPluginContext* context) = 0;
};
```

**Use Cases**:
- Custom inference backends
- Additional modalities (3D, audio gen)
- Domain-specific assistants

**Note**: API design is critical (hard to change later). Start with backend adapters.

---

## Deferred Items

### ❌ Distributed Inference
**Reason**: Very complex, limited audience
**Alternative**: Document using remote llama-server instances

### ❌ Web API Server Mode
**Reason**: Duplicates existing llama-server functionality
**Alternative**: Document deploying with llama-server

### ⏸️ Full Inference Request Queue
**Reason**: Server backend handles this internally
**Alternative**: Add `getActiveRequests()` to query server state

### ⏸️ Full Model Memory Management
**Reason**: Most users run one model at a time
**Alternative**: Add memory usage reporting
**Revisit**: When multi-model ensemble is added

---

## Implementation Notes

### Quick Wins (< 4 hours each)
1. Memory usage reporting (2-3h)
2. Server queue status API (2-3h)
3. Preset workflow helpers (4-6h)

### High-Value Features
1. Semantic cache - Huge speedup for repeated/similar queries
2. Hybrid RAG - Better retrieval accuracy
3. Health monitoring - Production readiness
4. LoRA support - Model customization

### Architectural Improvements
1. Result<T> Ex variants - API consistency
2. RAII integration - Code quality
3. Plugin system - Ecosystem growth

---

## Contributing

To propose new features or changes to this roadmap:

1. Open an issue with tag `roadmap-proposal`
2. Include:
   - Feature description
   - Use cases
   - Estimated effort
   - Priority justification
3. Maintainers will review and update roadmap

---

## Change Log

- **2026-04-21**: Memory usage reporting and server queue status APIs completed
- **2026-04-21**: Initial roadmap created from deep review analysis
- **2026-04-21**: Model checksums verified complete
- **2026-04-21**: Enhanced streaming progress implementation started
