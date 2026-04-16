#pragma once

#include "ofxGgmlInference.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

enum class ofxGgmlClipEmbeddingModality {
	Text = 0,
	Image
};

struct ofxGgmlClipEmbeddingRequest {
	ofxGgmlClipEmbeddingModality modality =
		ofxGgmlClipEmbeddingModality::Text;
	std::string inputId;
	std::string label;
	std::string text;
	std::string imagePath;
	bool normalize = true;
};

struct ofxGgmlClipEmbeddingResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string error;
	std::string backendName;
	std::string rawOutput;
	ofxGgmlClipEmbeddingModality modality =
		ofxGgmlClipEmbeddingModality::Text;
	std::string inputId;
	std::string label;
	std::string text;
	std::string imagePath;
	std::vector<float> embedding;
	std::vector<std::pair<std::string, std::string>> metadata;
};

struct ofxGgmlClipSimilarityHit {
	std::string inputId;
	std::string label;
	std::string imagePath;
	float score = 0.0f;
	size_t index = 0;
};

struct ofxGgmlClipImageRankingRequest {
	std::string prompt;
	std::string promptId;
	std::string promptLabel;
	std::vector<std::string> imagePaths;
	size_t topK = 0;
	bool normalizeEmbeddings = true;
};

struct ofxGgmlClipImageRankingResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string error;
	std::string backendName;
	std::string rawOutput;
	ofxGgmlClipEmbeddingResult queryEmbedding;
	std::vector<ofxGgmlClipEmbeddingResult> imageEmbeddings;
	std::vector<ofxGgmlClipSimilarityHit> hits;
};

class ofxGgmlClipBackend {
public:
	virtual ~ofxGgmlClipBackend() = default;
	virtual std::string backendName() const = 0;
	virtual ofxGgmlClipEmbeddingResult embed(
		const ofxGgmlClipEmbeddingRequest & request) const = 0;
};

class ofxGgmlClipBridgeBackend : public ofxGgmlClipBackend {
public:
	using EmbedFunction = std::function<ofxGgmlClipEmbeddingResult(
		const ofxGgmlClipEmbeddingRequest &)>;

	explicit ofxGgmlClipBridgeBackend(
		EmbedFunction embedFunction = {},
		std::string displayName = "ClipBridge");

	void setEmbedFunction(EmbedFunction embedFunction);
	bool isConfigured() const;

	std::string backendName() const override;
	ofxGgmlClipEmbeddingResult embed(
		const ofxGgmlClipEmbeddingRequest & request) const override;

private:
	EmbedFunction m_embedFunction;
	std::string m_displayName;
};

using ofxGgmlStableDiffusionClipBridgeBackend = ofxGgmlClipBridgeBackend;

class ofxGgmlClipInference {
public:
	ofxGgmlClipInference();

	static std::shared_ptr<ofxGgmlClipBackend>
		createClipBridgeBackend(
			ofxGgmlClipBridgeBackend::EmbedFunction embedFunction = {},
			const std::string & displayName = "ClipBridge");
	static std::shared_ptr<ofxGgmlClipBackend>
		createStableDiffusionClipBridgeBackend(
			ofxGgmlClipBridgeBackend::EmbedFunction embedFunction = {},
			const std::string & displayName = "ofxStableDiffusionClip");
	static float cosineSimilarity(
		const std::vector<float> & a,
		const std::vector<float> & b);

	void setBackend(std::shared_ptr<ofxGgmlClipBackend> backend);
	std::shared_ptr<ofxGgmlClipBackend> getBackend() const;

	ofxGgmlClipEmbeddingResult embed(
		const ofxGgmlClipEmbeddingRequest & request) const;
	ofxGgmlClipEmbeddingResult embedText(
		const std::string & text,
		bool normalize = true,
		const std::string & inputId = "prompt",
		const std::string & label = "Prompt") const;
	ofxGgmlClipEmbeddingResult embedImage(
		const std::string & imagePath,
		bool normalize = true,
		const std::string & inputId = {},
		const std::string & label = {}) const;
	ofxGgmlClipImageRankingResult rankImagesForText(
		const ofxGgmlClipImageRankingRequest & request) const;

private:
	std::shared_ptr<ofxGgmlClipBackend> m_backend;
};
