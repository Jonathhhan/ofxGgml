#pragma once

#include "inference/ofxGgmlVisionInference.h"
#include "ofMain.h"

#include <cstdint>
#include <memory>
#include <string>

struct ofxGgmlHoloscanSettings {
	bool enabled = false;
	bool useEventScheduler = true;
	int workerThreads = 2;
};

struct ofxGgmlHoloscanFramePacket {
	uint64_t frameIndex = 0;
	double timestampSeconds = 0.0;
	std::shared_ptr<ofPixels> pixels;
	std::string sourceLabel;

	bool isValid() const {
		return pixels != nullptr && pixels->isAllocated();
	}
};

struct ofxGgmlHoloscanVisionRequestTemplate {
	ofxGgmlVisionTask task = ofxGgmlVisionTask::Describe;
	std::string prompt;
	std::string systemPrompt;
	std::string responseLanguage;
	int maxTokens = 384;
	float temperature = 0.2f;
};

struct ofxGgmlHoloscanVisionResultPacket {
	uint64_t frameIndex = 0;
	double timestampSeconds = 0.0;
	std::string sourceLabel;
	ofxGgmlVisionResult result;
};

struct ofxGgmlHoloscanPreviewFrame {
	bool valid = false;
	uint64_t frameIndex = 0;
	double timestampSeconds = 0.0;
	std::string sourceLabel;
	std::string text;
	ofPixels pixels;
};
