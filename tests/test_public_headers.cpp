#include "test_harness.h"
#include "../src/ofxGgmlCore.h"
#include "../src/ofxGgmlEmbedding.h"
#include "../src/ofxGgmlSegmentation.h"
#include "../src/ofxGgmlText.h"
#include "../src/ofxGgml.h"

OFXGGML_TEST(public_core_header_compiles) {
	OFXGGML_REQUIRE(OFXGGML_VERSION_MAJOR == 2);
	OFXGGML_REQUIRE(OFXGGML_HAS_SAM3 == 0);
	OFXGGML_REQUIRE(std::string(ofxGgmlBackendName(ofxGgmlBackend::Auto)) == "Auto");
	OFXGGML_REQUIRE(std::string(ofxGgmlBackendName(ofxGgmlBackend::Cpu)) == "CPU");
	OFXGGML_REQUIRE(std::string(ofxGgmlBackendName(ofxGgmlBackend::Cuda)) == "CUDA");
	OFXGGML_REQUIRE(std::string(ofxGgmlBackendName(ofxGgmlBackend::Vulkan)) == "Vulkan");
	OFXGGML_REQUIRE(std::string(ofxGgmlBackendName(ofxGgmlBackend::Metal)) == "Metal");
	OFXGGML_REQUIRE(std::string(ofxGgmlBackendName(ofxGgmlBackend::OpenCL)) == "OpenCL");
}

OFXGGML_TEST(public_text_header_compiles) {
	ofxGgmlTextRequest request;
	request.prompt = "hello";

	OFXGGML_REQUIRE(request.prompt == "hello");
	OFXGGML_REQUIRE(request.settings.maxTokens > 0);
}

OFXGGML_TEST(public_embedding_header_compiles) {
	ofxGgmlEmbeddingRequest request;
	request.input = "hello";

	OFXGGML_REQUIRE(request.input == "hello");
	OFXGGML_REQUIRE(request.settings.timeoutSeconds > 0);
}

OFXGGML_TEST(public_segmentation_header_compiles) {
	ofxGgmlSegmentationRequest request;
	request.imagePath = "image.jpg";
	request.points.push_back({ 0.5f, 0.5f, true });

	OFXGGML_REQUIRE(request.imagePath == "image.jpg");
	OFXGGML_REQUIRE(request.points.size() == 1);
}
