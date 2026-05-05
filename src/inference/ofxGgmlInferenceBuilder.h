#pragma once

#include "ofxGgmlInference.h"
#include <optional>
#include <stdexcept>

/// Fluent builder for ofxGgmlInferenceSettings with compile-time validation.
///
/// Example usage:
///     auto settings = InferenceSettingsBuilder()
///         .temperature(0.8f)
///         .maxTokens(256)
///         .useServer("http://localhost:8080", "llama3")
///         .streaming()
///         .build();
///
/// Benefits:
/// - Type-safe configuration
/// - Auto-completion support
/// - Validation at build time
/// - Clear, readable code
/// - Immutable settings after build()
class InferenceSettingsBuilder {
public:
	InferenceSettingsBuilder() = default;

	// Sampling parameters
	InferenceSettingsBuilder& temperature(float value) {
		if (value < 0.0f || value > 2.0f) {
			throw std::invalid_argument("Temperature must be between 0.0 and 2.0");
		}
		m_settings.temperature = value;
		return *this;
	}

	InferenceSettingsBuilder& maxTokens(int value) {
		if (value < 1 || value > 1000000) {
			throw std::invalid_argument("maxTokens must be between 1 and 1000000");
		}
		m_settings.maxTokens = value;
		return *this;
	}

	InferenceSettingsBuilder& topP(float value) {
		if (value < 0.0f || value > 1.0f) {
			throw std::invalid_argument("topP must be between 0.0 and 1.0");
		}
		m_settings.topP = value;
		return *this;
	}

	InferenceSettingsBuilder& minP(float value) {
		if (value < 0.0f || value > 1.0f) {
			throw std::invalid_argument("minP must be between 0.0 and 1.0");
		}
		m_settings.minP = value;
		return *this;
	}

	InferenceSettingsBuilder& topK(int value) {
		if (value < 1 || value > 1000) {
			throw std::invalid_argument("topK must be between 1 and 1000");
		}
		m_settings.topK = value;
		return *this;
	}

	InferenceSettingsBuilder& repeatPenalty(float value) {
		if (value < 0.0f || value > 2.0f) {
			throw std::invalid_argument("repeatPenalty must be between 0.0 and 2.0");
		}
		m_settings.repeatPenalty = value;
		return *this;
	}

	InferenceSettingsBuilder& presencePenalty(float value) {
		if (value < -2.0f || value > 2.0f) {
			throw std::invalid_argument("presencePenalty must be between -2.0 and 2.0");
		}
		m_settings.presencePenalty = value;
		return *this;
	}

	InferenceSettingsBuilder& frequencyPenalty(float value) {
		if (value < -2.0f || value > 2.0f) {
			throw std::invalid_argument("frequencyPenalty must be between -2.0 and 2.0");
		}
		m_settings.frequencyPenalty = value;
		return *this;
	}

	// Mirostat sampling
	InferenceSettingsBuilder& mirostat(int mode, float tau = 5.0f, float eta = 0.1f) {
		if (mode < 0 || mode > 2) {
			throw std::invalid_argument("Mirostat mode must be 0, 1, or 2");
		}
		m_settings.mirostat = mode;
		m_settings.mirostatTau = tau;
		m_settings.mirostatEta = eta;
		return *this;
	}

	// Context and batch configuration
	InferenceSettingsBuilder& contextSize(int value) {
		if (value < 128 || value > 1000000) {
			throw std::invalid_argument("contextSize must be between 128 and 1000000");
		}
		m_settings.contextSize = value;
		return *this;
	}

	InferenceSettingsBuilder& batchSize(int value) {
		if (value < 1 || value > 100000) {
			throw std::invalid_argument("batchSize must be between 1 and 100000");
		}
		m_settings.batchSize = value;
		return *this;
	}

	InferenceSettingsBuilder& ubatchSize(int value) {
		if (value < 1 || value > 100000) {
			throw std::invalid_argument("ubatchSize must be between 1 and 100000");
		}
		m_settings.ubatchSize = value;
		return *this;
	}

	// Hardware configuration
	InferenceSettingsBuilder& gpuLayers(int value) {
		if (value < 0 || value > 1000) {
			throw std::invalid_argument("gpuLayers must be between 0 and 1000");
		}
		m_settings.gpuLayers = value;
		return *this;
	}

	InferenceSettingsBuilder& threads(int value, int batchThreads = 0) {
		if (value < 0 || value > 1024) {
			throw std::invalid_argument("threads must be between 0 and 1024");
		}
		m_settings.threads = value;
		m_settings.threadsBatch = batchThreads > 0 ? batchThreads : value;
		return *this;
	}

	InferenceSettingsBuilder& device(const std::string& deviceName) {
		m_settings.device = deviceName;
		return *this;
	}

	// Server backend
	InferenceSettingsBuilder& useServer(const std::string& url, const std::string& model = "") {
		if (url.empty()) {
			throw std::invalid_argument("Server URL cannot be empty");
		}
		m_settings.useServerBackend = true;
		m_settings.serverUrl = url;
		m_settings.serverModel = model;
		return *this;
	}

	InferenceSettingsBuilder& useCliBackend() {
		m_settings.useServerBackend = false;
		return *this;
	}

	// Prompt cache
	InferenceSettingsBuilder& promptCache(const std::string& path = "", bool cacheAll = false) {
		m_settings.autoPromptCache = path.empty();
		m_settings.promptCachePath = path;
		m_settings.promptCacheAll = cacheAll;
		return *this;
	}

	InferenceSettingsBuilder& noPromptCache() {
		m_settings.autoPromptCache = false;
		m_settings.promptCachePath.clear();
		m_settings.promptCacheAll = false;
		return *this;
	}

	// Structured output
	InferenceSettingsBuilder& jsonSchema(const std::string& schema) {
		if (!schema.empty() && !m_settings.grammarPath.empty()) {
			throw std::invalid_argument("Cannot use both JSON schema and grammar file");
		}
		m_settings.jsonSchema = schema;
		return *this;
	}

	InferenceSettingsBuilder& grammar(const std::string& grammarPath) {
		if (!grammarPath.empty() && !m_settings.jsonSchema.empty()) {
			throw std::invalid_argument("Cannot use both JSON schema and grammar file");
		}
		m_settings.grammarPath = grammarPath;
		return *this;
	}

	// Chat template
	InferenceSettingsBuilder& chatTemplate(const std::string& templateName) {
		m_settings.chatTemplate = templateName;
		return *this;
	}

	// Behavior flags
	InferenceSettingsBuilder& streaming(bool enabled = true) {
		m_settings.simpleIo = !enabled;
		return *this;
	}

	InferenceSettingsBuilder& singleTurn(bool enabled = true) {
		m_settings.singleTurn = enabled;
		return *this;
	}

	InferenceSettingsBuilder& multiTurn() {
		m_settings.singleTurn = false;
		return *this;
	}

	InferenceSettingsBuilder& flashAttention(bool enabled = true) {
		m_settings.flashAttn = enabled;
		return *this;
	}

	InferenceSettingsBuilder& mlock(bool enabled = true) {
		m_settings.mlock = enabled;
		return *this;
	}

	InferenceSettingsBuilder& autoContinueCutoff(bool enabled = true) {
		m_settings.autoContinueCutoff = enabled;
		m_settings.stopAtNaturalBoundary = enabled;
		return *this;
	}

	InferenceSettingsBuilder& trimPromptToContext(bool enabled = true) {
		m_settings.trimPromptToContext = enabled;
		return *this;
	}

	InferenceSettingsBuilder& allowBatchFallback(bool enabled = true) {
		m_settings.allowBatchFallback = enabled;
		return *this;
	}

	InferenceSettingsBuilder& seed(int value) {
		m_settings.seed = value;
		return *this;
	}

	InferenceSettingsBuilder& draftModel(const std::string& path) {
		m_settings.draftModelPath = path;
		return *this;
	}

	// Build final settings
	ofxGgmlInferenceSettings build() const {
		validate();
		return m_settings;
	}

	// Access current state (for debugging)
	const ofxGgmlInferenceSettings& current() const {
		return m_settings;
	}

private:
	void validate() const {
		if (m_settings.ubatchSize > m_settings.batchSize) {
			throw std::invalid_argument("ubatchSize cannot be larger than batchSize");
		}
		if (m_settings.useServerBackend && m_settings.serverUrl.empty()) {
			throw std::invalid_argument("Server URL required when using server backend");
		}
	}

	ofxGgmlInferenceSettings m_settings;
};


/// Fluent builder for ofxGgmlEmbeddingSettings with validation.
class EmbeddingSettingsBuilder {
public:
	EmbeddingSettingsBuilder() = default;

	EmbeddingSettingsBuilder& normalize(bool enabled = true) {
		m_settings.normalize = enabled;
		return *this;
	}

	EmbeddingSettingsBuilder& pooling(const std::string& method) {
		if (method != "mean" && method != "cls" && method != "max") {
			throw std::invalid_argument("Pooling method must be 'mean', 'cls', or 'max'");
		}
		m_settings.pooling = method;
		return *this;
	}

	EmbeddingSettingsBuilder& useServer(const std::string& url, const std::string& model = "") {
		if (url.empty()) {
			throw std::invalid_argument("Server URL cannot be empty");
		}
		m_settings.useServerBackend = true;
		m_settings.serverUrl = url;
		m_settings.serverModel = model;
		return *this;
	}

	EmbeddingSettingsBuilder& allowLocalFallback(bool enabled = true) {
		m_settings.allowLocalFallback = enabled;
		return *this;
	}

	ofxGgmlEmbeddingSettings build() const {
		return m_settings;
	}

private:
	ofxGgmlEmbeddingSettings m_settings;
};


/// Fluent builder for ofxGgmlPromptSourceSettings.
class PromptSourceSettingsBuilder {
public:
	PromptSourceSettingsBuilder() = default;

	PromptSourceSettingsBuilder& maxSources(size_t value) {
		if (value == 0 || value > 100) {
			throw std::invalid_argument("maxSources must be between 1 and 100");
		}
		m_settings.maxSources = value;
		return *this;
	}

	PromptSourceSettingsBuilder& maxCharsPerSource(size_t value) {
		if (value < 100 || value > 1000000) {
			throw std::invalid_argument("maxCharsPerSource must be between 100 and 1000000");
		}
		m_settings.maxCharsPerSource = value;
		return *this;
	}

	PromptSourceSettingsBuilder& maxTotalChars(size_t value) {
		if (value < 1000 || value > 10000000) {
			throw std::invalid_argument("maxTotalChars must be between 1000 and 10000000");
		}
		m_settings.maxTotalChars = value;
		return *this;
	}

	PromptSourceSettingsBuilder& normalizeWebText(bool enabled = true) {
		m_settings.normalizeWebText = enabled;
		return *this;
	}

	PromptSourceSettingsBuilder& includeSourceHeaders(bool enabled = true) {
		m_settings.includeSourceHeaders = enabled;
		return *this;
	}

	PromptSourceSettingsBuilder& requestCitations(bool enabled = true) {
		m_settings.requestCitations = enabled;
		return *this;
	}

	PromptSourceSettingsBuilder& heading(const std::string& text) {
		m_settings.heading = text;
		return *this;
	}

	PromptSourceSettingsBuilder& citationHint(const std::string& text) {
		m_settings.citationHint = text;
		return *this;
	}

	ofxGgmlPromptSourceSettings build() const {
		if (m_settings.maxTotalChars < m_settings.maxCharsPerSource) {
			throw std::invalid_argument("maxTotalChars must be >= maxCharsPerSource");
		}
		return m_settings;
	}

private:
	ofxGgmlPromptSourceSettings m_settings;
};
