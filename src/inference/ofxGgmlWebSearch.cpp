#include "ofxGgmlWebSearch.h"

#include "inference/ofxGgmlInferenceSourceInternals.h"

#ifndef OFXGGML_HEADLESS_STUBS
#include "ofJson.h"
#include "ofMain.h"
#endif

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <vector>

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

std::string stripTrailingSlash(std::string value) {
	value = trimCopy(value);
	while (value.size() > 1 && value.back() == '/') {
		value.pop_back();
	}
	return value;
}

std::string lowerCopy(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

std::string stripTrailingDot(std::string value) {
	while (!value.empty() && value.back() == '.') {
		value.pop_back();
	}
	return value;
}

std::string parseUrlHost(const std::string & rawUrl) {
	const std::string trimmed = trimCopy(rawUrl);
	const size_t schemePos = trimmed.find("://");
	if (schemePos == std::string::npos) {
		return {};
	}
	const size_t hostStart = schemePos + 3;
	size_t hostEnd = trimmed.find_first_of("/?#", hostStart);
	if (hostEnd == std::string::npos) {
		hostEnd = trimmed.size();
	}
	std::string hostPort = trimmed.substr(hostStart, hostEnd - hostStart);
	const size_t atPos = hostPort.rfind('@');
	if (atPos != std::string::npos) {
		hostPort = hostPort.substr(atPos + 1);
	}
	if (!hostPort.empty() && hostPort.front() == '[') {
		const size_t closing = hostPort.find(']');
		if (closing != std::string::npos) {
			return stripTrailingDot(lowerCopy(hostPort.substr(0, closing + 1)));
		}
	}
	const size_t colonPos = hostPort.find(':');
	if (colonPos != std::string::npos) {
		hostPort = hostPort.substr(0, colonPos);
	}
	return stripTrailingDot(lowerCopy(hostPort));
}

bool hostContains(const std::string & host, const std::string & needle) {
	return host.find(needle) != std::string::npos;
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

std::string jsonEscape(const std::string & text) {
	std::string escaped;
	escaped.reserve(text.size() + 16);
	for (unsigned char c : text) {
		switch (c) {
		case '\\': escaped += "\\\\"; break;
		case '"': escaped += "\\\""; break;
		case '\b': escaped += "\\b"; break;
		case '\f': escaped += "\\f"; break;
		case '\n': escaped += "\\n"; break;
		case '\r': escaped += "\\r"; break;
		case '\t': escaped += "\\t"; break;
		default:
			if (c < 0x20) {
				static const char * hex = "0123456789ABCDEF";
				escaped += "\\u00";
				escaped.push_back(hex[(c >> 4) & 0x0F]);
				escaped.push_back(hex[c & 0x0F]);
			} else {
				escaped.push_back(static_cast<char>(c));
			}
			break;
		}
	}
	return escaped;
}

float elapsedMsSince(const std::chrono::steady_clock::time_point & start) {
	return std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - start).count();
}

std::string normalizeReaderBaseUrl(const std::string & baseUrl) {
	std::string normalized = trimCopy(baseUrl);
	if (normalized.empty()) {
		normalized = "https://r.jina.ai/";
	}
	if (normalized.back() != '/') {
		normalized.push_back('/');
	}
	return normalized;
}

std::string buildJinaReaderUrl(
	const std::string & baseUrl,
	const std::string & targetUrl) {
	const std::string trimmedTarget = trimCopy(targetUrl);
	if (trimmedTarget.empty()) {
		return {};
	}
	return normalizeReaderBaseUrl(baseUrl) + trimmedTarget;
}

ofxGgmlPromptSource makePromptSourceFromItem(
	const ofxGgmlWebSearchItem & item) {
	ofxGgmlPromptSource source;
	source.label = trimCopy(item.title).empty() ? item.url : item.title;
	source.uri = item.url;
	source.isWebSource = true;
	source.content = trimCopy(item.readableText).empty()
		? trimCopy(item.snippet)
		: trimCopy(item.readableText);
	return source;
}

void dedupeItems(std::vector<ofxGgmlWebSearchItem> & items) {
	std::unordered_set<std::string> seen;
	std::vector<ofxGgmlWebSearchItem> deduped;
	deduped.reserve(items.size());
	for (auto & item : items) {
		const std::string key = trimCopy(item.url);
		if (key.empty() || !seen.insert(key).second) {
			continue;
		}
		item.rank = static_cast<int>(deduped.size() + 1);
		deduped.push_back(std::move(item));
	}
	items = std::move(deduped);
}

float sourceQualityScore(const ofxGgmlWebSearchItem & item, size_t originalIndex) {
	const std::string host = parseUrlHost(item.url);
	const std::string lowerTitle = lowerCopy(item.title);
	const std::string lowerSnippet = lowerCopy(item.snippet);
	float score = 1.0f;

	score += std::max(0.0f, 0.35f - static_cast<float>(originalIndex) * 0.025f);
	if (host.find(".edu") != std::string::npos) score += 0.45f;
	if (host.find(".gov") != std::string::npos) score += 0.35f;
	if (host.find(".org") != std::string::npos) score += 0.12f;
	if (hostContains(host, "plato.stanford.edu")) score += 0.65f;
	if (hostContains(host, "iep.utm.edu")) score += 0.45f;
	if (hostContains(host, "britannica.com")) score += 0.40f;
	if (hostContains(host, "wikipedia.org")) score += 0.35f;
	if (hostContains(host, "marxists.org")) score += 0.30f;
	if (hostContains(host, "jstor.org") ||
		hostContains(host, "cambridge.org") ||
		hostContains(host, "oxford") ||
		hostContains(host, "springer.com") ||
		hostContains(host, "tandfonline.com")) {
		score += 0.35f;
	}

	if (hostContains(host, "handwiki.org")) score -= 0.85f;
	if (hostContains(host, "newworldencyclopedia.org")) score -= 0.75f;
	if (hostContains(host, "philonotes.com")) score -= 0.55f;
	if (hostContains(host, "lettersfromtomis.com")) score -= 0.55f;
	if (hostContains(host, "fandom.com") ||
		hostContains(host, "wikiwand.com") ||
		hostContains(host, "dbpedia.org")) {
		score -= 0.45f;
	}

	if (lowerTitle.find("biography:") != std::string::npos ||
		lowerSnippet.find("jump to:navigation") != std::string::npos ||
		lowerSnippet.find("for other people with the name") != std::string::npos) {
		score -= 0.35f;
	}
	if (lowerTitle.find("key theories") != std::string::npos ||
		lowerTitle.find("key concepts") != std::string::npos) {
		score -= 0.15f;
	}
	if (!item.snippet.empty()) {
		score += 0.08f;
	}
	return score;
}

void rerankAndDiversifyItems(
	std::vector<ofxGgmlWebSearchItem> & items,
	size_t maxResults) {
	if (items.empty()) {
		return;
	}
	struct ScoredItem {
		float score = 0.0f;
		size_t originalIndex = 0;
		ofxGgmlWebSearchItem item;
	};
	std::vector<ScoredItem> scored;
	scored.reserve(items.size());
	for (size_t i = 0; i < items.size(); ++i) {
		scored.push_back({sourceQualityScore(items[i], i), i, std::move(items[i])});
	}
	std::sort(
		scored.begin(),
		scored.end(),
		[](const ScoredItem & a, const ScoredItem & b) {
			if (a.score != b.score) {
				return a.score > b.score;
			}
			return a.originalIndex < b.originalIndex;
		});

	std::vector<ofxGgmlWebSearchItem> diversified;
	diversified.reserve(std::min(maxResults, scored.size()));
	std::unordered_set<std::string> usedHosts;
	for (auto & entry : scored) {
		if (diversified.size() >= maxResults) {
			break;
		}
		const std::string host = parseUrlHost(entry.item.url);
		if (!host.empty() && !usedHosts.insert(host).second) {
			continue;
		}
		entry.item.rank = static_cast<int>(diversified.size() + 1);
		diversified.push_back(std::move(entry.item));
	}
	for (auto & entry : scored) {
		if (diversified.size() >= maxResults) {
			break;
		}
		if (trimCopy(entry.item.url).empty()) {
			continue;
		}
		entry.item.rank = static_cast<int>(diversified.size() + 1);
		diversified.push_back(std::move(entry.item));
	}
	items = std::move(diversified);
}

#ifndef OFXGGML_HEADLESS_STUBS
std::string bestJsonString(
	const ofJson & value,
	const std::vector<std::string> & keys) {
	for (const auto & key : keys) {
		if (value.contains(key) && value[key].is_string()) {
			const std::string text = trimCopy(value[key].get<std::string>());
			if (!text.empty()) {
				return text;
			}
		}
	}
	return {};
}

ofHttpResponse httpGet(
	const std::string & url,
	int timeoutSeconds,
	const std::vector<std::pair<std::string, std::string>> & headers = {}) {
	ofHttpRequest request(url, "ofxggml-web-search", false, true, false);
	request.method = ofHttpRequest::GET;
	request.headers["Accept"] = "application/json, text/markdown, text/plain, */*";
	request.headers["User-Agent"] =
		"ofxGgml/1.0 (openFrameworks desktop web search; https://openframeworks.cc/)";
	for (const auto & header : headers) {
		request.headers[header.first] = header.second;
	}
	request.timeoutSeconds = std::max(1, timeoutSeconds);
	ofURLFileLoader loader;
	return loader.handleRequest(request);
}

ofHttpResponse httpPostJson(
	const std::string & url,
	const std::string & body,
	int timeoutSeconds,
	const std::vector<std::pair<std::string, std::string>> & headers = {}) {
	ofHttpRequest request(url, "ofxggml-web-search", false, true, false);
	request.method = ofHttpRequest::POST;
	request.body = body;
	request.contentType = "application/json";
	request.headers["Accept"] = "application/json";
	request.headers["User-Agent"] =
		"ofxGgml/1.0 (openFrameworks desktop web search; https://openframeworks.cc/)";
	for (const auto & header : headers) {
		request.headers[header.first] = header.second;
	}
	request.timeoutSeconds = std::max(1, timeoutSeconds);
	ofURLFileLoader loader;
	return loader.handleRequest(request);
}

void fetchReadableTextForItems(
	const ofxGgmlWebSearchRequest & request,
	std::vector<ofxGgmlWebSearchItem> & items,
	std::vector<std::string> & attemptedEndpoints) {
	if (!request.fetchReadablePages || request.maxReadablePages == 0) {
		return;
	}

	size_t fetchedCount = 0;
	for (auto & item : items) {
		if (fetchedCount >= request.maxReadablePages) {
			break;
		}
		if (!trimCopy(item.readableText).empty() || trimCopy(item.url).empty()) {
			continue;
		}

		if (request.useJinaReader) {
			const std::string readerUrl =
				buildJinaReaderUrl(request.jinaReaderBaseUrl, item.url);
			attemptedEndpoints.push_back(readerUrl);
			const ofHttpResponse response =
				httpGet(readerUrl, request.timeoutSeconds);
			if (response.status >= 200 && response.status < 300) {
				const std::string body = trimCopy(response.data.getText());
				if (!body.empty()) {
					item.readableText = body;
					item.fetchedReadableText = true;
					++fetchedCount;
					continue;
				}
			}
		}

		if (!trimCopy(request.ollamaApiKey).empty()) {
			const std::string fetchUrl =
				stripTrailingSlash(request.ollamaBaseUrl) + "/api/web_fetch";
			const std::string body =
				"{\"url\":\"" + jsonEscape(item.url) + "\"}";
			attemptedEndpoints.push_back(fetchUrl);
			const ofHttpResponse response = httpPostJson(
				fetchUrl,
				body,
				request.timeoutSeconds,
				{{"Authorization", "Bearer " + trimCopy(request.ollamaApiKey)}});
			if (response.status < 200 || response.status >= 300) {
				continue;
			}
			try {
				const ofJson parsed = ofJson::parse(response.data.getText());
				const std::string content =
					bestJsonString(parsed, {"content", "text", "markdown"});
				if (!content.empty()) {
					item.readableText = content;
					item.fetchedReadableText = true;
					if (trimCopy(item.title).empty()) {
						item.title = bestJsonString(parsed, {"title"});
					}
					if (parsed.contains("links") && parsed["links"].is_array()) {
						for (const auto & link : parsed["links"]) {
							if (link.is_string()) {
								item.links.push_back(link.get<std::string>());
							}
						}
					}
					++fetchedCount;
				}
			} catch (const std::exception &) {
			}
		}
	}
}

ofxGgmlWebSearchResult searchSearxng(
	const ofxGgmlWebSearchRequest & request,
	const std::chrono::steady_clock::time_point & start) {
	ofxGgmlWebSearchResult result;
	result.providerName = ofxGgmlWebSearch::providerLabel(ofxGgmlWebSearchProvider::Searxng);
	result.normalizedQuery = trimCopy(request.query);

	const auto instances = request.searxngInstances.empty()
		? ofxGgmlWebSearch::defaultSearxngInstances()
		: request.searxngInstances;
	const size_t collectLimit =
		std::min<size_t>(std::max<size_t>(request.maxResults * 3, 12), 32);
	for (const auto & instance : instances) {
		const std::string base = stripTrailingSlash(instance);
		if (base.empty()) {
			continue;
		}
		const std::string url =
			base + "/search?q=" + urlEncode(result.normalizedQuery) +
			"&format=json&language=en";
		result.attemptedEndpoints.push_back(url);
		const ofHttpResponse response = httpGet(url, request.timeoutSeconds);
		if (response.status < 200 || response.status >= 300) {
			continue;
		}

		ofJson parsed;
		try {
			parsed = ofJson::parse(response.data.getText());
		} catch (const std::exception &) {
			continue;
		}
		if (!parsed.contains("results") || !parsed["results"].is_array()) {
			continue;
		}

		size_t addedFromInstance = 0;
		for (const auto & entry : parsed["results"]) {
			if (!entry.is_object()) {
				continue;
			}
			ofxGgmlWebSearchItem item;
			item.title = bestJsonString(entry, {"title"});
			item.url = bestJsonString(entry, {"url", "href"});
			item.snippet = bestJsonString(entry, {"content", "snippet", "description"});
			item.providerName = result.providerName;
			if (trimCopy(item.url).empty()) {
				continue;
			}
			result.items.push_back(std::move(item));
			++addedFromInstance;
			if (addedFromInstance >= collectLimit) {
				break;
			}
		}
	}

	dedupeItems(result.items);
	if (!result.items.empty()) {
		rerankAndDiversifyItems(result.items, request.maxResults);
		fetchReadableTextForItems(request, result.items, result.attemptedEndpoints);
		result.success = true;
		result.elapsedMs = elapsedMsSince(start);
		return result;
	}

	result.error = "All SearxNG search instances failed or returned no results.";
	result.elapsedMs = elapsedMsSince(start);
	return result;
}

ofxGgmlWebSearchResult searchOllama(
	const ofxGgmlWebSearchRequest & request,
	const std::chrono::steady_clock::time_point & start) {
	ofxGgmlWebSearchResult result;
	result.providerName = ofxGgmlWebSearch::providerLabel(ofxGgmlWebSearchProvider::Ollama);
	result.normalizedQuery = trimCopy(request.query);
	if (trimCopy(request.ollamaApiKey).empty()) {
		result.error = "Ollama web search requires ollamaApiKey.";
		result.elapsedMs = elapsedMsSince(start);
		return result;
	}

	const std::string url =
		stripTrailingSlash(request.ollamaBaseUrl) + "/api/web_search";
	const std::string body =
		"{\"query\":\"" + jsonEscape(result.normalizedQuery) + "\"," +
		"\"max_results\":" +
		std::to_string(std::clamp<size_t>(request.maxResults, 1, 10)) + "}";
	result.attemptedEndpoints.push_back(url);
	const ofHttpResponse response = httpPostJson(
		url,
		body,
		request.timeoutSeconds,
		{{"Authorization", "Bearer " + trimCopy(request.ollamaApiKey)}});
	if (response.status < 200 || response.status >= 300) {
		result.error =
			"Ollama web_search failed with HTTP " + ofToString(response.status) + ".";
		result.elapsedMs = elapsedMsSince(start);
		return result;
	}

	try {
		const ofJson parsed = ofJson::parse(response.data.getText());
		if (parsed.contains("results") && parsed["results"].is_array()) {
			for (const auto & entry : parsed["results"]) {
				if (!entry.is_object()) {
					continue;
				}
				ofxGgmlWebSearchItem item;
				item.title = bestJsonString(entry, {"title"});
				item.url = bestJsonString(entry, {"url"});
				item.snippet = bestJsonString(entry, {"content", "snippet"});
				item.providerName = result.providerName;
				if (!trimCopy(item.url).empty()) {
					result.items.push_back(std::move(item));
				}
			}
		}
	} catch (const std::exception & e) {
		result.error = std::string("Failed to parse Ollama web_search JSON: ") + e.what();
		result.elapsedMs = elapsedMsSince(start);
		return result;
	}

	dedupeItems(result.items);
	if (result.items.empty()) {
		result.error = "Ollama web_search returned no usable results.";
		result.elapsedMs = elapsedMsSince(start);
		return result;
	}
	rerankAndDiversifyItems(result.items, request.maxResults);
	fetchReadableTextForItems(request, result.items, result.attemptedEndpoints);
	result.success = true;
	result.elapsedMs = elapsedMsSince(start);
	return result;
}
#endif

} // namespace

std::vector<std::string> ofxGgmlWebSearch::defaultSearxngInstances() {
	return {
		"https://search.inetol.net",
		"https://searx.be",
		"https://search.sapti.me"
	};
}

const char * ofxGgmlWebSearch::providerLabel(ofxGgmlWebSearchProvider provider) {
	switch (provider) {
	case ofxGgmlWebSearchProvider::Auto: return "Auto";
	case ofxGgmlWebSearchProvider::Searxng: return "SearxNG";
	case ofxGgmlWebSearchProvider::Ollama: return "OllamaWebSearch";
	}
	return "Auto";
}

std::vector<ofxGgmlPromptSource> ofxGgmlWebSearch::toPromptSources(
	const ofxGgmlWebSearchResult & result,
	const ofxGgmlPromptSourceSettings & sourceSettings) {
	std::vector<ofxGgmlPromptSource> rawSources;
	rawSources.reserve(result.items.size());
	for (const auto & item : result.items) {
		ofxGgmlPromptSource source = makePromptSourceFromItem(item);
		if (!trimCopy(source.content).empty()) {
			rawSources.push_back(std::move(source));
		}
	}
	if (rawSources.empty()) {
		return rawSources;
	}
	std::vector<ofxGgmlPromptSource> usedSources;
	ofxGgmlInferenceSourceInternals::buildPromptWithSources(
		"",
		rawSources,
		sourceSettings,
		&usedSources);
	return usedSources;
}

ofxGgmlWebSearchResult ofxGgmlWebSearch::search(
	const ofxGgmlWebSearchRequest & request) const {
	const auto start = std::chrono::steady_clock::now();
	ofxGgmlWebSearchRequest normalized = request;
	normalized.query = trimCopy(normalized.query);
	normalized.maxResults = std::clamp<size_t>(normalized.maxResults, 1, 24);
	normalized.maxReadablePages =
		std::min(normalized.maxReadablePages, normalized.maxResults);

	ofxGgmlWebSearchResult result;
	result.normalizedQuery = normalized.query;
	result.providerName = providerLabel(normalized.provider);
	if (normalized.query.empty()) {
		result.error = "Web search query is empty.";
		result.elapsedMs = elapsedMsSince(start);
		return result;
	}

#ifdef OFXGGML_HEADLESS_STUBS
	result.error = "Web search is unavailable in headless stubs.";
	result.elapsedMs = elapsedMsSince(start);
	return result;
#else
	if (normalized.provider == ofxGgmlWebSearchProvider::Ollama) {
		return searchOllama(normalized, start);
	}
	if (normalized.provider == ofxGgmlWebSearchProvider::Searxng) {
		return searchSearxng(normalized, start);
	}

	if (!trimCopy(normalized.ollamaApiKey).empty()) {
		result = searchOllama(normalized, start);
		if (result.success) {
			return result;
		}
	}
	return searchSearxng(normalized, start);
#endif
}
