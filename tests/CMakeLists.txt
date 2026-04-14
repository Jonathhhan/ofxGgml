# ---------------------------------------------------------------------------
# ofxGgml Test Suite
# ---------------------------------------------------------------------------
# Builds and runs unit tests using Catch2.
#
# Usage:
#   cmake -B build/tests -S tests
#   cmake --build build/tests
#   ctest --test-dir build/tests --output-on-failure
# 
# Code Coverage:
#   cmake -B build/tests -S tests -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build/tests
#   ctest --test-dir build/tests --output-on-failure
#   lcov --capture --directory build/tests --output-file coverage.info
#   lcov --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage_filtered.info
#   genhtml coverage_filtered.info --output-directory coverage_html
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.18)
project(ofxGgml-tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Code Coverage Option
# ---------------------------------------------------------------------------

option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(STATUS "Enabling code coverage")
        add_compile_options(--coverage -O0 -g)
        add_link_options(--coverage)
    else()
        message(WARNING "Code coverage only supported with GCC/Clang")
    endif()
endif()

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

set(TEST_FILES
    test_tensor.cpp
    test_graph.cpp
    test_result.cpp
    test_core.cpp
    test_model.cpp
    test_inference.cpp
    test_integration.cpp
    test_benchmark.cpp
)

function(link_ggml_dependencies target_name)
    if(UNIX AND NOT APPLE)
        # Linux
        target_link_libraries(${target_name}
            ${GGML_BUILD_DIR}/src/libggml.a
            ${GGML_BUILD_DIR}/src/libggml-base.a
            ${GGML_BUILD_DIR}/src/libggml-cpu.a
            pthread
            dl
        )
    elseif(APPLE)
        # macOS
        target_link_libraries(${target_name}
            ${GGML_BUILD_DIR}/src/libggml.a
            ${GGML_BUILD_DIR}/src/libggml-base.a
            ${GGML_BUILD_DIR}/src/libggml-cpu.a
            "-framework Accelerate"
        )
    elseif(WIN32)
        # Windows
        target_link_libraries(${target_name}
            ${GGML_BUILD_DIR}/src/Release/ggml.lib
            ${GGML_BUILD_DIR}/src/Release/ggml-base.lib
            ${GGML_BUILD_DIR}/src/Release/ggml-cpu.lib
        )
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Enable testing
# ---------------------------------------------------------------------------

enable_testing()

# ---------------------------------------------------------------------------
# Test executables (one per test file)
# ---------------------------------------------------------------------------

set(TEST_TARGETS)
foreach(TEST_FILE ${TEST_FILES})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
    set(TARGET_NAME "${TEST_NAME}-tests")
    add_executable(${TARGET_NAME} ${TEST_FILE} ${ADDON_SOURCES})
    link_ggml_dependencies(${TARGET_NAME})
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
    list(APPEND TEST_TARGETS ${TARGET_NAME})
endforeach()

message(STATUS "ofxGgml test suite configured")
message(STATUS "  Build with: cmake --build ${CMAKE_BINARY_DIR}")
message(STATUS "  Targets: ${TEST_TARGETS}")
message(STATUS "  Run with: ctest --test-dir ${CMAKE_BINARY_DIR} --output-on-failure")
