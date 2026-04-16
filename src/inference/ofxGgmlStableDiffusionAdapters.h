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
	schedule_t schedule = DEFAULT;
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
	if (lowered.empty() || lowered == "eulera") return EULER_A;
	if (lowered == "euler") return EULER;
	if (lowered == "heun") return HEUN;
	if (lowered == "dpm2") return DPM2;
	if (lowered == "dpmpp2sa") return DPMPP2S_A;
	if (lowered == "dpmpp2m") return DPMPP2M;
	if (lowered == "dpmpp2mv2") return DPMPP2Mv2;
	if (lowered == "lcm") return LCM;
	return EULER_A;
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
			if (request.prompt.empty() &&
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
				result.images.push_back({
					outputPath,
					static_cast<int>(upscaled.width),
					static_cast<int>(upscaled.height),
					request.seed,
					0
				});
				result.metadata.push_back({"task", "Upscale"});
				result.metadata.push_back({"backend", result.backendName});
				return result;
			}

			if (request.modelPath.empty()) {
				result.error = "modelPath is empty";
				return result;
			}

			if (needsContextReload(*engine, request, options)) {
				engine->newSdCtx(
					request.modelPath,
					request.vaePath,
					options.taesdPath,
					options.controlNetPath,
					options.loraModelDir,
					options.embedDir,
					options.stackedIdEmbedDir,
					options.vaeDecodeOnly,
					options.vaeTiling,
					options.freeParamsImmediately,
					options.threads,
					options.weightType,
					options.rngType,
					options.schedule,
					options.keepClipOnCpu,
					options.keepControlNetCpu,
					options.keepVaeOnCpu);
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
			const sample_method_t sampleMethod =
				parseSampleMethod(request.sampler);
			if (request.task == ofxGgmlImageGenerationTask::TextToImage) {
				engine->txt2img(
					request.prompt,
					request.negativePrompt,
					options.clipSkip,
					request.cfgScale,
					request.width,
					request.height,
					sampleMethod,
					request.steps,
					request.seed,
					request.batchCount,
					controlImagePtr,
					request.controlImagePath.empty()
						? options.defaultControlStrength
						: request.controlStrength,
					options.styleStrength,
					options.normalizeInput,
					options.inputIdImagesPath);
			} else {
				if (request.initImagePath.empty()) {
					result.error = "img2img requires initImagePath";
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
				engine->img2img(
					initImage,
					request.prompt,
					request.negativePrompt,
					options.clipSkip,
					request.cfgScale,
					request.width,
					request.height,
					sampleMethod,
					request.steps,
					request.strength,
					request.seed,
					request.batchCount,
					controlImagePtr,
					request.controlImagePath.empty()
						? options.defaultControlStrength
						: request.controlStrength,
					options.styleStrength,
					options.normalizeInput,
					options.inputIdImagesPath);
			}

			if (!waitForIdle(*engine, options, &waitError)) {
				result.error = "generation failed: " + waitError;
				return result;
			}
			if (!engine->isDiffused()) {
				result.error =
					"ofxStableDiffusion returned without marking generation complete";
				return result;
			}

			sd_image_t * images = engine->returnImages();
			if (!images) {
				result.error = "ofxStableDiffusion returned no images";
				engine->setDiffused(false);
				return result;
			}

			const std::string prefix = sanitizeOutputPrefix(request.outputPrefix);
			const std::string timestamp = makeTimestampToken();
			for (int i = 0; i < std::max(1, request.batchCount); ++i) {
				const sd_image_t & image = images[i];
				const std::string fileName =
					prefix + "_" + timestamp + "_" + std::to_string(i) + ".png";
				const std::string outputPath =
					(std::filesystem::path(outputDir) / fileName).string();
				std::string saveError;
				if (!saveSdImageToPath(image, outputPath, &saveError)) {
					result.error = saveError;
					engine->setDiffused(false);
					return result;
				}
				result.images.push_back({
					outputPath,
					static_cast<int>(image.width),
					static_cast<int>(image.height),
					request.seed,
					i
				});
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
