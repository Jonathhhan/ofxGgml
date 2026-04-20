#pragma once

#include "inference/ofxGgmlMusicGenerator.h"
#include "ofJson.h"

#include <memory>
#include <string>
#include <vector>

struct ofxGgmlAceStepRequest {
	std::string caption;
	std::string lyrics;
	int bpm = 0;
	float durationSeconds = 30.0f;
	std::string keyscale;
	std::string timesignature = "4";
	std::string vocalLanguage;
	int seed = -1;
	int batchSize = 1;
	float lmTemperature = 0.85f;
	float lmCfgScale = 2.0f;
	float lmTopP = 0.9f;
	int lmTopK = 0;
	std::string lmNegativePrompt;
	bool useCotCaption = true;
	bool instrumentalOnly = false;
	std::string audioCodes;
	int inferenceSteps = 0;
	float guidanceScale = 0.0f;
	float shift = 0.0f;
	float audioCoverStrength = 0.5f;
	int repaintingStart = -1;
	int repaintingEnd = -1;
	std::string lego;
	bool wavOutput = false;
	std::string outputDir;
	std::string outputPrefix = "acestep";
};

struct ofxGgmlAceStepHealthResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string usedServerUrl;
	std::string status;
	std::string error;
};

struct ofxGgmlAceStepPropsResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string usedServerUrl;
	std::string rawJson;
	std::string lmStatus;
	std::string synthStatus;
	int maxBatch = 0;
	int mp3Bitrate = 0;
	std::string error;
};

struct ofxGgmlAceStepGenerateResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string usedServerUrl;
	std::string requestJson;
	std::string enrichedRequestsJson;
	std::string commandOutput;
	std::string error;
	std::vector<ofxGgmlGeneratedMusicTrack> tracks;
};

struct ofxGgmlAceStepUnderstandRequest {
	std::string audioPath;
	ofxGgmlAceStepRequest requestTemplate;
	bool includeRequestTemplate = false;
};

struct ofxGgmlAceStepUnderstandResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string usedServerUrl;
	std::string rawJson;
	std::string caption;
	std::string lyrics;
	int bpm = 0;
	float durationSeconds = 0.0f;
	std::string keyscale;
	std::string timesignature;
	std::string vocalLanguage;
	std::string audioCodes;
	std::string summary;
	std::string error;
};

class ofxGgmlAceStepBridge {
public:
	explicit ofxGgmlAceStepBridge(
		std::string serverUrl = "http://127.0.0.1:8085");

	void setServerUrl(const std::string & serverUrl);
	const std::string & getServerUrl() const;

	ofxGgmlAceStepHealthResult healthCheck(
		const std::string & serverUrl = "",
		long timeoutMs = 1500L) const;
	ofxGgmlAceStepPropsResult fetchProps(
		const std::string & serverUrl = "") const;
	ofxGgmlAceStepGenerateResult generate(
		const ofxGgmlAceStepRequest & request,
		const std::string & serverUrl = "") const;
	ofxGgmlAceStepUnderstandResult understandAudio(
		const ofxGgmlAceStepUnderstandRequest & request,
		const std::string & serverUrl = "") const;

	std::shared_ptr<ofxGgmlMusicGenerationBackend> createMusicGenerationBackend(
		const std::string & serverUrl = "") const;

	static std::string normalizeServerUrl(
		const std::string & serverUrl,
		const std::string & endpoint = "");
	static ofJson buildRequestJson(const ofxGgmlAceStepRequest & request);
	static std::string summarizeRequestJson(const ofJson & requestJson);
	static std::string summarizeUnderstandResult(
		const ofxGgmlAceStepUnderstandResult & result);

private:
	std::string m_serverUrl;
};
