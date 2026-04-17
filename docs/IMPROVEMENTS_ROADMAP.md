# Improvements Roadmap for ofxGgml

This document tracks the implementation status of architectural improvements identified during deep code review.

## Priority Matrix

| Improvement | Priority | Difficulty | Impact | Status |
|------------|----------|------------|--------|--------|
| Model Checksums | HIGH | LOW | Security | 🟡 Ready to Complete |
| RAII Integration | MEDIUM | HIGH | Code Quality | 🔄 Prepared |
| Result<T> Error Handling | MEDIUM | MEDIUM | API Quality | 📋 Planned |
| GUI Refactoring | LOW | MEDIUM | Maintainability | 📋 Planned |

---

## 1. Model Checksum Completion 🟡

**Status**: Infrastructure complete, awaiting checksum values
**Priority**: HIGH
**Estimated Effort**: 2-4 hours
**Files Affected**: `scripts/model-catalog.json`

### Current State
- SHA256 checksum framework implemented
- Validation in download scripts functional
- All 6 model presets have empty `sha256` fields
- Script `scripts/dev/update-model-checksums.sh` ready to use

### Implementation Steps
1. Download or locate each model file locally:
   - Preset 1: Qwen2.5-1.5B Instruct (~1.0 GB)
   - Preset 2: Qwen2.5-Coder-1.5B Instruct (~1.0 GB)
   - Preset 3: Phi-3.5-mini Instruct (~2.4 GB)
   - Preset 4: Llama-3.2-1B Instruct (~0.9 GB)
   - Preset 5: TinyLlama-1.1B Chat (~0.6 GB)
   - Preset 6: Qwen2.5-Coder-7B Instruct (~4.7 GB)

2. Run update script for each:
   ```bash
   ./scripts/dev/update-model-checksums.sh --preset 1
   ./scripts/dev/update-model-checksums.sh --preset 2
   # ... or use --all to process all at once
   ./scripts/dev/update-model-checksums.sh --all
   ```

3. Verify checksums match official sources when possible

4. Commit updated `model-catalog.json`

### Security Impact
- ✅ Prevents supply chain attacks
- ✅ Detects corrupted downloads
- ✅ Ensures model integrity
- ✅ Builds user trust

### Testing
- Download script already validates checksums
- Empty checksums issue warnings but don't block downloads
- Invalid checksums cause download failures

---

## 2. RAII Guards Integration 🔄

**Status**: Guards defined, integration prepared but requires extensive testing
**Priority**: MEDIUM
**Estimated Effort**: 8-12 hours
**Files Affected**: `src/core/ofxGgmlCore.cpp`, `src/core/ofxGgmlCore.h`

### Current State
- ✅ RAII guard classes created in `src/core/ofxGgmlResourceGuards.h`:
  - `GgmlBackendGuard` - wraps `ggml_backend_t`
  - `GgmlBackendBufferGuard` - wraps `ggml_backend_buffer_t`
  - `GgmlBackendSchedGuard` - wraps `ggml_backend_sched_t`
- ✅ Guards follow modern C++ RAII patterns
- ✅ Non-copyable, movable
- ✅ Automatic cleanup in destructors
- ⚠️ Not yet integrated into `ofxGgml::Impl`

### Implementation Blocker
The main blocker is handling the case where `backend` and `cpuBackend` point to the same allocation:

```cpp
// Current pattern in setup():
if (hasPrefixIgnoreCase(ggml_backend_name(m_impl->backend), "CPU")) {
    m_impl->cpuBackend = m_impl->backend;  // Both point to same allocation
} else {
    m_impl->cpuBackend = ggml_backend_init_by_type(...);  // Separate allocation
}
```

### Proposed Solution
Use a "non-owning" pattern for `cpuBackend` when it aliases `backend`:

```cpp
struct ofxGgml::Impl {
    GgmlBackendGuard backend;
    // cpuBackend only takes ownership when it's a separate allocation
    std::optional<GgmlBackendGuard> cpuBackend;

    // Helper to get the CPU backend pointer
    ggml_backend_t getCpuBackend() const {
        return cpuBackend ? cpuBackend->get() : backend.get();
    }
};
```

### Implementation Steps
1. Update `ofxGgml::Impl` structure to use guards
2. Modify `setup()` to handle ownership correctly
3. Update all `m_impl->backend` references to `m_impl->backend.get()`
4. Update all `m_impl->cpuBackend` references to use helper method
5. Simplify error paths (RAII handles cleanup automatically)
6. Update `getBackend()` and related accessors
7. Remove manual `ggml_backend_free()` calls from `close()`

### Benefits
- Eliminates ~30 lines of manual cleanup code
- Prevents resource leaks on error paths
- Simplifies exception safety
- Makes ownership semantics explicit
- Removes double-free prevention logic

### Testing Requirements
- Run full test suite (`./tests/run-tests.sh`)
- Test CPU-only setup
- Test GPU setup with CPU fallback
- Test error paths (allocation failures)
- Verify no memory leaks with valgrind
- Test multiple setup/close cycles

---

## 3. Result<T> Error Handling Standardization 📋

**Status**: Planned, `Result<T>` already implemented but underused
**Priority**: MEDIUM
**Estimated Effort**: 12-16 hours
**Files Affected**: All public API headers and implementations

### Current State
The codebase uses three different error patterns:

1. **Bool returns** (most common):
   ```cpp
   bool setup(const ofxGgmlSettings & settings);
   bool allocGraph(ofxGgmlGraph & graph);
   ```
   **Problem**: No error details, caller must check logs

2. **Custom result structs**:
   ```cpp
   struct ofxGgmlComputeResult {
       bool success;
       std::string error;
       float elapsedMs;
   };
   ```
   **Problem**: Inconsistent struct layouts

3. **Result<T>** (defined in `src/core/ofxGgmlResult.h` but underused):
   ```cpp
   template<typename T> class Result {
       // Modern error handling with error codes and messages
   };
   ```
   **Problem**: Exists but not used in public APIs

### Phase 1: Add Ex Variants (Non-Breaking)
Add `Result<T>` variants alongside existing methods:

```cpp
// In ofxGgmlCore.h:
// Keep existing:
bool setup(const ofxGgmlSettings & settings);

// Add new:
Result<void> setupEx(const ofxGgmlSettings & settings);
Result<void> allocGraphEx(ofxGgmlGraph & graph);
Result<void> loadModelWeightsEx(ofxGgmlModel & model);
```

Implementation pattern:
```cpp
Result<void> ofxGgml::setupEx(const ofxGgmlSettings & settings) {
    if (!setup(settings)) {
        return ofxGgmlError{
            ofxGgmlErrorCode::BackendInitFailed,
            "Failed to initialize backend"
        };
    }
    return Result<void>::ok();
}
```

### Phase 2: Migrate Result Structs
Replace custom result structs with `Result<T>`:

```cpp
// Old:
struct ofxGgmlComputeResult {
    bool success;
    std::string error;
    float elapsedMs;
};

// New:
struct ofxGgmlComputeInfo {
    float elapsedMs;
    // Other timing/metadata
};
Result<ofxGgmlComputeInfo> computeGraphEx(ofxGgmlGraph & graph);
```

### Phase 3: Deprecation (Future Release)
- Mark old bool-returning methods as deprecated
- Update examples to use new methods
- Document migration path in CHANGELOG

### Benefits
- Consistent error handling across all APIs
- Rich error context (codes + messages)
- Composable error propagation
- Better debugging and logging
- Type-safe success/error states

---

## 4. GUI Example Refactoring 📋

**Status**: Planned
**Priority**: LOW
**Estimated Effort**: 16-20 hours
**Files Affected**: `ofxGgmlGuiExample/src/ofApp.{h,cpp}`

### Current State
- `ofApp.cpp`: 10,923 lines in a single file
- `ofApp.h`: 570 lines
- All UI panels implemented in one class
- Difficult to navigate and maintain

### Proposed Structure
Split into focused panel classes:

```
ofxGgmlGuiExample/src/
├── ofApp.h/cpp              # Main app (reduced to ~1000 lines)
├── panels/
│   ├── GuiChatPanel.h/cpp        # Chat interface
│   ├── GuiScriptPanel.h/cpp      # Script/code assistance
│   ├── GuiVisionPanel.h/cpp      # Vision/video analysis
│   ├── GuiSpeechPanel.h/cpp      # Speech/TTS workflows
│   ├── GuiDiffusionPanel.h/cpp   # Image generation
│   └── GuiSettingsPanel.h/cpp    # Settings management
└── utils/
    ├── GuiSessionState.h/cpp     # Session persistence
    └── GuiHelpers.h/cpp          # Shared UI utilities
```

### Implementation Strategy
1. Create panel base class with common interface
2. Extract each mode into its own panel class
3. Move shared state to session manager
4. Update ofApp to delegate to panels
5. Maintain backward compatibility for settings persistence

### Benefits
- Each file <1500 lines
- Clear responsibility separation
- Easier to understand and modify
- Parallel development on different panels
- Better code navigation in IDEs

### Testing Requirements
- Verify all modes still function
- Test session save/restore
- Check UI layout consistency
- Validate keyboard shortcuts
- Test mode switching

---

## Implementation Priority

**Recommended Order:**

1. **Model Checksums** (HIGH priority, LOW effort)
   - Quick win for security
   - No code changes, just data population
   - Can be done independently

2. **Result<T> Phase 1** (MEDIUM priority, MEDIUM effort)
   - Add Ex variants alongside existing methods
   - Non-breaking, incremental improvement
   - Immediate benefit for new code

3. **RAII Integration** (MEDIUM priority, HIGH effort)
   - Requires careful testing
   - Significant code quality improvement
   - Best done in dedicated PR

4. **GUI Refactoring** (LOW priority, HIGH effort)
   - Nice to have, not urgent
   - Large change, needs comprehensive testing
   - Consider for major version bump

---

## Testing Strategy

For each improvement:
- ✅ Run full test suite before and after
- ✅ Test on Linux, macOS, and Windows
- ✅ Verify CPU-only and GPU configurations
- ✅ Check memory leaks with valgrind/sanitizers
- ✅ Run static analysis (cppcheck, clang-tidy)
- ✅ Update documentation and examples
- ✅ Add new tests for new functionality

---

## Rollout Recommendations

### Immediate (Next PR)
- Populate model checksums (2-4 hours)
- Document completion in CHANGELOG
- Update SECURITY_NOTES.md

### Next Minor Release (v1.1.0)
- Add Result<T> Ex variants
- Keep existing APIs unchanged
- Update examples to show both patterns

### Next Major Release (v2.0.0)
- Complete RAII integration
- Deprecate old bool-returning methods
- Consider GUI refactoring

### Long-term (v2.1.0+)
- Remove deprecated methods
- Finalize GUI modular structure
- Additional performance optimizations

---

## Success Metrics

**Code Quality:**
- Zero memory leaks in valgrind
- Zero clang-tidy warnings on new code
- 90%+ test coverage on new code

**API Consistency:**
- All error paths return rich context
- No raw pointer ownership in public APIs
- Consistent naming and patterns

**Security:**
- All model presets have verified checksums
- Checksum validation on all downloads
- No new vulnerabilities from refactoring

**Maintainability:**
- No file >2000 lines
- Clear ownership semantics
- Comprehensive inline documentation
