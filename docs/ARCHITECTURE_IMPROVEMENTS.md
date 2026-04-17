# Architecture Improvement Roadmap

This document outlines architectural improvements identified during code review and their implementation status.

> **Note**: For a comprehensive implementation plan with priorities, effort estimates, and phasing strategy, see [IMPROVEMENTS_ROADMAP.md](IMPROVEMENTS_ROADMAP.md).

## Completed Improvements ✅

### 1. Path Traversal Protection (Security Fix)
**Status**: ✅ Completed in commit 160f9fd

**Location**: `src/support/ofxGgmlScriptSource.cpp:1840-1881`

**Changes**:
- Enhanced `isSafeRepoPath()` to use `std::filesystem::weakly_canonical()`
- Prevents symlink attacks, case confusion on case-insensitive filesystems
- Validates canonical path remains relative after resolution
- Maintains existing string-based validation for basic attacks

**Impact**: Prevents attackers from using symlinks or filesystem tricks to access files outside the repository.

### 2. Temp File Security (Security Fix)
**Status**: ✅ Completed in commit 160f9fd

**Location**: `src/inference/ofxGgmlSpeechInference.cpp:67-116`

**Changes**:
- Improved entropy collection using multiple `std::random_device` samples
- Added timestamp component to prevent prediction attacks
- Path validation using `weakly_canonical` to prevent traversal in generated paths
- Better fallback logic for temp directory selection (prefer /tmp on Unix)

**Impact**: Prevents TOCTOU race conditions and filename prediction attacks.

### 3. Log Callback Race Condition (Thread Safety Fix)
**Status**: ✅ Completed in commit 160f9fd

**Location**: `src/core/ofxGgmlCore.cpp:251-295`

**Changes**:
- Added mutex-protected validation in `ggmlLogCallback()`
- Verifies impl pointer is still in `s_logOwners` before dereferencing
- Prevents use-after-free when callbacks arrive after object destruction
- Maintains backward compatibility with existing callback API

**Impact**: Eliminates data races and potential crashes from stale callback pointers.

---

## Planned Improvements 🔄

### 4. RAII Wrappers for ggml Resources
**Status**: 🔄 In Progress (guards defined, integration pending)

**Location**: `src/core/ofxGgmlResourceGuards.h` (new file)

**Design**:
Three RAII guard classes have been created:
- `GgmlBackendGuard` - wraps `ggml_backend_t`
- `GgmlBackendBufferGuard` - wraps `ggml_backend_buffer_t`
- `GgmlBackendSchedGuard` - wraps `ggml_backend_sched_t`

All guards follow modern C++ RAII patterns:
- Non-copyable, movable
- Automatic cleanup in destructor
- `release()` for ownership transfer
- `reset()` for explicit cleanup
- `get()` for raw pointer access

**Integration Plan**:
1. Update `ofxGgml::Impl` to use guard classes instead of raw pointers
2. Remove manual cleanup in `close()` method
3. Remove double-free prevention logic (guards handle this automatically)
4. Update error paths to rely on RAII cleanup

**Current Blockers**:
- Need to handle the special case where `backend` and `cpuBackend` point to same allocation
- Requires careful testing to ensure no behavioral changes
- May need reference counting or shared ownership for the dual-pointer case

**Estimated Impact**:
- Eliminates 30+ lines of manual cleanup code
- Prevents resource leaks on error paths
- Simplifies exception safety
- Makes ownership semantics explicit

**Files to Modify**:
- `src/core/ofxGgmlCore.cpp` (struct Impl, close(), error paths)
- `src/core/ofxGgmlCore.h` (include new guards)

---

### 5. Standardize Error Handling on Result<T>
**Status**: 📋 Planned (requires API design)

**Current State**:
The codebase has an excellent `Result<T>` implementation in `src/core/ofxGgmlResult.h` but uses three different error patterns:

1. **Bool returns** (most common):
   ```cpp
   bool setup(const ofxGgmlSettings & settings);
   bool allocGraph(ofxGgmlGraph & graph);
   ```
   **Problem**: No error details, caller must check logs or internal state

2. **Result structs with error strings**:
   ```cpp
   struct ofxGgmlComputeResult {
       bool success;
       std::string error;
       float elapsedMs;
   };
   ```
   **Problem**: Inconsistent struct layout, hard to compose

3. **Result<T>** (defined but underused):
   ```cpp
   template<typename T> class Result {
       // Modern error handling with error codes and messages
   };
   ```
   **Problem**: Not used in public APIs, exists but unused

**Proposed Design**:

#### Phase 1: New Methods (Non-Breaking)
Add `Result<T>` variants alongside existing methods:
```cpp
// Keep existing:
bool setup(const ofxGgmlSettings & settings);

// Add new:
Result<void> setupEx(const ofxGgmlSettings & settings);
Result<void> allocGraphEx(ofxGgmlGraph & graph);
Result<void> loadModelWeightsEx(ofxGgmlModel & model);
```

#### Phase 2: Migrate Structs
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

#### Phase 3: Deprecation (Major Version)
In next major version (2.0.0):
- Mark bool-returning methods as `[[deprecated]]`
- Update examples to use Result<T> APIs
- Update documentation

#### Phase 4: Removal (Future)
In future major version:
- Remove old bool-returning methods
- Make Result<T> the only error handling pattern

**Benefits**:
- Type-safe error propagation without exceptions
- Consistent error handling across all APIs
- Better error diagnostics (error codes + messages)
- Composable error handling
- Zero-cost abstractions (no exceptions)

**Challenges**:
- Breaking API change requiring major version bump
- All examples and documentation need updates
- User code must be migrated
- Need comprehensive migration guide

**Estimated Scope**:
- ~15 public methods to add Result<T> variants
- ~200 lines of new wrapper code
- Update all 3 examples
- Update README and documentation
- Write migration guide

**Priority**: Medium (good design, but breaking change)

---

## Additional Improvements Identified

### 6. Raw Pointer Lifetime Documentation
**Status**: 📋 Planned

**Issue**: Public API returns raw C pointers without lifetime documentation:
```cpp
struct ggml_backend * getBackend();
struct ggml_backend * getCpuBackend();
struct ggml_backend_sched * getScheduler();
```

**Proposed Solution**:
Add comprehensive Doxygen documentation:
```cpp
/// Returns the primary compute backend handle.
///
/// Lifetime: Valid until close() is called or the ofxGgml instance is destroyed.
/// Ownership: Retained by ofxGgml - do not call ggml_backend_free().
/// Thread Safety: Unsafe - do not call from multiple threads.
///
/// @return Backend handle, or nullptr if not initialized
struct ggml_backend * getBackend();
```

**Files**: `src/core/ofxGgmlCore.h`, `src/model/ofxGgmlModel.h`

### 7. Parameter Validation Documentation
**Status**: 📋 Planned

**Issue**: Inference settings lack documented constraints:
```cpp
struct ofxGgmlInferenceSettings {
    float temperature = 0.7f;   // Valid range?
    float topP = 0.9f;          // 0.0-1.0 or 0.0-100.0?
    float minP = 0.05f;         // What does this control?
};
```

**Proposed Solution**: Add Doxygen comments with valid ranges and semantics.

---

## Implementation Notes

### RAII Integration Strategy

The current `ofxGgml::Impl` structure:
```cpp
struct ofxGgml::Impl {
    ggml_backend_t backend = nullptr;
    ggml_backend_t cpuBackend = nullptr;
    ggml_backend_sched_t sched = nullptr;
    ggml_backend_buffer_t modelWeightBuf = nullptr;
    // ...
};
```

Should become:
```cpp
struct ofxGgml::Impl {
    GgmlBackendGuard backend;
    GgmlBackendGuard cpuBackend;
    GgmlBackendSchedGuard sched;
    GgmlBackendBufferGuard modelWeightBuf;
    // ...
};
```

**Special Case Handling**:
The current code has double-free prevention:
```cpp
const bool sameBackend = (m_impl->backend && m_impl->backend == m_impl->cpuBackend);
if (m_impl->backend) {
    ggml_backend_free(m_impl->backend);
    m_impl->backend = nullptr;
}
if (m_impl->cpuBackend && !sameBackend) {
    ggml_backend_free(m_impl->cpuBackend);
}
```

**Solution**: When backends are identical, use `release()` on one guard:
```cpp
if (backend.get() == cpuBackend.get()) {
    cpuBackend.release();  // Transfer ownership to backend guard
}
// Automatic cleanup via RAII
```

### Testing Strategy

All improvements should be validated with:
1. Existing unit test suite (`tests/`)
2. Manual testing of all 3 examples
3. Valgrind/ASan for memory leak detection
4. TSan for thread safety verification

---

## References

- Original code review: Deep review analysis (2026-04-16)
- Result<T> implementation: `src/core/ofxGgmlResult.h`
- RAII guards: `src/core/ofxGgmlResourceGuards.h`
- Security notes: `SECURITY_NOTES.md`

---

## Version History

- 2026-04-16: Initial improvements completed (path traversal, temp files, log callback)
- 2026-04-16: RAII guards defined, integration planned
- Future: Error handling standardization roadmap defined
