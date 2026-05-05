#include "ofxGgmlTtsInference.h"

#include "ofxGgmlNvigiTtsBackend.h"
#include "support/ofxGgmlProcessSecurity.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <random>
#include <sstream>
#include <utility>

namespace {

static std::string makeTtsTempOutputPath(const std::string & prefix, const std::string & ext) {
	std::error_code ec;
	const std::filesystem::path base = std::filesystem::temp_directory_path(ec);
	const auto now =
		std::chrono::system_clock::now().time_since_epoch().count();
	std::mt19937_64 rng(static_cast<uint64_t>(now));
	std::uniform_int_distribution<uint64_t> dist;
	std::ostringstream name;
	name << prefix << "_" << std::hex << dist(rng) << ext;
	std::filesystem::path result =
		ec ? std::filesystem::path("/tmp") : base;
	result /= name.str();
	return result.string();
}

} // namespace

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

// ---------------------------------------------------------------------------
// ofxGgmlLlamaTtsCliBackend
// ---------------------------------------------------------------------------

ofxGgmlLlamaTtsCliBackend::ofxGgmlLlamaTtsCliBackend(std::string executable)
	: m_executable(std::move(executable)) {
}

void ofxGgmlLlamaTtsCliBackend::setExecutable(const std::string & executable) {
	m_executable = executable;
}

const std::string & ofxGgmlLlamaTtsCliBackend::getExecutable() const {
	return m_executable;
}

std::string ofxGgmlLlamaTtsCliBackend::backendName() const {
	return "LlamaTTS";
}

std::vector<std::string> ofxGgmlLlamaTtsCliBackend::buildCommandArguments(
	const ofxGgmlTtsRequest & request) const {
	const std::string exe = m_executable.empty() ? "llama-tts" : m_executable;
	std::vector<std::string> args;
	args.reserve(14);
	args.push_back(exe);
	if (!request.modelPath.empty()) {
		args.push_back("-m");
		args.push_back(request.modelPath);
	}
	if (!request.text.empty()) {
		args.push_back("-p");
		args.push_back(request.text);
	}
	if (!request.outputPath.empty()) {
		args.push_back("-o");
		args.push_back(request.outputPath);
	}
	if (request.seed >= 0) {
		args.push_back("--seed");
		args.push_back(std::to_string(request.seed));
	}
	return args;
}

ofxGgmlTtsResult ofxGgmlLlamaTtsCliBackend::synthesize(
	const ofxGgmlTtsRequest & request) const {
	ofxGgmlTtsResult result;
	result.backendName = backendName();

	if (request.text.empty()) {
		result.error = "no text was provided for synthesis";
		return result;
	}
	if (request.modelPath.empty()) {
		result.error = "no model path was provided";
		return result;
	}

	const std::string exe = m_executable.empty() ? "llama-tts" : m_executable;
	if (!ofxGgmlProcessSecurity::isValidExecutablePath(exe)) {
		result.error = "invalid llama-tts executable: " + exe;
		return result;
	}

	std::string outputPath = request.outputPath;
	if (outputPath.empty()) {
		outputPath = makeTtsTempOutputPath("ofxggml_tts", ".wav");
	}

	ofxGgmlTtsRequest effectiveRequest = request;
	effectiveRequest.outputPath = outputPath;

	const auto args = buildCommandArguments(effectiveRequest);
	const auto t0 = std::chrono::steady_clock::now();

	std::string rawOutput;
	int exitCode = -1;
	if (!ofxGgmlProcessSecurity::runCommandCapture(args, rawOutput, exitCode, true)) {
		result.error = "failed to start llama-tts process";
		result.rawOutput = rawOutput;
		return result;
	}

	result.elapsedMs = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - t0).count();
	result.rawOutput = rawOutput;

	if (exitCode != 0 && exitCode != -1) {
		result.error = "llama-tts exited with code " + std::to_string(exitCode);
		return result;
	}

	std::error_code ec;
	if (std::filesystem::exists(std::filesystem::path(outputPath), ec) && !ec) {
		ofxGgmlTtsAudioArtifact artifact;
		artifact.path = outputPath;
		artifact.sampleRate = request.sampleRate > 0 ? request.sampleRate : 24000;
		artifact.channels = 1;
		result.audioFiles.push_back(std::move(artifact));
		result.success = true;
	} else {
		result.error = "llama-tts did not produce output file: " + outputPath;
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
		},
		{
			"llama-tts",
			"llama-tts (OuteTTS / Kokoro GGUF)",
			"llama.cpp / llama-tts binary (GGUF model)",
			"ggml-org/outetts-0.3-0.5b-GGUF or ggml-org/kokoro-82m-GGUF",
			"outetts-0.3-0.5b-q8_0.gguf",
			"",
			"",
			"",
			"",
			false,
			false,
			false
		},
		{
			"nvigi",
			"NVIGI TTS (optional SDK bridge)",
			"NVIDIA NVIGI SDK TTS plugin",
			"NVIDIA NVIGI SDK 1.4+ Basic C++ TTS sample",
			"",
			"",
			"",
			"",
			"",
			false,
			true,
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

std::shared_ptr<ofxGgmlTtsBackend>
ofxGgmlTtsInference::createPiperTtsBridgeBackend(
	ofxGgmlTtsBridgeBackend::SynthesizeFunction synthesizeFunction,
	const std::string & displayName) {
	return createTtsBridgeBackend(std::move(synthesizeFunction), displayName);
}

std::shared_ptr<ofxGgmlTtsBackend>
ofxGgmlTtsInference::createNvigiTtsBackend(
	ofxGgmlTtsBridgeBackend::SynthesizeFunction synthesizeFunction,
	const std::string & displayName) {
	return std::make_shared<ofxGgmlNvigiTtsBackend>(
		std::move(synthesizeFunction),
		ofxGgmlNvigiTtsBackend::Options{},
		displayName);
}

std::shared_ptr<ofxGgmlTtsBackend>
ofxGgmlTtsInference::createLlamaTtsCliBackend(
	const std::string & executable) {
	return std::make_shared<ofxGgmlLlamaTtsCliBackend>(executable);
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
