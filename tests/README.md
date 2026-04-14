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
./build/tests/ofxGgml-tests "[tensor]"     # Only tensor tests
./build/tests/ofxGgml-tests "[graph]"      # Only graph tests
./build/tests/ofxGgml-tests "[result]"     # Only result/error tests
```

Run tests by name pattern:

```bash
./build/tests/ofxGgml-tests "Tensor creation*"
./build/tests/ofxGgml-tests "*operations"
```

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
