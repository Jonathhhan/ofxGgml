#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class ofxGgmlClipInference;

enum class ofxGgmlImageSearchProvider {
	WikimediaCommons = 0
};

struct ofxGgmlImageSearchRequest {
	std::string prompt;
	size_t maxResults = 8;
	int thumbnailWidth = 320;
	ofxGgmlImageSearchProvider provider =
		ofxGgmlImageSearchProvider::WikimediaCommons;
	bool useSemanticRanking = false;
};

struct ofxGgmlImageSearchItem {
	std::string title;
	std::string description;
	std::string sourceLabel;
	std::string pageUrl;
	std::string imageUrl;
	std::string thumbnailUrl;
	int width = 0;
	int height = 0;
	int thumbnailWidth = 0;
	int thumbnailHeight = 0;
	float semanticScore = 0.0f;
};

struct ofxGgmlImageSearchResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string providerName;
	std::string normalizedQuery;
	std::string error;
	std::vector<ofxGgmlImageSearchItem> items;
};

class ofxGgmlImageSearchBackend {
public:
	virtual ~ofxGgmlImageSearchBackend() = default;
	virtual std::string backendName() const = 0;
	virtual ofxGgmlImageSearchResult search(
		const ofxGgmlImageSearchRequest & request) const = 0;
};

class ofxGgmlImageSearchBridgeBackend : public ofxGgmlImageSearchBackend {
public:
	using SearchCallback =
		std::function<ofxGgmlImageSearchResult(
			const ofxGgmlImageSearchRequest &)>;

	explicit ofxGgmlImageSearchBridgeBackend(SearchCallback callback);

	std::string backendName() const override;
	ofxGgmlImageSearchResult search(
		const ofxGgmlImageSearchRequest & request) const override;

private:
	SearchCallback m_callback;
};

class ofxGgmlWikimediaImageSearchBackend : public ofxGgmlImageSearchBackend {
public:
	std::string backendName() const override;
	ofxGgmlImageSearchResult search(
		const ofxGgmlImageSearchRequest & request) const override;
};

std::shared_ptr<ofxGgmlImageSearchBackend>
createImageSearchBridgeBackend(
	ofxGgmlImageSearchBridgeBackend::SearchCallback callback);

class ofxGgmlImageSearch {
public:
	ofxGgmlImageSearch();

	void setBackend(std::shared_ptr<ofxGgmlImageSearchBackend> backend);
	std::shared_ptr<ofxGgmlImageSearchBackend> getBackend() const;
	void setClipInference(ofxGgmlClipInference * clip);
	ofxGgmlClipInference * getClipInference() const;

	ofxGgmlImageSearchResult search(
		const ofxGgmlImageSearchRequest & request) const;
	ofxGgmlImageSearchResult searchWithSemanticRanking(
		const ofxGgmlImageSearchRequest & request) const;

private:
	std::shared_ptr<ofxGgmlImageSearchBackend> m_backend;
	ofxGgmlClipInference * m_clipInference = nullptr;
};
