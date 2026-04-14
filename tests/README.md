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

Use CTest to run a specific test executable:

```bash
ctest --test-dir build/tests -R test_tensor-tests --output-on-failure       # Only tensor tests
ctest --test-dir build/tests -R test_graph-tests --output-on-failure        # Only graph tests
ctest --test-dir build/tests -R test_result-tests --output-on-failure       # Only result/error tests
ctest --test-dir build/tests -R test_core-tests --output-on-failure         # Only core backend tests
ctest --test-dir build/tests -R test_model-tests --output-on-failure        # Only model loading tests
ctest --test-dir build/tests -R test_inference-tests --output-on-failure    # Only inference tests
ctest --test-dir build/tests -R test_integration-tests --output-on-failure  # Only integration tests
ctest --test-dir build/tests -R test_benchmark-tests --output-on-failure    # Only benchmarks
```

Run tests by tag or name pattern within a single executable:

```bash
./build/tests/test_tensor-tests "[tensor]"
./build/tests/test_graph-tests "Tensor creation*"
./build/tests/test_graph-tests "*operations"
```

### Code Coverage

Generate code coverage reports locally:

```bash
cd tests

# Build with coverage instrumentation
cmake -B build -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

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
./build/tests/test_benchmark-tests "[benchmark]"
```

Benchmarks measure:
- **Performance**: Tensor operations, matrix multiplication (with GFLOPS calculation)
- **Throughput**: Data transfer bandwidth
- **Latency**: Graph allocation, computation times
- **Comparison**: Sync vs async execution

Benchmark results are printed with timing statistics and throughput metrics.

### Writing New Tests

Create a new test file in `tests/`:

```cpp
#define CATCH_CONFIG_MAIN
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
set(TEST_FILES
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
