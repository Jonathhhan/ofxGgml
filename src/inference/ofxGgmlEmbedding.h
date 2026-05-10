#pragma once

#include "ofxGgmlLlamaServerTextBackend.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct ofxGgmlEmbeddingSettings {
	std::string serverUrl;
	std::string serverModel;
	int timeoutSeconds = 180;
};

struct ofxGgmlEmbeddingRequest {
	std::string input;
	std::vector<std::string> inputs;
	ofxGgmlEmbeddingSettings settings;
};

struct ofxGgmlEmbeddingResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::vector<float> embedding;
	std::vector<std::vector<float>> embeddings;
	std::string error;
	std::string backendName;
	std::string rawOutput;
	std::vector<std::pair<std::string, std::string>> metadata;
};

class ofxGgmlEmbeddingBackend {
public:
	virtual ~ofxGgmlEmbeddingBackend() = default;
	virtual std::string backendName() const = 0;
	virtual ofxGgmlEmbeddingResult embed(
		const ofxGgmlEmbeddingRequest & request) const = 0;
};

class ofxGgmlEmbeddingBridgeBackend : public ofxGgmlEmbeddingBackend {
public:
	using EmbedFunction = std::function<ofxGgmlEmbeddingResult(
		const ofxGgmlEmbeddingRequest &)>;

	explicit ofxGgmlEmbeddingBridgeBackend(
		EmbedFunction embedFunction = {},
		std::string displayName = "EmbeddingBridge");

	void setEmbedFunction(EmbedFunction embedFunction);
	bool isConfigured() const;

	std::string backendName() const override;
	ofxGgmlEmbeddingResult embed(
		const ofxGgmlEmbeddingRequest & request) const override;

private:
	EmbedFunction m_embedFunction;
	std::string m_displayName;
};

class ofxGgmlLlamaServerEmbeddingBackend : public ofxGgmlEmbeddingBackend {
public:
	explicit ofxGgmlLlamaServerEmbeddingBackend(
		std::string serverUrl = "http://127.0.0.1:8081",
		ofxGgmlTextServerRunner runner = {},
		std::string displayName = "llama-server-embedding");

	void setServerUrl(std::string serverUrl);
	const std::string & getServerUrl() const;

	void setRequestRunner(ofxGgmlTextServerRunner runner);
	bool hasRequestRunner() const;

	std::string backendName() const override;
	ofxGgmlEmbeddingResult embed(
		const ofxGgmlEmbeddingRequest & request) const override;

	static std::string normalizeServerUrl(const std::string & serverUrl);
	static std::string buildRequestBody(
		const ofxGgmlEmbeddingRequest & request,
		const std::string & serverModel = {});
	static std::vector<std::vector<float>> extractEmbeddingsFromResponse(
		const std::string & responseBody);

private:
	std::string m_serverUrl;
	ofxGgmlTextServerRunner m_runner;
	std::string m_displayName;
};

class ofxGgmlEmbeddingGenerator {
public:
	ofxGgmlEmbeddingGenerator();

	static std::shared_ptr<ofxGgmlEmbeddingBackend> createEmbeddingBridgeBackend(
		ofxGgmlEmbeddingBridgeBackend::EmbedFunction embedFunction = {},
		const std::string & displayName = "EmbeddingBridge");

	void setBackend(std::shared_ptr<ofxGgmlEmbeddingBackend> backend);
	std::shared_ptr<ofxGgmlEmbeddingBackend> getBackend() const;

	ofxGgmlEmbeddingResult embed(
		const ofxGgmlEmbeddingRequest & request) const;
	ofxGgmlEmbeddingResult embed(
		const std::string & input,
		const ofxGgmlEmbeddingSettings & settings = {}) const;

private:
	std::shared_ptr<ofxGgmlEmbeddingBackend> m_backend;
};
