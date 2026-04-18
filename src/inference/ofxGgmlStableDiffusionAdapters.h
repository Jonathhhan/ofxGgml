#pragma once

#include "ofxGgmlClipInference.h"
#include "ofxGgmlDiffusionInference.h"

#include <chrono>
#include <memory>
#include <string>
#include <utility>

#if defined(__has_include)
#if __has_include("ofxStableDiffusion.h")
#define OFXGGML_HAS_OFXSTABLEDIFFUSION 1
#include "ofxStableDiffusion.h"
#else
#define OFXGGML_HAS_OFXSTABLEDIFFUSION 0
#endif
#else
#define OFXGGML_HAS_OFXSTABLEDIFFUSION 0
#endif

#if OFXGGML_HAS_OFXSTABLEDIFFUSION
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <system_error>
#include <sstream>
#include <thread>
#include <vector>
#endif

namespace ofxGgmlStableDiffusionAdapters {

using ClipEmbedFunction = ofxGgmlClipBridgeBackend::EmbedFunction;

inline std::shared_ptr<ofxGgmlClipBackend> createClipBackend(
	ClipEmbedFunction embedFunction = {},
	const std::string & displayName = "ofxStableDiffusionClip") {
	return ofxGgmlClipInference::createClipBridgeBackend(
		std::move(embedFunction),
		displayName);
}

inline void attachClipBackend(
	ofxGgmlClipInference & inference,
	ClipEmbedFunction embedFunction,
	const std::string & displayName = "ofxStableDiffusionClip") {
	inference.setBackend(createClipBackend(std::move(embedFunction), displayName));
}

#if OFXGGML_HAS_OFXSTABLEDIFFUSION

struct RuntimeOptions {
	int clipSkip = -1;
	int threads = -1;
	sd_type_t weightType = SD_TYPE_F16;
	rng_type_t rngType = STD_DEFAULT_RNG;
	scheduler_t schedule = SCHEDULER_COUNT;
	bool vaeDecodeOnly = false;
	bool vaeTiling = false;
	bool freeParamsImmediately = false;
	bool keepClipOnCpu = false;
	bool keepControlNetCpu = false;
	bool keepVaeOnCpu = false;
	bool normalizeInput = true;
	float styleStrength = 20.0f;
	float defaultControlStrength = 0.9f;
	std::string taesdPath;
	std::string controlNetPath;
	std::string loraModelDir;
	std::string embedDir;
	std::string stackedIdEmbedDir;
	std::string inputIdImagesPath;
	std::string esrganPath;
	int esrganMultiplier = 4;
	std::shared_ptr<ofxGgmlClipInference> clipInference;
	std::string clipScorerName = "ofxGgmlClip";
	std::chrono::milliseconds pollInterval{15};
	std::chrono::seconds timeout{300};
};

inline sample_method_t parseSampleMethod(const std::string & sampler) {
	std::string lowered;
	lowered.reserve(sampler.size());
	for (const char c : sampler) {
		if (c == ' ' || c == '-' || c == '.') {
			continue;
		}
		lowered.push_back(static_cast<char>(std::tolower(
			static_cast<unsigned char>(c))));
	}
	if (lowered.empty() ||
		lowered == "eulera" ||
		lowered == "eulerasamplemethod") {
		return EULER_A_SAMPLE_METHOD;
	}
	if (lowered == "euler" || lowered == "eulersamplemethod") {
		return EULER_SAMPLE_METHOD;
	}
	if (lowered == "heun" || lowered == "heunsamplemethod") {
		return HEUN_SAMPLE_METHOD;
	}
	if (lowered == "dpm2" || lowered == "dpm2samplemethod") {
		return DPM2_SAMPLE_METHOD;
	}
	if (lowered == "dpmpp2sa" || lowered == "dpmpp2sasamplemethod") {
		return DPMPP2S_A_SAMPLE_METHOD;
	}
	if (lowered == "dpmpp2m" || lowered == "dpmpp2msamplemethod") {
		return DPMPP2M_SAMPLE_METHOD;
	}
	if (lowered == "dpmpp2mv2" || lowered == "dpmpp2mv2samplemethod") {
		return DPMPP2Mv2_SAMPLE_METHOD;
	}
	if (lowered == "lcm" || lowered == "lcmsamplemethod") {
		return LCM_SAMPLE_METHOD;
	}
	return EULER_A_SAMPLE_METHOD;
}

inline bool waitForIdle(
	ofxStableDiffusion & engine,
	const RuntimeOptions & options,
	std::string * error = nullptr) {
	const auto started = std::chrono::steady_clock::now();
	while (engine.isGenerating() || engine.isModelLoading) {
		if (std::chrono::steady_clock::now() - started > options.timeout) {
			if (error) {
				*error = "timed out while waiting for ofxStableDiffusion";
			}
			return false;
		}
		std::this_thread::sleep_for(options.pollInterval);
	}
	return true;
}

inline std::string sanitizeOutputPrefix(const std::string & value) {
	std::string sanitized;
	sanitized.reserve(value.size());
	for (const char c : value) {
		const unsigned char uc = static_cast<unsigned char>(c);
		if (std::isalnum(uc) || c == '_' || c == '-') {
			sanitized.push_back(c);
		} else if (c == ' ' || c == '.') {
			sanitized.push_back('_');
		}
	}
	return sanitized.empty() ? "diffusion" : sanitized;
}

inline std::string makeTimestampToken() {
	const auto now = std::chrono::system_clock::now();
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		now.time_since_epoch()).count();
	return std::to_string(ms);
}

inline ofxStableDiffusionImageMode mapImageMode(
	ofxGgmlImageGenerationTask task) {
	switch (task) {
	case ofxGgmlImageGenerationTask::InstructImage:
		return ofxStableDiffusionImageMode::InstructImage;
	case ofxGgmlImageGenerationTask::Variation:
		return ofxStableDiffusionImageMode::Variation;
	case ofxGgmlImageGenerationTask::Restyle:
		return ofxStableDiffusionImageMode::Restyle;
	case ofxGgmlImageGenerationTask::ImageToImage:
		return ofxStableDiffusionImageMode::ImageToImage;
	case ofxGgmlImageGenerationTask::TextToImage:
	default:
		return ofxStableDiffusionImageMode::TextToImage;
	}
}

inline ofxStableDiffusionImageSelectionMode mapSelectionMode(
	ofxGgmlImageSelectionMode mode) {
	switch (mode) {
	case ofxGgmlImageSelectionMode::Rerank:
		return ofxStableDiffusionImageSelectionMode::Rerank;
	case ofxGgmlImageSelectionMode::BestOnly:
		return ofxStableDiffusionImageSelectionMode::BestOnly;
	case ofxGgmlImageSelectionMode::KeepOrder:
	default:
		return ofxStableDiffusionImageSelectionMode::KeepOrder;
	}
}

inline bool loadSdImageFromPath(
	const std::string & path,
	ofPixels & storage,
	sd_image_t & image,
	std::string & error) {
	if (path.empty()) {
		error = "image path is empty";
		return false;
	}
	ofImage loaded;
	if (!ofLoadImage(loaded, path)) {
		error = "failed to load image: " + path;
		return false;
	}
	storage = loaded.getPixels();
	if (storage.getNumChannels() != 1 &&
		storage.getNumChannels() != 3 &&
		storage.getNumChannels() != 4) {
		storage.setImageType(OF_IMAGE_COLOR);
	}
	image.width = static_cast<uint32_t>(storage.getWidth());
	image.height = static_cast<uint32_t>(storage.getHeight());
	image.channel = static_cast<uint32_t>(storage.getNumChannels());
	image.data = storage.getData();
	return true;
}

inline ofImageType imageTypeForChannels(uint32_t channels) {
	switch (channels) {
	case 1: return OF_IMAGE_GRAYSCALE;
	case 4: return OF_IMAGE_COLOR_ALPHA;
	case 3:
	default:
		return OF_IMAGE_COLOR;
	}
}

inline bool saveSdImageToPath(
	const sd_image_t & image,
	const std::string & path,
	std::string * error = nullptr) {
	if (!image.data || image.width == 0 || image.height == 0) {
		if (error) {
			*error = "generated image buffer is empty";
		}
		return false;
	}
	ofPixels pixels;
	pixels.setFromPixels(
		image.data,
		static_cast<int>(image.width),
		static_cast<int>(image.height),
		imageTypeForChannels(image.channel));
	if (!ofSaveImage(pixels, path)) {
		if (error) {
			*error = "failed to save image: " + path;
		}
		return false;
	}
	return true;
}

inline std::vector<ofxStableDiffusionImageScore> scoreImagesWithClip(
	const ofxGgmlClipInference & clipInference,
	const ofxGgmlImageGenerationRequest & request,
	const std::vector<ofxStableDiffusionImageFrame> & frames,
	const std::string & scorerName) {
	std::vector<ofxStableDiffusionImageScore> scores(
		frames.size(),
		ofxStableDiffusionImageScore{});
	if (frames.empty()) {
		return scores;
	}

	const std::string rankingPrompt =
		!request.rankingPrompt.empty()
			? request.rankingPrompt
			: (!request.instruction.empty() ? request.instruction : request.prompt);
	if (rankingPrompt.empty()) {
		return scores;
	}

	const auto query = clipInference.embedText(
		rankingPrompt,
		request.normalizeClipEmbeddings,
		"prompt",
		"Prompt");
	if (!query.success || query.embedding.empty()) {
		for (auto & score : scores) {
			score.scorer = scorerName;
			score.summary = query.error.empty()
				? "CLIP text embedding failed"
				: query.error;
		}
		return scores;
	}

	const std::filesystem::path tempDir =
		std::filesystem::temp_directory_path() /
		("ofxggml_sd_clip_rank_" + makeTimestampToken());
	std::error_code dirEc;
	std::filesystem::create_directories(tempDir, dirEc);
	if (dirEc) {
		for (auto & score : scores) {
			score.scorer = scorerName;
			score.summary = "failed to create temp rank directory";
		}
		return scores;
	}

	for (std::size_t i = 0; i < frames.size(); ++i) {
		const auto & frame = frames[i];
		ofxStableDiffusionImageScore score;
		score.scorer = scorerName;
		if (!frame.isAllocated()) {
			score.summary = "frame is not allocated";
			scores[i] = std::move(score);
			continue;
		}

		const std::filesystem::path imagePath =
			tempDir / ("rank_" + std::to_string(i) + ".png");
		if (!ofSaveImage(frame.pixels, imagePath.string())) {
			score.summary = "failed to write temp frame";
			scores[i] = std::move(score);
			continue;
		}

		const auto imageEmbedding = clipInference.embedImage(
			imagePath.string(),
			request.normalizeClipEmbeddings,
			"image-" + std::to_string(i),
			imagePath.filename().string());
		std::error_code removeEc;
		std::filesystem::remove(imagePath, removeEc);

		if (!imageEmbedding.success || imageEmbedding.embedding.empty()) {
			score.summary = imageEmbedding.error.empty()
				? "CLIP image embedding failed"
				: imageEmbedding.error;
			scores[i] = std::move(score);
			continue;
		}

		score.valid = true;
		score.score = ofxGgmlClipInference::cosineSimilarity(
			query.embedding,
			imageEmbedding.embedding);
		score.summary = "CLIP cosine similarity";
		score.metadata.push_back({"prompt", rankingPrompt});
		score.metadata.push_back({"imagePath", imageEmbedding.imagePath});
		scores[i] = std::move(score);
	}

	std::error_code cleanupEc;
	std::filesystem::remove(tempDir, cleanupEc);
	return scores;
}

inline bool needsContextReload(
	const ofxStableDiffusion & engine,
	const ofxGgmlImageGenerationRequest & request,
	const RuntimeOptions & options) {
	return engine.thread.sdCtx == nullptr ||
		engine.modelPath != request.modelPath ||
		engine.vaePath != request.vaePath ||
		engine.taesdPath != options.taesdPath ||
		engine.controlNetPathCStr != options.controlNetPath ||
		engine.loraModelDir != options.loraModelDir ||
		engine.embedDirCStr != options.embedDir ||
		engine.stackedIdEmbedDirCStr != options.stackedIdEmbedDir ||
		engine.nThreads != options.threads ||
		engine.wType != options.weightType ||
		engine.rngType != options.rngType ||
		engine.schedule != options.schedule ||
		engine.vaeDecodeOnly != options.vaeDecodeOnly ||
		engine.vaeTiling != options.vaeTiling ||
		engine.freeParamsImmediately != options.freeParamsImmediately ||
		engine.keepClipOnCpu != options.keepClipOnCpu ||
		engine.keepControlNetCpu != options.keepControlNetCpu ||
		engine.keepVaeOnCpu != options.keepVaeOnCpu;
}

inline std::shared_ptr<ofxGgmlImageGenerationBackend> createImageBackend(
	std::shared_ptr<ofxStableDiffusion> engine,
	const RuntimeOptions & options = {}) {
	return ofxGgmlDiffusionInference::createStableDiffusionBridgeBackend(
		[engine, options](const ofxGgmlImageGenerationRequest & request) {
			ofxGgmlImageGenerationResult result;
			result.backendName = "ofxStableDiffusion";

			if (!engine) {
				result.error = "ofxStableDiffusion engine is null";
				return result;
			}
			const bool isInstructionTask =
				request.task == ofxGgmlImageGenerationTask::InstructImage;
			if (request.prompt.empty() &&
				request.instruction.empty() &&
				request.task != ofxGgmlImageGenerationTask::Upscale) {
				result.error = "prompt is empty";
				return result;
			}
			if (request.task == ofxGgmlImageGenerationTask::Inpaint) {
				result.error =
					"inpaint is not exposed by the current ofxStableDiffusion wrapper yet";
				return result;
			}

			const auto started = std::chrono::steady_clock::now();
			std::string waitError;
			if (!waitForIdle(*engine, options, &waitError)) {
				result.error = waitError;
				return result;
			}

			const std::string outputDir = request.outputDir.empty()
				? ofToDataPath("generated", true)
				: request.outputDir;
			std::error_code dirEc;
			std::filesystem::create_directories(outputDir, dirEc);
			if (dirEc) {
				result.error = "failed to create output directory: " + outputDir;
				return result;
			}

			if (request.task == ofxGgmlImageGenerationTask::Upscale) {
				if (request.initImagePath.empty()) {
					result.error = "upscale requires an init image";
					return result;
				}
				if (options.esrganPath.empty()) {
					result.error =
						"upscale requires RuntimeOptions.esrganPath for the current wrapper";
					return result;
				}

				ofPixels inputPixels;
				sd_image_t inputImage{};
				std::string imageError;
				if (!loadSdImageFromPath(
					request.initImagePath,
					inputPixels,
					inputImage,
					imageError)) {
					result.error = imageError;
					return result;
				}

				engine->newUpscalerCtx(
					options.esrganPath,
					options.threads,
					options.weightType);
				const sd_image_t upscaled =
					engine->upscaleImage(inputImage, options.esrganMultiplier);
				const std::string outputPath =
					(std::filesystem::path(outputDir) /
						(sanitizeOutputPrefix(request.outputPrefix) + "_" +
							makeTimestampToken() + "_upscale.png")).string();
				std::string saveError;
				if (!saveSdImageToPath(upscaled, outputPath, &saveError)) {
					result.error = saveError;
					return result;
				}
				result.success = true;
				result.elapsedMs = std::chrono::duration<float, std::milli>(
					std::chrono::steady_clock::now() - started).count();
				ofxGgmlGeneratedImage generated;
				generated.path = outputPath;
				generated.width = static_cast<int>(upscaled.width);
				generated.height = static_cast<int>(upscaled.height);
				generated.seed = request.seed;
				generated.index = 0;
				result.images.push_back(std::move(generated));
				result.metadata.push_back({"task", "Upscale"});
				result.metadata.push_back({"backend", result.backendName});
				return result;
			}

			if (request.modelPath.empty()) {
				result.error = "modelPath is empty";
				return result;
			}

			if (needsContextReload(*engine, request, options)) {
				ofxStableDiffusionContextSettings contextSettings;
				contextSettings.modelPath = request.modelPath;
				contextSettings.diffusionModelPath = request.modelPath;
				contextSettings.clipLPath = request.clipLPath;
				contextSettings.clipGPath = request.clipGPath;
				contextSettings.t5xxlPath = request.t5xxlPath;
				contextSettings.vaePath = request.vaePath;
				contextSettings.taesdPath = options.taesdPath;
				contextSettings.controlNetPath = options.controlNetPath;
				contextSettings.loraModelDir = options.loraModelDir;
				contextSettings.embedDir = options.embedDir;
				contextSettings.stackedIdEmbedDir = options.stackedIdEmbedDir;
				contextSettings.vaeDecodeOnly = options.vaeDecodeOnly;
				contextSettings.vaeTiling = options.vaeTiling;
				contextSettings.freeParamsImmediately = options.freeParamsImmediately;
				contextSettings.nThreads = options.threads;
				contextSettings.weightType = options.weightType;
				contextSettings.rngType = options.rngType;
				contextSettings.schedule = options.schedule;
				contextSettings.keepClipOnCpu = options.keepClipOnCpu;
				contextSettings.keepControlNetCpu = options.keepControlNetCpu;
				contextSettings.keepVaeOnCpu = options.keepVaeOnCpu;
				contextSettings.offloadParamsToCpu = engine->offloadParamsToCpu;
				contextSettings.flashAttn = engine->flashAttn;
				contextSettings.enableMmap = engine->enableMmap;
				engine->newSdCtx(contextSettings);
				if (!waitForIdle(*engine, options, &waitError)) {
					result.error = "model setup failed: " + waitError;
					return result;
				}
				if (engine->thread.sdCtx == nullptr) {
					result.error = "ofxStableDiffusion failed to create an sd context";
					return result;
				}
			}

			ofPixels initPixels;
			ofPixels controlPixels;
			sd_image_t initImage{};
			sd_image_t controlImage{};
			std::string imageError;
			sd_image_t * controlImagePtr = nullptr;
			if (!request.controlImagePath.empty()) {
				if (!loadSdImageFromPath(
					request.controlImagePath,
					controlPixels,
					controlImage,
					imageError)) {
					result.error = imageError;
					return result;
				}
				controlImagePtr = &controlImage;
			}

			engine->setDiffused(false);
			engine->setImageSelectionMode(mapSelectionMode(request.selectionMode));
			if (options.clipInference &&
				request.selectionMode != ofxGgmlImageSelectionMode::KeepOrder) {
				engine->setImageRankCallback(
					[clipInference = options.clipInference,
						request,
						scorerName = options.clipScorerName](
							const ofxStableDiffusionImageRequest &,
							const std::vector<ofxStableDiffusionImageFrame> & frames) {
						return scoreImagesWithClip(
							*clipInference,
							request,
							frames,
							scorerName);
					});
			} else {
				engine->setImageRankCallback({});
			}
			const sample_method_t sampleMethod =
				parseSampleMethod(request.sampler);
			ofxStableDiffusionImageRequest sdRequest;
			sdRequest.mode = mapImageMode(request.task);
			sdRequest.selectionMode = mapSelectionMode(request.selectionMode);
			sdRequest.prompt = request.prompt;
			sdRequest.instruction = isInstructionTask
				? (!request.instruction.empty() ? request.instruction : request.prompt)
				: request.instruction;
			sdRequest.negativePrompt = request.negativePrompt;
			sdRequest.clipSkip = options.clipSkip;
			sdRequest.cfgScale = request.cfgScale;
			sdRequest.width = request.width;
			sdRequest.height = request.height;
			sdRequest.sampleMethod = sampleMethod;
			sdRequest.sampleSteps = request.steps;
			sdRequest.strength = request.strength;
			sdRequest.seed = request.seed;
			sdRequest.batchCount = request.batchCount;
			sdRequest.controlCond = controlImagePtr;
			sdRequest.controlStrength = request.controlImagePath.empty()
				? options.defaultControlStrength
				: request.controlStrength;
			sdRequest.styleStrength = options.styleStrength;
			sdRequest.normalizeInput = options.normalizeInput;
			sdRequest.inputIdImagesPath = options.inputIdImagesPath;

			if (sdRequest.mode != ofxStableDiffusionImageMode::TextToImage) {
				if (request.initImagePath.empty()) {
					result.error = "selected task requires initImagePath";
					return result;
				}
				if (!loadSdImageFromPath(
					request.initImagePath,
					initPixels,
					initImage,
					imageError)) {
					result.error = imageError;
					return result;
				}
				sdRequest.initImage = initImage;
			}
			engine->generate(sdRequest);

			if (!waitForIdle(*engine, options, &waitError)) {
				result.error = "generation failed: " + waitError;
				return result;
			}
			if (!engine->isDiffused()) {
				result.error =
					"ofxStableDiffusion returned without marking generation complete";
				return result;
			}

			const auto & rankedResult = engine->getLastResult();
			if (rankedResult.images.empty()) {
				result.error = "ofxStableDiffusion returned no images";
				engine->setDiffused(false);
				return result;
			}

			const std::string prefix = sanitizeOutputPrefix(request.outputPrefix);
			const std::string timestamp = makeTimestampToken();
			std::size_t savedImageCount = 0;
			for (std::size_t i = 0; i < rankedResult.images.size(); ++i) {
				const auto & frame = rankedResult.images[i];
				if (!frame.isAllocated()) {
					continue;
				}
				const std::string fileName =
					prefix + "_" + timestamp + "_" + std::to_string(i) + ".png";
				const std::string outputPath =
					(std::filesystem::path(outputDir) / fileName).string();
				if (!ofSaveImage(frame.pixels, outputPath)) {
					result.error = "failed to save image: " + outputPath;
					engine->setDiffused(false);
					return result;
				}
				ofxGgmlGeneratedImage generated;
				generated.path = outputPath;
				generated.width = frame.width();
				generated.height = frame.height();
				generated.seed = static_cast<int>(frame.seed);
				generated.index = static_cast<int>(i);
				generated.sourceIndex = frame.sourceIndex;
				generated.selected = frame.isSelected;
				generated.score = frame.score.score;
				generated.scorer = frame.score.scorer;
				generated.scoreSummary = frame.score.summary;
				result.images.push_back(std::move(generated));
				if (frame.score.valid) {
					result.metadata.push_back({
						"image[" + std::to_string(i) + "].score",
						ofToString(frame.score.score, 6)
					});
				}
				if (!frame.score.scorer.empty()) {
					result.metadata.push_back({
						"image[" + std::to_string(i) + "].scorer",
						frame.score.scorer
					});
				}
				if (frame.isSelected) {
					result.metadata.push_back({
						"selectedImageIndex",
						std::to_string(i)
					});
				}
				++savedImageCount;
			}
			if (savedImageCount == 0) {
				result.error =
					"ofxStableDiffusion produced frames, but none could be saved";
				engine->setDiffused(false);
				return result;
			}

			engine->setDiffused(false);
			result.success = true;
			result.elapsedMs = std::chrono::duration<float, std::milli>(
				std::chrono::steady_clock::now() - started).count();
			result.metadata.push_back({
				"task",
				ofxGgmlDiffusionInference::taskLabel(request.task)
			});
			result.metadata.push_back({"sampler", request.sampler});
			result.metadata.push_back({"steps", std::to_string(request.steps)});
			result.metadata.push_back({"batchCount", std::to_string(request.batchCount)});
			result.metadata.push_back({
				"selectionMode",
				ofxGgmlDiffusionInference::selectionModeLabel(request.selectionMode)
			});
			result.metadata.push_back({"modelPath", request.modelPath});
			if (!request.vaePath.empty()) {
				result.metadata.push_back({"vaePath", request.vaePath});
			}
			return result;
		},
		"ofxStableDiffusion");
}

inline void attachImageBackend(
	ofxGgmlDiffusionInference & inference,
	std::shared_ptr<ofxStableDiffusion> engine,
	const RuntimeOptions & options = {}) {
	inference.setBackend(createImageBackend(std::move(engine), options));
}

#endif

} // namespace ofxGgmlStableDiffusionAdapters
