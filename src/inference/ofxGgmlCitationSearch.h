#pragma once

#include "inference/ofxGgmlInference.h"
#include "inference/ofxGgmlRAGPipeline.h"
#include "inference/ofxGgmlWebCrawler.h"

#include <string>
#include <vector>

struct ofxGgmlCitationItem {
	std::string quote;
	std::string note;
	std::string sourceLabel;
	std::string sourceUri;
	int sourceIndex = -1;
};

struct ofxGgmlCitationSearchRequest {
	std::string modelPath;
	std::string topic;
	size_t maxCitations = 100;
	bool useCrawler = false;
	ofxGgmlWebCrawlerRequest crawlerRequest;
	std::vector<std::string> sourceUrls;
	ofxGgmlInferenceSettings inferenceSettings;
	ofxGgmlPromptSourceSettings sourceSettings;
	struct QueryRewriteSettings {
		bool enabled = true;
		bool useInference = true;
		bool allowFallbackHeuristics = true;
		bool allowDomainHints = true;
		size_t maxAlternateQueries = 2;
	} queryRewriteSettings;
};

struct ofxGgmlCitationSearchInputSettings {
	std::vector<std::string> triggerWords = {
		"search",
		"find",
		"query",
		"google",
		"citation",
		"citations",
		"cite",
		"source",
		"sources",
		"quote",
		"quotes",
		"evidence"
	};
	size_t minTopicLength = 3;
};

struct ofxGgmlCitationSearchInputMatch {
	bool matched = false;
	std::string triggerWord;
	std::string topic;
};

struct ofxGgmlCitationSearchResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string backendName;
	std::string requestedTopic;
	std::string inputTriggerWord;
	std::string rawResponse;
	std::string summary;
	std::string error;
	struct QueryRewriteResult {
		bool applied = false;
		bool usedInference = false;
		bool usedFallback = false;
		std::string originalTopic;
		std::string rewrittenTopic;
		std::vector<std::string> alternateQueries;
		std::vector<std::string> domainHints;
		std::vector<std::string> queriesUsed;
		std::string rawResponse;
		std::string error;
	} queryRewrite;
	std::vector<ofxGgmlCitationItem> citations;
	std::vector<ofxGgmlPromptSource> sourcesUsed;
	ofxGgmlWebCrawlerResult crawlerResult;
	ofxGgmlRAGRetrievalResult retrieval;
};

namespace ofxGgmlCitationSearchInternal {

std::string cleanCrawlerMarkdownForCitations(const std::string & rawMarkdown);
std::vector<std::string> extractExactQuoteCandidatesForTesting(
	const std::string & content);

} // namespace ofxGgmlCitationSearchInternal

class ofxGgmlCitationSearch {
public:
	ofxGgmlCitationSearch();

	ofxGgmlInference & getInference();
	const ofxGgmlInference & getInference() const;
	ofxGgmlWebCrawler & getWebCrawler();
	const ofxGgmlWebCrawler & getWebCrawler() const;

	static ofxGgmlCitationSearchInputMatch detectInputIntent(
		const std::string & userInput,
		const ofxGgmlCitationSearchInputSettings & settings = {});

	ofxGgmlCitationSearchResult search(
		const ofxGgmlCitationSearchRequest & request) const;
	ofxGgmlCitationSearchResult searchFromInput(
		const std::string & userInput,
		const ofxGgmlCitationSearchRequest & baseRequest,
		const ofxGgmlCitationSearchInputSettings & inputSettings = {}) const;

private:
	ofxGgmlInference m_inference;
	ofxGgmlWebCrawler m_webCrawler;
};
