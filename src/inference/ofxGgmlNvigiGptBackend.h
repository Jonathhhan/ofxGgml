#pragma once

#include "ofxGgmlInference.h"

#include <chrono>
#include <functional>
#include <string>
#include <utility>

#ifndef OFXGGML_ENABLE_NVIGI
#define OFXGGML_ENABLE_NVIGI 0
#endif

class ofxGgmlNvigiGptBackend {
public:
	using GenerateFunction = std::function<ofxGgmlInferenceResult(
		const std::string &,
		const std::string &,
		const ofxGgmlInferenceSettings &,
		std::function<bool(const std::string &)>)>;

	struct Options {
		std::string pluginId;
		std::string modelId;
		std::string device;
		bool preferStreaming = false;
	};

	explicit ofxGgmlNvigiGptBackend(
		GenerateFunction generateFunction = {},
		std::string displayName = "NVIGI GPT")
		: ofxGgmlNvigiGptBackend(
			std::move(generateFunction),
			Options{},
			std::move(displayName)) {
	}

	ofxGgmlNvigiGptBackend(
		GenerateFunction generateFunction,
		Options options,
		std::string displayName = "NVIGI GPT")
		: m_generateFunction(std::move(generateFunction))
		, m_options(std::move(options))
		, m_displayName(std::move(displayName)) {
	}

	void setGenerateFunction(GenerateFunction generateFunction) {
		m_generateFunction = std::move(generateFunction);
	}

	bool isConfigured() const {
		return static_cast<bool>(m_generateFunction);
	}

	static bool isSdkEnabled() {
		return OFXGGML_ENABLE_NVIGI != 0;
	}

	const Options & getOptions() const {
		return m_options;
	}

	std::string backendName() const {
		return m_displayName.empty() ? "NVIGI GPT" : m_displayName;
	}

	ofxGgmlInferenceResult generate(
		const std::string & modelPath,
		const std::string & prompt,
		const ofxGgmlInferenceSettings & settings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const {
		ofxGgmlInferenceResult result;
		if (!isSdkEnabled()) {
			result.error =
				"NVIGI GPT is disabled. Define OFXGGML_ENABLE_NVIGI=1 and "
				"attach an NVIGI SDK generation callback before calling generate().";
			return result;
		}
		if (!m_generateFunction) {
			result.error =
				"NVIGI GPT backend is not configured yet. Attach an NVIGI SDK "
				"generation callback before calling generate().";
			return result;
		}

		const auto started = std::chrono::steady_clock::now();
		result = m_generateFunction(modelPath, prompt, settings, std::move(onChunk));
		if (result.elapsedMs <= 0.0f) {
			result.elapsedMs = std::chrono::duration<float, std::milli>(
				std::chrono::steady_clock::now() - started).count();
		}
		return result;
	}

private:
	GenerateFunction m_generateFunction;
	Options m_options;
	std::string m_displayName;
};
