# Roadmap Implementation Notes

This document contains technical notes and guidance for implementing features from `ROADMAP.md`.

**Last Updated**: 2026-04-21

---

## Completed Features

### ✅ Enhanced Streaming Progress (v1.1.0)

**Implemented**: 2026-04-21
**Files Modified**:
- `src/inference/ofxGgmlStreamingContext.h` - Core implementation
- `README.md` - User documentation
- `CHANGELOG.md` - Release notes

**Architecture**:
- Added `ofxGgmlStreamingProgress` struct as value type for progress snapshots
- Extended `ofxGgmlStreamingContext` with token counting and time tracking
- Progress calculations are thread-safe and lock-protected
- Backward compatible - no breaking changes to existing API

**Usage Pattern**:
```cpp
auto ctx = std::make_shared<ofxGgmlStreamingContext>();
ctx->setEstimatedTotal(settings.maxTokens);

inference.generate(model, prompt, settings, [ctx](const string& chunk) {
    ctx->addTokens(1);
    auto progress = ctx->getProgress(chunk);
    // Use progress.percentComplete, progress.tokensPerSecond, etc.
    return !ctx->isCancelled();
});
```

**Testing Status**: ⚠️ Needs unit tests
**GUI Example**: 📋 Needs demonstration

**Next Steps**:
1. Add unit tests in `tests/` directory
2. Update GUI example to show progress bar
3. Consider adding example to `ofxGgmlBasicExample` or `ofxGgmlNeuralExample`

---

## Implementation Guidelines

### General Principles

1. **Backward Compatibility**: Always add new APIs alongside existing ones
   - Example: `setupEx()` alongside `setup()`
   - Keep existing methods unchanged until major version bump

2. **Non-Breaking Changes**: Prefer additive changes
   - Add new struct fields at the end
   - Add new methods to classes
   - Use optional parameters with defaults

3. **Testing**: Every feature needs tests
   - Unit tests in `tests/` directory
   - Integration tests where appropriate
   - Benchmark tests for performance features

4. **Documentation**: Three levels required
   - Inline code comments (doxygen style)
   - README.md examples
   - CHANGELOG.md entry

### Code Style

Follow existing patterns in the codebase:
- Use `m_` prefix for member variables
- Use camelCase for methods and variables
- Use PascalCase for classes and structs
- Thread safety: Use `std::lock_guard<std::mutex>` for state protection

---

## Next Priority Features

### 1. Preset Workflow Helpers (4-6 hours)

**Goal**: Add common workflow shortcuts to `ofxGgmlEasy`

**Implementation Approach**:
```cpp
// In ofxGgmlEasy.h
struct WorkflowResult {
    bool success;
    string output;
    vector<string> intermediateResults;
    float totalElapsedMs;
};

// Add these methods to ofxGgmlEasy
WorkflowResult summarizeAndTranslate(
    const string& text,
    const string& targetLang,
    const string& sourceLang = "auto"
);

WorkflowResult transcribeAndSummarize(
    const string& audioPath,
    int maxSummaryWords = 100
);

WorkflowResult describeAndAnalyze(
    const string& imagePath,
    const string& analysisPrompt = "Analyze this image in detail"
);
```

**Files to Modify**:
- `src/support/ofxGgmlEasy.h` - Add method declarations
- `src/support/ofxGgmlEasy.cpp` - Implement workflow logic
- `README.md` - Add usage examples
- `CHANGELOG.md` - Document new feature

**Testing**:
- Add tests in `tests/ofxGgmlEasyTest.cpp`
- Test each workflow independently
- Test error handling (missing files, etc.)

---

### 2. Semantic Cache (14-18 hours)

**Goal**: Cache inference results by semantic similarity

**Design Decisions**:
- Where to place: `src/inference/ofxGgmlSemanticCache.h`
- Dependencies: Uses existing `ofxGgmlEmbeddingIndex`
- Storage: In-memory with optional disk persistence
- TTL: Time-based expiration (simple approach)

**Implementation Plan**:

1. **Create Core Class** (4 hours)
```cpp
class ofxGgmlSemanticCache {
public:
    struct CacheEntry {
        string prompt;
        string result;
        vector<float> embedding;
        uint64_t timestampMs;
        float ttlSeconds;
    };

    struct LookupResult {
        bool found;
        string cachedResult;
        float similarity;
        string originalPrompt;
    };

    void setInference(ofxGgmlInference* inf);
    void setEmbeddingModel(const string& modelPath);

    LookupResult lookup(
        const string& prompt,
        float similarityThreshold = 0.95f
    );

    void store(
        const string& prompt,
        const string& result,
        float ttlSeconds = 3600.0f
    );

    void clearExpired();
    void clear();

    size_t size() const;
    CacheStats getStats() const;

private:
    ofxGgmlInference* m_inference;
    string m_embeddingModel;
    vector<CacheEntry> m_entries;
    ofxGgmlEmbeddingIndex m_index;
};
```

2. **Add to ofxGgmlInference** (2 hours)
```cpp
// In ofxGgmlInference.h
void setSemanticCache(ofxGgmlSemanticCache* cache);

// Modify generate() to check cache first
```

3. **Testing** (4 hours)
- Test cache hits and misses
- Test similarity threshold
- Test TTL expiration
- Test thread safety

4. **Integration** (2 hours)
- Add to `ofxGgmlEasy`
- Update GUI example

5. **Documentation** (2 hours)
- README example
- CHANGELOG entry
- Inline documentation

**Challenges**:
- Embedding computation adds 20-50ms overhead
- Need to tune similarity threshold (0.95 may be too strict)
- Cache invalidation strategy (start simple with TTL only)

---

### 3. Result<T> Ex Variants (12-16 hours)

**Goal**: Add `Result<T>` error handling alongside existing bool returns

**Already Defined**: `src/core/ofxGgmlResult.h` exists

**Implementation Strategy**:

Phase 1: Add Ex variants (8 hours)
- `setupEx()` alongside `setup()`
- `allocGraphEx()` alongside `allocGraph()`
- `loadModelWeightsEx()` alongside `loadModelWeights()`
- `generateEx()` alongside `generate()` (already exists!)

Phase 2: Update one example (4 hours)
- Modify `ofxGgmlBasicExample` to use Ex variants
- Show error handling patterns

Phase 3: Documentation (2 hours)
- Migration guide in docs
- README examples
- CHANGELOG

**Template for Implementation**:
```cpp
// In ofxGgmlCore.h
Result<void> setupEx(const ofxGgmlSettings& settings) {
    if (!setup(settings)) {
        return ofxGgmlError{
            ofxGgmlErrorCode::BackendInitFailed,
            "Failed to initialize backend: " + getLastError()
        };
    }
    return Result<void>::ok();
}
```

---

## Long-Term Architecture Notes

### Multi-Agent Framework (v2.0.0)

**Design Considerations**:
- Start with simple sequential delegation
- Build task memory/context system first
- Add parallel execution in phase 2
- Learn from `ofxGgmlCodingAgent` patterns

**Minimal Viable Agent**:
```cpp
class ofxGgmlAgent {
    string role;
    string systemPrompt;

    virtual AgentResponse execute(const AgentTask& task) = 0;
    virtual bool canHandle(const AgentTask& task) const = 0;
};
```

### Plugin System (v2.0.0)

**Critical Design Decisions**:
- C API boundary for ABI stability
- Version negotiation protocol
- Sandbox/permissions model
- Resource management (who owns what?)

**Research First**:
- Study existing plugin systems (VST, LADSPA, etc.)
- Define minimal plugin interface
- Prototype with one backend adapter
- Document plugin development guide

---

## Testing Strategy

### Unit Tests
- Location: `tests/`
- Framework: Catch2 (already in use)
- Coverage goal: 80%+
- Run with: `./scripts/benchmark-addon.sh`

### Integration Tests
- Test cross-component workflows
- Test error paths
- Test resource cleanup

### Performance Tests
- Benchmark new features
- Track regression
- Document expected performance

---

## Documentation Checklist

For each feature:
- [ ] Inline doxygen comments
- [ ] README.md example
- [ ] CHANGELOG.md entry
- [ ] Update ROADMAP.md status
- [ ] Add to relevant docs/ guide if complex

---

## Collaboration Notes

### For Future Contributors

When implementing roadmap features:

1. **Check Status**: Read ROADMAP.md for current priority
2. **Announce Intent**: Open issue or PR draft
3. **Follow Patterns**: Study existing code style
4. **Test First**: Write tests before implementation when possible
5. **Document**: Update docs as you code, not after
6. **Incremental**: Small PRs are easier to review

### Review Checklist

Before submitting PR:
- [ ] Tests added and passing
- [ ] Documentation updated
- [ ] No breaking changes (or justified in major version)
- [ ] Backward compatible
- [ ] Code follows existing style
- [ ] CHANGELOG.md updated
- [ ] ROADMAP.md status updated

---

## Known Issues / Blockers

### Current
- None

### Potential Future Blockers
- **RAII Integration**: Requires handling shared backend allocation case
- **Plugin System**: Needs ABI stability design
- **Distributed Inference**: Very complex networking requirements

---

## Resource Links

- Main Roadmap: `docs/ROADMAP.md`
- Improvements Roadmap: `docs/IMPROVEMENTS_ROADMAP.md`
- Deep Review: `docs/DEEP_REVIEW_SUMMARY.md`
- Feature Synergies: `docs/FEATURE_SYNERGIES.md`
- Architecture: `docs/ARCHITECTURE_IMPROVEMENTS.md`

---

## Change Log

- **2026-04-21**: Initial implementation notes created
- **2026-04-21**: Completed enhanced streaming progress tracking
