#pragma once

#include "inference/ofxGgmlInference.h"

#include <string>
#include <vector>

enum class ofxGgmlWebSearchProvider {
	Auto = 0,
	Searxng,
	Ollama
};

struct ofxGgmlWebSearchItem {
	std::string title;
	std::string url;
	std::string snippet;
	std::string readableText;
	std::vector<std::string> links;
	std::string providerName;
	int rank = -1;
	bool fetchedReadableText = false;
};

struct ofxGgmlWebSearchRequest {
	std::string query;
	size_t maxResults = 8;
	ofxGgmlWebSearchProvider provider = ofxGgmlWebSearchProvider::Auto;
	std::vector<std::string> searxngInstances;
	std::string ollamaApiKey;
	std::string ollamaBaseUrl = "https://ollama.com";
	bool fetchReadablePages = true;
	bool useJinaReader = true;
	std::string jinaReaderBaseUrl = "https://r.jina.ai/";
	size_t maxReadablePages = 4;
	int timeoutSeconds = 20;
};

struct ofxGgmlWebSearchResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string providerName;
	std::string normalizedQuery;
	std::string error;
	std::vector<std::string> attemptedEndpoints;
	std::vector<ofxGgmlWebSearchItem> items;
};

class ofxGgmlWebSearch {
public:
	static std::vector<std::string> defaultSearxngInstances();
	static const char * providerLabel(ofxGgmlWebSearchProvider provider);
	static std::vector<ofxGgmlPromptSource> toPromptSources(
		const ofxGgmlWebSearchResult & result,
		const ofxGgmlPromptSourceSettings & sourceSettings);

	ofxGgmlWebSearchResult search(
		const ofxGgmlWebSearchRequest & request) const;
};
