#include "ofxGgmlCitationSearch.h"

#include "ofJson.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <sstream>
#include <unordered_set>
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

float elapsedMsSince(const std::chrono::steady_clock::time_point & start) {
	return std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - start).count();
}

std::string buildCitationPrompt(
	const std::string & topic,
	size_t maxCitations) {
	std::ostringstream prompt;
	prompt
		<< "You are a citation extraction assistant.\n"
		<< "Using only the provided source material, extract up to "
		<< std::max<size_t>(1, maxCitations)
		<< " short, relevant citations about the topic below.\n"
		<< "Quotes should be verbatim when possible, concise, and grounded in the sources.\n"
		<< "If the evidence is weak, return fewer citations instead of guessing.\n"
		<< "Return valid JSON only in this shape:\n"
		<< "{\"summary\":\"...\",\"citations\":[{\"quote\":\"...\",\"sourceIndex\":1,\"note\":\"...\"}]}\n"
		<< "Use 1-based sourceIndex values that match the provided [Source N] labels.\n"
		<< "Do not invent sources, URLs, or quotes.\n\n"
		<< "Topic: " << topic << "\n";
	return prompt.str();
}

ofJson parseLooseJson(const std::string & rawText) {
	const std::string trimmed = trimCopy(rawText);
	if (trimmed.empty()) {
		return ofJson();
	}

	try {
		return ofJson::parse(trimmed);
	} catch (...) {
	}

	const size_t codeFenceStart = trimmed.find("```");
	if (codeFenceStart != std::string::npos) {
		const size_t firstLineEnd = trimmed.find('\n', codeFenceStart);
		if (firstLineEnd != std::string::npos) {
			const size_t closingFence = trimmed.find("```", firstLineEnd + 1);
			if (closingFence != std::string::npos && closingFence > firstLineEnd) {
				try {
					return ofJson::parse(trimCopy(
						trimmed.substr(firstLineEnd + 1, closingFence - firstLineEnd - 1)));
				} catch (...) {
				}
			}
		}
	}

	const size_t objectStart = trimmed.find('{');
	const size_t objectEnd = trimmed.rfind('}');
	if (objectStart != std::string::npos &&
		objectEnd != std::string::npos &&
		objectEnd > objectStart) {
		try {
			return ofJson::parse(trimmed.substr(objectStart, objectEnd - objectStart + 1));
		} catch (...) {
		}
	}

	return ofJson();
}

std::vector<ofxGgmlPromptSource> convertCrawlerDocsToSources(
	const ofxGgmlWebCrawlerResult & crawlerResult) {
	std::vector<ofxGgmlPromptSource> sources;
	sources.reserve(crawlerResult.documents.size());
	std::unordered_set<std::string> seen;
	for (const auto & document : crawlerResult.documents) {
		const std::string markdown = trimCopy(document.markdown);
		if (markdown.empty()) {
			continue;
		}
		const std::string uri = !document.sourceUrl.empty()
			? trimCopy(document.sourceUrl)
			: trimCopy(document.localPath);
		const std::string dedupeKey = uri + "\n---\n" + markdown;
		if (!seen.insert(dedupeKey).second) {
			continue;
		}
		ofxGgmlPromptSource source;
		source.label = document.title.empty()
			? std::string("Crawled page")
			: document.title;
		source.uri = uri;
		source.content = markdown;
		source.isWebSource = true;
		sources.push_back(std::move(source));
	}
	return sources;
}

std::vector<std::string> normalizeSourceUrls(
	const std::vector<std::string> & urls) {
	std::vector<std::string> normalized;
	normalized.reserve(urls.size());
	std::unordered_set<std::string> seen;
	for (const auto & rawUrl : urls) {
		const std::string url = trimCopy(rawUrl);
		if (url.empty()) {
			continue;
		}
		if (!seen.insert(url).second) {
			continue;
		}
		normalized.push_back(url);
	}
	return normalized;
}

std::vector<ofxGgmlCitationItem> dedupeCitationItems(
	std::vector<ofxGgmlCitationItem> items) {
	std::vector<ofxGgmlCitationItem> deduped;
	deduped.reserve(items.size());
	std::unordered_set<std::string> seen;
	for (auto & item : items) {
		const std::string key = trimCopy(item.quote) + "\n---\n" + item.sourceUri;
		if (key == "\n---\n") {
			continue;
		}
		if (!seen.insert(key).second) {
			continue;
		}
		deduped.push_back(std::move(item));
	}
	return deduped;
}

std::string resolveSourceLabel(
	const std::vector<ofxGgmlPromptSource> & sources,
	int sourceIndex) {
	if (sourceIndex < 1 || sourceIndex > static_cast<int>(sources.size())) {
		return {};
	}
	const auto & source = sources[static_cast<size_t>(sourceIndex - 1)];
	return source.label.empty() ? ("Source " + std::to_string(sourceIndex)) : source.label;
}

std::string resolveSourceUri(
	const std::vector<ofxGgmlPromptSource> & sources,
	int sourceIndex) {
	if (sourceIndex < 1 || sourceIndex > static_cast<int>(sources.size())) {
		return {};
	}
	return sources[static_cast<size_t>(sourceIndex - 1)].uri;
}

} // namespace

ofxGgmlCitationSearch::ofxGgmlCitationSearch() = default;

ofxGgmlInference & ofxGgmlCitationSearch::getInference() {
	return m_inference;
}

const ofxGgmlInference & ofxGgmlCitationSearch::getInference() const {
	return m_inference;
}

ofxGgmlWebCrawler & ofxGgmlCitationSearch::getWebCrawler() {
	return m_webCrawler;
}

const ofxGgmlWebCrawler & ofxGgmlCitationSearch::getWebCrawler() const {
	return m_webCrawler;
}

ofxGgmlCitationSearchResult ofxGgmlCitationSearch::search(
	const ofxGgmlCitationSearchRequest & request) const {
	using Clock = std::chrono::steady_clock;
	const auto start = Clock::now();

	ofxGgmlCitationSearchResult result;
	result.backendName = request.useCrawler ? "Mojo+Inference" : "Inference";

	const std::string topic = trimCopy(request.topic);
	if (topic.empty()) {
		result.error = "Citation topic is empty.";
		result.elapsedMs = elapsedMsSince(start);
		return result;
	}
	if (trimCopy(request.modelPath).empty()) {
		result.error = "Citation search requires a model path.";
		result.elapsedMs = elapsedMsSince(start);
		return result;
	}

	const std::string prompt = buildCitationPrompt(topic, request.maxCitations);
	ofxGgmlInferenceResult inferenceResult;

	if (request.useCrawler) {
		result.crawlerResult = m_webCrawler.crawl(request.crawlerRequest);
		if (!result.crawlerResult.success) {
			result.error = result.crawlerResult.error.empty()
				? std::string("Crawler did not return any documents.")
				: result.crawlerResult.error;
			result.elapsedMs = elapsedMsSince(start);
			return result;
		}

		auto sources = convertCrawlerDocsToSources(result.crawlerResult);
		if (sources.empty()) {
			result.error = "Crawler did not return any usable markdown documents.";
			result.elapsedMs = elapsedMsSince(start);
			return result;
		}
		inferenceResult = m_inference.generateWithSources(
			request.modelPath,
			prompt,
			sources,
			request.inferenceSettings,
			request.sourceSettings);
	} else {
		const auto normalizedUrls = normalizeSourceUrls(request.sourceUrls);
		if (normalizedUrls.empty()) {
			result.error =
				"Citation search needs either source URLs or a crawler request.";
			result.elapsedMs = elapsedMsSince(start);
			return result;
		}
		inferenceResult = m_inference.generateWithUrls(
			request.modelPath,
			prompt,
			normalizedUrls,
			request.inferenceSettings,
			request.sourceSettings);
	}

	result.rawResponse = inferenceResult.text;
	result.sourcesUsed = inferenceResult.sourcesUsed;
	if (!inferenceResult.success) {
		result.error = inferenceResult.error.empty()
			? std::string("Inference failed while extracting citations.")
			: inferenceResult.error;
		result.elapsedMs = elapsedMsSince(start);
		return result;
	}

	const ofJson parsed = parseLooseJson(inferenceResult.text);
	if (!parsed.is_object()) {
		result.error =
			"Citation extraction returned non-JSON output. Raw response is available.";
		result.elapsedMs = elapsedMsSince(start);
		return result;
	}

	result.summary = parsed.value("summary", std::string());
	if (parsed.contains("citations") && parsed["citations"].is_array()) {
		std::vector<ofxGgmlCitationItem> parsedItems;
		for (const auto & entry : parsed["citations"]) {
			if (!entry.is_object()) {
				continue;
			}
			ofxGgmlCitationItem item;
			item.quote = trimCopy(entry.value("quote", std::string()));
			item.note = trimCopy(entry.value("note", std::string()));
			item.sourceIndex = entry.value("sourceIndex", -1);
			item.sourceLabel = resolveSourceLabel(result.sourcesUsed, item.sourceIndex);
			item.sourceUri = resolveSourceUri(result.sourcesUsed, item.sourceIndex);
			if (!item.quote.empty()) {
				parsedItems.push_back(std::move(item));
			}
		}
		result.citations = dedupeCitationItems(std::move(parsedItems));
	}

	result.success = true;
	result.elapsedMs = elapsedMsSince(start);
	return result;
}
