#include "catch2.hpp"
#include "../src/inference/ofxGgmlYoloInference.h"

#include <algorithm>
#include <memory>

TEST_CASE("YOLO inference initialization", "[yolo_inference]") {
	ofxGgmlYoloInference yolo;

	SECTION("Default construction") {
		REQUIRE_NOTHROW(ofxGgmlYoloInference());
	}

	SECTION("Has default backend") {
		REQUIRE(yolo.getBackend() != nullptr);
	}
}

TEST_CASE("YOLO default model profiles", "[yolo_inference]") {
	const auto profiles = ofxGgmlYoloInference::defaultProfiles();
	REQUIRE_FALSE(profiles.empty());

	bool foundGgmlYolo = false;
	for (const auto & profile : profiles) {
		REQUIRE_FALSE(profile.name.empty());
		REQUIRE_FALSE(profile.architecture.empty());
		if (profile.backendId == "ggml-yolo" &&
			profile.modelFileHint.find("yolov3-tiny") != std::string::npos) {
			foundGgmlYolo = true;
		}
	}
	REQUIRE(foundGgmlYolo);
}

TEST_CASE("YOLO bridge backend creation and execution", "[yolo_inference]") {
	SECTION("Create generic bridge backend") {
		auto backend = ofxGgmlYoloInference::createYoloBridgeBackend();
		REQUIRE(backend != nullptr);
		REQUIRE_FALSE(backend->backendName().empty());
	}

	SECTION("Create with custom name") {
		auto backend = ofxGgmlYoloInference::createYoloBridgeBackend(
			{},
			"CustomYOLO");
		REQUIRE(backend != nullptr);
		REQUIRE(backend->backendName() == "CustomYOLO");
	}

	SECTION("Unconfigured backend reports an error") {
		ofxGgmlYoloInference yolo;
		const auto result = yolo.detectImage("image.jpg");
		REQUIRE_FALSE(result.success);
		REQUIRE_FALSE(result.error.empty());
	}

	SECTION("Configured backend receives requests") {
		auto backend = std::dynamic_pointer_cast<ofxGgmlYoloBridgeBackend>(
			ofxGgmlYoloInference::createYoloBridgeBackend());
		backend->setDetectFunction([](const ofxGgmlYoloRequest & request) {
			ofxGgmlYoloResult result;
			result.success = true;
			result.imagePath = request.imagePath;
			result.outputPath = request.outputPath;
			ofxGgmlYoloDetection detection;
			detection.label = "dog";
			detection.confidence = 0.57f;
			result.detections.push_back(detection);
			return result;
		});

		ofxGgmlYoloInference yolo;
		yolo.setBackend(backend);
		const auto result = yolo.detectImage(
			"dog.jpg",
			"yolov3-tiny.gguf",
			0.5f,
			"predictions.jpg");

		REQUIRE(result.success);
		REQUIRE(result.imagePath == "dog.jpg");
		REQUIRE(result.outputPath == "predictions.jpg");
		REQUIRE(result.detections.size() == 1);
		REQUIRE(result.detections.front().label == "dog");
	}
}

TEST_CASE("ggml YOLO CLI backend helpers", "[yolo_inference][ggml_yolo]") {
	SECTION("Create CLI backend") {
		auto backend = ofxGgmlYoloInference::createGgmlYoloCliBackend("yolov3-tiny");
		REQUIRE(backend != nullptr);
		REQUIRE(backend->backendName() == "ggml YOLO");
	}

	SECTION("Build command arguments") {
		auto rawBackend =
			ofxGgmlYoloInference::createGgmlYoloCliBackend("yolov3-tiny");
		auto * backend = dynamic_cast<ofxGgmlYoloCliBackend *>(rawBackend.get());
		REQUIRE(backend != nullptr);

		ofxGgmlYoloRequest request;
		request.modelPath = "models/yolov3-tiny.gguf";
		request.imagePath = "images/dog.jpg";
		request.outputPath = "out/predictions.jpg";
		request.threads = 4;
		request.threshold = 0.6f;
		request.device = "CPU";

		const auto args = backend->buildCommandArguments(request);
		REQUIRE(args[0] == "yolov3-tiny");
		REQUIRE(std::find(args.begin(), args.end(), "-m") != args.end());
		REQUIRE(std::find(args.begin(), args.end(), request.modelPath) != args.end());
		REQUIRE(std::find(args.begin(), args.end(), "-i") != args.end());
		REQUIRE(std::find(args.begin(), args.end(), request.imagePath) != args.end());
		REQUIRE(std::find(args.begin(), args.end(), "-o") != args.end());
		REQUIRE(std::find(args.begin(), args.end(), request.outputPath) != args.end());
		REQUIRE(std::find(args.begin(), args.end(), "-th") != args.end());
		REQUIRE(std::find(args.begin(), args.end(), "-d") != args.end());
	}

	SECTION("Parse ggml examples/yolo output") {
		const std::string output =
			"Layer 22 output shape:   26 x  26 x  255 x   1\n"
			"dog: 57%\n"
			"car: 52%\n"
			"Detected objects saved in 'predictions.jpg' (time: 0.057000 sec.)\n";

		const auto detections = ofxGgmlYoloCliBackend::parseDetections(output);
		REQUIRE(detections.size() == 2);
		REQUIRE(detections[0].label == "dog");
		REQUIRE(detections[0].confidence == Approx(0.57f));
		REQUIRE(detections[1].label == "car");
		REQUIRE(detections[1].confidence == Approx(0.52f));
		REQUIRE(
			ofxGgmlYoloCliBackend::parseSavedOutputPath(output) ==
			"predictions.jpg");
	}

	SECTION("Validate required CLI inputs before execution") {
		auto rawBackend =
			ofxGgmlYoloInference::createGgmlYoloCliBackend("yolov3-tiny");
		ofxGgmlYoloRequest request;
		request.imagePath = "dog.jpg";

		const auto result = rawBackend->detect(request);
		REQUIRE_FALSE(result.success);
		REQUIRE(result.error.find("model path") != std::string::npos);
	}
}

