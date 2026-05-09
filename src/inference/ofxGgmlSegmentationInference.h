#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

enum class ofxGgmlSegmentationPromptType {
	Point = 0
};

struct ofxGgmlSegmentationPoint {
	float x = 0.0f;
	float y = 0.0f;
	bool positive = true;
};

struct ofxGgmlSegmentationRequest {
	ofxGgmlSegmentationPromptType promptType =
		ofxGgmlSegmentationPromptType::Point;
	std::string imagePath;
	int imageWidth = 0;
	int imageHeight = 0;
	std::vector<unsigned char> imageRgb;
	std::string modelPath;
	int threads = -1;
	std::vector<ofxGgmlSegmentationPoint> points;
	bool returnMultipleMasks = true;
};

struct ofxGgmlSegmentationMask {
	std::string maskId;
	int width = 0;
	int height = 0;
	std::vector<unsigned char> pixels;
	float score = 0.0f;
	std::vector<std::pair<std::string, std::string>> metadata;
};

struct ofxGgmlSegmentationResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string error;
	std::string backendName;
	std::string rawOutput;
	std::string imagePath;
	std::vector<ofxGgmlSegmentationMask> masks;
	std::vector<std::pair<std::string, std::string>> metadata;
};

class ofxGgmlSegmentationBackend {
public:
	virtual ~ofxGgmlSegmentationBackend() = default;
	virtual std::string backendName() const = 0;
	virtual ofxGgmlSegmentationResult segment(
		const ofxGgmlSegmentationRequest & request) const = 0;
};

class ofxGgmlSegmentationBridgeBackend : public ofxGgmlSegmentationBackend {
public:
	using SegmentFunction = std::function<ofxGgmlSegmentationResult(
		const ofxGgmlSegmentationRequest &)>;

	explicit ofxGgmlSegmentationBridgeBackend(
		SegmentFunction segmentFunction = {},
		std::string displayName = "SegmentationBridge");

	void setSegmentFunction(SegmentFunction segmentFunction);
	bool isConfigured() const;

	std::string backendName() const override;
	ofxGgmlSegmentationResult segment(
		const ofxGgmlSegmentationRequest & request) const override;

private:
	SegmentFunction m_segmentFunction;
	std::string m_displayName;
};

class ofxGgmlSegmentationInference {
public:
	ofxGgmlSegmentationInference();

	static std::shared_ptr<ofxGgmlSegmentationBackend>
		createSegmentationBridgeBackend(
			ofxGgmlSegmentationBridgeBackend::SegmentFunction segmentFunction = {},
			const std::string & displayName = "SegmentationBridge");

	void setBackend(std::shared_ptr<ofxGgmlSegmentationBackend> backend);
	std::shared_ptr<ofxGgmlSegmentationBackend> getBackend() const;

	ofxGgmlSegmentationResult segment(
		const ofxGgmlSegmentationRequest & request) const;
	ofxGgmlSegmentationResult segmentPoint(
		const std::string & imagePath,
		float x,
		float y,
		const std::string & modelPath = {},
		int threads = -1) const;

private:
	std::shared_ptr<ofxGgmlSegmentationBackend> m_backend;
};
