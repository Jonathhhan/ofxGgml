# ---------------------------------------------------------------------------
# ofxGgml Test Suite
# ---------------------------------------------------------------------------
# Builds and runs unit tests using Catch2.
#
# Usage:
#   cmake -B build/tests -S tests
#   cmake --build build/tests
#   ./build/tests/ofxGgml-tests
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.18)
project(ofxGgml-tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Find ggml library (must be built first)
# ---------------------------------------------------------------------------

set(GGML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libs/ggml")
set(GGML_BUILD_DIR "${GGML_DIR}/build")

# Check if ggml is built
if(NOT EXISTS "${GGML_BUILD_DIR}")
    message(FATAL_ERROR "ggml not built. Run ./scripts/build-ggml.sh first.")
endif()

# Include directories
include_directories(${GGML_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------

set(ADDON_SOURCES
    ../src/ofxGgmlCore.cpp
    ../src/ofxGgmlGraph.cpp
    ../src/ofxGgmlTensor.cpp
    ../src/ofxGgmlModel.cpp
    ../src/ofxGgmlInference.cpp
    ../src/ofxGgmlProjectMemory.cpp
    ../src/ofxGgmlScriptSource.cpp
)

set(TEST_SOURCES
    test_main.cpp
    test_tensor.cpp
    test_graph.cpp
    test_result.cpp
    test_core.cpp
    test_model.cpp
    test_inference.cpp
    test_integration.cpp
    test_benchmark.cpp
)

# ---------------------------------------------------------------------------
# Test executable
# ---------------------------------------------------------------------------

add_executable(ofxGgml-tests ${TEST_SOURCES} ${ADDON_SOURCES})

# Link ggml libraries
if(UNIX AND NOT APPLE)
    # Linux
    target_link_libraries(ofxGgml-tests
        ${GGML_BUILD_DIR}/src/libggml.a
        ${GGML_BUILD_DIR}/src/libggml-base.a
        ${GGML_BUILD_DIR}/src/libggml-cpu.a
        pthread
        dl
    )
elseif(APPLE)
    # macOS
    target_link_libraries(ofxGgml-tests
        ${GGML_BUILD_DIR}/src/libggml.a
        ${GGML_BUILD_DIR}/src/libggml-base.a
        ${GGML_BUILD_DIR}/src/libggml-cpu.a
        "-framework Accelerate"
    )
elseif(WIN32)
    # Windows
    target_link_libraries(ofxGgml-tests
        ${GGML_BUILD_DIR}/src/Release/ggml.lib
        ${GGML_BUILD_DIR}/src/Release/ggml-base.lib
        ${GGML_BUILD_DIR}/src/Release/ggml-cpu.lib
    )
endif()

# ---------------------------------------------------------------------------
# Enable testing
# ---------------------------------------------------------------------------

enable_testing()
add_test(NAME ofxGgml-tests COMMAND ofxGgml-tests)

message(STATUS "ofxGgml test suite configured")
message(STATUS "  Build with: cmake --build ${CMAKE_BINARY_DIR}")
message(STATUS "  Run with: ./ofxGgml-tests")
