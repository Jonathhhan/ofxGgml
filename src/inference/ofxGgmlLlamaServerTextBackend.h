#pragma once

#include "ofxGgmlTextGeneration.h"

#include <functional>
#include <string>

struct ofxGgmlTextServerRequest {
	std::string url;
	std::string body;
	std::string contentType = "application/json";
	int timeoutSeconds = 180;
	bool stream = false;
	ofxGgmlTextChunkCallback onChunk;
	std::function<bool()> shouldCancel;
};

struct ofxGgmlTextServerResponse {
	bool started = false;
	bool cancelled = false;
	int status = 0;
	std::string body;
	std::string text;
	std::string error;

	explicit operator bool() const {
		return isOk();
	}

	bool isOk() const {
		return started && !cancelled && status >= 200 && status < 300;
	}

	bool isError() const {
		return !isOk();
	}
};

using ofxGgmlTextServerRunner = std::function<ofxGgmlTextServerResponse(
	const ofxGgmlTextServerRequest &)>;

class ofxGgmlLlamaServerTextBackend : public ofxGgmlTextBackend {
public:
	explicit ofxGgmlLlamaServerTextBackend(
		std::string serverUrl = "http://127.0.0.1:8080",
		ofxGgmlTextServerRunner runner = {},
		std::string displayName = "llama-server");

	void setServerUrl(std::string serverUrl);
	const std::string & getServerUrl() const;

	void setRequestRunner(ofxGgmlTextServerRunner runner);
	bool hasRequestRunner() const;

	std::string backendName() const override;
	ofxGgmlTextResult generate(
		const ofxGgmlTextRequest & request,
		ofxGgmlTextChunkCallback onChunk = nullptr) const override;

	static std::string normalizeServerUrl(const std::string & serverUrl);
	static std::string composePrompt(const ofxGgmlTextRequest & request);
	static std::string buildRequestBody(
		const ofxGgmlTextRequest & request,
		const std::string & prompt,
		const std::string & serverModel = {});
	static std::string extractTextFromResponse(const std::string & responseBody);
	static ofxGgmlTextServerResponse runRequest(
		const ofxGgmlTextServerRequest & request);

private:
	std::string m_serverUrl;
	ofxGgmlTextServerRunner m_runner;
	std::string m_displayName;
};
