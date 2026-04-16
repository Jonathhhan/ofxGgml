#pragma once

#include "ofxGgmlClipInference.h"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(__has_include)
#if __has_include("clip.h")
#define OFXGGML_HAS_CLIPCPP 1
#include "clip.h"
#else
#define OFXGGML_HAS_CLIPCPP 0
#endif
#else
#define OFXGGML_HAS_CLIPCPP 0
#endif

namespace ofxGgmlClipCppAdapters {

#if OFXGGML_HAS_CLIPCPP

using ModelHandle = std::shared_ptr<clip_ctx>;

struct RuntimeOptions {
	int threads = -1;
	int verbosity = 1;
	bool normalizeByDefault = true;
};

inline int resolveThreadCount(const RuntimeOptions & options) {
	if (options.threads > 0) {
		return options.threads;
	}
	const unsigned int detected = std::thread::hardware_concurrency();
	return detected > 0 ? static_cast<int>(detected) : 4;
}

inline int embeddingDimension(
	clip_ctx * ctx,
	ofxGgmlClipEmbeddingModality modality) {
	if (!ctx) {
		return 0;
	}
	if (modality == ofxGgmlClipEmbeddingModality::Text) {
		if (auto * text = clip_get_text_hparams(ctx)) {
			if (text->projection_dim > 0) {
				return text->projection_dim;
			}
		}
	}
	if (auto * vision = clip_get_vision_hparams(ctx)) {
		if (vision->projection_dim > 0) {
			return vision->projection_dim;
		}
	}
	if (auto * text = clip_get_text_hparams(ctx)) {
		return text->projection_dim;
	}
	return 0;
}

inline ModelHandle loadModel(
	const std::string & modelPath,
	int verbosity = 1,
	std::string * error = nullptr) {
	if (modelPath.empty()) {
		if (error) {
			*error = "CLIP model path is empty";
		}
		return {};
	}
	clip_ctx * raw = clip_model_load(modelPath.c_str(), verbosity);
	if (!raw) {
		if (error) {
			*error = "failed to load clip.cpp model: " + modelPath;
		}
		return {};
	}
	return ModelHandle(raw, [](clip_ctx * ctx) {
		if (ctx) {
			clip_free(ctx);
		}
	});
}

inline ofxGgmlClipEmbeddingResult embedWithModel(
	const ModelHandle & model,
	const ofxGgmlClipEmbeddingRequest & request,
	const RuntimeOptions & options = {},
	const std::string & modelPath = {}) {
	ofxGgmlClipEmbeddingResult result;
	result.backendName = "clip.cpp";
	result.modality = request.modality;
	result.inputId = request.inputId;
	result.label = request.label;
	result.text = request.text;
	result.imagePath = request.imagePath;
	if (!model) {
		result.error = "clip.cpp model is not loaded";
		return result;
	}

	const auto started = std::chrono::steady_clock::now();
	const bool normalize = request.normalize;
	const int threads = resolveThreadCount(options);
	const int dimension = embeddingDimension(model.get(), request.modality);
	if (dimension <= 0) {
		result.error = "clip.cpp model does not expose a valid embedding dimension";
		return result;
	}

	result.embedding.assign(static_cast<size_t>(dimension), 0.0f);
	bool ok = false;
	if (request.modality == ofxGgmlClipEmbeddingModality::Text) {
		if (request.text.empty()) {
			result.error = "text input is empty";
			return result;
		}
		clip_tokens tokens{};
		ok = clip_tokenize(model.get(), request.text.c_str(), &tokens) &&
			clip_text_encode(
				model.get(),
				threads,
				&tokens,
				result.embedding.data(),
				normalize);
		if (tokens.data != nullptr) {
			std::free(tokens.data);
		}
		if (!ok) {
			result.error = "clip.cpp failed to encode text";
			result.embedding.clear();
			return result;
		}
	} else {
		if (request.imagePath.empty()) {
			result.error = "imagePath is empty";
			return result;
		}
		clip_image_u8 * source = clip_image_u8_make();
		clip_image_f32 * processed = clip_image_f32_make();
		if (!source || !processed) {
			if (source) {
				clip_image_u8_free(source);
			}
			if (processed) {
				clip_image_f32_free(processed);
			}
			result.error = "clip.cpp failed to allocate image buffers";
			return result;
		}
		ok = clip_image_load_from_file(request.imagePath.c_str(), source) &&
			clip_image_preprocess(model.get(), source, processed) &&
			clip_image_encode(
				model.get(),
				threads,
				processed,
				result.embedding.data(),
				normalize);
		clip_image_u8_free(source);
		clip_image_f32_free(processed);
		if (!ok) {
			result.error = "clip.cpp failed to encode image";
			result.embedding.clear();
			return result;
		}
	}

	result.success = true;
	result.elapsedMs = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - started).count();
	result.metadata.push_back({"backend", "clip.cpp"});
	result.metadata.push_back({"threads", std::to_string(threads)});
	if (!modelPath.empty()) {
		result.metadata.push_back({"modelPath", modelPath});
	}
	return result;
}

inline std::shared_ptr<ofxGgmlClipBackend> createBackend(
	ModelHandle model,
	const RuntimeOptions & options = {},
	const std::string & modelPath = {},
	const std::string & displayName = "clip.cpp") {
	return ofxGgmlClipInference::createClipBridgeBackend(
		[model, options, modelPath](const ofxGgmlClipEmbeddingRequest & request) {
			return embedWithModel(model, request, options, modelPath);
		},
		displayName);
}

inline std::shared_ptr<ofxGgmlClipBackend> createBackend(
	const std::string & modelPath,
	const RuntimeOptions & options = {},
	const std::string & displayName = "clip.cpp") {
	std::string error;
	const auto model = loadModel(modelPath, options.verbosity, &error);
	if (!model) {
		return ofxGgmlClipInference::createClipBridgeBackend(
			[error, modelPath](const ofxGgmlClipEmbeddingRequest & request) {
				ofxGgmlClipEmbeddingResult result;
				result.backendName = "clip.cpp";
				result.modality = request.modality;
				result.inputId = request.inputId;
				result.label = request.label;
				result.text = request.text;
				result.imagePath = request.imagePath;
				result.error = error.empty()
					? "failed to load clip.cpp model: " + modelPath
					: error;
				return result;
			},
			displayName);
	}
	return createBackend(model, options, modelPath, displayName);
}

inline void attachBackend(
	ofxGgmlClipInference & inference,
	ModelHandle model,
	const RuntimeOptions & options = {},
	const std::string & modelPath = {},
	const std::string & displayName = "clip.cpp") {
	inference.setBackend(createBackend(model, options, modelPath, displayName));
}

inline void attachBackend(
	ofxGgmlClipInference & inference,
	const std::string & modelPath,
	const RuntimeOptions & options = {},
	const std::string & displayName = "clip.cpp") {
	inference.setBackend(createBackend(modelPath, options, displayName));
}

#endif

} // namespace ofxGgmlClipCppAdapters
