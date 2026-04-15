#pragma once

#include "ofxGgmlInference.h"
#include "ofxGgmlProjectMemory.h"
#include "ofxGgmlScriptSource.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

struct ofxGgmlCodeReviewFileInfo {
	std::string name;
	std::string fullPath;
	std::string content;
	std::string truncatedContent;
	std::vector<std::string> dependencies;
	size_t loc = 0;
	size_t complexity = 0;
	size_t dependencyFanOut = 0;
	size_t dependencyFanIn = 0;
	float recencyScore = 0.0f;
	float importanceScore = 0.0f;
	float similarityScore = 0.0f;
	float priorityScore = 0.0f;
	int tokenCount = 0;
	bool truncated = false;
	bool selected = false;
	std::vector<float> embedding;
	std::string summary;
};

struct ofxGgmlCodeReviewSettings {
	int maxTokens = 768;
	int contextSize = 4096;
	int batchSize = 512;
	int gpuLayers = 0;
	int threads = 0;
	size_t maxRepoTocFiles = 50;
	size_t maxEmbeddingSnippetChars = 4000;
	size_t maxEmbedParallelTasks = 4;
	size_t maxSummaryParallelTasks = 3;
	bool usePromptCache = true;
	bool autoContinueCutoff = false;
	ofxGgmlProjectMemory * projectMemory = nullptr;
};

struct ofxGgmlCodeReviewProgress {
	std::string stage;
	size_t completed = 0;
	size_t total = 0;
};

struct ofxGgmlCodeReviewResult {
	bool success = false;
	std::string status;
	std::string error;
	std::string tableOfContents;
	std::string repoTree;
	std::string architectureReview;
	std::string integrationReview;
	std::string firstPassSummary;
	std::string combinedReport;
	std::vector<ofxGgmlCodeReviewFileInfo> files;
	std::vector<size_t> selectedFileIndices;
};

/// High-level multi-pass repository review helper built on top of
/// ofxGgmlInference, ofxGgmlScriptSource, and embeddings.
class ofxGgmlCodeReview {
public:
	void setCompletionExecutable(const std::string & path);
	void setEmbeddingExecutable(const std::string & path);
	ofxGgmlInference & getInference();
	const ofxGgmlInference & getInference() const;

	ofxGgmlCodeReviewResult reviewScriptSource(
		const std::string & modelPath,
		ofxGgmlScriptSource & scriptSource,
		const std::string & reviewQuery,
		const ofxGgmlCodeReviewSettings & settings = {},
		std::function<bool(const ofxGgmlCodeReviewProgress &)> onProgress = nullptr);

	static std::string defaultReviewQuery();

private:
	ofxGgmlInference m_inference;
};
