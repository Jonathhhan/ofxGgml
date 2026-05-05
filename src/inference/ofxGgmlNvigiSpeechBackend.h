#pragma once

#include "ofxGgmlSpeechInference.h"

#include <chrono>
#include <functional>
#include <string>
#include <utility>

#ifndef OFXGGML_ENABLE_NVIGI
#define OFXGGML_ENABLE_NVIGI 0
#endif

class ofxGgmlNvigiAsrSpeechBackend : public ofxGgmlSpeechBackend {
public:
	using TranscribeFunction = std::function<ofxGgmlSpeechResult(
		const ofxGgmlSpeechRequest &)>;

	struct Options {
		std::string pluginId;
		std::string modelId;
		std::string device;
		bool preferStreaming = false;
	};

	explicit ofxGgmlNvigiAsrSpeechBackend(
		TranscribeFunction transcribeFunction = {},
		std::string displayName = "NVIGI ASR")
		: ofxGgmlNvigiAsrSpeechBackend(
			std::move(transcribeFunction),
			Options{},
			std::move(displayName)) {
	}

	ofxGgmlNvigiAsrSpeechBackend(
		TranscribeFunction transcribeFunction,
		Options options,
		std::string displayName = "NVIGI ASR")
		: m_transcribeFunction(std::move(transcribeFunction))
		, m_options(std::move(options))
		, m_displayName(std::move(displayName)) {
	}

	void setTranscribeFunction(TranscribeFunction transcribeFunction) {
		m_transcribeFunction = std::move(transcribeFunction);
	}

	bool isConfigured() const {
		return static_cast<bool>(m_transcribeFunction);
	}

	static bool isSdkEnabled() {
		return OFXGGML_ENABLE_NVIGI != 0;
	}

	const Options & getOptions() const {
		return m_options;
	}

	std::string backendName() const override {
		return m_displayName.empty() ? "NVIGI ASR" : m_displayName;
	}

	ofxGgmlSpeechResult transcribe(
		const ofxGgmlSpeechRequest & request) const override {
		ofxGgmlSpeechResult result;
		result.backendName = backendName();
		if (!isSdkEnabled()) {
			result.error =
				"NVIGI ASR is disabled. Define OFXGGML_ENABLE_NVIGI=1 and "
				"attach an NVIGI SDK ASR callback before calling transcribe().";
			return result;
		}
		if (!m_transcribeFunction) {
			result.error =
				"NVIGI ASR backend is not configured yet. Attach an NVIGI SDK "
				"ASR callback before calling transcribe().";
			return result;
		}

		const auto started = std::chrono::steady_clock::now();
		result = m_transcribeFunction(request);
		if (result.backendName.empty()) {
			result.backendName = backendName();
		}
		if (result.elapsedMs <= 0.0f) {
			result.elapsedMs = std::chrono::duration<float, std::milli>(
				std::chrono::steady_clock::now() - started).count();
		}
		return result;
	}

private:
	TranscribeFunction m_transcribeFunction;
	Options m_options;
	std::string m_displayName;
};
