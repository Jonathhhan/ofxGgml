#pragma once

#include "bridges/ofxGgmlHoloscanTypes.h"

#include <memory>
#include <string>
#include <vector>

class ofxGgmlVisionInference;

class ofxGgmlHoloscanBridge {
public:
	ofxGgmlHoloscanBridge();
	~ofxGgmlHoloscanBridge();

	bool setup(
		ofxGgmlVisionInference* visionInference,
		const ofxGgmlHoloscanSettings& settings = {});
	void shutdown();

	bool startVisionPipeline();
	void stop();
	void update();

	void submitFrame(
		const ofPixels& pixels,
		double timestampSeconds,
		const std::string& sourceLabel = "frame");
	void submitProfile(const ofxGgmlVisionModelProfile& profile);
	void submitRequestTemplate(
		const ofxGgmlHoloscanVisionRequestTemplate& requestTemplate);

	bool hasPreviewFrame() const;
	const ofTexture& getPreviewTexture() const;
	ofxGgmlHoloscanPreviewFrame getPreviewFrameCopy() const;
	std::vector<ofxGgmlHoloscanVisionResultPacket> consumeFinishedResults();

	bool isConfigured() const;
	bool isRunning() const;
	bool isHoloscanAvailable() const;
	std::string getLastError() const;
	const ofxGgmlHoloscanSettings& getSettings() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
