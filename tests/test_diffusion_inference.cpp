#include "catch2.hpp"
#include "../src/inference/ofxGgmlDiffusionInference.h"

TEST_CASE("Diffusion Inference initialization", "[diffusion_inference]") {
	ofxGgmlDiffusionInference diffusion;

	SECTION("Default construction") {
		REQUIRE_NOTHROW(ofxGgmlDiffusionInference());
	}

	SECTION("No backend by default") {
		REQUIRE(diffusion.getBackend() == nullptr);
	}
}

TEST_CASE("Diffusion task labels", "[diffusion_inference]") {
	SECTION("Text to image task") {
		const char * label = ofxGgmlDiffusionInference::taskLabel(ofxGgmlDiffusionTask::TextToImage);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label).length() > 0);
	}

	SECTION("Image to image task") {
		const char * label = ofxGgmlDiffusionInference::taskLabel(ofxGgmlDiffusionTask::ImageToImage);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label).length() > 0);
	}
}

TEST_CASE("Diffusion image mode labels", "[diffusion_inference]") {
	SECTION("Single image mode") {
		const char * label = ofxGgmlDiffusionInference::imageModeLabel(ofxGgmlDiffusionImageMode::Single);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label).length() > 0);
	}

	SECTION("Batch image mode") {
		const char * label = ofxGgmlDiffusionInference::imageModeLabel(ofxGgmlDiffusionImageMode::Batch);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label).length() > 0);
	}

	SECTION("Grid image mode") {
		const char * label = ofxGgmlDiffusionInference::imageModeLabel(ofxGgmlDiffusionImageMode::Grid);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label).length() > 0);
	}
}

TEST_CASE("Diffusion selection mode labels", "[diffusion_inference]") {
	SECTION("None selection mode") {
		const char * label = ofxGgmlDiffusionInference::selectionModeLabel(ofxGgmlDiffusionSelectionMode::None);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label).length() > 0);
	}

	SECTION("BestOfN selection mode") {
		const char * label = ofxGgmlDiffusionInference::selectionModeLabel(ofxGgmlDiffusionSelectionMode::BestOfN);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label).length() > 0);
	}

	SECTION("AllRanked selection mode") {
		const char * label = ofxGgmlDiffusionInference::selectionModeLabel(ofxGgmlDiffusionSelectionMode::AllRanked);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label).length() > 0);
	}
}

TEST_CASE("Diffusion bridge backend creation", "[diffusion_inference]") {
	SECTION("Create generic bridge backend") {
		auto backend = ofxGgmlDiffusionInference::createDiffusionBridgeBackend();
		REQUIRE(backend != nullptr);
		REQUIRE_FALSE(backend->backendName().empty());
	}

	SECTION("Create with custom name") {
		auto backend = ofxGgmlDiffusionInference::createDiffusionBridgeBackend({}, "CustomDiffusion");
		REQUIRE(backend != nullptr);
		REQUIRE(backend->backendName() == "CustomDiffusion");
	}

	SECTION("Create Stable Diffusion backend") {
		auto backend = ofxGgmlDiffusionInference::createStableDiffusionBridgeBackend();
		REQUIRE(backend != nullptr);
		REQUIRE_FALSE(backend->backendName().empty());
	}
}

TEST_CASE("Diffusion backend setting and getting", "[diffusion_inference]") {
	ofxGgmlDiffusionInference diffusion;

	SECTION("Set backend") {
		auto backend = ofxGgmlDiffusionInference::createDiffusionBridgeBackend();
		diffusion.setBackend(backend);
		REQUIRE(diffusion.getBackend() == backend);
	}

	SECTION("Replace backend") {
		auto backend1 = ofxGgmlDiffusionInference::createDiffusionBridgeBackend({}, "Backend1");
		auto backend2 = ofxGgmlDiffusionInference::createDiffusionBridgeBackend({}, "Backend2");

		diffusion.setBackend(backend1);
		REQUIRE(diffusion.getBackend()->backendName() == "Backend1");

		diffusion.setBackend(backend2);
		REQUIRE(diffusion.getBackend()->backendName() == "Backend2");
	}
}

TEST_CASE("Diffusion request structure", "[diffusion_inference]") {
	ofxGgmlDiffusionRequest request;

	SECTION("Default task is TextToImage") {
		REQUIRE(request.task == ofxGgmlDiffusionTask::TextToImage);
	}

	SECTION("Default image mode is Single") {
		REQUIRE(request.imageMode == ofxGgmlDiffusionImageMode::Single);
	}

	SECTION("Default selection mode is None") {
		REQUIRE(request.selectionMode == ofxGgmlDiffusionSelectionMode::None);
	}

	SECTION("Default parameters") {
		REQUIRE(request.width == 512);
		REQUIRE(request.height == 512);
		REQUIRE(request.batchCount == 1);
		REQUIRE(request.seed == -1);
		REQUIRE(request.sampleSteps == 20);
		REQUIRE(request.cfgScale == 7.0f);
		REQUIRE(request.clipSkip == -1);
		REQUIRE(request.sampleMethod == 0);
	}

	SECTION("Prompt can be set") {
		request.prompt = "A beautiful landscape";
		REQUIRE(request.prompt == "A beautiful landscape");
	}

	SECTION("Negative prompt can be set") {
		request.negativePrompt = "blurry";
		REQUIRE(request.negativePrompt == "blurry");
	}
}

TEST_CASE("Diffusion result structure", "[diffusion_inference]") {
	ofxGgmlDiffusionResult result;

	SECTION("Default state is failure") {
		REQUIRE_FALSE(result.success);
		REQUIRE(result.elapsedMs == 0.0f);
		REQUIRE(result.images.empty());
		REQUIRE(result.metadata.empty());
	}

	SECTION("Image mode is preserved") {
		result.imageMode = ofxGgmlDiffusionImageMode::Grid;
		REQUIRE(result.imageMode == ofxGgmlDiffusionImageMode::Grid);
	}

	SECTION("Selection mode is preserved") {
		result.selectionMode = ofxGgmlDiffusionSelectionMode::BestOfN;
		REQUIRE(result.selectionMode == ofxGgmlDiffusionSelectionMode::BestOfN);
	}
}

TEST_CASE("Diffusion image artifact structure", "[diffusion_inference]") {
	ofxGgmlDiffusionImageArtifact artifact;

	SECTION("Default values") {
		REQUIRE(artifact.path.empty());
		REQUIRE(artifact.width == 0);
		REQUIRE(artifact.height == 0);
		REQUIRE(artifact.seed == 0);
		REQUIRE(artifact.rankScore == 0.0f);
		REQUIRE(artifact.batchIndex == 0);
	}

	SECTION("Values can be set") {
		artifact.path = "/tmp/image.png";
		artifact.width = 512;
		artifact.height = 512;
		artifact.seed = 12345;
		artifact.rankScore = 0.95f;
		artifact.batchIndex = 2;

		REQUIRE(artifact.path == "/tmp/image.png");
		REQUIRE(artifact.width == 512);
		REQUIRE(artifact.height == 512);
		REQUIRE(artifact.seed == 12345);
		REQUIRE(artifact.rankScore == 0.95f);
		REQUIRE(artifact.batchIndex == 2);
	}
}

TEST_CASE("Diffusion generate with mock backend", "[diffusion_inference]") {
	ofxGgmlDiffusionInference diffusion;

	SECTION("Generate with no backend returns error") {
		ofxGgmlDiffusionRequest request;
		request.prompt = "A cat";
		auto result = diffusion.generate(request);
		REQUIRE_FALSE(result.success);
	}

	SECTION("Generate with configured backend") {
		auto backend = std::dynamic_pointer_cast<ofxGgmlDiffusionBridgeBackend>(
			ofxGgmlDiffusionInference::createDiffusionBridgeBackend());

		backend->setGenerateFunction([](const ofxGgmlDiffusionRequest & req) {
			ofxGgmlDiffusionResult res;
			res.success = true;
			res.backendName = "MockDiffusion";
			res.elapsedMs = 2500.0f;
			res.imageMode = req.imageMode;
			res.selectionMode = req.selectionMode;

			for (int i = 0; i < req.batchCount; ++i) {
				ofxGgmlDiffusionImageArtifact artifact;
				artifact.path = "/tmp/image_" + std::to_string(i) + ".png";
				artifact.width = req.width;
				artifact.height = req.height;
				artifact.seed = req.seed + i;
				artifact.batchIndex = i;
				res.images.push_back(artifact);
			}

			return res;
		});

		diffusion.setBackend(backend);

		ofxGgmlDiffusionRequest request;
		request.prompt = "A beautiful sunset";
		request.width = 512;
		request.height = 512;
		request.batchCount = 3;
		request.seed = 100;

		auto result = diffusion.generate(request);

		REQUIRE(result.success);
		REQUIRE(result.backendName == "MockDiffusion");
		REQUIRE(result.elapsedMs == 2500.0f);
		REQUIRE(result.images.size() == 3);

		for (size_t i = 0; i < result.images.size(); ++i) {
			REQUIRE(result.images[i].width == 512);
			REQUIRE(result.images[i].height == 512);
			REQUIRE(result.images[i].batchIndex == i);
		}
	}

	SECTION("Generate propagates backend errors") {
		auto backend = std::dynamic_pointer_cast<ofxGgmlDiffusionBridgeBackend>(
			ofxGgmlDiffusionInference::createDiffusionBridgeBackend());

		backend->setGenerateFunction([](const ofxGgmlDiffusionRequest &) {
			ofxGgmlDiffusionResult res;
			res.success = false;
			res.error = "Mock generation error";
			return res;
		});

		diffusion.setBackend(backend);

		ofxGgmlDiffusionRequest request;
		request.prompt = "Test";
		auto result = diffusion.generate(request);

		REQUIRE_FALSE(result.success);
		REQUIRE(result.error == "Mock generation error");
	}
}

TEST_CASE("Diffusion ranking metadata", "[diffusion_inference]") {
	ofxGgmlDiffusionResult result;

	SECTION("Ranking scores on images") {
		ofxGgmlDiffusionImageArtifact img1;
		img1.rankScore = 0.95f;
		img1.batchIndex = 0;

		ofxGgmlDiffusionImageArtifact img2;
		img2.rankScore = 0.75f;
		img2.batchIndex = 1;

		result.images = {img1, img2};

		REQUIRE(result.images[0].rankScore == 0.95f);
		REQUIRE(result.images[1].rankScore == 0.75f);
	}

	SECTION("General metadata") {
		result.metadata.push_back({"model", "stable-diffusion-v1.5"});
		result.metadata.push_back({"sampler", "euler_a"});

		REQUIRE(result.metadata.size() == 2);
		REQUIRE(result.metadata[0].first == "model");
		REQUIRE(result.metadata[1].first == "sampler");
	}
}
