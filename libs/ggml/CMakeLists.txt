# ---------------------------------------------------------------------------
# ofxGgml bundled ggml build
# ---------------------------------------------------------------------------
#
# Builds ggml as a static library with GPU backend autodetection.
# GPU backends (CUDA, Vulkan, Metal) are compiled directly into the
# library — no dynamic plugin loading, no duplicate static initializers.
#
# Usage:
#   cmake -B build libs/ggml
#   cmake --build build --config Release
#   cmake --install build --prefix libs/ggml/install
#
# Or from the addon root:
#   ./scripts/build-ggml.sh
#
# Options (passed via -D):
#   OFXGGML_GPU_AUTODETECT  ON/OFF  (default ON)  — auto-enable GPU backends
#   OFXGGML_CUDA            ON/OFF  (default OFF)  — force CUDA on/off
#   OFXGGML_VULKAN          ON/OFF  (default OFF)  — force Vulkan on/off
#   OFXGGML_METAL           ON/OFF  (default OFF)  — force Metal on/off
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.18)
project(ofxGgml-ggml C CXX)

# ggml version (from upstream)
set(GGML_VERSION_MAJOR 0)
set(GGML_VERSION_MINOR 9)
set(GGML_VERSION_PATCH 11)
set(GGML_VERSION "${GGML_VERSION_MAJOR}.${GGML_VERSION_MINOR}.${GGML_VERSION_PATCH}")

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(OFXGGML_GPU_AUTODETECT "Auto-detect and enable available GPU backends" ON)
option(OFXGGML_CUDA   "Enable CUDA backend"   OFF)
option(OFXGGML_VULKAN "Enable Vulkan backend" OFF)
option(OFXGGML_METAL  "Enable Metal backend"  OFF)

# ---------------------------------------------------------------------------
# GPU autodetection
# ---------------------------------------------------------------------------

if (OFXGGML_GPU_AUTODETECT)
    # CUDA
    if (NOT OFXGGML_CUDA)
        find_package(CUDAToolkit QUIET)
        if (CUDAToolkit_FOUND)
            message(STATUS "ofxGgml: CUDA toolkit detected — enabling CUDA backend")
            set(OFXGGML_CUDA ON)
        endif()
    endif()

    # Vulkan
    if (NOT OFXGGML_VULKAN)
        find_package(Vulkan QUIET)
        if (Vulkan_FOUND)
            message(STATUS "ofxGgml: Vulkan SDK detected — enabling Vulkan backend")
            set(OFXGGML_VULKAN ON)
        endif()
    endif()

    # Metal (macOS only)
    if (NOT OFXGGML_METAL AND APPLE)
        find_library(METAL_FRAMEWORK Metal QUIET)
        if (METAL_FRAMEWORK)
            message(STATUS "ofxGgml: Metal framework detected — enabling Metal backend")
            set(OFXGGML_METAL ON)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Propagate options to ggml's CMake variables
# ---------------------------------------------------------------------------

# Static library — no dynamic backend loading
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GGML_STATIC ON CACHE BOOL "" FORCE)
set(GGML_BACKEND_DL OFF CACHE BOOL "" FORCE)

# Core settings
set(GGML_CPU ON CACHE BOOL "" FORCE)
set(GGML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GGML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GGML_SCHED_MAX_COPIES "4" CACHE STRING "" FORCE)

# GPU backends
set(GGML_CUDA   ${OFXGGML_CUDA}   CACHE BOOL "" FORCE)
set(GGML_VULKAN ${OFXGGML_VULKAN} CACHE BOOL "" FORCE)
set(GGML_METAL  ${OFXGGML_METAL}  CACHE BOOL "" FORCE)

# CUDA options (must have defaults so the CUDA CMakeLists.txt works correctly)
set(GGML_CUDA_PEER_MAX_BATCH_SIZE "128" CACHE STRING "ggml: max batch size for using peer access" FORCE)
set(GGML_CUDA_FA ON CACHE BOOL "ggml: enable CUDA flash attention" FORCE)

# Disable backends we don't need
set(GGML_BLAS    OFF CACHE BOOL "" FORCE)
set(GGML_CANN    OFF CACHE BOOL "" FORCE)
set(GGML_HIP     OFF CACHE BOOL "" FORCE)
set(GGML_MUSA    OFF CACHE BOOL "" FORCE)
set(GGML_RPC     OFF CACHE BOOL "" FORCE)
set(GGML_VirtGPU OFF CACHE BOOL "" FORCE)
set(GGML_SYCL    OFF CACHE BOOL "" FORCE)
set(GGML_WebGPU  OFF CACHE BOOL "" FORCE)
set(GGML_zDNN    OFF CACHE BOOL "" FORCE)
set(GGML_OpenCL  OFF CACHE BOOL "" FORCE)
set(GGML_Hexagon OFF CACHE BOOL "" FORCE)
set(GGML_ZenDNN  OFF CACHE BOOL "" FORCE)
set(GGML_OPENVINO OFF CACHE BOOL "" FORCE)

# Metal embedding (bundle .metal shaders into the library)
if (OFXGGML_METAL)
    set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE)
endif()

# Let ggml detect CPU features automatically
set(GGML_NATIVE ON CACHE BOOL "" FORCE)

# Compiler features
set(GGML_OPENMP ON CACHE BOOL "" FORCE)
set(GGML_LLAMAFILE ON CACHE BOOL "" FORCE)
set(GGML_CPU_REPACK ON CACHE BOOL "" FORCE)

# Enable CUDA if needed
if (OFXGGML_CUDA)
    enable_language(CUDA)
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Version / commit macros expected by ggml.c
# ---------------------------------------------------------------------------

set(GGML_BUILD_COMMIT "bundled")

# ---------------------------------------------------------------------------
# Include ggml's own build system
# ---------------------------------------------------------------------------

# ggml's cmake helpers
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(cmake/common.cmake)

# The src/CMakeLists.txt expects to be included from the ggml root
add_subdirectory(src)

# Define the version macros that ggml.c expects (normally set by the
# upstream root CMakeLists.txt which we replace with our own).
target_compile_definitions(ggml-base PRIVATE
    GGML_VERSION="${GGML_VERSION}"
    GGML_COMMIT="${GGML_BUILD_COMMIT}"
)

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

message(STATUS "")
message(STATUS "ofxGgml ggml build configuration:")
message(STATUS "  ggml version:  ${GGML_VERSION}")
message(STATUS "  CPU backend:   ON")
message(STATUS "  CUDA backend:  ${OFXGGML_CUDA}")
message(STATUS "  Vulkan backend: ${OFXGGML_VULKAN}")
message(STATUS "  Metal backend: ${OFXGGML_METAL}")
message(STATUS "  Shared libs:   OFF (static)")
message(STATUS "  Backend DL:    OFF (no dynamic loading)")
message(STATUS "")
