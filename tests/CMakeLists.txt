cmake_minimum_required(VERSION 3.18)
project(ofxGgml_rewrite_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ofxGgml_rewrite_tests
	test_main.cpp
	test_result.cpp
	test_public_headers.cpp
	test_tensor.cpp
	test_graph.cpp
	test_runtime.cpp
	test_model.cpp
	test_segmentation.cpp
	../src/core/ofxGgmlRuntime.cpp
	../src/compute/ofxGgmlTensor.cpp
	../src/compute/ofxGgmlGraph.cpp
	../src/model/ofxGgmlModel.cpp
	../src/inference/ofxGgmlSegmentationInference.cpp)

target_include_directories(ofxGgml_rewrite_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/../src)

enable_testing()
add_test(NAME ofxGgml_rewrite_tests COMMAND ofxGgml_rewrite_tests)
