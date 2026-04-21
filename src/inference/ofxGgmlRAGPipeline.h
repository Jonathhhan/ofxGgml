#pragma once

#include "inference/ofxGgmlInference.h"

#include <functional>
#include <string>
#include <vector>

/// A single source document for the RAG pipeline.
struct ofxGgmlRAGDocument {
	std::string id;
	std::string content;
	std::string sourceLabel;
	std::string sourceUri;
};

/// A scored text passage retrieved from a document.
struct ofxGgmlRAGChunk {
	std::string docId;
	std::string sourceLabel;
	std::string sourceUri;
	std::string text;
	int chunkIndex = 0;
	float score = 0.0f;
};

/// Retrieval query parameters.
struct ofxGgmlRAGQuery {
	std::string query;
	size_t topK = 5;
	size_t chunkSize = 400;
	size_t chunkOverlap = 80;
	bool includeSourceHeaders = true;
};

/// Result from the retrieval step only (no generation).
struct ofxGgmlRAGRetrievalResult {
	bool success = false;
	std::string error;
	std::vector<ofxGgmlRAGChunk> chunks;
	std::string augmentedContext;
};

/// Full RAG request: retrieval + generation.
struct ofxGgmlRAGRequest {
	ofxGgmlRAGQuery query;
	std::string modelPath;
	std::string promptPrefix;
	ofxGgmlInferenceSettings inferenceSettings;
};

/// Full RAG result: retrieval + inference.
struct ofxGgmlRAGResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string error;
	ofxGgmlRAGRetrievalResult retrieval;
	std::string augmentedPrompt;
	ofxGgmlInferenceResult inference;
	std::string answer;
};

/// Local Retrieval-Augmented Generation pipeline.
///
/// Add documents, then call retrieve() for grounded context assembly or
/// generate() to run the full retrieval + inference loop. No network or
/// external process is required for retrieval; scoring uses keyword overlap.
class ofxGgmlRAGPipeline {
public:
	void addDocument(const ofxGgmlRAGDocument & doc);
	void addTextDocument(
		const std::string & content,
		const std::string & id = "",
		const std::string & label = "",
		const std::string & uri = "");
	void clearDocuments();
	size_t documentCount() const;

	ofxGgmlInference & getInference();
	const ofxGgmlInference & getInference() const;

	/// Retrieve the top-K passages matching the query.
	ofxGgmlRAGRetrievalResult retrieve(const ofxGgmlRAGQuery & query) const;

	/// Retrieve relevant passages and run LLM inference over the assembled context.
	ofxGgmlRAGResult generate(
		const ofxGgmlRAGRequest & request,
		std::function<bool(const std::string &)> onChunk = nullptr) const;

	// --- Static helpers -------------------------------------------------

	/// Split a document into overlapping text chunks.
	static std::vector<ofxGgmlRAGChunk> chunkDocument(
		const ofxGgmlRAGDocument & doc,
		size_t chunkSize = 400,
		size_t overlap = 80);

	/// Compute a BM25-inspired keyword overlap score for a chunk against a query.
	static float scoreChunk(
		const ofxGgmlRAGChunk & chunk,
		const std::string & query);

	/// Assemble retrieved chunks into a formatted context string.
	static std::string buildAugmentedContext(
		const std::vector<ofxGgmlRAGChunk> & chunks,
		bool includeSourceHeaders = true);

	/// Build the full augmented prompt from context + user query.
	static std::string buildAugmentedPrompt(
		const std::string & augmentedContext,
		const std::string & query,
		const std::string & promptPrefix = "");

private:
	std::vector<ofxGgmlRAGDocument> m_documents;
	ofxGgmlInference m_inference;
};
