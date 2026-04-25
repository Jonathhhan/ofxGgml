#include "ofxGgmlCitationSearch.h"

#include "ofJson.h"
#include "inference/ofxGgmlInferenceSourceInternals.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_map>
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

std::string toLowerCopy(const std::string & text) {
	std::string lowered = text;
	std::transform(
		lowered.begin(),
		lowered.end(),
		lowered.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return lowered;
}

std::string stripTrailingDot(std::string value) {
	while (!value.empty() && value.back() == '.') {
		value.pop_back();
	}
	return value;
}

std::string stripTrailingPunctuation(std::string value) {
	static const std::unordered_set<char> allowedTrailingDelimiters = {
		')', ']', '"', '\''
	};
	while (!value.empty()) {
		const unsigned char last = static_cast<unsigned char>(value.back());
		if (std::isalnum(last) ||
			allowedTrailingDelimiters.find(value.back()) !=
				allowedTrailingDelimiters.end()) {
			break;
		}
		value.pop_back();
	}
	return trimCopy(value);
}

std::string urlEncodeComponent(const std::string & value) {
	std::ostringstream escaped;
	escaped.fill('0');
	escaped << std::hex;
	for (unsigned char c : value) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			escaped << c;
		} else if (c == ' ') {
			escaped << '_';
		} else {
			escaped << '%' << std::setw(2) << std::uppercase << int(c);
		}
	}
	return escaped.str();
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
			return stripTrailingDot(toLowerCopy(hostPort.substr(0, closing + 1)));
		}
	}
	const size_t colonPos = hostPort.find(':');
	if (colonPos != std::string::npos) {
		hostPort = hostPort.substr(0, colonPos);
	}
	return stripTrailingDot(toLowerCopy(hostPort));
}

std::string resolveTopicAwareCrawlerUrl(
	const std::string & rawUrl,
	const std::string & topic) {
	const std::string trimmedUrl = trimCopy(rawUrl);
	const std::string trimmedTopic = trimCopy(topic);
	if (trimmedUrl.empty() || trimmedTopic.empty()) {
		return trimmedUrl;
	}

	const std::string host = parseUrlHost(trimmedUrl);
	const size_t schemePos = trimmedUrl.find("://");
	if (schemePos == std::string::npos) {
		return trimmedUrl;
	}
	const size_t hostStart = schemePos + 3;
	size_t pathStart = trimmedUrl.find('/', hostStart);
	const std::string pathAndMore =
		pathStart == std::string::npos ? std::string("/") : trimmedUrl.substr(pathStart);
	const std::string lowerPathAndMore = toLowerCopy(pathAndMore);
	const bool isWikipediaHost =
		host == "www.wikipedia.org" ||
		host == "wikipedia.org" ||
		(host.size() > std::string(".wikipedia.org").size() &&
		 host.rfind(".wikipedia.org") == host.size() - std::string(".wikipedia.org").size());
	const bool isGenericWikipediaRoot =
		isWikipediaHost &&
		(lowerPathAndMore == "/" ||
		 lowerPathAndMore == "/wiki" ||
		 lowerPathAndMore == "/wiki/" ||
		 lowerPathAndMore.find("/wiki/special:search") == 0);
	if (!isGenericWikipediaRoot) {
		return trimmedUrl;
	}

	return "https://en.wikipedia.org/wiki/" + urlEncodeComponent(trimmedTopic);
}

std::string normalizeForExactQuoteMatch(const std::string & text) {
	std::string out;
	out.reserve(text.size());
	bool lastWasSpace = false;
	for (char c : text) {
		unsigned char uc = static_cast<unsigned char>(c);
		if (uc == 0x91 || uc == 0x92 || uc == 0xB4) {
			c = '\'';
			uc = static_cast<unsigned char>(c);
		} else if (uc == 0x93 || uc == 0x94) {
			c = '"';
			uc = static_cast<unsigned char>(c);
		}
		if (std::isspace(uc)) {
			if (!lastWasSpace) {
				out.push_back(' ');
				lastWasSpace = true;
			}
			continue;
		}
		lastWasSpace = false;
		out.push_back(static_cast<char>(std::tolower(uc)));
	}
	return trimCopy(out);
}

struct NormalizedTextMap {
	std::string normalized;
	std::vector<size_t> originalIndices;
};

NormalizedTextMap buildNormalizedTextMap(const std::string & text) {
	NormalizedTextMap map;
	map.normalized.reserve(text.size());
	map.originalIndices.reserve(text.size());
	bool lastWasSpace = false;
	for (size_t i = 0; i < text.size(); ++i) {
		char c = text[i];
		unsigned char uc = static_cast<unsigned char>(c);
		if (uc == 0x91 || uc == 0x92 || uc == 0xB4) {
			c = '\'';
			uc = static_cast<unsigned char>(c);
		} else if (uc == 0x93 || uc == 0x94) {
			c = '"';
			uc = static_cast<unsigned char>(c);
		}
		if (std::isspace(uc)) {
			if (!lastWasSpace) {
				map.normalized.push_back(' ');
				map.originalIndices.push_back(i);
				lastWasSpace = true;
			}
			continue;
		}
		lastWasSpace = false;
		map.normalized.push_back(static_cast<char>(std::tolower(uc)));
		map.originalIndices.push_back(i);
	}
	while (!map.normalized.empty() && map.normalized.front() == ' ') {
		map.normalized.erase(map.normalized.begin());
		map.originalIndices.erase(map.originalIndices.begin());
	}
	while (!map.normalized.empty() && map.normalized.back() == ' ') {
		map.normalized.pop_back();
		map.originalIndices.pop_back();
	}
	return map;
}

std::string buildExactSourceLabel(
	const std::string & label,
	const std::string & uri,
	int sourceIndex) {
	const std::string trimmedLabel = trimCopy(label);
	const std::string host = parseUrlHost(uri);
	if (!host.empty()) {
		if (trimmedLabel.empty()) {
			return host;
		}
		if (trimmedLabel == uri || trimmedLabel == host) {
			return host;
		}
		return trimmedLabel + " [" + host + "]";
	}
	if (!trimmedLabel.empty()) {
		return trimmedLabel;
	}
	return sourceIndex > 0 ? ("Source " + std::to_string(sourceIndex)) : std::string();
}

std::string appendHostToLabel(
	const std::string & label,
	const std::string & uri) {
	return buildExactSourceLabel(label, uri, -1);
}

float elapsedMsSince(const std::chrono::steady_clock::time_point & start) {
	return std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - start).count();
}

bool isWordBoundary(const std::string & text, size_t index) {
	if (index >= text.size()) {
		return true;
	}
	const unsigned char ch = static_cast<unsigned char>(text[index]);
	return !std::isalnum(ch) && ch != '_';
}

bool hasWordBoundaries(
	const std::string & text,
	size_t start,
	size_t length) {
	const bool leftBoundary = start == 0 || isWordBoundary(text, start - 1);
	const bool rightBoundary = isWordBoundary(text, start + length);
	return leftBoundary && rightBoundary;
}

std::string stripCitationLeadIn(std::string value) {
	static const std::unordered_set<std::string> fillerWords = {
		"for",
		"about",
		"on",
		"regarding",
		"re",
		"related",
		"to",
		"the",
		"some",
		"any",
		"relevant",
		"supporting",
		"web",
		"citation",
		"citations",
		"cite",
		"source",
		"sources",
		"quote",
		"quotes",
		"evidence"
	};
	value = trimCopy(value);
	size_t start = 0;
	while (start < value.size()) {
		while (start < value.size() &&
			!std::isalnum(static_cast<unsigned char>(value[start])) &&
			value[start] != '_') {
			++start;
		}
		size_t end = start;
		while (end < value.size() &&
			(std::isalnum(static_cast<unsigned char>(value[end])) || value[end] == '_')) {
			++end;
		}
		if (end == start) {
			break;
		}
		const std::string word = toLowerCopy(value.substr(start, end - start));
		if (fillerWords.find(word) == fillerWords.end()) {
			break;
		}
		start = end;
	}
	return stripTrailingPunctuation(trimCopy(value.substr(start)));
}

std::string buildCitationPrompt(
	const std::string & topic,
	size_t maxCitations) {
	std::ostringstream prompt;
	prompt
		<< "You are a citation extraction assistant.\n"
		<< "Using only the provided source material, extract up to "
		<< std::max<size_t>(1, maxCitations)
		<< " relevant exact citations about the topic below.\n"
		<< "CRITICAL REQUIREMENTS:\n"
		<< "- Each quote MUST be an exact verbatim span copied word-for-word from one provided source\n"
		<< "- Quotes may be short or longer multi-sentence spans when that better preserves the exact evidence\n"
		<< "- Each citation MUST include a valid sourceIndex (1-based) matching a [Source N] label\n"
		<< "- Do NOT paraphrase, summarize, or rewrite source text inside the quote field\n"
		<< "- Do NOT invent sources, URLs, or quotes\n"
		<< "- If you cannot find an exact quote, omit that citation entirely\n"
		<< "- If the evidence is weak, return fewer citations instead of guessing\n"
		<< "- Prioritize diverse sources over multiple quotes from the same source\n"
		<< "Return valid JSON only in this shape:\n"
		<< "{\"summary\":\"...\",\"citations\":[{\"quote\":\"...\",\"sourceIndex\":1,\"note\":\"...\"}]}\n"
		<< "Use 1-based sourceIndex values that match the provided [Source N] labels.\n\n"
		<< "Topic: " << topic << "\n";
	return prompt.str();
}

int parseCitationSourceIndex(const ofJson & value) {
	try {
		if (value.is_number()) {
			return value.get<int>();
		}
		if (value.is_string()) {
			const std::string text = trimCopy(value.get<std::string>());
			if (text.empty()) {
				return -1;
			}
			for (unsigned char ch : text) {
				if (!std::isdigit(ch)) {
					return -1;
				}
			}
			const unsigned long parsed = std::stoul(text);
			return parsed <= static_cast<unsigned long>(std::numeric_limits<int>::max())
				? static_cast<int>(parsed)
				: -1;
		}
	} catch (...) {
	}
	return -1;
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

std::vector<std::string> dedupeStrings(const std::vector<std::string> & values) {
	std::vector<std::string> out;
	std::unordered_set<std::string> seen;
	for (const auto & value : values) {
		const std::string trimmed = trimCopy(value);
		if (trimmed.empty()) {
			continue;
		}
		const std::string normalized = toLowerCopy(trimmed);
		if (!seen.insert(normalized).second) {
			continue;
		}
		out.push_back(trimmed);
	}
	return out;
}

std::vector<std::string> tokenizeLongTerms(const std::string & text) {
	std::vector<std::string> terms;
	std::unordered_set<std::string> seen;
	std::string current;
	for (char c : text) {
		const unsigned char uc = static_cast<unsigned char>(c);
		if (std::isalnum(uc) || c == '-' || c == '_') {
			current.push_back(static_cast<char>(std::tolower(uc)));
		} else if (!current.empty()) {
			if (current.size() >= 4 && seen.insert(current).second) {
				terms.push_back(current);
			}
			current.clear();
		}
	}
	if (!current.empty() && current.size() >= 4 && seen.insert(current).second) {
		terms.push_back(current);
	}
	return terms;
}

std::vector<std::string> buildHeuristicAlternateQueries(
	const std::string & topic,
	size_t maxAlternateQueries) {
	std::vector<std::string> queries;
	if (maxAlternateQueries == 0) {
		return queries;
	}
	const auto terms = tokenizeLongTerms(topic);
	if (!terms.empty()) {
		std::ostringstream focused;
		for (size_t i = 0; i < terms.size(); ++i) {
			if (i > 0) {
				focused << ' ';
			}
			focused << terms[i];
		}
		queries.push_back(focused.str());
	}
	if (queries.size() < maxAlternateQueries && terms.size() >= 2) {
		queries.push_back(terms.front() + " " + terms.back());
	}
	if (queries.size() > maxAlternateQueries) {
		queries.resize(maxAlternateQueries);
	}
	return dedupeStrings(queries);
}

std::vector<std::string> extractHeuristicDomainHints(
	const std::string & topic,
	bool allowDomainHints) {
	std::vector<std::string> hints;
	if (!allowDomainHints) {
		return hints;
	}
	const std::string lowered = toLowerCopy(topic);
	if (lowered.find("wikipedia") != std::string::npos) {
		hints.push_back("wikipedia.org");
	}
	if (lowered.find("github") != std::string::npos) {
		hints.push_back("github.com");
	}
	if (lowered.find("api") != std::string::npos || lowered.find("docs") != std::string::npos) {
		hints.push_back("official docs");
	}
	if (lowered.find("official") != std::string::npos) {
		hints.push_back("official sources");
	}
	return dedupeStrings(hints);
}

std::string buildQueryRewritePrompt(const std::string & topic) {
	std::ostringstream prompt;
	prompt
		<< "Rewrite the research topic below for better grounded web retrieval.\n"
		<< "Return JSON only with this shape:\n"
		<< "{\"topic\":\"clean topic\",\"alternateQueries\":[\"...\"],\"domainHints\":[\"...\"]}\n"
		<< "Rules:\n"
		<< "- Keep the same intent\n"
		<< "- Make `topic` concise and specific\n"
		<< "- Return at most 2 alternateQueries\n"
		<< "- Return only high-signal domainHints like wikipedia.org, github.com, or official docs\n"
		<< "- Do not include commentary\n\n"
		<< "Topic: " << topic << "\n";
	return prompt.str();
}

ofxGgmlCitationSearchResult::QueryRewriteResult rewriteTopicForSearch(
	const ofxGgmlInference & inference,
	const std::string & modelPath,
	const std::string & topic,
	const ofxGgmlInferenceSettings & inferenceSettings,
	const ofxGgmlCitationSearchRequest::QueryRewriteSettings & settings) {
	ofxGgmlCitationSearchResult::QueryRewriteResult result;
	result.originalTopic = topic;
	result.rewrittenTopic = stripCitationLeadIn(topic);
	if (result.rewrittenTopic.empty()) {
		result.rewrittenTopic = topic;
	}

	if (!settings.enabled) {
		result.queriesUsed = {result.rewrittenTopic};
		return result;
	}

	if (settings.useInference && !modelPath.empty()) {
		const auto rewriteInference = inference.generate(
			modelPath,
			buildQueryRewritePrompt(topic),
			inferenceSettings);
		result.rawResponse = rewriteInference.text;
		if (rewriteInference.success) {
			const ofJson parsed = parseLooseJson(rewriteInference.text);
			if (parsed.is_object()) {
				const std::string parsedTopic = trimCopy(parsed.value("topic", std::string()));
				if (!parsedTopic.empty()) {
					result.rewrittenTopic = parsedTopic;
					result.applied = true;
					result.usedInference = true;
				}
				if (parsed.contains("alternateQueries") && parsed["alternateQueries"].is_array()) {
					for (const auto & value : parsed["alternateQueries"]) {
						if (value.is_string()) {
							result.alternateQueries.push_back(value.get<std::string>());
						}
					}
				}
				if (parsed.contains("domainHints") && parsed["domainHints"].is_array()) {
					for (const auto & value : parsed["domainHints"]) {
						if (value.is_string()) {
							result.domainHints.push_back(value.get<std::string>());
						}
					}
				}
			} else {
				result.error = "Query rewrite did not return valid JSON.";
			}
		} else if (!rewriteInference.error.empty()) {
			result.error = rewriteInference.error;
		}
	}

	if (settings.allowFallbackHeuristics) {
		if (result.rewrittenTopic.empty()) {
			result.rewrittenTopic = stripCitationLeadIn(topic);
		}
		const auto heuristicQueries =
			buildHeuristicAlternateQueries(result.rewrittenTopic, settings.maxAlternateQueries);
		result.alternateQueries.insert(
			result.alternateQueries.end(),
			heuristicQueries.begin(),
			heuristicQueries.end());
		const auto heuristicHints =
			extractHeuristicDomainHints(result.rewrittenTopic, settings.allowDomainHints);
		result.domainHints.insert(
			result.domainHints.end(),
			heuristicHints.begin(),
			heuristicHints.end());
		result.usedFallback =
			!heuristicQueries.empty() || !heuristicHints.empty() || !result.rewrittenTopic.empty();
	}

	result.alternateQueries = dedupeStrings(result.alternateQueries);
	if (result.alternateQueries.size() > settings.maxAlternateQueries) {
		result.alternateQueries.resize(settings.maxAlternateQueries);
	}
	result.domainHints = dedupeStrings(result.domainHints);
	result.queriesUsed.push_back(result.rewrittenTopic.empty() ? topic : result.rewrittenTopic);
	result.queriesUsed.insert(
		result.queriesUsed.end(),
		result.alternateQueries.begin(),
		result.alternateQueries.end());
	result.queriesUsed = dedupeStrings(result.queriesUsed);
	return result;
}

bool isLikelyYamlFrontMatter(const std::vector<std::string> & lines) {
	if (lines.empty() || trimCopy(lines.front()) != "---") {
		return false;
	}

	bool foundMetadataField = false;
	for (size_t i = 1; i < lines.size(); ++i) {
		const std::string trimmed = trimCopy(lines[i]);
		if (trimmed.empty()) {
			continue;
		}
		if (trimmed == "---") {
			return foundMetadataField;
		}
		const size_t colonPos = trimmed.find(':');
		if (colonPos == std::string::npos || colonPos == 0) {
			return false;
		}
		foundMetadataField = true;
	}
	return false;
}

} // namespace

std::string ofxGgmlCitationSearchInternal::cleanCrawlerMarkdownForCitations(
	const std::string & rawMarkdown) {
	std::vector<std::string> lines;
	std::istringstream lineReader(rawMarkdown);
	std::string line;
	while (std::getline(lineReader, line)) {
		lines.push_back(line);
	}

	std::ostringstream cleaned;
	bool inFrontMatter = isLikelyYamlFrontMatter(lines);
	bool frontMatterConsumed = !inFrontMatter;
	bool seenFrontMatterOpening = !inFrontMatter;
	bool previousWasBlank = true;
	bool sawBody = false;

	const std::unordered_set<std::string> stopHeadings = {
		"see also",
		"references",
		"notes",
		"citations",
		"bibliography",
		"further reading",
		"external links",
		"navigation menu"
	};

	auto normalizeHeading = [](std::string heading) {
		heading = trimCopy(heading);
		while (!heading.empty() && heading.front() == '#') {
			heading.erase(heading.begin());
		}
		return normalizeForExactQuoteMatch(heading);
	};

	auto looksLikeBoilerplate = [](const std::string & trimmed) {
		if (trimmed.empty()) {
			return false;
		}
		const std::string normalized = normalizeForExactQuoteMatch(trimmed);
		if (normalized.empty()) {
			return true;
		}
		if (normalized == "contents" ||
			normalized == "toc" ||
			normalized == "menu" ||
			normalized == "navigation" ||
			normalized == "jump to content") {
			return true;
		}
		if (trimmed.size() <= 3) {
			return true;
		}
		size_t alphaCount = 0;
		for (char c : trimmed) {
			if (std::isalpha(static_cast<unsigned char>(c))) {
				++alphaCount;
			}
		}
		return alphaCount > 0 && alphaCount <= 2;
	};

	for (const auto & rawLine : lines) {
		const std::string trimmed = trimCopy(rawLine);
		if (inFrontMatter) {
			if (!seenFrontMatterOpening) {
				seenFrontMatterOpening = true;
				continue;
			}
			if (trimmed == "---") {
				inFrontMatter = false;
				frontMatterConsumed = true;
			}
			continue;
		}
		if (!frontMatterConsumed) {
			frontMatterConsumed = true;
		}
		if (!trimmed.empty() && trimmed.front() == '#') {
			const std::string normalizedHeading = normalizeHeading(trimmed);
			if (stopHeadings.find(normalizedHeading) != stopHeadings.end()) {
				break;
			}
		}
		if (looksLikeBoilerplate(trimmed)) {
			continue;
		}
		if (trimmed.empty()) {
			if (!previousWasBlank && sawBody) {
				cleaned << "\n\n";
			}
			previousWasBlank = true;
			continue;
		}

		if (!previousWasBlank) {
			cleaned << '\n';
		}
		cleaned << trimmed;
		previousWasBlank = false;
		sawBody = true;
	}

	const std::string cleanedMarkdown = trimCopy(cleaned.str());
	return cleanedMarkdown.empty() ? trimCopy(rawMarkdown) : cleanedMarkdown;
}

namespace {

std::vector<ofxGgmlPromptSource> convertCrawlerDocsToSources(
	const ofxGgmlWebCrawlerResult & crawlerResult) {

	std::vector<ofxGgmlPromptSource> sources;
	sources.reserve(crawlerResult.documents.size());
	std::unordered_set<std::string> seen;
	for (const auto & document : crawlerResult.documents) {
		const std::string markdown =
			ofxGgmlCitationSearchInternal::cleanCrawlerMarkdownForCitations(
				document.markdown);
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

std::vector<ofxGgmlPromptSource> mergeUniqueSources(
	const std::vector<ofxGgmlPromptSource> & sources,
	size_t maxSources) {
	std::vector<ofxGgmlPromptSource> merged;
	std::unordered_set<std::string> seen;
	for (const auto & source : sources) {
		const std::string key = trimCopy(source.uri) + "\n---\n" + trimCopy(source.content);
		if (key == "\n---\n" || !seen.insert(key).second) {
			continue;
		}
		merged.push_back(source);
		if (merged.size() >= maxSources) {
			break;
		}
	}
	return merged;
}

std::vector<ofxGgmlPromptSource> fetchRealtimeSourcesForQueries(
	const std::vector<std::string> & queries,
	const ofxGgmlRealtimeInfoSettings & settings) {
	std::vector<ofxGgmlPromptSource> collected;
	for (const auto & query : dedupeStrings(queries)) {
		auto sources = ofxGgmlInference::fetchRealtimeSources(query, settings);
		collected.insert(collected.end(), sources.begin(), sources.end());
		if (collected.size() >= settings.maxSources) {
			break;
		}
	}
	return mergeUniqueSources(collected, settings.maxSources);
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

std::vector<std::string> tokenizeTopicTerms(const std::string & topic) {
	std::vector<std::string> terms;
	std::unordered_set<std::string> seen;
	std::string current;
	for (char c : topic) {
		const unsigned char uc = static_cast<unsigned char>(c);
		if (std::isalnum(uc) || c == '-' || c == '_') {
			current.push_back(static_cast<char>(std::tolower(uc)));
		} else if (!current.empty()) {
			if (current.size() >= 3 && seen.insert(current).second) {
				terms.push_back(current);
			}
			current.clear();
		}
	}
	if (!current.empty() && current.size() >= 3 && seen.insert(current).second) {
		terms.push_back(current);
	}
	return terms;
}

std::vector<std::string> splitIntoExactQuoteCandidates(const std::string & content) {
	std::vector<std::string> candidates;
	const std::string trimmedContent = trimCopy(content);
	if (trimmedContent.empty()) {
		return candidates;
	}

	auto appendCandidate = [&candidates](std::string candidate) {
		candidate = trimCopy(candidate);
		if (candidate.size() < 24 || candidate.size() > 1100) {
			return;
		}
		candidates.push_back(std::move(candidate));
	};

	{
		std::istringstream paragraphStream(trimmedContent);
		std::ostringstream paragraph;
		std::string line;
		auto flushParagraph = [&]() {
			const std::string text = trimCopy(paragraph.str());
			if (!text.empty()) {
				appendCandidate(text);
			}
			paragraph.str("");
			paragraph.clear();
		};

		while (std::getline(paragraphStream, line)) {
			const std::string trimmedLine = trimCopy(line);
			if (trimmedLine.empty()) {
				flushParagraph();
				continue;
			}
			if (!paragraph.str().empty()) {
				paragraph << ' ';
			}
			paragraph << trimmedLine;
		}
		flushParagraph();
	}

	std::vector<std::string> sentenceCandidates;
	size_t start = 0;
	auto pushRange = [&](size_t endExclusive) {
		if (endExclusive <= start) {
			return;
		}
		std::string candidate = trimCopy(content.substr(start, endExclusive - start));
		start = endExclusive;
		if (candidate.size() < 24 || candidate.size() > 900) {
			return;
		}
		sentenceCandidates.push_back(std::move(candidate));
	};

	for (size_t i = 0; i < content.size(); ++i) {
		const char c = content[i];
		if ((c == '.' || c == '!' || c == '?' || c == '\n') &&
			(i + 1 == content.size() || std::isspace(static_cast<unsigned char>(content[i + 1])))) {
			pushRange(i + 1);
		}
	}
	pushRange(content.size());

	for (const auto & sentence : sentenceCandidates) {
		appendCandidate(sentence);
	}
	for (size_t i = 0; i + 1 < sentenceCandidates.size(); ++i) {
		appendCandidate(sentenceCandidates[i] + " " + sentenceCandidates[i + 1]);
	}
	for (size_t i = 0; i + 2 < sentenceCandidates.size(); ++i) {
		appendCandidate(
			sentenceCandidates[i] + " " +
			sentenceCandidates[i + 1] + " " +
			sentenceCandidates[i + 2]);
	}

	std::unordered_set<std::string> seen;
	std::vector<std::string> deduped;
	deduped.reserve(candidates.size());
	for (auto & candidate : candidates) {
		const std::string normalized = normalizeForExactQuoteMatch(candidate);
		if (normalized.empty() || !seen.insert(normalized).second) {
			continue;
		}
		deduped.push_back(std::move(candidate));
	}
	return deduped;
}

float scoreExactQuoteCandidate(
	const std::string & candidate,
	const std::string & sourceLabel,
	const std::string & sourceUri,
	const std::string & normalizedTopic,
	const std::vector<std::string> & topicTerms) {
	const std::string normalizedCandidate = normalizeForExactQuoteMatch(candidate);
	if (normalizedCandidate.empty()) {
		return 0.0f;
	}
	const std::string normalizedLabel = normalizeForExactQuoteMatch(sourceLabel);
	const std::string normalizedUri = normalizeForExactQuoteMatch(sourceUri);

	float score = 0.0f;
	if (!normalizedTopic.empty() &&
		normalizedCandidate.find(normalizedTopic) != std::string::npos) {
		score += 5.0f;
	}

	for (const auto & term : topicTerms) {
		if (normalizedCandidate.find(term) != std::string::npos) {
			score += 1.0f;
		}
		if (!normalizedLabel.empty() &&
			normalizedLabel.find(term) != std::string::npos) {
			score += 0.75f;
		}
		if (!normalizedUri.empty() &&
			normalizedUri.find(term) != std::string::npos) {
			score += 0.35f;
		}
	}

	if (score == 0.0f &&
		(!normalizedLabel.empty() || !normalizedUri.empty())) {
		for (const auto & term : topicTerms) {
			if ((!normalizedLabel.empty() &&
				 normalizedLabel.find(term) != std::string::npos) ||
				(!normalizedUri.empty() &&
				 normalizedUri.find(term) != std::string::npos)) {
				score += 0.25f;
			}
		}
	}

	if (normalizedCandidate.size() >= 40 && normalizedCandidate.size() <= 420) {
		score += 0.5f;
	}
	if (candidate.find('\n') != std::string::npos) {
		score += 0.2f;
	}
	if (candidate.find('"') != std::string::npos) {
		score += 0.15f;
	}
	return score;
}

std::vector<ofxGgmlCitationItem> fallbackExtractExactCitations(
	const std::string & topic,
	const std::vector<ofxGgmlPromptSource> & sources,
	size_t maxCitations) {
	struct RankedCandidate {
		float score = 0.0f;
		ofxGgmlCitationItem item;
	};

	const std::string normalizedTopic = normalizeForExactQuoteMatch(topic);
	const auto topicTerms = tokenizeTopicTerms(topic);
	std::vector<RankedCandidate> ranked;
	const float minimumScore = sources.size() <= 1 ? 0.2f : (sources.size() == 2 ? 0.5f : 1.0f);

	for (size_t sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex) {
		const auto & source = sources[sourceIndex];
		for (const auto & candidate : splitIntoExactQuoteCandidates(source.content)) {
			const float score =
				scoreExactQuoteCandidate(
					candidate,
					source.label,
					source.uri,
					normalizedTopic,
					topicTerms);
			if (score < minimumScore) {
				continue;
			}
			RankedCandidate rankedCandidate;
			rankedCandidate.score = score;
			rankedCandidate.item.quote = candidate;
			rankedCandidate.item.sourceIndex = static_cast<int>(sourceIndex + 1);
			rankedCandidate.item.sourceLabel =
				buildExactSourceLabel(source.label, source.uri, rankedCandidate.item.sourceIndex);
			rankedCandidate.item.sourceUri = source.uri;
			ranked.push_back(std::move(rankedCandidate));
		}
	}

	std::sort(
		ranked.begin(),
		ranked.end(),
		[](const RankedCandidate & a, const RankedCandidate & b) {
			if (a.score != b.score) {
				return a.score > b.score;
			}
			return a.item.quote.size() < b.item.quote.size();
		});

	std::vector<ofxGgmlCitationItem> citations;
	citations.reserve(std::min(maxCitations, ranked.size()));
	std::unordered_set<std::string> seen;
	for (const auto & candidate : ranked) {
		const std::string key =
			normalizeForExactQuoteMatch(candidate.item.quote) + "\n---\n" + candidate.item.sourceUri;
		if (!seen.insert(key).second) {
			continue;
		}
		citations.push_back(candidate.item);
		if (citations.size() >= maxCitations) {
			break;
		}
	}
	return citations;
}

struct CitationSourceChunk {
	std::string id;
	std::string label;
	std::string uri;
	std::string content;
	size_t originalSourceIndex = 0;
	size_t chunkIndex = 0;
};

std::vector<CitationSourceChunk> chunkSourcesForRetrieval(
	const std::vector<ofxGgmlPromptSource> & sources,
	size_t chunkSizeChars = 900,
	size_t overlapChars = 180) {
	std::vector<CitationSourceChunk> chunks;
	if (chunkSizeChars < 200) {
		chunkSizeChars = 200;
	}
	overlapChars = std::min(overlapChars, chunkSizeChars / 2);

	for (size_t sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex) {
		const auto & source = sources[sourceIndex];
		const std::string content = trimCopy(source.content);
		if (content.empty()) {
			continue;
		}
		if (content.size() <= chunkSizeChars) {
			chunks.push_back({
				"src-" + std::to_string(sourceIndex + 1) + "-chunk-1",
				appendHostToLabel(source.label, source.uri),
				source.uri,
				content,
				sourceIndex,
				0
			});
			continue;
		}

		size_t start = 0;
		size_t chunkIndex = 0;
		while (start < content.size()) {
			size_t end = std::min(start + chunkSizeChars, content.size());
			if (end < content.size()) {
				const size_t newlineBreak = content.rfind('\n', end);
				const size_t sentenceBreak = content.find_last_of(".!?", end);
				size_t breakPos = std::string::npos;
				if (newlineBreak != std::string::npos && newlineBreak > start + 200) {
					breakPos = newlineBreak + 1;
				}
				if (sentenceBreak != std::string::npos && sentenceBreak > start + 200) {
					breakPos = std::max(breakPos, sentenceBreak + 1);
				}
				if (breakPos != std::string::npos && breakPos < end) {
					end = breakPos;
				}
			}
			const std::string chunkText = trimCopy(content.substr(start, end - start));
			if (!chunkText.empty()) {
				chunks.push_back({
					"src-" + std::to_string(sourceIndex + 1) + "-chunk-" + std::to_string(chunkIndex + 1),
					appendHostToLabel(source.label, source.uri),
					source.uri,
					chunkText,
					sourceIndex,
					chunkIndex
				});
			}
			if (end >= content.size()) {
				break;
			}
			start = (end > overlapChars) ? (end - overlapChars) : end;
			++chunkIndex;
		}
	}
	return chunks;
}

std::vector<ofxGgmlPromptSource> buildSourcesFromChunks(
	const std::vector<CitationSourceChunk> & chunks) {
	std::vector<ofxGgmlPromptSource> sources;
	sources.reserve(chunks.size());
	for (const auto & chunk : chunks) {
		ofxGgmlPromptSource source;
		source.label = chunk.label;
		source.uri = chunk.uri;
		source.content = chunk.content;
		source.isWebSource = true;
		sources.push_back(std::move(source));
	}
	return sources;
}

std::vector<ofxGgmlPromptSource> selectRetrievedSources(
	const ofxGgmlInference & inference,
	const std::string & modelPath,
	const std::string & topic,
	const ofxGgmlCitationSearchResult::QueryRewriteResult & queryRewrite,
	const ofxGgmlInferenceSettings & inferenceSettings,
	const std::vector<ofxGgmlPromptSource> & sources,
	ofxGgmlRAGRetrievalResult * retrievalOut) {
	if (sources.size() <= 3) {
		if (retrievalOut) {
			retrievalOut->success = true;
		}
		return sources;
	}

	ofxGgmlRAGPipeline pipeline;
	for (size_t i = 0; i < sources.size(); ++i) {
		ofxGgmlRAGDocument document;
		document.id = "source-" + std::to_string(i + 1);
		document.content = sources[i].content;
		document.sourceLabel = appendHostToLabel(sources[i].label, sources[i].uri);
		document.sourceUri = sources[i].uri;
		document.byteSize = sources[i].content.size();
		document.qualityHint = sources[i].isWebSource ? 0.1f : 0.0f;
		pipeline.addDocument(document);
	}

	ofxGgmlEmbeddingSettings embeddingSettings;
	embeddingSettings.useServerBackend = inferenceSettings.useServerBackend;
	embeddingSettings.serverUrl = inferenceSettings.serverUrl;
	embeddingSettings.serverModel = inferenceSettings.serverModel;

	ofxGgmlRAGQuery query;
	query.query = topic;
	query.queryVariants = queryRewrite.alternateQueries;
	query.topK = std::max<size_t>(std::min<size_t>(sources.size(), 8), 3);
	query.chunkSize = 900;
	query.chunkOverlap = 180;
	query.includeSourceHeaders = false;
	query.embeddingModelPath = modelPath;
	query.embeddingSettings = embeddingSettings;
	query.rerankTopN = std::max<size_t>(query.topK, 12);
	query.maxRefinementSteps = std::max<size_t>(1, queryRewrite.alternateQueries.size());

	const auto retrieval = pipeline.retrieve(query);
	if (retrievalOut) {
		*retrievalOut = retrieval;
	}
	if (!retrieval.success || retrieval.chunks.empty()) {
		return sources;
	}

	std::vector<ofxGgmlPromptSource> selected;
	selected.reserve(retrieval.chunks.size());
	std::unordered_set<std::string> seen;
	for (const auto & chunk : retrieval.chunks) {
		const std::string key = chunk.sourceUri + "\n---\n" + chunk.text;
		if (!seen.insert(key).second) {
			continue;
		}
		ofxGgmlPromptSource source;
		source.label = chunk.sourceLabel;
		source.uri = chunk.sourceUri;
		source.content = chunk.text;
		source.isWebSource = true;
		selected.push_back(std::move(source));
	}

	return selected.empty() ? sources : selected;
}

std::string resolveSourceLabel(
	const std::vector<ofxGgmlPromptSource> & sources,
	int sourceIndex) {
	if (sourceIndex < 1 || sourceIndex > static_cast<int>(sources.size())) {
		return {};
	}
	const auto & source = sources[static_cast<size_t>(sourceIndex - 1)];
	return buildExactSourceLabel(source.label, source.uri, sourceIndex);
}

std::string resolveSourceUri(
	const std::vector<ofxGgmlPromptSource> & sources,
	int sourceIndex) {
	if (sourceIndex < 1 || sourceIndex > static_cast<int>(sources.size())) {
		return {};
	}
	return sources[static_cast<size_t>(sourceIndex - 1)].uri;
}

std::optional<std::string> findExactQuoteInSource(
	const std::vector<ofxGgmlPromptSource> & sources,
	const ofxGgmlCitationItem & item) {
	if (item.sourceIndex < 1 || item.sourceIndex > static_cast<int>(sources.size())) {
		return std::nullopt;
	}
	const auto & source = sources[static_cast<size_t>(item.sourceIndex - 1)];
	const std::string rawQuote = trimCopy(item.quote);
	if (rawQuote.empty()) {
		return std::nullopt;
	}
	const size_t exactPos = source.content.find(rawQuote);
	if (exactPos != std::string::npos) {
		return source.content.substr(exactPos, rawQuote.size());
	}

	const std::string normalizedQuote = normalizeForExactQuoteMatch(rawQuote);
	if (normalizedQuote.size() < 6) {
		return std::nullopt;
	}
	const auto normalizedSource = buildNormalizedTextMap(source.content);
	if (normalizedSource.normalized.empty() ||
		normalizedSource.originalIndices.size() != normalizedSource.normalized.size()) {
		return std::nullopt;
	}
	const size_t normalizedPos = normalizedSource.normalized.find(normalizedQuote);
	if (normalizedPos == std::string::npos) {
		return std::nullopt;
	}
	const size_t normalizedEnd = normalizedPos + normalizedQuote.size() - 1;
	if (normalizedEnd >= normalizedSource.originalIndices.size()) {
		return std::nullopt;
	}
	size_t originalStart = normalizedSource.originalIndices[normalizedPos];
	size_t originalEnd = normalizedSource.originalIndices[normalizedEnd];
	if (originalEnd < originalStart || originalEnd >= source.content.size()) {
		return std::nullopt;
	}
	while (originalStart < source.content.size() &&
		std::isspace(static_cast<unsigned char>(source.content[originalStart]))) {
		++originalStart;
	}
	while (originalEnd > originalStart &&
		std::isspace(static_cast<unsigned char>(source.content[originalEnd]))) {
		--originalEnd;
	}
	return source.content.substr(originalStart, originalEnd - originalStart + 1);
}

ofxGgmlRealtimeInfoSettings makeRealtimeCitationSettings(
	const ofxGgmlPromptSourceSettings & sourceSettings) {
	ofxGgmlRealtimeInfoSettings settings;
	settings.enabled = true;
	settings.allowPromptUrlFetch = true;
	settings.allowDomainProviders = true;
	settings.allowGenericSearch = true;
	settings.maxSources = std::max<size_t>(sourceSettings.maxSources, 12);
	settings.maxCharsPerSource = sourceSettings.maxCharsPerSource;
	settings.maxTotalChars = sourceSettings.maxTotalChars;
	settings.requestCitations = sourceSettings.requestCitations;
	return settings;
}

} // namespace

std::vector<std::string>
ofxGgmlCitationSearchInternal::extractExactQuoteCandidatesForTesting(
	const std::string & content) {
	return splitIntoExactQuoteCandidates(content);
}

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

ofxGgmlCitationSearchInputMatch ofxGgmlCitationSearch::detectInputIntent(
	const std::string & userInput,
	const ofxGgmlCitationSearchInputSettings & settings) {
	ofxGgmlCitationSearchInputMatch match;
	const std::string trimmedInput = trimCopy(userInput);
	if (trimmedInput.empty()) {
		return match;
	}

	const std::string lowered = toLowerCopy(trimmedInput);
	size_t bestPos = std::string::npos;
	size_t bestLength = 0;
	std::string bestTrigger;
	for (const auto & rawTrigger : settings.triggerWords) {
		const std::string trigger = toLowerCopy(trimCopy(rawTrigger));
		if (trigger.empty()) {
			continue;
		}
		size_t pos = lowered.find(trigger);
		while (pos != std::string::npos) {
			if (hasWordBoundaries(lowered, pos, trigger.size())) {
				if (bestPos == std::string::npos || pos < bestPos ||
					(pos == bestPos && trigger.size() > bestLength)) {
					bestPos = pos;
					bestLength = trigger.size();
					bestTrigger = trigger;
				}
				break;
			}
			pos = lowered.find(trigger, pos + 1);
		}
	}

	if (bestPos == std::string::npos) {
		return match;
	}

	const size_t topicStart = bestPos + bestLength;
	const std::string rawTopic = topicStart < trimmedInput.size()
		? trimmedInput.substr(topicStart)
		: std::string();
	const std::string topic = stripCitationLeadIn(rawTopic);
	if (topic.size() < settings.minTopicLength) {
		return match;
	}

	match.matched = true;
	match.triggerWord = bestTrigger;
	match.topic = topic;
	return match;
}

ofxGgmlCitationSearchResult ofxGgmlCitationSearch::search(
	const ofxGgmlCitationSearchRequest & request) const {
	using Clock = std::chrono::steady_clock;
	const auto start = Clock::now();

	ofxGgmlCitationSearchResult result;
	result.backendName = request.useCrawler ? "Mojo+Inference" : "Inference";
	result.requestedTopic = trimCopy(request.topic);

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

	result.queryRewrite = rewriteTopicForSearch(
		m_inference,
		request.modelPath,
		topic,
		request.inferenceSettings,
		request.queryRewriteSettings);
	const std::string effectiveTopic = trimCopy(
		result.queryRewrite.rewrittenTopic.empty()
			? topic
			: result.queryRewrite.rewrittenTopic);
	result.requestedTopic = effectiveTopic.empty() ? topic : effectiveTopic;

	const std::string prompt = buildCitationPrompt(result.requestedTopic, request.maxCitations);
	ofxGgmlInferenceResult inferenceResult;

	if (request.useCrawler) {
		ofxGgmlWebCrawlerRequest crawlerRequest = request.crawlerRequest;
		crawlerRequest.startUrl =
			resolveTopicAwareCrawlerUrl(crawlerRequest.startUrl, result.requestedTopic);
		result.crawlerResult = m_webCrawler.crawl(crawlerRequest);
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
		sources = selectRetrievedSources(
			m_inference,
			request.modelPath,
			result.requestedTopic,
			result.queryRewrite,
			request.inferenceSettings,
			sources,
			&result.retrieval);
		if (!result.retrieval.queriesUsed.empty()) {
			result.queryRewrite.queriesUsed = result.retrieval.queriesUsed;
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
			auto realtimeSettings = makeRealtimeCitationSettings(request.sourceSettings);
			auto queries = result.queryRewrite.queriesUsed;
			if (queries.empty()) {
				queries.push_back(result.requestedTopic);
			}
			auto sources = fetchRealtimeSourcesForQueries(queries, realtimeSettings);
			if (sources.empty()) {
				result.error = "No usable web sources were found for the citation topic.";
				result.elapsedMs = elapsedMsSince(start);
				return result;
			}
			sources = selectRetrievedSources(
				m_inference,
				request.modelPath,
				result.requestedTopic,
				result.queryRewrite,
				request.inferenceSettings,
				sources,
				&result.retrieval);
			result.queryRewrite.queriesUsed = result.retrieval.queriesUsed.empty()
				? queries
				: result.retrieval.queriesUsed;
			inferenceResult = m_inference.generateWithSources(
				request.modelPath,
				prompt,
				sources,
				request.inferenceSettings,
				request.sourceSettings);
			result.backendName = "Realtime+Inference";
		} else {
			auto sources = ofxGgmlInferenceSourceInternals::fetchUrlSources(
				normalizedUrls,
				request.sourceSettings);
			if (sources.empty()) {
				result.error = "No usable content could be fetched from the provided URLs.";
				result.elapsedMs = elapsedMsSince(start);
				return result;
			}
			sources = selectRetrievedSources(
				m_inference,
				request.modelPath,
				result.requestedTopic,
				result.queryRewrite,
				request.inferenceSettings,
				sources,
				&result.retrieval);
			if (!result.retrieval.queriesUsed.empty()) {
				result.queryRewrite.queriesUsed = result.retrieval.queriesUsed;
			}
			inferenceResult = m_inference.generateWithSources(
				request.modelPath,
				prompt,
				sources,
				request.inferenceSettings,
				request.sourceSettings);
		}
	}

	result.rawResponse = inferenceResult.text;
	result.sourcesUsed = inferenceResult.sourcesUsed;
	if (!inferenceResult.success) {
		result.citations = fallbackExtractExactCitations(
			result.requestedTopic,
			result.sourcesUsed,
			std::max<size_t>(1, request.maxCitations));
		if (!result.citations.empty()) {
			result.success = true;
			result.error.clear();
			result.elapsedMs = elapsedMsSince(start);
			return result;
		}
		result.error = inferenceResult.error.empty()
			? std::string("Inference failed while extracting citations.")
			: inferenceResult.error;
		result.elapsedMs = elapsedMsSince(start);
		return result;
	}

	const ofJson parsed = parseLooseJson(inferenceResult.text);
	if (!parsed.is_object()) {
		result.citations = fallbackExtractExactCitations(
			result.requestedTopic,
			result.sourcesUsed,
			std::max<size_t>(1, request.maxCitations));
		result.success = !result.citations.empty();
		if (!result.success) {
			result.error =
				"Citation extraction returned non-JSON output. Raw response is available.";
		}
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
			item.sourceIndex =
				entry.contains("sourceIndex")
					? parseCitationSourceIndex(entry["sourceIndex"])
					: -1;
			item.sourceLabel = resolveSourceLabel(result.sourcesUsed, item.sourceIndex);
			item.sourceUri = resolveSourceUri(result.sourcesUsed, item.sourceIndex);
			const auto exactQuote = findExactQuoteInSource(result.sourcesUsed, item);
			if (!item.quote.empty() && exactQuote.has_value()) {
				item.quote = *exactQuote;
				parsedItems.push_back(std::move(item));
			}
		}
		result.citations = dedupeCitationItems(std::move(parsedItems));
	}
	if (result.citations.empty()) {
		result.citations = fallbackExtractExactCitations(
			result.requestedTopic,
			result.sourcesUsed,
			std::max<size_t>(1, request.maxCitations));
		if (result.citations.empty()) {
			result.summary.clear();
		}
	}

	result.success = true;
	result.elapsedMs = elapsedMsSince(start);
	return result;
}

ofxGgmlCitationSearchResult ofxGgmlCitationSearch::searchFromInput(
	const std::string & userInput,
	const ofxGgmlCitationSearchRequest & baseRequest,
	const ofxGgmlCitationSearchInputSettings & inputSettings) const {
	const auto match = detectInputIntent(userInput, inputSettings);
	if (!match.matched) {
		ofxGgmlCitationSearchResult result;
		result.error =
			"Input did not contain a recognized citation-search trigger or topic.";
		return result;
	}

	ofxGgmlCitationSearchRequest request = baseRequest;
	request.topic = match.topic;
	ofxGgmlCitationSearchResult result = search(request);
	result.inputTriggerWord = match.triggerWord;
	if (result.requestedTopic.empty()) {
		result.requestedTopic = match.topic;
	}
	return result;
}
