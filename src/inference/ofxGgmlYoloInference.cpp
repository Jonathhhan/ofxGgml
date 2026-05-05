#include "ofxGgmlYoloInference.h"

#include "support/ofxGgmlProcessSecurity.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <utility>

namespace {

std::string trimCopy(const std::string & value) {
	size_t start = 0;
	size_t end = value.size();
	while (start < end &&
		std::isspace(static_cast<unsigned char>(value[start]))) {
		++start;
	}
	while (end > start &&
		std::isspace(static_cast<unsigned char>(value[end - 1]))) {
		--end;
	}
	return value.substr(start, end - start);
}

bool parsePercent(const std::string & value, float & confidence) {
	const std::string trimmed = trimCopy(value);
	if (trimmed.empty() || trimmed.back() != '%') {
		return false;
	}
	try {
		confidence = std::stof(trimmed.substr(0, trimmed.size() - 1)) / 100.0f;
		return true;
	} catch (...) {
		return false;
	}
}

} // namespace

ofxGgmlYoloBridgeBackend::ofxGgmlYoloBridgeBackend(
	DetectFunction detectFunction,
	std::string displayName)
	: m_detectFunction(std::move(detectFunction))
	, m_displayName(std::move(displayName)) {
}

void ofxGgmlYoloBridgeBackend::setDetectFunction(
	DetectFunction detectFunction) {
	m_detectFunction = std::move(detectFunction);
}

bool ofxGgmlYoloBridgeBackend::isConfigured() const {
	return static_cast<bool>(m_detectFunction);
}

std::string ofxGgmlYoloBridgeBackend::backendName() const {
	return m_displayName.empty() ? "YoloBridge" : m_displayName;
}

ofxGgmlYoloResult ofxGgmlYoloBridgeBackend::detect(
	const ofxGgmlYoloRequest & request) const {
	ofxGgmlYoloResult result;
	result.backendName = backendName();
	result.imagePath = request.imagePath;
	result.outputPath = request.outputPath;
	if (!m_detectFunction) {
		result.error =
			"YOLO bridge backend is not configured yet. Attach a detection "
			"adapter callback before calling detect().";
		return result;
	}

	const auto started = std::chrono::steady_clock::now();
	result = m_detectFunction(request);
	if (result.backendName.empty()) {
		result.backendName = backendName();
	}
	if (result.imagePath.empty()) {
		result.imagePath = request.imagePath;
	}
	if (result.outputPath.empty()) {
		result.outputPath = request.outputPath;
	}
	if (result.elapsedMs <= 0.0f) {
		result.elapsedMs = std::chrono::duration<float, std::milli>(
			std::chrono::steady_clock::now() - started).count();
	}
	return result;
}

ofxGgmlYoloCliBackend::ofxGgmlYoloCliBackend(std::string executable)
	: m_executable(std::move(executable)) {
}

void ofxGgmlYoloCliBackend::setExecutable(const std::string & executable) {
	m_executable = executable;
}

const std::string & ofxGgmlYoloCliBackend::getExecutable() const {
	return m_executable;
}

std::string ofxGgmlYoloCliBackend::backendName() const {
	return "ggml YOLO";
}

std::vector<std::string> ofxGgmlYoloCliBackend::buildCommandArguments(
	const ofxGgmlYoloRequest & request) const {
	const std::string exe = m_executable.empty() ? "yolov3-tiny" : m_executable;
	std::vector<std::string> args;
	args.reserve(14);
	args.push_back(exe);
	if (!request.modelPath.empty()) {
		args.push_back("-m");
		args.push_back(request.modelPath);
	}
	if (!request.imagePath.empty()) {
		args.push_back("-i");
		args.push_back(request.imagePath);
	}
	if (!request.outputPath.empty()) {
		args.push_back("-o");
		args.push_back(request.outputPath);
	}
	if (request.threads > 0) {
		args.push_back("-t");
		args.push_back(std::to_string(request.threads));
	}
	if (request.threshold >= 0.0f && request.threshold <= 1.0f) {
		args.push_back("-th");
		args.push_back(std::to_string(request.threshold));
	}
	if (!request.device.empty()) {
		args.push_back("-d");
		args.push_back(request.device);
	}
	return args;
}

ofxGgmlYoloResult ofxGgmlYoloCliBackend::detect(
	const ofxGgmlYoloRequest & request) const {
	ofxGgmlYoloResult result;
	result.backendName = backendName();
	result.imagePath = request.imagePath;
	result.outputPath = request.outputPath;

	if (request.imagePath.empty()) {
		result.error = "no image path was provided for YOLO detection";
		return result;
	}
	if (request.modelPath.empty()) {
		result.error = "no YOLO model path was provided";
		return result;
	}
	if (request.threshold < 0.0f || request.threshold > 1.0f) {
		result.error = "YOLO threshold must be between 0 and 1";
		return result;
	}

	const std::string exe = m_executable.empty() ? "yolov3-tiny" : m_executable;
	if (!ofxGgmlProcessSecurity::isValidExecutablePath(exe)) {
		result.error = "invalid YOLO executable: " + exe;
		return result;
	}

	const auto args = buildCommandArguments(request);
	const auto started = std::chrono::steady_clock::now();
	std::string rawOutput;
	int exitCode = -1;
	if (!ofxGgmlProcessSecurity::runCommandCapture(args, rawOutput, exitCode, true)) {
		result.error = "failed to start YOLO process";
		result.rawOutput = rawOutput;
		return result;
	}

	result.elapsedMs = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - started).count();
	result.rawOutput = rawOutput;
	result.detections = parseDetections(rawOutput);
	const std::string savedPath = parseSavedOutputPath(rawOutput);
	if (!savedPath.empty()) {
		result.outputPath = savedPath;
	}
	result.metadata.push_back({"backendFamily", "ggml examples/yolo"});
	result.metadata.push_back({"threshold", std::to_string(request.threshold)});

	if (exitCode != 0 && exitCode != -1) {
		result.error = "YOLO process exited with code " + std::to_string(exitCode);
		return result;
	}

	std::error_code ec;
	const std::string outputPath =
		result.outputPath.empty() ? request.outputPath : result.outputPath;
	if (!outputPath.empty() &&
		std::filesystem::exists(std::filesystem::path(outputPath), ec) &&
		!ec) {
		result.success = true;
	} else if (!result.detections.empty()) {
		result.success = true;
	} else {
		result.error = "YOLO process did not report detections or an output file";
	}
	return result;
}

std::vector<ofxGgmlYoloDetection> ofxGgmlYoloCliBackend::parseDetections(
	const std::string & output) {
	std::vector<ofxGgmlYoloDetection> detections;
	std::istringstream stream(output);
	std::string line;
	while (std::getline(stream, line)) {
		const std::string trimmed = trimCopy(line);
		const auto colon = trimmed.find(':');
		if (colon == std::string::npos) {
			continue;
		}
		const std::string label = trimCopy(trimmed.substr(0, colon));
		float confidence = 0.0f;
		if (label.empty() ||
			!parsePercent(trimmed.substr(colon + 1), confidence)) {
			continue;
		}
		ofxGgmlYoloDetection detection;
		detection.label = label;
		detection.confidence = confidence;
		detection.index = detections.size();
		detections.push_back(std::move(detection));
	}
	return detections;
}

std::string ofxGgmlYoloCliBackend::parseSavedOutputPath(
	const std::string & output) {
	const std::string marker = "Detected objects saved in '";
	const auto begin = output.find(marker);
	if (begin == std::string::npos) {
		return {};
	}
	const auto pathBegin = begin + marker.size();
	const auto pathEnd = output.find('\'', pathBegin);
	if (pathEnd == std::string::npos || pathEnd <= pathBegin) {
		return {};
	}
	return output.substr(pathBegin, pathEnd - pathBegin);
}

ofxGgmlYoloInference::ofxGgmlYoloInference()
	: m_backend(createYoloBridgeBackend()) {
}

std::vector<ofxGgmlYoloModelProfile> ofxGgmlYoloInference::defaultProfiles() {
	return {
		{
			"ggml-yolo",
			"ggml YOLOv3-tiny",
			"ggml examples/yolo YOLOv3-tiny GGUF",
			"rgerganov/yolo-gguf or converted pjreddie yolov3-tiny.weights",
			"yolov3-tiny.gguf",
			"yolov3-tiny"
		}
	};
}

std::shared_ptr<ofxGgmlYoloBackend>
ofxGgmlYoloInference::createYoloBridgeBackend(
	ofxGgmlYoloBridgeBackend::DetectFunction detectFunction,
	const std::string & displayName) {
	return std::make_shared<ofxGgmlYoloBridgeBackend>(
		std::move(detectFunction),
		displayName);
}

std::shared_ptr<ofxGgmlYoloBackend>
ofxGgmlYoloInference::createGgmlYoloCliBackend(
	const std::string & executable) {
	return std::make_shared<ofxGgmlYoloCliBackend>(executable);
}

void ofxGgmlYoloInference::setBackend(
	std::shared_ptr<ofxGgmlYoloBackend> backend) {
	m_backend = backend ? std::move(backend) : createYoloBridgeBackend();
}

std::shared_ptr<ofxGgmlYoloBackend> ofxGgmlYoloInference::getBackend() const {
	return m_backend;
}

ofxGgmlYoloResult ofxGgmlYoloInference::detect(
	const ofxGgmlYoloRequest & request) const {
	const auto backend = m_backend ? m_backend : createYoloBridgeBackend();
	return backend->detect(request);
}

ofxGgmlYoloResult ofxGgmlYoloInference::detectImage(
	const std::string & imagePath,
	const std::string & modelPath,
	float threshold,
	const std::string & outputPath) const {
	ofxGgmlYoloRequest request;
	request.imagePath = imagePath;
	request.modelPath = modelPath;
	request.threshold = threshold;
	request.outputPath = outputPath;
	return detect(request);
}

