#include "catch2.hpp"
#include "../src/inference/ofxGgmlDiffusionInference.h"

TEST_CASE("Diffusion Inference initialization", "[diffusion_inference]") {
	ofxGgmlDiffusionInference diffusion;

	SECTION("Default construction") {
		REQUIRE_NOTHROW(ofxGgmlDiffusionInference());
	}

	SECTION("Has default backend") {
		REQUIRE(diffusion.getBackend() != nullptr);
	}

	SECTION("Default capabilities are empty") {
		auto caps = diffusion.getCapabilities();
		REQUIRE_FALSE(caps.supportsTextToImage);
		REQUIRE_FALSE(caps.supportsProgressCallbacks);
		REQUIRE(caps.maxBatchSize == 1);
	}
}

TEST_CASE("Diffusion error type labels", "[diffusion_inference]") {
	SECTION("Configuration error") {
		const char * label = ofxGgmlDiffusionInference::errorTypeLabel(
			ofxGgmlImageGenerationErrorType::ConfigurationError);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label) == "Configuration Error");
	}

	SECTION("Model load error") {
		const char * label = ofxGgmlDiffusionInference::errorTypeLabel(
			ofxGgmlImageGenerationErrorType::ModelLoadError);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label) == "Model Load Error");
	}

	SECTION("Validation error") {
		const char * label = ofxGgmlDiffusionInference::errorTypeLabel(
			ofxGgmlImageGenerationErrorType::ValidationError);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label) == "Validation Error");
	}

	SECTION("None error") {
		const char * label = ofxGgmlDiffusionInference::errorTypeLabel(
			ofxGgmlImageGenerationErrorType::None);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label) == "No Error");
	}
}

TEST_CASE("Diffusion task labels", "[diffusion_inference]") {
	SECTION("Text to image task") {
		const char * label = ofxGgmlDiffusionInference::taskLabel(ofxGgmlImageGenerationTask::TextToImage);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label).length() > 0);
	}

	SECTION("Image to image task") {
		const char * label = ofxGgmlDiffusionInference::taskLabel(ofxGgmlImageGenerationTask::ImageToImage);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label).length() > 0);
	}

	SECTION("Instruct image task") {
		const char * label = ofxGgmlDiffusionInference::taskLabel(ofxGgmlImageGenerationTask::InstructImage);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label).length() > 0);
	}
}

TEST_CASE("Diffusion selection mode labels", "[diffusion_inference]") {
	SECTION("KeepOrder selection mode") {
		const char * label = ofxGgmlDiffusionInference::selectionModeLabel(ofxGgmlImageSelectionMode::KeepOrder);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label).length() > 0);
	}

	SECTION("Rerank selection mode") {
		const char * label = ofxGgmlDiffusionInference::selectionModeLabel(ofxGgmlImageSelectionMode::Rerank);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label).length() > 0);
	}

	SECTION("BestOnly selection mode") {
		const char * label = ofxGgmlDiffusionInference::selectionModeLabel(ofxGgmlImageSelectionMode::BestOnly);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label).length() > 0);
	}
}

TEST_CASE("Diffusion backend creation", "[diffusion_inference]") {
	SECTION("Create Stable Diffusion backend") {
		auto backend = ofxGgmlDiffusionInference::createStableDiffusionBridgeBackend();
		REQUIRE(backend != nullptr);
		REQUIRE_FALSE(backend->backendName().empty());
	}

	SECTION("Create with custom name") {
		auto backend = ofxGgmlDiffusionInference::createStableDiffusionBridgeBackend({}, "CustomDiffusion");
		REQUIRE(backend != nullptr);
		REQUIRE(backend->backendName() == "CustomDiffusion");
	}
}

TEST_CASE("Diffusion backend setting and getting", "[diffusion_inference]") {
	ofxGgmlDiffusionInference diffusion;

	SECTION("Set backend") {
		auto backend = ofxGgmlDiffusionInference::createStableDiffusionBridgeBackend();
		diffusion.setBackend(backend);
		REQUIRE(diffusion.getBackend() == backend);
	}

	SECTION("Replace backend") {
		auto backend1 = ofxGgmlDiffusionInference::createStableDiffusionBridgeBackend({}, "Backend1");
		auto backend2 = ofxGgmlDiffusionInference::createStableDiffusionBridgeBackend({}, "Backend2");

		diffusion.setBackend(backend1);
		REQUIRE(diffusion.getBackend()->backendName() == "Backend1");

		diffusion.setBackend(backend2);
		REQUIRE(diffusion.getBackend()->backendName() == "Backend2");
	}

	SECTION("Backend capabilities") {
		auto backend = std::dynamic_pointer_cast<ofxGgmlStableDiffusionBridgeBackend>(
			ofxGgmlDiffusionInference::createStableDiffusionBridgeBackend());

		backend->setGetCapabilitiesFunction([]() {
			ofxGgmlImageGenerationCapabilities caps;
			caps.supportsTextToImage = true;
			caps.supportsImageToImage = true;
			caps.maxBatchSize = 4;
			caps.supportedSamplers = {"euler_a", "dpm2"};
			caps.modelArchitecture = "SD 1.5";
			return caps;
		});

		diffusion.setBackend(backend);
		auto caps = diffusion.getCapabilities();

		REQUIRE(caps.supportsTextToImage);
		REQUIRE(caps.supportsImageToImage);
		REQUIRE(caps.maxBatchSize == 4);
		REQUIRE(caps.supportedSamplers.size() == 2);
		REQUIRE(caps.modelArchitecture == "SD 1.5");
	}
}

TEST_CASE("Diffusion request structure", "[diffusion_inference]") {
	ofxGgmlImageGenerationRequest request;

	SECTION("Default task is TextToImage") {
		REQUIRE(request.task == ofxGgmlImageGenerationTask::TextToImage);
	}

	SECTION("Default selection mode is KeepOrder") {
		REQUIRE(request.selectionMode == ofxGgmlImageSelectionMode::KeepOrder);
	}

	SECTION("Default parameters") {
		REQUIRE(request.width == 1024);
		REQUIRE(request.height == 1024);
		REQUIRE(request.batchCount == 1);
		REQUIRE(request.seed == -1);
		REQUIRE(request.steps == 20);
		REQUIRE(request.cfgScale == 7.0f);
		REQUIRE(request.strength == 1.0f);
		REQUIRE(request.normalizeClipEmbeddings);
		REQUIRE(request.saveMetadata);
	}

	SECTION("Prompt can be set") {
		request.prompt = "A beautiful landscape";
		REQUIRE(request.prompt == "A beautiful landscape");
	}

	SECTION("Negative prompt can be set") {
		request.negativePrompt = "blurry";
		REQUIRE(request.negativePrompt == "blurry");
	}

	SECTION("Progress callback can be set") {
		bool callbackCalled = false;
		request.progressCallback = [&callbackCalled](const ofxGgmlImageGenerationProgress & progress) {
			callbackCalled = true;
			return true;
		};
		REQUIRE(request.progressCallback != nullptr);

		ofxGgmlImageGenerationProgress progress;
		request.progressCallback(progress);
		REQUIRE(callbackCalled);
	}
}

TEST_CASE("Diffusion result structure", "[diffusion_inference]") {
	ofxGgmlImageGenerationResult result;

	SECTION("Default state is failure") {
		REQUIRE_FALSE(result.success);
		REQUIRE(result.elapsedMs == 0.0f);
		REQUIRE(result.images.empty());
		REQUIRE(result.metadata.empty());
		REQUIRE(result.errorType == ofxGgmlImageGenerationErrorType::None);
	}

	SECTION("Backend name can be set") {
		result.backendName = "TestBackend";
		REQUIRE(result.backendName == "TestBackend");
	}

	SECTION("Diagnostics are available") {
		result.diagnostics.modelLoadTimeMs = 1000.0f;
		result.diagnostics.generationTimeMs = 5000.0f;
		result.diagnostics.contextReloads = 1;
		result.diagnostics.modelArchitecture = "SD 1.5";

		REQUIRE(result.diagnostics.modelLoadTimeMs == 1000.0f);
		REQUIRE(result.diagnostics.generationTimeMs == 5000.0f);
		REQUIRE(result.diagnostics.contextReloads == 1);
		REQUIRE(result.diagnostics.modelArchitecture == "SD 1.5");
	}
}

TEST_CASE("Diffusion image artifact structure", "[diffusion_inference]") {
	ofxGgmlGeneratedImage image;

	SECTION("Default values") {
		REQUIRE(image.path.empty());
		REQUIRE(image.width == 0);
		REQUIRE(image.height == 0);
		REQUIRE(image.seed == -1);
		REQUIRE(image.index == 0);
		REQUIRE_FALSE(image.selected);
		REQUIRE(image.score == 0.0f);
	}

	SECTION("Values can be set") {
		image.path = "/tmp/image.png";
		image.width = 512;
		image.height = 512;
		image.seed = 12345;
		image.score = 0.95f;
		image.index = 2;
		image.selected = true;

		REQUIRE(image.path == "/tmp/image.png");
		REQUIRE(image.width == 512);
		REQUIRE(image.height == 512);
		REQUIRE(image.seed == 12345);
		REQUIRE(image.score == 0.95f);
		REQUIRE(image.index == 2);
		REQUIRE(image.selected);
	}
}

TEST_CASE("Diffusion generate with mock backend", "[diffusion_inference]") {
	ofxGgmlDiffusionInference diffusion;

	SECTION("Generate with no backend returns error") {
		ofxGgmlImageGenerationRequest request;
		request.prompt = "A cat";
		auto result = diffusion.generate(request);
		REQUIRE_FALSE(result.success);
		REQUIRE(result.errorType == ofxGgmlImageGenerationErrorType::ConfigurationError);
	}

	SECTION("Generate with configured backend") {
		auto backend = std::dynamic_pointer_cast<ofxGgmlStableDiffusionBridgeBackend>(
			ofxGgmlDiffusionInference::createStableDiffusionBridgeBackend());

		backend->setGenerateFunction([](const ofxGgmlImageGenerationRequest & req) {
			ofxGgmlImageGenerationResult res;
			res.success = true;
			res.backendName = "MockDiffusion";
			res.elapsedMs = 2500.0f;
			res.diagnostics.generationTimeMs = 2000.0f;
			res.diagnostics.modelLoadTimeMs = 500.0f;

			for (int i = 0; i < req.batchCount; ++i) {
				ofxGgmlGeneratedImage image;
				image.path = "/tmp/image_" + std::to_string(i) + ".png";
				image.width = req.width;
				image.height = req.height;
				image.seed = req.seed + i;
				image.index = i;
				res.images.push_back(image);
			}

			return res;
		});

		diffusion.setBackend(backend);

		ofxGgmlImageGenerationRequest request;
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
		REQUIRE(result.diagnostics.generationTimeMs == 2000.0f);

		for (size_t i = 0; i < result.images.size(); ++i) {
			REQUIRE(result.images[i].width == 512);
			REQUIRE(result.images[i].height == 512);
			REQUIRE(result.images[i].index == i);
		}
	}

	SECTION("Generate propagates backend errors") {
		auto backend = std::dynamic_pointer_cast<ofxGgmlStableDiffusionBridgeBackend>(
			ofxGgmlDiffusionInference::createStableDiffusionBridgeBackend());

		backend->setGenerateFunction([](const ofxGgmlImageGenerationRequest &) {
			ofxGgmlImageGenerationResult res;
			res.success = false;
			res.error = "Mock generation error";
			res.errorType = ofxGgmlImageGenerationErrorType::GenerationError;
			return res;
		});

		diffusion.setBackend(backend);

		ofxGgmlImageGenerationRequest request;
		request.prompt = "Test";
		auto result = diffusion.generate(request);

		REQUIRE_FALSE(result.success);
		REQUIRE(result.error == "Mock generation error");
		REQUIRE(result.errorType == ofxGgmlImageGenerationErrorType::GenerationError);
	}

	SECTION("Generate with progress callback") {
		auto backend = std::dynamic_pointer_cast<ofxGgmlStableDiffusionBridgeBackend>(
			ofxGgmlDiffusionInference::createStableDiffusionBridgeBackend());

		backend->setGenerateFunction([](const ofxGgmlImageGenerationRequest & req) {
			ofxGgmlImageGenerationResult res;
			res.success = true;

			// Simulate progress callbacks
			if (req.progressCallback) {
				for (int step = 0; step < req.steps; ++step) {
					ofxGgmlImageGenerationProgress progress;
					progress.currentStep = step;
					progress.totalSteps = req.steps;
					progress.progress = static_cast<float>(step) / req.steps;
					progress.currentPhase = "diffusion";

					if (!req.progressCallback(progress)) {
						res.success = false;
						res.error = "Cancelled by user";
						res.errorType = ofxGgmlImageGenerationErrorType::GenerationError;
						return res;
					}
				}
			}

			return res;
		});

		diffusion.setBackend(backend);

		ofxGgmlImageGenerationRequest request;
		request.prompt = "Test";
		request.steps = 10;

		int callbackCount = 0;
		request.progressCallback = [&callbackCount](const ofxGgmlImageGenerationProgress & progress) {
			callbackCount++;
			REQUIRE(progress.currentPhase == "diffusion");
			REQUIRE(progress.totalSteps == 10);
			return true;
		};

		auto result = diffusion.generate(request);
		REQUIRE(result.success);
		REQUIRE(callbackCount == 10);
	}
}

TEST_CASE("Diffusion ranking metadata", "[diffusion_inference]") {
	ofxGgmlImageGenerationResult result;

	SECTION("Ranking scores on images") {
		ofxGgmlGeneratedImage img1;
		img1.score = 0.95f;
		img1.index = 0;
		img1.selected = true;

		ofxGgmlGeneratedImage img2;
		img2.score = 0.75f;
		img2.index = 1;
		img2.selected = false;

		result.images = {img1, img2};

		REQUIRE(result.images[0].score == 0.95f);
		REQUIRE(result.images[1].score == 0.75f);
		REQUIRE(result.images[0].selected);
		REQUIRE_FALSE(result.images[1].selected);
	}

	SECTION("General metadata") {
		result.metadata.push_back({"model", "stable-diffusion-v1.5"});
		result.metadata.push_back({"sampler", "euler_a"});

		REQUIRE(result.metadata.size() == 2);
		REQUIRE(result.metadata[0].first == "model");
		REQUIRE(result.metadata[1].first == "sampler");
	}
}

TEST_CASE("Diffusion model profiles", "[diffusion_inference]") {
	auto profiles = ofxGgmlDiffusionInference::defaultProfiles();

	SECTION("Returns model profiles") {
		REQUIRE(profiles.size() >= 0);
	}

	SECTION("Profile structure") {
		if (!profiles.empty()) {
			const auto & profile = profiles[0];
			REQUIRE(profile.name.length() >= 0);
			REQUIRE(profile.architecture.length() >= 0);
		}
	}
}
