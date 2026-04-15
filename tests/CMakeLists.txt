# ---------------------------------------------------------------------------
# ofxGgml Test Suite
# ---------------------------------------------------------------------------
# Builds and runs unit tests using Catch2.
#
# Usage:
#   cmake -B build/tests -S tests
#   cmake --build build/tests
#   ./build/tests/ofxGgml-tests
#
# Code Coverage:
#   cmake -B build/tests -S tests -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build/tests
#   ./build/tests/ofxGgml-tests
#   lcov --capture --directory build/tests --output-file coverage.info
#   lcov --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage_filtered.info
#   genhtml coverage_filtered.info --output-directory coverage_html
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.18)
project(ofxGgml-tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(OFXGGML_WITH_OPENFRAMEWORKS "Build tests against full openFrameworks (headers + libs)" OFF)

if(OFXGGML_WITH_OPENFRAMEWORKS)
    # Try to locate openFrameworks root for headers/libraries used by addon sources.
    if(DEFINED ENV{OF_ROOT} AND EXISTS "$ENV{OF_ROOT}/libs/openFrameworks/ofMain.h")
        set(OF_ROOT "$ENV{OF_ROOT}")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/openFrameworks/ofMain.h")
        set(OF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../libs/openFrameworks/ofMain.h")
        set(OF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
    endif()

    if(NOT DEFINED OF_ROOT)
        message(FATAL_ERROR "openFrameworks root not found. Set OF_ROOT or place addon under <OF_ROOT>/addons/ofxGgml.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Code Coverage Option
# ---------------------------------------------------------------------------

option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)
option(OFXGGML_ENABLE_BENCHMARK_TESTS "Build benchmark tests (slower)" OFF)
option(OFXGGML_ENABLE_RUNTIME_SOURCES "Build inference/script-source with stubs in headless mode" ON)

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

if(OFXGGML_WITH_OPENFRAMEWORKS)
    include_directories(${OF_ROOT}/libs/openFrameworks)

    # openFrameworks headers are spread across many subfolders and are often
    # included without folder prefixes (e.g. "ofConstants.h").
    file(GLOB_RECURSE OF_HEADER_DIRS LIST_DIRECTORIES true "${OF_ROOT}/libs/openFrameworks/*")
    set(OF_INCLUDE_DIRS)
    foreach(d ${OF_HEADER_DIRS})
        if(IS_DIRECTORY "${d}")
            list(APPEND OF_INCLUDE_DIRS "${d}")
        endif()
    endforeach()

    # Add OF third-party include directories (glew, glfw, etc.) required by ofMain.h.
    file(GLOB_RECURSE OF_3RDPARTY_DIRS LIST_DIRECTORIES true "${OF_ROOT}/libs/*")
    foreach(d ${OF_3RDPARTY_DIRS})
        if(IS_DIRECTORY "${d}" AND NOT d MATCHES "openFrameworksCompiled")
            list(APPEND OF_INCLUDE_DIRS "${d}")
        endif()
    endforeach()

    list(REMOVE_DUPLICATES OF_INCLUDE_DIRS)
    include_directories(${OF_INCLUDE_DIRS})
else()
    # Lightweight stubs for running core tests without full OF toolchain.
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/stubs)
endif()

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------

set(ADDON_SOURCES
    ../src/ofxGgmlCore.cpp
    ../src/ofxGgmlGraph.cpp
    ../src/ofxGgmlTensor.cpp
    ../src/ofxGgmlModel.cpp
    ../src/ofxGgmlProjectMemory.cpp
)

if(OFXGGML_WITH_OPENFRAMEWORKS OR OFXGGML_ENABLE_RUNTIME_SOURCES)
    list(APPEND ADDON_SOURCES
        ../src/ofxGgmlChatAssistant.cpp
        ../src/ofxGgmlInference.cpp
        ../src/ofxGgmlScriptSource.cpp
        ../src/ofxGgmlCodeAssistant.cpp
        ../src/ofxGgmlWorkspaceAssistant.cpp
        ../src/ofxGgmlTextAssistant.cpp
        ../src/ofxGgmlCodeReview.cpp)
endif()

set(TEST_SOURCES
    test_main.cpp
    test_tensor.cpp
    test_graph.cpp
    test_result.cpp
    test_core.cpp
    test_model.cpp
    test_project_memory.cpp
    test_integration.cpp
)

if(OFXGGML_WITH_OPENFRAMEWORKS OR OFXGGML_ENABLE_RUNTIME_SOURCES)
    list(APPEND TEST_SOURCES
        test_inference.cpp
        test_assistant_eval.cpp
        test_chat_assistant.cpp
        test_code_assistant.cpp
        test_text_assistant.cpp
        test_workspace_assistant.cpp
        test_code_review.cpp)
endif()

if(OFXGGML_ENABLE_BENCHMARK_TESTS)
    list(APPEND TEST_SOURCES test_benchmark.cpp)
endif()

# ---------------------------------------------------------------------------
# Test executable
# ---------------------------------------------------------------------------

add_executable(ofxGgml-tests ${TEST_SOURCES} ${ADDON_SOURCES})

set(GGML_OPTIONAL_LIBS)
set(VULKAN_RUNTIME_LIBS)
if(WIN32)
    file(GLOB GGML_OPTIONAL_LIBS
        "${GGML_BUILD_DIR}/src/Release/ggml-*.lib"
        "${GGML_BUILD_DIR}/src/*/Release/ggml-*.lib")
    list(FILTER GGML_OPTIONAL_LIBS EXCLUDE REGEX "ggml-(base|cpu)\\.lib$")

    set(CUDA_RUNTIME_LIBS)
    if(GGML_OPTIONAL_LIBS MATCHES "ggml-cuda\\.lib")
        set(CUDA_ROOT "$ENV{CUDA_PATH}")
        if(CUDA_ROOT AND EXISTS "${CUDA_ROOT}/lib/x64/cublas.lib")
            list(APPEND CUDA_RUNTIME_LIBS
                "${CUDA_ROOT}/lib/x64/cublas.lib"
                "${CUDA_ROOT}/lib/x64/cudart.lib"
                "${CUDA_ROOT}/lib/x64/cuda.lib")
        endif()
    endif()
else()
    foreach(optlib libggml-cuda.a libggml-vulkan.a libggml-metal.a libggml-blas.a)
        set(_p "${GGML_BUILD_DIR}/src/${optlib}")
        if(EXISTS "${_p}")
            list(APPEND GGML_OPTIONAL_LIBS "${_p}")
        endif()
    endforeach()
endif()

if(GGML_OPTIONAL_LIBS MATCHES "ggml-vulkan")
    find_package(Vulkan QUIET)
    if(TARGET Vulkan::Vulkan)
        list(APPEND VULKAN_RUNTIME_LIBS Vulkan::Vulkan)
    elseif(DEFINED ENV{VULKAN_SDK} AND EXISTS "$ENV{VULKAN_SDK}/Lib/vulkan-1.lib")
        list(APPEND VULKAN_RUNTIME_LIBS "$ENV{VULKAN_SDK}/Lib/vulkan-1.lib")
    endif()
endif()

# Link ggml libraries
if(UNIX AND NOT APPLE)
    # Linux
    find_package(OpenMP QUIET)
    target_link_libraries(ofxGgml-tests
        ${GGML_BUILD_DIR}/src/libggml.a
        ${GGML_BUILD_DIR}/src/libggml-base.a
        ${GGML_BUILD_DIR}/src/libggml-cpu.a
        ${GGML_OPTIONAL_LIBS}
        pthread
        dl
    )
    if(OpenMP_CXX_FOUND)
        target_link_libraries(ofxGgml-tests OpenMP::OpenMP_CXX)
    endif()
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
    if(OFXGGML_WITH_OPENFRAMEWORKS)
        set(OF_LIB_RELEASE "${OF_ROOT}/libs/openFrameworksCompiled/lib/vs/x64/openframeworksLib.lib")
        if(NOT EXISTS "${OF_LIB_RELEASE}")
            message(FATAL_ERROR "openFrameworks library not found: ${OF_LIB_RELEASE}")
        endif()
        target_link_libraries(ofxGgml-tests
            ${GGML_BUILD_DIR}/src/Release/ggml.lib
            ${GGML_BUILD_DIR}/src/Release/ggml-base.lib
            ${GGML_BUILD_DIR}/src/Release/ggml-cpu.lib
            ${GGML_OPTIONAL_LIBS}
            ${CUDA_RUNTIME_LIBS}
            ${VULKAN_RUNTIME_LIBS}
            ${OF_LIB_RELEASE}
            ws2_32
            Winmm
            Version
        )
    else()
        target_link_libraries(ofxGgml-tests
            ${GGML_BUILD_DIR}/src/Release/ggml.lib
            ${GGML_BUILD_DIR}/src/Release/ggml-base.lib
            ${GGML_BUILD_DIR}/src/Release/ggml-cpu.lib
            ${GGML_OPTIONAL_LIBS}
            ${CUDA_RUNTIME_LIBS}
            ${VULKAN_RUNTIME_LIBS}
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Enable testing
# ---------------------------------------------------------------------------

enable_testing()
add_test(NAME ofxGgml-tests COMMAND ofxGgml-tests)

message(STATUS "ofxGgml test suite configured")
message(STATUS "  Build with: cmake --build ${CMAKE_BINARY_DIR}")
message(STATUS "  Run with: ./ofxGgml-tests")
