# Testing Guide

## Running Tests

The ofxGgml addon includes a comprehensive unit test suite using Catch2.

### Prerequisites

Tests require ggml to be built first:

```bash
./scripts/build-ggml.sh --cpu-only
```

### Running All Tests

```bash
cd tests
./run-tests.sh
```

### Running Specific Tests

Run tests by tag:

```bash
./build/tests/ofxGgml-tests "[tensor]"       # Only tensor tests
./build/tests/ofxGgml-tests "[graph]"        # Only graph tests
./build/tests/ofxGgml-tests "[result]"       # Only result/error tests
./build/tests/ofxGgml-tests "[core]"         # Only core backend tests
./build/tests/ofxGgml-tests "[model]"        # Only model loading tests
./build/tests/ofxGgml-tests "[inference]"    # Only inference tests
./build/tests/ofxGgml-tests "[integration]"  # Only integration tests
./build/tests/ofxGgml-tests "[benchmark]"    # Only benchmarks
```

Run tests by name pattern:

```bash
./build/tests/ofxGgml-tests "Tensor creation*"
./build/tests/ofxGgml-tests "*operations"
```

### Code Coverage

Generate code coverage reports locally:

```bash
cd tests

# Build with coverage instrumentation
cmake -B build -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run tests
./build/ofxGgml-tests

# Generate coverage report
lcov --capture --directory build --output-file coverage.info --rc lcov_branch_coverage=1
lcov --remove coverage.info '/usr/*' '*/tests/*' '*/libs/ggml/*' --output-file coverage_filtered.info --rc lcov_branch_coverage=1

# View summary
lcov --list coverage_filtered.info

# Generate HTML report (optional)
genhtml coverage_filtered.info --output-directory coverage_html --branch-coverage --title "ofxGgml Code Coverage"
```

Coverage reports are automatically generated in CI and uploaded to Codecov. View detailed coverage statistics at: https://codecov.io/gh/Jonathhhan/ofxGgml

### Test Coverage

Current test coverage includes:

- **Tensor Operations** (test_tensor.cpp)
  - Tensor creation (1D, 2D, 3D, 4D)
  - Tensor properties and validation
  - Element-wise operations (add, sub, mul, div, scale, clamp)
  - Unary operations (sqr, sqrt)

- **Graph Building** (test_graph.cpp)
  - Graph lifecycle (creation, reset)
  - Tensor marking (input, output, param)
  - Matrix operations (matMul, transpose)
  - Reduction operations (sum, mean, sumRows, argmax)
  - Activation functions (relu, gelu, silu, sigmoid, tanh, softmax)
  - Normalization (norm, rmsNorm)
  - Reshape operations
  - Convolution and pooling

- **Error Handling** (test_result.cpp)
  - Result<T> type for success/error values
  - Error codes and messages
  - Copy and move semantics
  - Exception safety

- **Core Backend** (test_core.cpp)
  - Backend initialization and lifecycle
  - Device enumeration
  - Backend information queries
  - Graph allocation
  - Tensor data operations (set/get)
  - Synchronous and asynchronous computation
  - Timing tracking
  - Log callback configuration

- **Model Loading** (test_model.cpp)
  - Model initialization and state
  - GGUF file loading (with and without actual files)
  - Metadata querying
  - Tensor access and iteration
  - Model weight loading to backend
  - API robustness

- **Inference** (test_inference.cpp)
  - Inference class initialization
  - Executable configuration
  - Settings structures
  - Result structures
  - Tokenization utilities
  - Sampling utilities
  - Embedding index and similarity search
  - Cosine similarity calculations

- **Integration Tests** (test_integration.cpp)
  - End-to-end matrix multiplication
  - Element-wise operation chains
  - Activation function verification
  - Reduction operation correctness
  - Normalization operations
  - Complex neural network layers
  - Sequential computations
  - Async computation workflows
  - Graph reuse and allocation
  - Large tensor computations

- **Benchmarks** (test_benchmark.cpp)
  - Tensor operation performance
  - Matrix multiplication GFLOPS
  - Activation function speed
  - Reduction operation benchmarks
  - Graph allocation timing
  - Data transfer bandwidth
  - Sync vs async comparison
  - Memory operation benchmarks

**Total test cases: 280+** (140 original + 140+ new)

### Benchmarks

Benchmarks are marked with `[benchmark][!hide]` and don't run by default.

To run benchmarks:

```bash
./build/tests/ofxGgml-tests "[benchmark]"
```

From the addon root you can also use the dedicated wrappers:

```bash
./scripts/benchmark-addon.sh
```

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\benchmark-addon.ps1
```

Benchmarks measure:
- **Performance**: Tensor operations, matrix multiplication (with GFLOPS calculation)
- **Throughput**: Data transfer bandwidth
- **Latency**: Graph allocation, computation times
- **Comparison**: Sync vs async execution

Benchmark results are printed with timing statistics and throughput metrics.

Benchmarks tagged `[manual]` are intentionally excluded by the wrapper scripts because they exercise heavier teardown/reinit paths or require manual interpretation.

### Writing New Tests

Create a new test file in `tests/`:

```cpp
#include "catch2.hpp"
#include "../src/ofxGgml.h"

TEST_CASE("My test case", "[mytag]") {
    SECTION("Test section 1") {
        // Test code
        REQUIRE(condition);
    }

    SECTION("Test section 2") {
        // More tests
        REQUIRE_FALSE(condition);
    }
}
```

Add the file to CMakeLists.txt:

```cmake
set(TEST_SOURCES
    test_main.cpp
    test_tensor.cpp
    test_graph.cpp
    test_result.cpp
    test_mynewfile.cpp  # Add here
)
```

### CI Integration

Tests run automatically in GitHub Actions on every push and pull request. See `.github/workflows/ci.yml`.

## Error Handling

ofxGgml uses a standardized Result<T> pattern for error handling:

```cpp
#include "ofxGgml.h"

// Functions can return Result<T> instead of throwing exceptions
Result<int> divide(int a, int b) {
    if (b == 0) {
        return ofxGgmlError(ofxGgmlErrorCode::InvalidArgument, "division by zero");
    }
    return a / b;
}

// Check for success
auto result = divide(10, 2);
if (result.isOk()) {
    std::cout << "Result: " << result.value() << std::endl;
} else {
    std::cout << "Error: " << result.error().toString() << std::endl;
}

// Or use valueOr for a default
int value = result.valueOr(0);
```

### Error Codes

Available error codes in `ofxGgmlErrorCode`:

- `BackendInitFailed` — Backend initialization failed
- `DeviceNotFound` — Requested device not available
- `OutOfMemory` — Insufficient memory for operation
- `GraphNotBuilt` — Graph must be built before use
- `GraphAllocFailed` — Graph buffer allocation failed
- `InvalidTensor` — Invalid tensor handle
- `TensorShapeMismatch` — Incompatible tensor shapes
- `ComputeFailed` — Computation execution failed
- `SynchronizationFailed` — Async sync failed
- `ModelLoadFailed` — Model file load error
- `InferenceExecutableMissing` — llama CLI not found
- `InvalidArgument` — Invalid function argument
- And more...

See `src/ofxGgmlResult.h` for the complete list.
