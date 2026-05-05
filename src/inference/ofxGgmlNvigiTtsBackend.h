#pragma once

#include "ofxGgmlTtsInference.h"

#include <chrono>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#ifndef OFXGGML_ENABLE_NVIGI
#define OFXGGML_ENABLE_NVIGI 0
#endif

class ofxGgmlNvigiTtsBackend : public ofxGgmlTtsBackend {
public:
	using SynthesizeFunction = std::function<ofxGgmlTtsResult(
		const ofxGgmlTtsRequest &)>;

	struct Options {
		std::string pluginId;
		std::string voiceId;
		std::string device;
		bool preferStreaming = false;
	};

	explicit ofxGgmlNvigiTtsBackend(
		SynthesizeFunction synthesizeFunction = {},
		std::string displayName = "NVIGI TTS")
		: ofxGgmlNvigiTtsBackend(
			std::move(synthesizeFunction),
			Options{},
			std::move(displayName)) {
	}

	ofxGgmlNvigiTtsBackend(
		SynthesizeFunction synthesizeFunction,
		Options options,
		std::string displayName = "NVIGI TTS")
		: m_synthesizeFunction(std::move(synthesizeFunction))
		, m_options(std::move(options))
		, m_displayName(std::move(displayName)) {
	}

	void setSynthesizeFunction(SynthesizeFunction synthesizeFunction) {
		m_synthesizeFunction = std::move(synthesizeFunction);
	}

	bool isConfigured() const {
		return static_cast<bool>(m_synthesizeFunction);
	}

	static bool isSdkEnabled() {
		return OFXGGML_ENABLE_NVIGI != 0;
	}

	const Options & getOptions() const {
		return m_options;
	}

	std::string backendName() const override {
		return m_displayName.empty() ? "NVIGI TTS" : m_displayName;
	}

	ofxGgmlTtsResult synthesize(
		const ofxGgmlTtsRequest & request) const override {
		ofxGgmlTtsResult result;
		result.backendName = backendName();
		if (!isSdkEnabled()) {
			result.error =
				"NVIGI TTS is disabled. Define OFXGGML_ENABLE_NVIGI=1 and "
				"attach an NVIGI SDK synthesis callback before calling synthesize().";
			return result;
		}
		if (!m_synthesizeFunction) {
			result.error =
				"NVIGI TTS backend is not configured yet. Attach an NVIGI SDK "
				"synthesis callback before calling synthesize().";
			return result;
		}

		const auto started = std::chrono::steady_clock::now();
		result = m_synthesizeFunction(request);
		if (result.backendName.empty()) {
			result.backendName = backendName();
		}
		if (result.elapsedMs <= 0.0f) {
			result.elapsedMs = std::chrono::duration<float, std::milli>(
				std::chrono::steady_clock::now() - started).count();
		}
		if (!m_options.pluginId.empty()) {
			result.metadata.push_back({"nvigiPluginId", m_options.pluginId});
		}
		if (!m_options.voiceId.empty()) {
			result.metadata.push_back({"nvigiVoiceId", m_options.voiceId});
		}
		if (!m_options.device.empty()) {
			result.metadata.push_back({"nvigiDevice", m_options.device});
		}
		return result;
	}

private:
	SynthesizeFunction m_synthesizeFunction;
	Options m_options;
	std::string m_displayName;
};
