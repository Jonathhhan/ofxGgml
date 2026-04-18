#pragma once

#include "inference/ofxGgmlInference.h"
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
	size_t maxCitations = 5;
	bool useCrawler = false;
	ofxGgmlWebCrawlerRequest crawlerRequest;
	std::vector<std::string> sourceUrls;
	ofxGgmlInferenceSettings inferenceSettings;
	ofxGgmlPromptSourceSettings sourceSettings;
};

struct ofxGgmlCitationSearchResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string backendName;
	std::string rawResponse;
	std::string summary;
	std::string error;
	std::vector<ofxGgmlCitationItem> citations;
	std::vector<ofxGgmlPromptSource> sourcesUsed;
	ofxGgmlWebCrawlerResult crawlerResult;
};

class ofxGgmlCitationSearch {
public:
	ofxGgmlCitationSearch();

	ofxGgmlInference & getInference();
	const ofxGgmlInference & getInference() const;
	ofxGgmlWebCrawler & getWebCrawler();
	const ofxGgmlWebCrawler & getWebCrawler() const;

	ofxGgmlCitationSearchResult search(
		const ofxGgmlCitationSearchRequest & request) const;

private:
	ofxGgmlInference m_inference;
	ofxGgmlWebCrawler m_webCrawler;
};
