#pragma once

#include "ofMain.h"

#include <string>
#include <vector>

enum class ofxGgmlVisionTask {
	Describe = 0,
	Ocr,
	Ask
};

struct ofxGgmlVisionModelProfile {
	std::string name;
	std::string architecture;
	std::string modelRepoHint;
	std::string modelFileHint;
	std::string modelDownloadUrl;
	std::string modelPath;
	std::string mmprojPath;
	std::string serverUrl = "http://127.0.0.1:8080";
	bool supportsOcr = true;
	bool supportsMultipleImages = true;
	bool mayRequireMmproj = false;
};

struct ofxGgmlVisionImageInput {
	std::string path;
	std::string label;
	std::string mimeType;
};

struct ofxGgmlVisionRequest {
	ofxGgmlVisionTask task = ofxGgmlVisionTask::Describe;
	std::string prompt;
	std::string systemPrompt;
	std::string responseLanguage;
	std::vector<ofxGgmlVisionImageInput> images;
	int maxTokens = 384;
	float temperature = 0.2f;
};

struct ofxGgmlVisionPreparedPrompt {
	std::string systemPrompt;
	std::string userPrompt;
};

struct ofxGgmlVisionResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string text;
	std::string error;
	std::string usedServerUrl;
	std::string requestJson;
	std::string responseJson;
};

class ofxGgmlVisionInference {
public:
	static std::vector<ofxGgmlVisionModelProfile> defaultProfiles();
	static const char * taskLabel(ofxGgmlVisionTask task);
	static std::string defaultPromptForTask(ofxGgmlVisionTask task);
	static std::string defaultSystemPromptForTask(ofxGgmlVisionTask task);
	static ofxGgmlVisionPreparedPrompt preparePrompt(
		const ofxGgmlVisionRequest & request);
	static std::string detectMimeType(const std::string & path);
	static std::string encodeImageFileBase64(const std::string & path);
	static std::string normalizeServerUrl(const std::string & serverUrl);
	static std::string buildChatCompletionsJson(
		const ofxGgmlVisionModelProfile & profile,
		const ofxGgmlVisionRequest & request);

	ofxGgmlVisionResult runServerRequest(
		const ofxGgmlVisionModelProfile & profile,
		const ofxGgmlVisionRequest & request) const;
};
