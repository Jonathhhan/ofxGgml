#include "ofxGgmlTtsInference.h"

#include <chrono>
#include <utility>

ofxGgmlTtsBridgeBackend::ofxGgmlTtsBridgeBackend(
	SynthesizeFunction synthesizeFunction,
	std::string displayName)
	: m_synthesizeFunction(std::move(synthesizeFunction))
	, m_displayName(std::move(displayName)) {
}

void ofxGgmlTtsBridgeBackend::setSynthesizeFunction(
	SynthesizeFunction synthesizeFunction) {
	m_synthesizeFunction = std::move(synthesizeFunction);
}

bool ofxGgmlTtsBridgeBackend::isConfigured() const {
	return static_cast<bool>(m_synthesizeFunction);
}

std::string ofxGgmlTtsBridgeBackend::backendName() const {
	return m_displayName.empty() ? "TtsBridge" : m_displayName;
}

ofxGgmlTtsResult ofxGgmlTtsBridgeBackend::synthesize(
	const ofxGgmlTtsRequest & request) const {
	ofxGgmlTtsResult result;
	result.backendName = backendName();
	if (!m_synthesizeFunction) {
		result.error =
			"tts bridge backend is not configured yet. "
			"Attach a chatllm.cpp or other synthesis adapter callback before calling synthesize().";
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
	return result;
}

ofxGgmlTtsInference::ofxGgmlTtsInference()
	: m_backend(createChatLlmTtsBridgeBackend()) {
}

std::vector<ofxGgmlTtsModelProfile> ofxGgmlTtsInference::defaultProfiles() {
	return {
		{
			"ChatLLM TTS (manual model)",
			"Manual model path / chatllm.cpp",
			"",
			"",
			"",
			"",
			"",
			"",
			false,
			false,
			false
		},
		{
			"Custom chatllm.cpp TTS model",
			"Custom / manual path",
			"",
			"",
			"",
			"",
			"",
			"",
			false,
			false,
			false
		}
	};
}

const char * ofxGgmlTtsInference::taskLabel(ofxGgmlTtsTask task) {
	switch (task) {
	case ofxGgmlTtsTask::Synthesize: return "Synthesize";
	case ofxGgmlTtsTask::CloneVoice: return "Clone Voice";
	case ofxGgmlTtsTask::ContinueSpeech: return "Continue Speech";
	}
	return "Synthesize";
}

std::shared_ptr<ofxGgmlTtsBackend>
ofxGgmlTtsInference::createTtsBridgeBackend(
	ofxGgmlTtsBridgeBackend::SynthesizeFunction synthesizeFunction,
	const std::string & displayName) {
	return std::make_shared<ofxGgmlTtsBridgeBackend>(
		std::move(synthesizeFunction),
		displayName);
}

std::shared_ptr<ofxGgmlTtsBackend>
ofxGgmlTtsInference::createChatLlmTtsBridgeBackend(
	ofxGgmlTtsBridgeBackend::SynthesizeFunction synthesizeFunction,
	const std::string & displayName) {
	return createTtsBridgeBackend(std::move(synthesizeFunction), displayName);
}

std::shared_ptr<ofxGgmlTtsBackend>
ofxGgmlTtsInference::createOuteTtsBridgeBackend(
	ofxGgmlTtsBridgeBackend::SynthesizeFunction synthesizeFunction,
	const std::string & displayName) {
	return createChatLlmTtsBridgeBackend(
		std::move(synthesizeFunction),
		displayName);
}

void ofxGgmlTtsInference::setBackend(std::shared_ptr<ofxGgmlTtsBackend> backend) {
	m_backend = backend ? std::move(backend) : createChatLlmTtsBridgeBackend();
}

std::shared_ptr<ofxGgmlTtsBackend> ofxGgmlTtsInference::getBackend() const {
	return m_backend;
}

ofxGgmlTtsResult ofxGgmlTtsInference::synthesize(
	const ofxGgmlTtsRequest & request) const {
	const auto backend = m_backend ? m_backend : createChatLlmTtsBridgeBackend();
	return backend->synthesize(request);
}
