#pragma once

#include "ofxGgmlSegmentationInference.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(OFXGGML_ENABLE_SAM3_ADAPTER) && defined(__has_include)
#if __has_include("sam3.h")
#define OFXGGML_HAS_SAM3 1
#include "sam3.h"
#else
#define OFXGGML_HAS_SAM3 0
#endif
#else
#define OFXGGML_HAS_SAM3 0
#endif

namespace ofxGgmlSam3Adapters {

struct RuntimeOptions {
	int threads = -1;
	int seed = 42;
	bool useGpu = true;
	int encodeImageSize = 0;
};

inline int resolveThreadCount(const RuntimeOptions & options) {
	if (options.threads > 0) {
		return options.threads;
	}
	const unsigned int detected = std::thread::hardware_concurrency();
	return detected > 0 ? static_cast<int>(detected) : 4;
}

#if OFXGGML_HAS_SAM3

using ModelHandle = std::shared_ptr<sam3_model>;

struct Runtime {
	ModelHandle model;
	sam3_state_ptr state;
	sam3_params params;
	std::string modelPath;
};

inline sam3_params makeParams(
	const std::string & modelPath,
	const RuntimeOptions & options) {
	sam3_params params;
	params.model_path = modelPath;
	params.n_threads = resolveThreadCount(options);
	params.use_gpu = options.useGpu;
	params.seed = options.seed;
	params.encode_img_size = options.encodeImageSize;
	return params;
}

inline std::shared_ptr<Runtime> loadRuntime(
	const std::string & modelPath,
	const RuntimeOptions & options = {},
	std::string * error = nullptr) {
	if (modelPath.empty()) {
		if (error) {
			*error = "sam3.cpp model path is empty";
		}
		return {};
	}

	auto runtime = std::make_shared<Runtime>();
	runtime->params = makeParams(modelPath, options);
	runtime->modelPath = modelPath;
	runtime->model = sam3_load_model(runtime->params);
	if (!runtime->model) {
		if (error) {
			*error = "failed to load sam3.cpp model: " + modelPath;
		}
		return {};
	}

	runtime->state = sam3_create_state(*runtime->model, runtime->params);
	if (!runtime->state) {
		if (error) {
			*error = "failed to create sam3.cpp inference state: " + modelPath;
		}
		return {};
	}
	return runtime;
}

inline bool fillSam3Image(
	const ofxGgmlSegmentationRequest & request,
	sam3_image & image,
	std::string * error = nullptr) {
	if (request.imageWidth <= 0 || request.imageHeight <= 0) {
		if (error) {
			*error = "sam3.cpp adapter requires imageWidth and imageHeight";
		}
		return false;
	}
	const size_t expectedSize =
		static_cast<size_t>(request.imageWidth) *
		static_cast<size_t>(request.imageHeight) *
		3u;
	if (request.imageRgb.size() != expectedSize) {
		if (error) {
			*error =
				"sam3.cpp adapter requires RGB image data sized imageWidth * imageHeight * 3";
		}
		return false;
	}
	image.width = request.imageWidth;
	image.height = request.imageHeight;
	image.channels = 3;
	image.data.assign(request.imageRgb.begin(), request.imageRgb.end());
	return true;
}

inline ofxGgmlSegmentationResult segmentWithRuntime(
	const std::shared_ptr<Runtime> & runtime,
	const ofxGgmlSegmentationRequest & request) {
	ofxGgmlSegmentationResult result;
	result.backendName = "sam3.cpp";
	result.imagePath = request.imagePath;
	if (!runtime || !runtime->model || !runtime->state) {
		result.error = "sam3.cpp runtime is not loaded";
		return result;
	}
	if (request.points.empty()) {
		result.error = "sam3.cpp adapter requires at least one point prompt";
		return result;
	}

	sam3_image image;
	if (!fillSam3Image(request, image, &result.error)) {
		return result;
	}

	const auto started = std::chrono::steady_clock::now();
	if (!sam3_encode_image(*runtime->state, *runtime->model, image)) {
		result.error = "sam3.cpp failed to encode image";
		return result;
	}

	sam3_pvs_params pvs;
	pvs.multimask = request.returnMultipleMasks;
	for (const auto & point : request.points) {
		const sam3_point samPoint {
			std::clamp(point.x, 0.0f, 1.0f) *
				static_cast<float>(request.imageWidth),
			std::clamp(point.y, 0.0f, 1.0f) *
				static_cast<float>(request.imageHeight)
		};
		if (point.positive) {
			pvs.pos_points.push_back(samPoint);
		} else {
			pvs.neg_points.push_back(samPoint);
		}
	}

	const sam3_result samResult =
		sam3_segment_pvs(*runtime->state, *runtime->model, pvs);
	if (samResult.detections.empty()) {
		result.error = "sam3.cpp did not produce any masks";
		return result;
	}

	const size_t maskCount = request.returnMultipleMasks
		? samResult.detections.size()
		: 1u;
	for (size_t i = 0; i < maskCount; ++i) {
		const auto & detection = samResult.detections[i];
		const auto & samMask = detection.mask;
		if (samMask.width <= 0 || samMask.height <= 0 || samMask.data.empty()) {
			continue;
		}

		ofxGgmlSegmentationMask mask;
		mask.maskId = "mask-" + std::to_string(i);
		mask.width = samMask.width;
		mask.height = samMask.height;
		mask.score = samMask.iou_score;
		mask.pixels = samMask.data;
		mask.metadata.push_back({ "prompt", "point" });
		mask.metadata.push_back({ "objScore", std::to_string(detection.score) });
		mask.metadata.push_back({ "iouScore", std::to_string(detection.iou_score) });
		mask.metadata.push_back({ "instanceId", std::to_string(detection.instance_id) });
		result.masks.push_back(std::move(mask));
	}

	if (result.masks.empty()) {
		result.error = "sam3.cpp produced detections without usable masks";
		return result;
	}

	result.success = true;
	result.elapsedMs = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - started).count();
	result.metadata.push_back({ "backend", "sam3.cpp" });
	result.metadata.push_back({ "threads", std::to_string(runtime->params.n_threads) });
	result.metadata.push_back({ "useGpu", runtime->params.use_gpu ? "true" : "false" });
	result.metadata.push_back({ "modelPath", runtime->modelPath });
	return result;
}

inline std::shared_ptr<ofxGgmlSegmentationBackend> createBackend(
	std::shared_ptr<Runtime> runtime,
	const std::string & displayName = "sam3.cpp") {
	auto mutex = std::make_shared<std::mutex>();
	return ofxGgmlSegmentationInference::createSegmentationBridgeBackend(
		[runtime, mutex](const ofxGgmlSegmentationRequest & request) {
			std::lock_guard<std::mutex> lock(*mutex);
			return segmentWithRuntime(runtime, request);
		},
		displayName);
}

inline std::shared_ptr<ofxGgmlSegmentationBackend> createBackend(
	const std::string & modelPath,
	const RuntimeOptions & options = {},
	const std::string & displayName = "sam3.cpp") {
	std::string error;
	auto runtime = loadRuntime(modelPath, options, &error);
	if (!runtime) {
		return ofxGgmlSegmentationInference::createSegmentationBridgeBackend(
			[error, modelPath](const ofxGgmlSegmentationRequest & request) {
				ofxGgmlSegmentationResult result;
				result.backendName = "sam3.cpp";
				result.imagePath = request.imagePath;
				result.error = error.empty()
					? "failed to load sam3.cpp model: " + modelPath
					: error;
				return result;
			},
			displayName);
	}
	return createBackend(std::move(runtime), displayName);
}

#else

inline std::shared_ptr<ofxGgmlSegmentationBackend> createBackend(
	const std::string & modelPath,
	const RuntimeOptions & = {},
	const std::string & displayName = "sam3.cpp") {
	return ofxGgmlSegmentationInference::createSegmentationBridgeBackend(
		[modelPath](const ofxGgmlSegmentationRequest & request) {
			ofxGgmlSegmentationResult result;
			result.backendName = "sam3.cpp";
			result.imagePath = request.imagePath;
			result.error =
				"sam3.cpp adapter is disabled. Define OFXGGML_ENABLE_SAM3_ADAPTER, "
				"add sam3.cpp headers, and link sam3.lib before using model: " + modelPath;
			return result;
		},
		displayName);
}

#endif

inline void attachBackend(
	ofxGgmlSegmentationInference & inference,
	const std::string & modelPath,
	const RuntimeOptions & options = {},
	const std::string & displayName = "sam3.cpp") {
	inference.setBackend(createBackend(modelPath, options, displayName));
}

} // namespace ofxGgmlSam3Adapters
