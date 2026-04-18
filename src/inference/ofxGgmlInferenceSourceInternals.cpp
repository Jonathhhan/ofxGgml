#include "ofxGgmlInferenceSourceInternals.h"

#include "support/ofxGgmlScriptSource.h"

#include "ofMain.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace {

constexpr size_t kMaxSourceLabelChars = 96;

std::string trimCopy(const std::string & s) {
	size_t b = 0;
	while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
		++b;
	}
	size_t e = s.size();
	while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
		--e;
	}
	return s.substr(b, e - b);
}

std::string clipTextWithMarker(
	const std::string & text,
	size_t maxChars,
	bool * wasTruncated = nullptr) {
	if (wasTruncated) {
		*wasTruncated = false;
	}
	if (maxChars == 0) {
		if (wasTruncated) {
			*wasTruncated = !text.empty();
		}
		return {};
	}
	if (text.size() <= maxChars) {
		return text;
	}
	if (wasTruncated) {
		*wasTruncated = true;
	}
	static constexpr const char * kMarker = "\n...[truncated]";
	const size_t markerLen = std::char_traits<char>::length(kMarker);
	if (maxChars <= markerLen) {
		return std::string(kMarker, kMarker + maxChars);
	}
	return text.substr(0, maxChars - markerLen) + kMarker;
}

std::string stripHtmlComments(const std::string & html) {
	std::string out;
	out.reserve(html.size());
	size_t pos = 0;
	while (pos < html.size()) {
		const size_t commentStart = html.find("<!--", pos);
		if (commentStart == std::string::npos) {
			out.append(html, pos, std::string::npos);
			break;
		}
		out.append(html, pos, commentStart - pos);
		const size_t commentEnd = html.find("-->", commentStart + 4);
		if (commentEnd == std::string::npos) {
			break;
		}
		pos = commentEnd + 3;
	}
	return out;
}

std::string stripHtmlBlocks(
	const std::string & html,
	const std::string & tagName) {
	if (html.empty() || tagName.empty()) {
		return html;
	}
	std::string lowerHtml = html;
	std::transform(lowerHtml.begin(), lowerHtml.end(), lowerHtml.begin(),
		[](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});

	std::string out;
	out.reserve(html.size());
	const std::string openTag = "<" + tagName;
	const std::string closeTag = "</" + tagName;
	size_t pos = 0;
	while (pos < html.size()) {
		const size_t start = lowerHtml.find(openTag, pos);
		if (start == std::string::npos) {
			out.append(html, pos, std::string::npos);
			break;
		}
		out.append(html, pos, start - pos);
		const size_t close = lowerHtml.find(closeTag, start + openTag.size());
		if (close == std::string::npos) {
			break;
		}
		const size_t closeEnd = lowerHtml.find('>', close + closeTag.size());
		if (closeEnd == std::string::npos) {
			break;
		}
		pos = closeEnd + 1;
	}
	return out;
}

std::string decodeBasicHtmlEntities(const std::string & text) {
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] != '&') {
			out.push_back(text[i]);
			continue;
		}
		if (text.compare(i, 5, "&amp;") == 0) {
			out.push_back('&');
			i += 4;
		} else if (text.compare(i, 4, "&lt;") == 0) {
			out.push_back('<');
			i += 3;
		} else if (text.compare(i, 4, "&gt;") == 0) {
			out.push_back('>');
			i += 3;
		} else if (text.compare(i, 6, "&quot;") == 0) {
			out.push_back('"');
			i += 5;
		} else if (text.compare(i, 5, "&#39;") == 0) {
			out.push_back('\'');
			i += 4;
		} else if (text.compare(i, 6, "&nbsp;") == 0) {
			out.push_back(' ');
			i += 5;
		} else {
			out.push_back(text[i]);
		}
	}
	return out;
}

bool looksLikeHtmlDocument(const std::string & text) {
	if (text.empty()) {
		return false;
	}
	std::string sample = text.substr(0, std::min<size_t>(text.size(), 2048));
	std::transform(sample.begin(), sample.end(), sample.begin(),
		[](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
	return sample.find("<html") != std::string::npos ||
		sample.find("<body") != std::string::npos ||
		sample.find("<article") != std::string::npos ||
		sample.find("<main") != std::string::npos ||
		sample.find("<p") != std::string::npos ||
		sample.find("<div") != std::string::npos ||
		sample.find("<!doctype html") != std::string::npos;
}

std::string extractPlainTextFromHtml(const std::string & html) {
	std::string cleaned = stripHtmlComments(html);
	for (const char * tag : { "script", "style", "svg", "noscript", "head" }) {
		cleaned = stripHtmlBlocks(cleaned, tag);
	}

	std::string text;
	text.reserve(cleaned.size());
	bool inTag = false;
	for (size_t i = 0; i < cleaned.size(); ++i) {
		const char c = cleaned[i];
		if (c == '<') {
			inTag = true;
			size_t j = i + 1;
			while (j < cleaned.size() &&
				std::isspace(static_cast<unsigned char>(cleaned[j]))) {
				++j;
			}
			if (j < cleaned.size()) {
				const char tagLead = static_cast<char>(
					std::tolower(static_cast<unsigned char>(cleaned[j])));
				if (tagLead == 'p' || tagLead == 'b' || tagLead == 'd' ||
					tagLead == 'h' || tagLead == 'l' || tagLead == 'u' ||
					tagLead == 't') {
					text.append("\n\n");
				} else if (tagLead == 's') {
					text.push_back('\n');
				}
			}
			continue;
		}
		if (c == '>') {
			inTag = false;
			continue;
		}
		if (!inTag) {
			text.push_back(c);
		}
	}

	text = decodeBasicHtmlEntities(text);

	std::string collapsed;
	collapsed.reserve(text.size());
	bool lastWasSpace = false;
	int newlineRun = 0;
	for (char c : text) {
		if (c == '\r') {
			continue;
		}
		if (c == '\n') {
			if (newlineRun < 2) {
				collapsed.push_back('\n');
				++newlineRun;
			}
			lastWasSpace = false;
			continue;
		}
		if (std::isspace(static_cast<unsigned char>(c))) {
			if (!lastWasSpace && newlineRun == 0) {
				collapsed.push_back(' ');
				lastWasSpace = true;
			}
			continue;
		}
		newlineRun = 0;
		lastWasSpace = false;
		collapsed.push_back(c);
	}

	return trimCopy(collapsed);
}

std::string normalizeSourceContent(
	const ofxGgmlPromptSource & source,
	const ofxGgmlPromptSourceSettings & settings) {
	std::string content = trimCopy(source.content);
	if (content.empty()) {
		return {};
	}
	if (settings.normalizeWebText && source.isWebSource && looksLikeHtmlDocument(content)) {
		content = extractPlainTextFromHtml(content);
	}
	return trimCopy(content);
}

std::string formatSourceLabel(const ofxGgmlPromptSource & source) {
	std::string label = trimCopy(source.label);
	if (label.empty()) {
		label = trimCopy(source.uri);
	}
	if (label.empty()) {
		label = "Source";
	}
	if (label.size() > kMaxSourceLabelChars) {
		label.resize(kMaxSourceLabelChars - 3);
		label += "...";
	}
	return label;
}

bool isLikelyWebUri(const std::string & uri) {
	return uri.rfind("http://", 0) == 0 || uri.rfind("https://", 0) == 0;
}

std::vector<std::string> extractHttpUrls(const std::string & text) {
	static const std::regex urlRegex(R"(https?://[^\s<>\"]+)", std::regex::icase);
	std::vector<std::string> urls;
	std::unordered_set<std::string> seen;
	for (std::sregex_iterator it(text.begin(), text.end(), urlRegex), end;
		it != end;
		++it) {
		const std::string url = trimCopy(it->str());
		if (!url.empty() && seen.insert(url).second) {
			urls.push_back(url);
		}
	}
	return urls;
}

std::string urlEncode(const std::string & value) {
	std::ostringstream escaped;
	escaped.fill('0');
	escaped << std::hex;
	for (unsigned char c : value) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			escaped << c;
		} else {
			escaped << '%' << std::setw(2) << std::uppercase << int(c);
		}
	}
	return escaped.str();
}

bool looksLikeWeatherQuery(const std::string & text) {
	std::string lowered = text;
	std::transform(lowered.begin(), lowered.end(), lowered.begin(),
		[](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
	return lowered.find("weather") != std::string::npos ||
		lowered.find("temperature") != std::string::npos ||
		lowered.find("forecast") != std::string::npos ||
		lowered.find("rain") != std::string::npos;
}

bool looksLikeNewsQuery(const std::string & text) {
	std::string lowered = text;
	std::transform(lowered.begin(), lowered.end(), lowered.begin(),
		[](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
	return lowered.find("news") != std::string::npos ||
		lowered.find("headline") != std::string::npos ||
		lowered.find("headlines") != std::string::npos ||
		lowered.find("breaking") != std::string::npos ||
		lowered.find("current events") != std::string::npos ||
		lowered.find("latest") != std::string::npos ||
		lowered.find("today") != std::string::npos;
}

std::string detectWeatherLocation(const std::string & text) {
	static const std::regex weatherLocRegex(
		R"(weather\s+(?:in|for)\s+([A-Za-z0-9 ,._-]+))",
		std::regex::icase);
	std::smatch match;
	if (std::regex_search(text, match, weatherLocRegex) && match.size() >= 2) {
		std::string location = trimCopy(match[1].str());
		if (location.size() > 64) {
			location.resize(64);
		}
		return location;
	}
	return "home";
}

ofxGgmlPromptSourceSettings toPromptSourceSettings(
	const ofxGgmlRealtimeInfoSettings & realtimeSettings) {
	ofxGgmlPromptSourceSettings sourceSettings;
	sourceSettings.maxSources = realtimeSettings.maxSources;
	sourceSettings.maxCharsPerSource = realtimeSettings.maxCharsPerSource;
	sourceSettings.maxTotalChars = realtimeSettings.maxTotalChars;
	sourceSettings.requestCitations = realtimeSettings.requestCitations;
	sourceSettings.heading = realtimeSettings.heading;
	return sourceSettings;
}

void appendUniqueUrls(
	std::vector<std::string> & urls,
	const std::vector<std::string> & candidates) {
	std::unordered_set<std::string> seen(urls.begin(), urls.end());
	for (const auto & candidate : candidates) {
		const std::string url = trimCopy(candidate);
		if (!url.empty() && seen.insert(url).second) {
			urls.push_back(url);
		}
	}
}

ofxGgmlPromptSource fetchWeatherSource(
	const std::string & query,
	const ofxGgmlPromptSourceSettings & sourceSettings) {
	ofxGgmlPromptSource source;
	const std::string location = detectWeatherLocation(query);
	const std::string url = "https://wttr.in/" + urlEncode(location) + "?format=3";
	const ofHttpResponse response = ofLoadURL(url);
	if (response.status < 200 || response.status >= 300) {
		return {};
	}

	source.label = "Live weather";
	source.uri = url;
	source.isWebSource = true;
	source.content = clipTextWithMarker(
		trimCopy(response.data.getText()),
		sourceSettings.maxCharsPerSource,
		&source.wasTruncated);
	return source;
}

ofxGgmlPromptSource fetchSearchSnippetSource(
	const std::string & query,
	const ofxGgmlPromptSourceSettings & sourceSettings) {
	ofxGgmlPromptSource source;
	if (trimCopy(query).empty()) {
		return source;
	}

	const std::string url = "https://lite.duckduckgo.com/lite/?q=" + urlEncode(query);
	const ofHttpResponse response = ofLoadURL(url);
	if (response.status < 200 || response.status >= 300) {
		return source;
	}

	static const std::regex snippetRe(
		R"(<td[^>]*class=\"result-snippet\"[^>]*>([\s\S]*?)</td>)",
		std::regex::icase);
	std::smatch match;
	const std::string body = response.data.getText();
	if (!std::regex_search(body, match, snippetRe) || match.size() < 2) {
		return source;
	}

	source.label = "Web search snippet";
	source.uri = url;
	source.isWebSource = true;
	source.content = trimCopy(match[1].str());
	source.content = normalizeSourceContent(source, sourceSettings);
	source.content = clipTextWithMarker(
		source.content,
		sourceSettings.maxCharsPerSource,
		&source.wasTruncated);
	return source;
}

std::string cleanRssText(const std::string & text) {
	std::string cleaned = trimCopy(text);
	if (cleaned.rfind("<![CDATA[", 0) == 0) {
		const size_t end = cleaned.rfind("]]>");
		if (end != std::string::npos && end > 9) {
			cleaned = cleaned.substr(9, end - 9);
		}
	}
	cleaned = std::regex_replace(cleaned, std::regex(R"(<[^>]+>)"), "");
	return trimCopy(decodeBasicHtmlEntities(cleaned));
}

ofxGgmlPromptSource fetchNewsSource(
	const std::string & query,
	const ofxGgmlPromptSourceSettings & sourceSettings) {
	ofxGgmlPromptSource source;
	const std::string trimmedQuery = trimCopy(query);
	if (trimmedQuery.empty()) {
		return source;
	}

	const std::string url =
		"https://news.google.com/rss/search?q=" + urlEncode(trimmedQuery) +
		"&hl=en-US&gl=US&ceid=US:en";
	const ofHttpResponse response = ofLoadURL(url);
	if (response.status < 200 || response.status >= 300) {
		return source;
	}

	static const std::regex itemRe(R"(<item\b[^>]*>([\s\S]*?)</item>)", std::regex::icase);
	static const std::regex titleRe(R"(<title\b[^>]*>([\s\S]*?)</title>)", std::regex::icase);
	static const std::regex sourceRe(R"(<source\b[^>]*>([\s\S]*?)</source>)", std::regex::icase);
	static const std::regex pubDateRe(R"(<pubDate\b[^>]*>([\s\S]*?)</pubDate>)", std::regex::icase);

	std::ostringstream content;
	content << "Latest news results for \"" << trimmedQuery << "\":\n";

	size_t itemCount = 0;
	const std::string body = response.data.getText();
	for (std::sregex_iterator it(body.begin(), body.end(), itemRe), end; it != end; ++it) {
		if (itemCount >= 4) {
			break;
		}
		const std::string item = (*it)[1].str();
		std::smatch titleMatch;
		if (!std::regex_search(item, titleMatch, titleRe) || titleMatch.size() < 2) {
			continue;
		}
		const std::string title = cleanRssText(titleMatch[1].str());
		if (title.empty()) {
			continue;
		}

		std::string publisher;
		std::smatch sourceMatch;
		if (std::regex_search(item, sourceMatch, sourceRe) && sourceMatch.size() >= 2) {
			publisher = cleanRssText(sourceMatch[1].str());
		}

		std::string pubDate;
		std::smatch pubDateMatch;
		if (std::regex_search(item, pubDateMatch, pubDateRe) && pubDateMatch.size() >= 2) {
			pubDate = cleanRssText(pubDateMatch[1].str());
		}

		content << "- " << title;
		if (!publisher.empty()) {
			content << " (" << publisher;
			if (!pubDate.empty()) {
				content << ", " << pubDate;
			}
			content << ")";
		} else if (!pubDate.empty()) {
			content << " (" << pubDate << ")";
		}
		content << "\n";
		++itemCount;
	}

	if (itemCount == 0) {
		return source;
	}

	source.label = "Latest news";
	source.uri = url;
	source.isWebSource = true;
	source.content = clipTextWithMarker(
		trimCopy(content.str()),
		sourceSettings.maxCharsPerSource,
		&source.wasTruncated);
	return source;
}

} // namespace

namespace ofxGgmlInferenceSourceInternals {

std::vector<ofxGgmlPromptSource> fetchUrlSources(
	const std::vector<std::string> & urls,
	const ofxGgmlPromptSourceSettings & sourceSettings) {
	std::vector<ofxGgmlPromptSource> sources;
	if (urls.empty() || sourceSettings.maxSources == 0 ||
		sourceSettings.maxCharsPerSource == 0 ||
		sourceSettings.maxTotalChars == 0) {
		return sources;
	}

	sources.reserve(std::min(urls.size(), sourceSettings.maxSources));
	size_t usedChars = 0;
	for (const std::string & url : urls) {
		if (sources.size() >= sourceSettings.maxSources ||
			usedChars >= sourceSettings.maxTotalChars) {
			break;
		}

		const ofHttpResponse response = ofLoadURL(url);
		if (response.status < 200 || response.status >= 300) {
			continue;
		}

		ofxGgmlPromptSource source;
		source.uri = url;
		source.label = url;
		source.isWebSource = true;
		source.content = response.data.getText();
		source.content = normalizeSourceContent(source, sourceSettings);
		if (source.content.empty()) {
			continue;
		}

		const size_t remainingChars = sourceSettings.maxTotalChars - usedChars;
		const size_t sourceLimit = std::min(sourceSettings.maxCharsPerSource, remainingChars);
		source.content = clipTextWithMarker(source.content, sourceLimit, &source.wasTruncated);
		if (source.content.empty()) {
			continue;
		}

		usedChars += source.content.size();
		sources.push_back(std::move(source));
	}

	return sources;
}

std::vector<ofxGgmlPromptSource> collectScriptSourceDocuments(
	ofxGgmlScriptSource & scriptSource,
	const ofxGgmlPromptSourceSettings & sourceSettings) {
	std::vector<ofxGgmlPromptSource> sources;
	if (sourceSettings.maxSources == 0 ||
		sourceSettings.maxCharsPerSource == 0 ||
		sourceSettings.maxTotalChars == 0) {
		return sources;
	}

	const auto entries = scriptSource.getFiles();
	if (entries.empty()) {
		return sources;
	}

	sources.reserve(std::min(entries.size(), sourceSettings.maxSources));
	size_t usedChars = 0;
	for (size_t i = 0; i < entries.size(); ++i) {
		if (sources.size() >= sourceSettings.maxSources ||
			usedChars >= sourceSettings.maxTotalChars) {
			break;
		}

		const auto & entry = entries[i];
		if (entry.isDirectory) {
			continue;
		}

		std::string content;
		if (!scriptSource.loadFileContent(static_cast<int>(i), content)) {
			continue;
		}

		ofxGgmlPromptSource source;
		source.label = entry.name;
		source.uri = entry.fullPath;
		source.isWebSource = isLikelyWebUri(entry.fullPath);
		source.content = std::move(content);
		source.content = normalizeSourceContent(source, sourceSettings);
		if (source.content.empty()) {
			continue;
		}

		const size_t remainingChars = sourceSettings.maxTotalChars - usedChars;
		const size_t sourceLimit = std::min(sourceSettings.maxCharsPerSource, remainingChars);
		source.content = clipTextWithMarker(source.content, sourceLimit, &source.wasTruncated);
		if (source.content.empty()) {
			continue;
		}

		usedChars += source.content.size();
		sources.push_back(std::move(source));
	}

	return sources;
}

std::string buildPromptWithSources(
	const std::string & prompt,
	const std::vector<ofxGgmlPromptSource> & sources,
	const ofxGgmlPromptSourceSettings & sourceSettings,
	std::vector<ofxGgmlPromptSource> * usedSources) {
	if (usedSources) {
		usedSources->clear();
	}
	if (sources.empty() || sourceSettings.maxSources == 0 ||
		sourceSettings.maxCharsPerSource == 0 ||
		sourceSettings.maxTotalChars == 0) {
		return prompt;
	}

	std::ostringstream ctx;
	std::vector<ofxGgmlPromptSource> normalizedSources;
	normalizedSources.reserve(std::min(sources.size(), sourceSettings.maxSources));
	size_t usedChars = 0;

	for (const auto & inputSource : sources) {
		if (normalizedSources.size() >= sourceSettings.maxSources ||
			usedChars >= sourceSettings.maxTotalChars) {
			break;
		}

		ofxGgmlPromptSource source = inputSource;
		source.content = normalizeSourceContent(source, sourceSettings);
		if (source.content.empty()) {
			continue;
		}

		const size_t remainingChars = sourceSettings.maxTotalChars - usedChars;
		const size_t sourceLimit = std::min(sourceSettings.maxCharsPerSource, remainingChars);
		source.content = clipTextWithMarker(source.content, sourceLimit, &source.wasTruncated);
		if (source.content.empty()) {
			continue;
		}

		usedChars += source.content.size();
		normalizedSources.push_back(std::move(source));
	}

	if (normalizedSources.empty()) {
		return prompt;
	}

	ctx << prompt << "\n\n";
	ctx << sourceSettings.heading << ":\n";
	ctx << "Use these sources as supporting context. Prefer the sources over guesses.\n";
	if (sourceSettings.requestCitations) {
		ctx << sourceSettings.citationHint << "\n";
	}

	for (size_t i = 0; i < normalizedSources.size(); ++i) {
		const auto & source = normalizedSources[i];
		ctx << "\n[Source " << (i + 1) << "]";
		if (sourceSettings.includeSourceHeaders) {
			ctx << " " << formatSourceLabel(source);
			if (!trimCopy(source.uri).empty() && trimCopy(source.uri) != formatSourceLabel(source)) {
				ctx << "\nURI: " << trimCopy(source.uri);
			}
		}
		ctx << "\n" << source.content << "\n";
	}

	if (usedSources) {
		*usedSources = std::move(normalizedSources);
	}
	return ctx.str();
}

std::vector<ofxGgmlPromptSource> fetchRealtimeSources(
	const std::string & queryOrPrompt,
	const ofxGgmlRealtimeInfoSettings & realtimeSettings) {
	std::vector<ofxGgmlPromptSource> sources;
	if ((!realtimeSettings.enabled && realtimeSettings.explicitUrls.empty()) ||
		realtimeSettings.maxSources == 0 ||
		realtimeSettings.maxCharsPerSource == 0 ||
		realtimeSettings.maxTotalChars == 0) {
		return sources;
	}

	const std::string query = trimCopy(
		realtimeSettings.queryOverride.empty()
			? queryOrPrompt
			: realtimeSettings.queryOverride);
	const ofxGgmlPromptSourceSettings sourceSettings =
		toPromptSourceSettings(realtimeSettings);

	std::vector<std::string> urls = realtimeSettings.explicitUrls;
	if (realtimeSettings.allowPromptUrlFetch) {
		appendUniqueUrls(urls, extractHttpUrls(queryOrPrompt));
	}

	sources = fetchUrlSources(urls, sourceSettings);
	if (!sources.empty() || !realtimeSettings.enabled) {
		return sources;
	}

	if (realtimeSettings.allowDomainProviders && looksLikeWeatherQuery(query)) {
		ofxGgmlPromptSource weather = fetchWeatherSource(query, sourceSettings);
		if (!trimCopy(weather.content).empty()) {
			sources.push_back(std::move(weather));
			return sources;
		}
	}

	if (realtimeSettings.allowDomainProviders && looksLikeNewsQuery(query)) {
		ofxGgmlPromptSource news = fetchNewsSource(query, sourceSettings);
		if (!trimCopy(news.content).empty()) {
			sources.push_back(std::move(news));
			return sources;
		}
	}

	if (realtimeSettings.allowGenericSearch) {
		ofxGgmlPromptSource snippet = fetchSearchSnippetSource(query, sourceSettings);
		if (!trimCopy(snippet.content).empty()) {
			sources.push_back(std::move(snippet));
		}
	}

	return sources;
}

std::string buildPromptWithRealtimeInfo(
	const std::string & prompt,
	const std::string & queryOrPrompt,
	const ofxGgmlRealtimeInfoSettings & realtimeSettings,
	std::vector<ofxGgmlPromptSource> * usedSources) {
	const auto sources = fetchRealtimeSources(queryOrPrompt, realtimeSettings);
	if (sources.empty()) {
		if (usedSources) {
			usedSources->clear();
		}
		return prompt;
	}
	return buildPromptWithSources(
		prompt,
		sources,
		toPromptSourceSettings(realtimeSettings),
		usedSources);
}

} // namespace ofxGgmlInferenceSourceInternals
