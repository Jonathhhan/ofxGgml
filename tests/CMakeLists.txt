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
set(GGML_INCLUDE_DIR "${GGML_DIR}/include")
set(GGML_LIB_DIR "${GGML_DIR}/lib")

if(NOT EXISTS "${GGML_INCLUDE_DIR}/ggml.h")
    message(FATAL_ERROR "ggml headers not found in ${GGML_INCLUDE_DIR}. Run ./scripts/build-ggml.sh first.")
endif()

# Include directories
include_directories(${GGML_INCLUDE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../libs/libxml2/include/libxml/parser.h")
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../libs/libxml2/include")
endif()

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
    ../src/core/ofxGgmlCore.cpp
    ../src/compute/ofxGgmlGraph.cpp
    ../src/compute/ofxGgmlTensor.cpp
    ../src/model/ofxGgmlModel.cpp
    ../src/support/ofxGgmlEasy.cpp
    ../src/support/ofxGgmlProjectMemory.cpp
)

if(OFXGGML_WITH_OPENFRAMEWORKS OR OFXGGML_ENABLE_RUNTIME_SOURCES)
    list(APPEND ADDON_SOURCES
        ../src/assistants/ofxGgmlChatAssistant.cpp
        ../src/inference/ofxGgmlDiffusionInference.cpp
        ../src/inference/ofxGgmlAceStepBridge.cpp
        ../src/inference/ofxGgmlCitationSearch.cpp
        ../src/inference/ofxGgmlInference.cpp
        ../src/inference/ofxGgmlMediaPromptGenerator.cpp
        ../src/inference/ofxGgmlMusicGenerator.cpp
        ../src/inference/ofxGgmlMilkDropGenerator.cpp
        ../src/inference/ofxGgmlLongVideoPlanner.cpp
        ../src/inference/ofxGgmlInferenceSourceInternals.cpp
        ../src/inference/ofxGgmlInferenceTextCleanup.cpp
        ../src/inference/ofxGgmlVideoEssayWorkflow.cpp
        ../src/inference/ofxGgmlMontagePlanner.cpp
        ../src/inference/ofxGgmlMontagePreviewBridge.cpp
        ../src/inference/ofxGgmlRAGPipeline.cpp
        ../src/inference/ofxGgmlSpeechInference.cpp
        ../src/inference/ofxGgmlVideoPlanner.cpp
        ../src/inference/ofxGgmlVisionInference.cpp
        ../src/inference/ofxGgmlVideoInference.cpp
        ../src/inference/ofxGgmlWebCrawler.cpp
        ../src/inference/ofxGgmlWebSearch.cpp
        ../src/inference/ofxGgmlTtsInference.cpp
        ../src/inference/ofxGgmlClipInference.cpp
        ../src/inference/ofxGgmlValidationLoop.cpp
        ../src/support/ofxGgmlConversationManager.cpp
        ../src/support/ofxGgmlScriptSource.cpp
        ../src/support/ofxGgmlProcessSecurity.cpp
        ../src/assistants/ofxGgmlCodeAssistant.cpp
        ../src/assistants/ofxGgmlCodingAgent.cpp
        ../src/assistants/ofxGgmlWorkspaceAssistant.cpp
        ../src/assistants/ofxGgmlTextAssistant.cpp
        ../src/assistants/ofxGgmlCodeReview.cpp)
endif()

if(OFXGGML_WITH_OPENFRAMEWORKS)
    list(APPEND ADDON_SOURCES
        ../src/inference/ofxGgmlLiveSpeechTranscriber.cpp)
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
    test_helpers.cpp
    test_resource_guards.cpp
    test_srt_parser.cpp
)

if(OFXGGML_WITH_OPENFRAMEWORKS OR OFXGGML_ENABLE_RUNTIME_SOURCES)
    list(APPEND TEST_SOURCES
        test_inference.cpp
        test_montage_planner.cpp
        test_montage_preview_bridge.cpp
        test_video_planner_workflow.cpp
        test_speech_inference.cpp
        test_vision_inference.cpp
        test_video_inference.cpp
        test_assistant_eval.cpp
        test_easy_api.cpp
        test_media_prompt_generator.cpp
        test_music_generator.cpp
        test_long_video_planner.cpp
        test_video_essay_workflow.cpp
        test_acestep_bridge.cpp
        test_citation_search.cpp
        test_milkdrop_generator.cpp
        test_chat_assistant.cpp
        test_code_assistant.cpp
        test_coding_agent.cpp
        test_text_assistant.cpp
        test_workspace_assistant.cpp
        test_code_review.cpp
        test_tts_inference.cpp
        test_clip_inference.cpp
        test_diffusion_inference.cpp
        test_rag_pipeline.cpp
        test_conversation_manager.cpp
        test_feature_synergies.cpp
        test_validation_loop.cpp)
endif()

# LiveSpeechTranscriber requires full openFrameworks
if(OFXGGML_WITH_OPENFRAMEWORKS)
    list(APPEND TEST_SOURCES test_live_speech_transcriber.cpp)
endif()

if(OFXGGML_ENABLE_BENCHMARK_TESTS)
    list(APPEND TEST_SOURCES test_benchmark.cpp)
endif()

# ---------------------------------------------------------------------------
# Test executable
# ---------------------------------------------------------------------------

add_executable(ofxGgml-tests ${TEST_SOURCES} ${ADDON_SOURCES})

if(NOT OFXGGML_WITH_OPENFRAMEWORKS)
    target_compile_definitions(ofxGgml-tests PRIVATE OFXGGML_HEADLESS_STUBS=1)
endif()

if(WIN32)
    set(GGML_CORE_LIBS
        ${GGML_LIB_DIR}/ggml.lib
        ${GGML_LIB_DIR}/ggml-base.lib
        ${GGML_LIB_DIR}/ggml-cpu.lib)
else()
    set(GGML_CORE_LIBS
        ${GGML_LIB_DIR}/libggml.a
        ${GGML_LIB_DIR}/libggml-base.a
        ${GGML_LIB_DIR}/libggml-cpu.a)
endif()

foreach(lib ${GGML_CORE_LIBS})
    if(NOT EXISTS "${lib}")
        message(FATAL_ERROR "Required ggml library not found: ${lib}. Run ./scripts/build-ggml.sh first.")
    endif()
endforeach()

set(GGML_OPTIONAL_LIBS)
set(VULKAN_RUNTIME_LIBS)
if(WIN32)
    file(GLOB GGML_OPTIONAL_LIBS "${GGML_LIB_DIR}/ggml-*.lib")
    list(FILTER GGML_OPTIONAL_LIBS EXCLUDE REGEX "ggml(-base|-cpu)?\\.lib$")

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
    foreach(optlib libggml-cuda.a libggml-vulkan.a libggml-metal.a libggml-blas.a libggml-opencl.a libggml-sycl.a libggml-virtgpu.a libggml-zdnn.a)
        set(_p "${GGML_LIB_DIR}/${optlib}")
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
        ${GGML_CORE_LIBS}
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
        ${GGML_CORE_LIBS}
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
            ${GGML_CORE_LIBS}
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
            ${GGML_CORE_LIBS}
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
