#include "test_harness.h"
#include "../src/inference/ofxGgmlSam3Adapters.h"
#include "../src/inference/ofxGgmlSegmentationInference.h"

#include <memory>
#include <string>

OFXGGML_TEST(segmentation_default_backend_exists) {
	ofxGgmlSegmentationInference segmentation;

	OFXGGML_REQUIRE(segmentation.getBackend() != nullptr);
	OFXGGML_REQUIRE(!segmentation.getBackend()->backendName().empty());
}

OFXGGML_TEST(segmentation_unconfigured_backend_reports_error) {
	ofxGgmlSegmentationInference segmentation;
	const auto result = segmentation.segmentPoint("image.jpg", 0.5f, 0.5f);

	OFXGGML_REQUIRE(!result.success);
	OFXGGML_REQUIRE(!result.error.empty());
	OFXGGML_REQUIRE(result.imagePath == "image.jpg");
}

OFXGGML_TEST(segmentation_bridge_backend_runs_callback) {
	auto backend = std::dynamic_pointer_cast<ofxGgmlSegmentationBridgeBackend>(
		ofxGgmlSegmentationInference::createSegmentationBridgeBackend());
	OFXGGML_REQUIRE(backend != nullptr);
	OFXGGML_REQUIRE(!backend->isConfigured());

	backend->setSegmentFunction(
		[](const ofxGgmlSegmentationRequest & request) {
			ofxGgmlSegmentationResult result;
			result.success = true;
			result.imagePath = request.imagePath;
			result.metadata.push_back({
				"pointCount",
				std::to_string(request.points.size())
			});
			ofxGgmlSegmentationMask mask;
			mask.maskId = "mock";
			mask.width = 1;
			mask.height = 1;
			mask.pixels = { 255 };
			result.masks.push_back(mask);
			return result;
		});
	OFXGGML_REQUIRE(backend->isConfigured());

	ofxGgmlSegmentationInference segmentation;
	segmentation.setBackend(backend);
	const auto result = segmentation.segmentPoint(
		"image.jpg",
		0.25f,
		0.75f,
		"sam3.gguf",
		4);

	OFXGGML_REQUIRE(result.success);
	OFXGGML_REQUIRE(result.imagePath == "image.jpg");
	OFXGGML_REQUIRE(result.masks.size() == 1);
	OFXGGML_REQUIRE(result.masks.front().pixels.front() == 255);
	OFXGGML_REQUIRE(result.metadata.front().second == "1");
}

OFXGGML_TEST(sam3_adapter_missing_integration_fails_cleanly) {
	OFXGGML_REQUIRE(OFXGGML_HAS_SAM3 == 0);

	ofxGgmlSegmentationInference segmentation;
	ofxGgmlSam3Adapters::attachBackend(
		segmentation,
		"/definitely/missing/ofxGgml-test-sam3-model.gguf");

	ofxGgmlSegmentationRequest request;
	request.imagePath = "image.jpg";
	request.imageWidth = 1;
	request.imageHeight = 1;
	request.imageRgb = { 0, 0, 0 };
	request.points.push_back({ 0.5f, 0.5f, true });

	const auto result = segmentation.segment(request);
	OFXGGML_REQUIRE(!result.success);
	OFXGGML_REQUIRE(result.backendName == "sam3.cpp");
	OFXGGML_REQUIRE(result.error.find("sam3.cpp adapter is disabled") != std::string::npos);
}
