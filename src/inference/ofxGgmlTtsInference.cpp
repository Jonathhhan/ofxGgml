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
	: m_backend(createPiperTtsBridgeBackend()) {
}

std::vector<ofxGgmlTtsModelProfile> ofxGgmlTtsInference::defaultProfiles() {
	return {
		{
			"piper",
			"Piper Voice (.onnx)",
			"Piper / ONNX voice (+ matching .onnx.json)",
			"OHF-Voice Piper voices (for example en_US-lessac-medium)",
			"piper/en_US-lessac-medium.onnx",
			"",
			"",
			"",
			"",
			false,
			false,
			false
		},
		{
			"chatllm",
			"ChatLLM OuteTTS (converted model)",
			"chatllm.cpp / converted OuteTTS (+ DAC codec)",
			"OuteAI/Llama-OuteTTS-1.0-1B or OuteAI/OuteTTS-1.0-0.6B; convert with chatllm.cpp convert.py",
			"outetts.bin",
			"",
			"",
			"",
			"",
			true,
			false,
			false
		},
		{
			"chatllm",
			"ChatLLM OuteTTS (speaker.json clone voice)",
			"chatllm.cpp / converted OuteTTS (+ DAC codec)",
			"OuteAI/Llama-OuteTTS-1.0-1B or OuteAI/OuteTTS-1.0-0.6B; convert with chatllm.cpp convert.py",
			"outetts.bin",
			"",
			"",
			"speaker.json",
			"",
			true,
			false,
			true
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

std::shared_ptr<ofxGgmlTtsBackend>
ofxGgmlTtsInference::createPiperTtsBridgeBackend(
	ofxGgmlTtsBridgeBackend::SynthesizeFunction synthesizeFunction,
	const std::string & displayName) {
	return createTtsBridgeBackend(std::move(synthesizeFunction), displayName);
}

void ofxGgmlTtsInference::setBackend(std::shared_ptr<ofxGgmlTtsBackend> backend) {
	m_backend = backend ? std::move(backend) : createPiperTtsBridgeBackend();
}

std::shared_ptr<ofxGgmlTtsBackend> ofxGgmlTtsInference::getBackend() const {
	return m_backend;
}

ofxGgmlTtsResult ofxGgmlTtsInference::synthesize(
	const ofxGgmlTtsRequest & request) const {
	const auto backend = m_backend ? m_backend : createPiperTtsBridgeBackend();
	return backend->synthesize(request);
}
