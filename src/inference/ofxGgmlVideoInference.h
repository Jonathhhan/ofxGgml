#pragma once

#include "ofxGgmlVisionInference.h"

#include <memory>
#include <string>
#include <vector>

enum class ofxGgmlVideoTask {
	Summarize = 0,
	Ocr,
	Ask,
	Action,
	Emotion
};

struct ofxGgmlSampledVideoFrame {
	std::string imagePath;
	std::string label;
	double timestampSeconds = 0.0;
};

struct ofxGgmlVideoRequest {
	ofxGgmlVideoTask task = ofxGgmlVideoTask::Summarize;
	std::string videoPath;
	std::string prompt;
	std::string systemPrompt;
	std::string responseLanguage;
	std::string sidecarUrl;
	std::string sidecarModel;
	int maxTokens = 512;
	float temperature = 0.2f;
	int maxFrames = 6;
	double startSeconds = 0.0;
	double endSeconds = -1.0;
	double minFrameSpacingSeconds = 2.0;
	bool includeTimestamps = true;
};

struct ofxGgmlVideoStructuredAnalysis {
	std::string analysisType;
	std::string primaryLabel;
	float confidence = -1.0f;
	std::vector<std::string> secondaryLabels;
	std::vector<std::string> timeline;
	std::vector<std::string> evidence;
	std::string valence;
	std::string arousal;
	std::string notes;
};

struct ofxGgmlVideoResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string text;
	std::string error;
	std::string backendName;
	std::string usedServerUrl;
	std::string requestJson;
	std::string responseJson;
	std::vector<ofxGgmlSampledVideoFrame> sampledFrames;
	ofxGgmlVideoStructuredAnalysis structured;
	ofxGgmlVisionResult visionResult;
};

struct ofxGgmlVideoBackendSampleResult {
	bool success = false;
	std::string backendName;
	std::string error;
	std::vector<ofxGgmlSampledVideoFrame> sampledFrames;
};

class ofxGgmlVideoBackend {
public:
	virtual ~ofxGgmlVideoBackend() = default;
	virtual std::string backendName() const = 0;
	virtual ofxGgmlVideoBackendSampleResult sampleFrames(
		const ofxGgmlVideoRequest & request) const = 0;
};

class ofxGgmlSampledFramesVideoBackend : public ofxGgmlVideoBackend {
public:
	std::string backendName() const override;
	ofxGgmlVideoBackendSampleResult sampleFrames(
		const ofxGgmlVideoRequest & request) const override;
};

class ofxGgmlVideoInference {
public:
	ofxGgmlVideoInference();

	static const char * taskLabel(ofxGgmlVideoTask task);
	static std::string defaultPromptForTask(ofxGgmlVideoTask task);
	static std::string defaultSystemPromptForTask(ofxGgmlVideoTask task);
	static std::string formatTimestamp(double seconds);
	static std::vector<double> buildSampleTimeline(
		double durationSeconds,
		int maxFrames,
		double startSeconds = 0.0,
		double endSeconds = -1.0,
		double minFrameSpacingSeconds = 0.0);
	static std::string buildFrameAwarePrompt(
		const ofxGgmlVideoRequest & request,
		const std::vector<ofxGgmlSampledVideoFrame> & frames);
	static std::string normalizeSidecarUrl(const std::string & sidecarUrl);
	static std::string buildTemporalSidecarJson(
		const ofxGgmlVideoRequest & request,
		const std::vector<ofxGgmlSampledVideoFrame> & frames);
	static std::shared_ptr<ofxGgmlVideoBackend> createSampledFramesBackend();

	void setBackend(std::shared_ptr<ofxGgmlVideoBackend> backend);
	std::shared_ptr<ofxGgmlVideoBackend> getBackend() const;

	std::vector<ofxGgmlSampledVideoFrame> sampleFrames(
		const ofxGgmlVideoRequest & request,
		std::string & error) const;

	ofxGgmlVideoResult runServerRequest(
		const ofxGgmlVisionModelProfile & profile,
		const ofxGgmlVideoRequest & request) const;
	ofxGgmlVideoResult runServerRequest(
		const ofxGgmlVisionModelProfile & profile,
		const ofxGgmlVideoRequest & request,
		const std::vector<ofxGgmlSampledVideoFrame> & sampledFrames) const;
	ofxGgmlVideoResult runTemporalSidecarRequest(
		const ofxGgmlVideoRequest & request) const;
	ofxGgmlVideoResult runTemporalSidecarRequest(
		const ofxGgmlVideoRequest & request,
		const std::vector<ofxGgmlSampledVideoFrame> & sampledFrames) const;

private:
	std::shared_ptr<ofxGgmlVideoBackend> m_backend;
};
