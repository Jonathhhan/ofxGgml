#include "ofxGgmlImageSearch.h"

#ifndef OFXGGML_HEADLESS_STUBS
#include "ofJson.h"
#include "ofMain.h"
#endif

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <utility>

namespace {

std::string trimCopy(const std::string & text) {
	size_t start = 0;
	while (start < text.size() &&
		std::isspace(static_cast<unsigned char>(text[start]))) {
		++start;
	}
	size_t end = text.size();
	while (end > start &&
		std::isspace(static_cast<unsigned char>(text[end - 1]))) {
		--end;
	}
	return text.substr(start, end - start);
}

std::string urlEncode(const std::string & text) {
	std::ostringstream encoded;
	encoded.fill('0');
	encoded << std::hex << std::uppercase;
	for (unsigned char c : text) {
		if ((c >= 'A' && c <= 'Z') ||
			(c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') ||
			c == '-' || c == '_' || c == '.' || c == '~') {
			encoded << static_cast<char>(c);
		} else if (c == ' ') {
			encoded << "%20";
		} else {
			encoded << '%' << std::setw(2) << static_cast<int>(c);
		}
	}
	return encoded.str();
}

float elapsedMsSince(const std::chrono::steady_clock::time_point & start) {
	return std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - start).count();
}

} // namespace

ofxGgmlImageSearchBridgeBackend::ofxGgmlImageSearchBridgeBackend(
	SearchCallback callback)
	: m_callback(std::move(callback)) {}

std::string ofxGgmlImageSearchBridgeBackend::backendName() const {
	return "Bridge";
}

ofxGgmlImageSearchResult ofxGgmlImageSearchBridgeBackend::search(
	const ofxGgmlImageSearchRequest & request) const {
	if (!m_callback) {
		return {
			false,
			0.0f,
			backendName(),
			request.prompt,
			"Image search bridge callback is not configured.",
			{}
		};
	}
	return m_callback(request);
}

std::string ofxGgmlWikimediaImageSearchBackend::backendName() const {
	return "WikimediaCommons";
}

ofxGgmlImageSearchResult ofxGgmlWikimediaImageSearchBackend::search(
	const ofxGgmlImageSearchRequest & request) const {
	using Clock = std::chrono::steady_clock;
	const auto start = Clock::now();

	ofxGgmlImageSearchResult result;
	result.providerName = backendName();
	result.normalizedQuery = trimCopy(request.prompt);

	if (result.normalizedQuery.empty()) {
		result.error = "Image search prompt is empty.";
		result.elapsedMs = elapsedMsSince(start);
		return result;
	}

#ifdef OFXGGML_HEADLESS_STUBS
	result.error = "Image search is unavailable in headless stubs.";
	result.elapsedMs = elapsedMsSince(start);
	return result;
#else
	const size_t maxResults = std::clamp<size_t>(request.maxResults, 1, 24);
	const int thumbWidth = std::clamp(request.thumbnailWidth, 64, 1024);

	std::ostringstream url;
	url
		<< "https://commons.wikimedia.org/w/api.php"
		<< "?action=query"
		<< "&generator=search"
		<< "&gsrsearch=" << urlEncode(result.normalizedQuery)
		<< "&gsrnamespace=6"
		<< "&gsrlimit=" << maxResults
		<< "&prop=imageinfo|info|pageimages"
		<< "&iiprop=url|size"
		<< "&inprop=url"
		<< "&piprop=thumbnail"
		<< "&pithumbsize=" << thumbWidth
		<< "&format=json"
		<< "&formatversion=2"
		<< "&utf8=1";

	ofHttpRequest httpRequest(url.str(), "wikimedia-image-search", false, true, false);
	httpRequest.method = ofHttpRequest::GET;
	httpRequest.headers["Accept"] = "application/json";
	httpRequest.headers["User-Agent"] =
		"ofxGgml/1.0 (openFrameworks desktop image search; https://openframeworks.cc/)";
	httpRequest.timeoutSeconds = 20;
	ofURLFileLoader loader;
	const ofHttpResponse response = loader.handleRequest(httpRequest);
	result.elapsedMs = elapsedMsSince(start);
	if (response.status != 200) {
		result.error =
			"Wikimedia search failed with HTTP " +
			ofToString(response.status) + ".";
		return result;
	}

	ofJson json;
	try {
		json = ofJson::parse(response.data.getText());
	} catch (const std::exception & e) {
		result.error = std::string("Failed to parse image search JSON: ") + e.what();
		return result;
	}

	if (json.contains("error")) {
		result.error =
			"Wikimedia API error: " +
			json["error"].value("info", std::string("unknown"));
		return result;
	}

	if (!json.contains("query") ||
		!json["query"].contains("pages") ||
		!json["query"]["pages"].is_array()) {
		result.success = true;
		return result;
	}

	struct IndexedItem {
		int index = 0;
		ofxGgmlImageSearchItem item;
	};
	std::vector<IndexedItem> indexedItems;
	for (const auto & page : json["query"]["pages"]) {
		if (!page.is_object()) {
			continue;
		}
		ofxGgmlImageSearchItem item;
		item.title = page.value("title", "");
		item.description = page.value("extract", "");
		item.pageUrl = page.value("fullurl", "");
		item.sourceLabel = "Wikimedia Commons";

		if (page.contains("thumbnail") && page["thumbnail"].is_object()) {
			item.thumbnailUrl = page["thumbnail"].value("source", "");
			item.thumbnailWidth = page["thumbnail"].value("width", 0);
			item.thumbnailHeight = page["thumbnail"].value("height", 0);
		}

		if (page.contains("imageinfo") &&
			page["imageinfo"].is_array() &&
			!page["imageinfo"].empty() &&
			page["imageinfo"][0].is_object()) {
			const auto & info = page["imageinfo"][0];
			item.imageUrl = info.value("url", "");
			item.width = info.value("width", 0);
			item.height = info.value("height", 0);
		}

		if (item.pageUrl.empty() && !item.title.empty()) {
			item.pageUrl =
				"https://commons.wikimedia.org/wiki/" +
				urlEncode(item.title);
		}

		if (item.imageUrl.empty() && item.thumbnailUrl.empty()) {
			continue;
		}

		IndexedItem indexed;
		indexed.index = page.value("index", static_cast<int>(indexedItems.size()));
		indexed.item = std::move(item);
		indexedItems.push_back(std::move(indexed));
	}

	std::sort(
		indexedItems.begin(),
		indexedItems.end(),
		[](const IndexedItem & a, const IndexedItem & b) {
			return a.index < b.index;
		});
	for (auto & indexed : indexedItems) {
		result.items.push_back(std::move(indexed.item));
	}

	result.success = true;
	return result;
#endif
}

std::shared_ptr<ofxGgmlImageSearchBackend>
createImageSearchBridgeBackend(
	ofxGgmlImageSearchBridgeBackend::SearchCallback callback) {
	return std::make_shared<ofxGgmlImageSearchBridgeBackend>(std::move(callback));
}

ofxGgmlImageSearch::ofxGgmlImageSearch()
	: m_backend(std::make_shared<ofxGgmlWikimediaImageSearchBackend>()) {}

void ofxGgmlImageSearch::setBackend(
	std::shared_ptr<ofxGgmlImageSearchBackend> backend) {
	m_backend = std::move(backend);
}

std::shared_ptr<ofxGgmlImageSearchBackend>
ofxGgmlImageSearch::getBackend() const {
	return m_backend;
}

ofxGgmlImageSearchResult ofxGgmlImageSearch::search(
	const ofxGgmlImageSearchRequest & request) const {
	if (!m_backend) {
		return {
			false,
			0.0f,
			"",
			request.prompt,
			"Image search backend is not configured.",
			{}
		};
	}
	return m_backend->search(request);
}
