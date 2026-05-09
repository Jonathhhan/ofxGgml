#include "ofxGgmlSegmentationInference.h"

#include <chrono>
#include <utility>

ofxGgmlSegmentationBridgeBackend::ofxGgmlSegmentationBridgeBackend(
	SegmentFunction segmentFunction,
	std::string displayName)
	: m_segmentFunction(std::move(segmentFunction))
	, m_displayName(std::move(displayName)) {
}

void ofxGgmlSegmentationBridgeBackend::setSegmentFunction(
	SegmentFunction segmentFunction) {
	m_segmentFunction = std::move(segmentFunction);
}

bool ofxGgmlSegmentationBridgeBackend::isConfigured() const {
	return static_cast<bool>(m_segmentFunction);
}

std::string ofxGgmlSegmentationBridgeBackend::backendName() const {
	return m_displayName.empty() ? "SegmentationBridge" : m_displayName;
}

ofxGgmlSegmentationResult ofxGgmlSegmentationBridgeBackend::segment(
	const ofxGgmlSegmentationRequest & request) const {
	ofxGgmlSegmentationResult result;
	result.backendName = backendName();
	result.imagePath = request.imagePath;
	if (!m_segmentFunction) {
		result.error =
			"segmentation bridge backend is not configured. Attach a "
			"segmentation adapter callback before calling segment().";
		return result;
	}

	const auto started = std::chrono::steady_clock::now();
	result = m_segmentFunction(request);
	if (result.backendName.empty()) {
		result.backendName = backendName();
	}
	if (result.imagePath.empty()) {
		result.imagePath = request.imagePath;
	}
	if (result.elapsedMs <= 0.0f) {
		result.elapsedMs = std::chrono::duration<float, std::milli>(
			std::chrono::steady_clock::now() - started).count();
	}
	return result;
}

ofxGgmlSegmentationInference::ofxGgmlSegmentationInference()
	: m_backend(createSegmentationBridgeBackend()) {
}

std::shared_ptr<ofxGgmlSegmentationBackend>
ofxGgmlSegmentationInference::createSegmentationBridgeBackend(
	ofxGgmlSegmentationBridgeBackend::SegmentFunction segmentFunction,
	const std::string & displayName) {
	return std::make_shared<ofxGgmlSegmentationBridgeBackend>(
		std::move(segmentFunction),
		displayName);
}

void ofxGgmlSegmentationInference::setBackend(
	std::shared_ptr<ofxGgmlSegmentationBackend> backend) {
	m_backend = backend
		? std::move(backend)
		: createSegmentationBridgeBackend();
}

std::shared_ptr<ofxGgmlSegmentationBackend>
ofxGgmlSegmentationInference::getBackend() const {
	return m_backend;
}

ofxGgmlSegmentationResult ofxGgmlSegmentationInference::segment(
	const ofxGgmlSegmentationRequest & request) const {
	const auto backend = m_backend
		? m_backend
		: createSegmentationBridgeBackend();
	return backend->segment(request);
}

ofxGgmlSegmentationResult ofxGgmlSegmentationInference::segmentPoint(
	const std::string & imagePath,
	float x,
	float y,
	const std::string & modelPath,
	int threads) const {
	ofxGgmlSegmentationRequest request;
	request.promptType = ofxGgmlSegmentationPromptType::Point;
	request.imagePath = imagePath;
	request.modelPath = modelPath;
	request.threads = threads;
	request.points.push_back({ x, y, true });
	return segment(request);
}
