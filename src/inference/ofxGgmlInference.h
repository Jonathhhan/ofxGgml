#pragma once

#include "ofMain.h"
#include "ofThread.h"
#include "ofThreadChannel.h"
#include "core/ofxGgmlResult.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class ofxGgmlScriptSource;

struct ofxGgmlInferenceSettings {
	int maxTokens = 256;
	float temperature = 0.7f;
	float topP = 0.9f;
	float minP = 0.05f;
	int topK = 40;
	int mirostat = 0;
	float mirostatTau = 5.0f;
	float mirostatEta = 0.1f;
	float presencePenalty = 0.0f;
	float frequencyPenalty = 0.0f;
	float repeatPenalty = 1.1f;
	int contextSize = 2048;
	int batchSize = 512;
	int ubatchSize = 512;
	int gpuLayers = 0;
	int threads = 0;
	int threadsBatch = 0;
	int seed = -1;
	bool simpleIo = true;
	bool promptCacheAll = false;
	bool flashAttn = false;
	bool mlock = false;
	bool singleTurn = true;
	bool autoProbeCliCapabilities = true;
	bool trimPromptToContext = false;
	bool allowBatchFallback = true;
	bool autoContinueCutoff = false;
	bool stopAtNaturalBoundary = true;
	std::string promptCachePath;
	bool autoPromptCache = true;
	std::string jsonSchema;
	std::string grammarPath;
	std::string chatTemplate;
	std::string device;
	bool useServerBackend = false;
	std::string serverUrl;
	std::string serverModel;
};

struct ofxGgmlInferenceCapabilities {
	bool probed = false;
	bool supportsTopK = true;
	bool supportsMinP = true;
	bool supportsMirostat = true;
	bool supportsSingleTurn = true;
	std::string helpText;
};

struct ofxGgmlPromptSource {
	std::string label;
	std::string uri;
	std::string content;
	bool isWebSource = false;
	bool wasTruncated = false;
};

struct ofxGgmlPromptSourceSettings {
	size_t maxSources = 3;
	size_t maxCharsPerSource = 2000;
	size_t maxTotalChars = 6000;
	bool normalizeWebText = true;
	bool includeSourceHeaders = true;
	bool requestCitations = true;
	std::string heading = "Reference sources";
	std::string citationHint =
		"When you rely on a source, cite it inline as [Source N].";
};

struct ofxGgmlRealtimeInfoSettings {
	bool enabled = false;
	bool allowPromptUrlFetch = true;
	bool allowDomainProviders = true;
	bool allowGenericSearch = true;
	size_t maxSources = 4;
	size_t maxCharsPerSource = 1800;
	size_t maxTotalChars = 6000;
	bool requestCitations = true;
	std::string heading = "Realtime and web context";
	std::string queryOverride;
	std::vector<std::string> explicitUrls;
};

struct ofxGgmlInferenceResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string text;
	std::string error;
	bool promptWasTrimmed = false;
	bool outputLikelyCutoff = false;
	int continuationCount = 0;
	std::vector<ofxGgmlPromptSource> sourcesUsed;
};

struct ofxGgmlEmbeddingSettings {
	bool normalize = true;
	std::string pooling = "mean";
	bool useServerBackend = false;
	std::string serverUrl;
	std::string serverModel;
	bool allowLocalFallback = true;
};

struct ofxGgmlEmbeddingResult {
	bool success = false;
	std::vector<float> embedding;
	std::string error;
};

struct ofxGgmlServerProbeResult {
	bool reachable = false;
	bool healthOk = false;
	bool modelsOk = false;
	bool embeddingsRouteLikely = false;
	bool visionCapable = false;
	bool routerLikely = false;
	std::string baseUrl;
	std::string chatCompletionsUrl;
	std::string embeddingsUrl;
	std::string activeModel;
	std::string capabilitySummary;
	std::vector<std::string> modelIds;
	std::string error;
};

/// Server queue status for monitoring active inference requests.
struct ofxGgmlServerQueueStatus {
	bool available = false;
	int queueLength = 0;
	int processingCount = 0;
	int completedCount = 0;
	int failedCount = 0;
	std::string serverUrl;
	std::string error;
};

struct ofxGgmlSimilarityHit {
	std::string id;
	std::string text;
	float score = 0.0f;
	size_t index = 0;
};

/// Request for batched inference.
struct ofxGgmlBatchRequest {
	std::string id;
	std::string prompt;
	ofxGgmlInferenceSettings settings;
	std::function<bool(const std::string &)> onChunk;

	ofxGgmlBatchRequest() = default;
	ofxGgmlBatchRequest(const std::string & requestId, const std::string & requestPrompt,
		const ofxGgmlInferenceSettings & requestSettings = {},
		std::function<bool(const std::string &)> chunkCallback = nullptr)
		: id(requestId), prompt(requestPrompt), settings(requestSettings), onChunk(chunkCallback) {}
};

/// Result for a single item in a batch.
struct ofxGgmlBatchItemResult {
	std::string id;
	ofxGgmlInferenceResult result;
	size_t batchIndex = 0;
};

/// Overall result for batch inference.
struct ofxGgmlBatchResult {
	bool success = false;
	float totalElapsedMs = 0.0f;
	std::vector<ofxGgmlBatchItemResult> results;
	std::string error;
	size_t processedCount = 0;
	size_t failedCount = 0;
};

/// Settings for batch processing behavior.
struct ofxGgmlBatchSettings {
	bool allowParallelProcessing = true;
	bool stopOnFirstError = false;
	size_t maxConcurrentRequests = 4;
	bool preferServerBatch = true;
	bool fallbackToSequential = true;
};

/// Inference helper for llama.cpp CLI tools and OpenAI-compatible local servers.
class ofxGgmlInference {
public:
	ofxGgmlInference();

	void setCompletionExecutable(const std::string & path);
	void setEmbeddingExecutable(const std::string & path);
	const std::string & getCompletionExecutable() const;
	const std::string & getEmbeddingExecutable() const;
	ofxGgmlInferenceCapabilities probeCompletionCapabilities(
		bool forceRefresh = false) const;
	ofxGgmlInferenceCapabilities getCompletionCapabilities() const;

	ofxGgmlInferenceResult generate(
		const std::string & modelPath,
		const std::string & prompt,
		const ofxGgmlInferenceSettings & settings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const;
	Result<ofxGgmlInferenceResult> generateEx(
		const std::string & modelPath,
		const std::string & prompt,
		const ofxGgmlInferenceSettings & settings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const;

	ofxGgmlInferenceResult generateWithSources(
		const std::string & modelPath,
		const std::string & prompt,
		const std::vector<ofxGgmlPromptSource> & sources,
		const ofxGgmlInferenceSettings & settings = {},
		const ofxGgmlPromptSourceSettings & sourceSettings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const;

	ofxGgmlInferenceResult generateWithUrls(
		const std::string & modelPath,
		const std::string & prompt,
		const std::vector<std::string> & urls,
		const ofxGgmlInferenceSettings & settings = {},
		const ofxGgmlPromptSourceSettings & sourceSettings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const;

	ofxGgmlInferenceResult generateWithScriptSource(
		const std::string & modelPath,
		const std::string & prompt,
		ofxGgmlScriptSource & scriptSource,
		const ofxGgmlInferenceSettings & settings = {},
		const ofxGgmlPromptSourceSettings & sourceSettings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const;

	ofxGgmlInferenceResult generateWithRealtimeInfo(
		const std::string & modelPath,
		const std::string & prompt,
		const std::string & queryOrPrompt,
		const ofxGgmlInferenceSettings & settings = {},
		const ofxGgmlRealtimeInfoSettings & realtimeSettings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const;

	/// Process multiple inference requests in a batch.
	ofxGgmlBatchResult generateBatch(
		const std::string & modelPath,
		const std::vector<ofxGgmlBatchRequest> & requests,
		const ofxGgmlBatchSettings & batchSettings = {}) const;

	/// Process multiple inference requests with shared settings.
	ofxGgmlBatchResult generateBatchSimple(
		const std::string & modelPath,
		const std::vector<std::string> & prompts,
		const ofxGgmlInferenceSettings & settings = {},
		const ofxGgmlBatchSettings & batchSettings = {}) const;

	/// Process multiple embedding requests in a batch.
	std::vector<ofxGgmlEmbeddingResult> embedBatch(
		const std::string & modelPath,
		const std::vector<std::string> & texts,
		const ofxGgmlEmbeddingSettings & settings = {}) const;

	ofxGgmlEmbeddingResult embed(
		const std::string & modelPath,
		const std::string & text,
		const ofxGgmlEmbeddingSettings & settings = {}) const;
	Result<ofxGgmlEmbeddingResult> embedEx(
		const std::string & modelPath,
		const std::string & text,
		const ofxGgmlEmbeddingSettings & settings = {}) const;

	/// Count prompt tokens using the model's tokenizer. Returns -1 on failure.
	int countPromptTokens(
		const std::string & modelPath,
		const std::string & text) const;

	static std::vector<ofxGgmlPromptSource> fetchUrlSources(
		const std::vector<std::string> & urls,
		const ofxGgmlPromptSourceSettings & sourceSettings = {});
	static ofxGgmlServerProbeResult probeServer(
		const std::string & serverUrl,
		bool fetchModels = true);

	/// Query server queue status for monitoring active requests.
	/// Returns queue length, processing count, and completion stats when available.
	static ofxGgmlServerQueueStatus getServerQueueStatus(
		const std::string & serverUrl);
	static std::vector<ofxGgmlPromptSource> collectScriptSourceDocuments(
		ofxGgmlScriptSource & scriptSource,
		const ofxGgmlPromptSourceSettings & sourceSettings = {});
	static std::string buildPromptWithSources(
		const std::string & prompt,
		const std::vector<ofxGgmlPromptSource> & sources,
		const ofxGgmlPromptSourceSettings & sourceSettings = {},
		std::vector<ofxGgmlPromptSource> * usedSources = nullptr);
	static std::vector<ofxGgmlPromptSource> fetchRealtimeSources(
		const std::string & queryOrPrompt,
		const ofxGgmlRealtimeInfoSettings & realtimeSettings = {});
	static std::string buildPromptWithRealtimeInfo(
		const std::string & prompt,
		const std::string & queryOrPrompt,
		const ofxGgmlRealtimeInfoSettings & realtimeSettings = {},
		std::vector<ofxGgmlPromptSource> * usedSources = nullptr);
	static std::string clampPromptToContext(
		const std::string & prompt,
		size_t contextTokens,
		bool * trimmed = nullptr);
	static bool isLikelyCutoffOutput(
		const std::string & text,
		bool codeLike = false);
	static std::string buildCutoffContinuationRequest(
		const std::string & tailText);
	static std::string sanitizeGeneratedText(
		const std::string & raw,
		const std::string & prompt = {});
	static std::string sanitizeStructuredText(
		const std::string & raw);

	static std::vector<std::string> tokenize(const std::string & text);
	static std::string detokenize(const std::vector<std::string> & tokens);
	static int sampleFromLogits(
		const std::vector<float> & logits,
		float temperature = 1.0f,
		float topP = 1.0f,
		uint32_t seed = 0);

private:
	std::string m_completionExe;
	std::string m_embeddingExe;
	mutable bool m_completionCapabilitiesValid = false;
	mutable ofxGgmlInferenceCapabilities m_completionCapabilities;
	mutable std::mutex m_completionCapabilitiesMutex;

	mutable std::unordered_map<std::string, int> m_tokenCountCache;
	mutable std::mutex m_tokenCountCacheMutex;

	/// Helper to process batch via server backend
	ofxGgmlBatchResult processBatchViaServer(
		const std::string & modelPath,
		const std::vector<ofxGgmlBatchRequest> & requests,
		const ofxGgmlBatchSettings & batchSettings) const;

	/// Helper to process batch sequentially (CLI fallback)
	ofxGgmlBatchResult processBatchSequentially(
		const std::string & modelPath,
		const std::vector<ofxGgmlBatchRequest> & requests,
		const ofxGgmlBatchSettings & batchSettings) const;
};

/// Lightweight in-memory similarity index for RAG-style retrieval.
class ofxGgmlEmbeddingIndex {
public:
	void clear();
	void add(const std::string & id, const std::string & text, const std::vector<float> & embedding);
	std::vector<ofxGgmlSimilarityHit> search(
		const std::vector<float> & queryEmbedding,
		size_t topK = 3) const;

	static float cosineSimilarity(const std::vector<float> & a, const std::vector<float> & b);

private:
	struct Entry {
		std::string id;
		std::string text;
		std::vector<float> embedding;
	};

	std::vector<Entry> m_entries;
};

class ofxGgmlInferenceAsync : public ofThread {
public:
	ofxGgmlInferenceAsync();
	~ofxGgmlInferenceAsync();

	ofEvent<std::string> onTokenStream;
	ofEvent<ofxGgmlInferenceResult> onInferenceComplete;

	void startInference(
		const std::string & modelPath,
		const std::string & prompt,
		const ofxGgmlInferenceSettings & settings = {});

	void update();
	void stopInference();

protected:
	void threadedFunction() override;

private:
	std::string m_modelPath;
	std::string m_prompt;
	ofxGgmlInferenceSettings m_settings;

	ofThreadChannel<std::string> m_tokenQueue;
	std::string m_fullResponse;
};
