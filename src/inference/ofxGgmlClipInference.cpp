#include "ofxGgmlClipInference.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <utility>

ofxGgmlClipBridgeBackend::ofxGgmlClipBridgeBackend(
	EmbedFunction embedFunction,
	std::string displayName)
	: m_embedFunction(std::move(embedFunction))
	, m_displayName(std::move(displayName)) {
}

void ofxGgmlClipBridgeBackend::setEmbedFunction(
	EmbedFunction embedFunction) {
	m_embedFunction = std::move(embedFunction);
}

bool ofxGgmlClipBridgeBackend::isConfigured() const {
	return static_cast<bool>(m_embedFunction);
}

std::string ofxGgmlClipBridgeBackend::backendName() const {
	return m_displayName.empty() ? "ClipBridge" : m_displayName;
}

ofxGgmlClipEmbeddingResult ofxGgmlClipBridgeBackend::embed(
	const ofxGgmlClipEmbeddingRequest & request) const {
	ofxGgmlClipEmbeddingResult result;
	result.backendName = backendName();
	result.modality = request.modality;
	result.inputId = request.inputId;
	result.label = request.label;
	result.text = request.text;
	result.imagePath = request.imagePath;
	if (!m_embedFunction) {
		result.error =
			"clip bridge backend is not configured yet. "
			"Attach a CLIP adapter callback before calling embed().";
		return result;
	}

	const auto started = std::chrono::steady_clock::now();
	result = m_embedFunction(request);
	if (result.backendName.empty()) {
		result.backendName = backendName();
	}
	if (result.elapsedMs <= 0.0f) {
		result.elapsedMs = std::chrono::duration<float, std::milli>(
			std::chrono::steady_clock::now() - started).count();
	}
	return result;
}

ofxGgmlClipInference::ofxGgmlClipInference()
	: m_backend(createClipBridgeBackend()) {
}

std::shared_ptr<ofxGgmlClipBackend>
ofxGgmlClipInference::createClipBridgeBackend(
	ofxGgmlClipBridgeBackend::EmbedFunction embedFunction,
	const std::string & displayName) {
	return std::make_shared<ofxGgmlClipBridgeBackend>(
		std::move(embedFunction),
		displayName);
}

std::shared_ptr<ofxGgmlClipBackend>
ofxGgmlClipInference::createStableDiffusionClipBridgeBackend(
	ofxGgmlClipBridgeBackend::EmbedFunction embedFunction,
	const std::string & displayName) {
	return createClipBridgeBackend(std::move(embedFunction), displayName);
}

float ofxGgmlClipInference::cosineSimilarity(
	const std::vector<float> & a,
	const std::vector<float> & b) {
	return ofxGgmlEmbeddingIndex::cosineSimilarity(a, b);
}

void ofxGgmlClipInference::setBackend(
	std::shared_ptr<ofxGgmlClipBackend> backend) {
	m_backend = backend
		? std::move(backend)
		: createClipBridgeBackend();
}

std::shared_ptr<ofxGgmlClipBackend> ofxGgmlClipInference::getBackend() const {
	return m_backend;
}

ofxGgmlClipEmbeddingResult ofxGgmlClipInference::embed(
	const ofxGgmlClipEmbeddingRequest & request) const {
	const auto backend = m_backend
		? m_backend
		: createClipBridgeBackend();
	return backend->embed(request);
}

ofxGgmlClipEmbeddingResult ofxGgmlClipInference::embedText(
	const std::string & text,
	bool normalize,
	const std::string & inputId,
	const std::string & label) const {
	ofxGgmlClipEmbeddingRequest request;
	request.modality = ofxGgmlClipEmbeddingModality::Text;
	request.text = text;
	request.normalize = normalize;
	request.inputId = inputId;
	request.label = label;
	return embed(request);
}

ofxGgmlClipEmbeddingResult ofxGgmlClipInference::embedImage(
	const std::string & imagePath,
	bool normalize,
	const std::string & inputId,
	const std::string & label) const {
	ofxGgmlClipEmbeddingRequest request;
	request.modality = ofxGgmlClipEmbeddingModality::Image;
	request.imagePath = imagePath;
	request.normalize = normalize;
	request.inputId = inputId.empty()
		? std::filesystem::path(imagePath).stem().string()
		: inputId;
	request.label = label.empty()
		? std::filesystem::path(imagePath).filename().string()
		: label;
	return embed(request);
}

ofxGgmlClipImageRankingResult ofxGgmlClipInference::rankImagesForText(
	const ofxGgmlClipImageRankingRequest & request) const {
	ofxGgmlClipImageRankingResult result;
	const auto started = std::chrono::steady_clock::now();
	result.backendName = m_backend
		? m_backend->backendName()
		: "ClipBridge";
	if (request.prompt.empty()) {
		result.error = "prompt is empty";
		return result;
	}
	if (request.imagePaths.empty()) {
		result.error = "imagePaths is empty";
		return result;
	}

	result.queryEmbedding = embedText(
		request.prompt,
		request.normalizeEmbeddings,
		request.promptId.empty() ? "prompt" : request.promptId,
		request.promptLabel.empty() ? "Prompt" : request.promptLabel);
	if (!result.queryEmbedding.success) {
		result.error = result.queryEmbedding.error;
		result.backendName = result.queryEmbedding.backendName;
		result.rawOutput = result.queryEmbedding.rawOutput;
		result.elapsedMs = result.queryEmbedding.elapsedMs;
		return result;
	}

	result.backendName = result.queryEmbedding.backendName;
	std::ostringstream rawOutput;
	for (size_t i = 0; i < request.imagePaths.size(); ++i) {
		const std::string & imagePath = request.imagePaths[i];
		auto imageEmbedding = embedImage(
			imagePath,
			request.normalizeEmbeddings,
			"image-" + std::to_string(i),
			std::filesystem::path(imagePath).filename().string());
		if (!imageEmbedding.success) {
			if (!rawOutput.str().empty()) {
				rawOutput << "\n";
			}
			rawOutput << "Failed to embed " << imagePath << ": " << imageEmbedding.error;
			continue;
		}
		result.imageEmbeddings.push_back(imageEmbedding);
		result.hits.push_back({
			imageEmbedding.inputId,
			imageEmbedding.label,
			imageEmbedding.imagePath,
			cosineSimilarity(
				result.queryEmbedding.embedding,
				imageEmbedding.embedding),
			i
		});
	}

	if (result.hits.empty()) {
		result.error = "no image embeddings were produced";
		result.rawOutput = rawOutput.str();
		result.elapsedMs = std::chrono::duration<float, std::milli>(
			std::chrono::steady_clock::now() - started).count();
		return result;
	}

	std::sort(
		result.hits.begin(),
		result.hits.end(),
		[](const ofxGgmlClipSimilarityHit & a,
			const ofxGgmlClipSimilarityHit & b) {
			return a.score > b.score;
		});
	if (request.topK > 0 && result.hits.size() > request.topK) {
		result.hits.resize(request.topK);
	}

	result.success = true;
	result.rawOutput = rawOutput.str();
	result.elapsedMs = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - started).count();
	return result;
}
