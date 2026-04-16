#include "ofxGgmlCodeReview.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unordered_set>
#include <unordered_map>

namespace {

std::string trimCopy(const std::string & s) {
	size_t start = 0;
	while (start < s.size() &&
		std::isspace(static_cast<unsigned char>(s[start]))) {
		++start;
	}
	size_t end = s.size();
	while (end > start &&
		std::isspace(static_cast<unsigned char>(s[end - 1]))) {
		--end;
	}
	return s.substr(start, end - start);
}

size_t countLines(const std::string & text) {
	if (text.empty()) return 0;
	return static_cast<size_t>(std::count(text.begin(), text.end(), '\n') + 1);
}

size_t estimateCyclomaticComplexity(const std::string & text) {
	static constexpr const char * tokens[] = {
		" if ", " for ", " while ", " case ", "&&", "||", "?", " catch ", " else if ",
		" switch ", " foreach ", " guard ", " when ", " except ", " elif ", " goto "
	};
	size_t score = 1;
	std::string lower = text;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	for (const auto * tok : tokens) {
		const std::string token(tok);
		size_t pos = 0;
		while ((pos = lower.find(token, pos)) != std::string::npos) {
			++score;
			pos += token.size();
		}
	}
	return score;
}

std::vector<std::string> extractDependencies(const std::string & text) {
	std::vector<std::string> deps;
	std::istringstream iss(text);
	std::string line;
	while (std::getline(iss, line)) {
		const std::string trimmed = trimCopy(line);
		if (trimmed.empty()) continue;
		auto addDep = [&](const std::string & dep) {
			if (!dep.empty()) deps.push_back(dep);
		};
		if (trimmed.rfind("#include", 0) == 0) {
			size_t quote = trimmed.find('"');
			if (quote != std::string::npos) {
				size_t end = trimmed.find('"', quote + 1);
				if (end != std::string::npos && end > quote + 1) {
					addDep(trimmed.substr(quote + 1, end - quote - 1));
					continue;
				}
			}
			size_t lt = trimmed.find('<');
			if (lt != std::string::npos) {
				size_t gt = trimmed.find('>', lt + 1);
				if (gt != std::string::npos && gt > lt + 1) {
					addDep(trimmed.substr(lt + 1, gt - lt - 1));
					continue;
				}
			}
		}

		auto lower = trimmed;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		static constexpr const char * prefixes[] = { "import ", "from ", "require(" };
		for (const auto * p : prefixes) {
			const std::string prefix(p);
			if (lower.rfind(prefix, 0) != 0) continue;
			size_t quote = trimmed.find('"');
			if (quote == std::string::npos) quote = trimmed.find('\'');
			if (quote != std::string::npos) {
				size_t end = trimmed.find(trimmed[quote], quote + 1);
				if (end != std::string::npos && end > quote + 1) {
					addDep(trimmed.substr(quote + 1, end - quote - 1));
				}
			} else {
				std::istringstream ls(trimmed.substr(prefix.size()));
				std::string dep;
				if (ls >> dep) addDep(dep);
			}
			break;
		}
	}
	return deps;
}

float importanceFromExtension(const std::string & name) {
	std::string lower = name;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	static constexpr const char * coreExt[] = {
		".cpp", ".c", ".h", ".hpp", ".cc", ".hh", ".cxx", ".hxx",
		".py", ".js", ".ts", ".go", ".rs", ".java", ".kt", ".swift",
		".cs", ".m", ".mm"
	};
	static constexpr const char * docExt[] = {
		".md", ".rst", ".txt"
	};
	static constexpr const char * configExt[] = {
		".json", ".yaml", ".yml", ".toml", ".ini"
	};

	for (const auto * ext : docExt) {
		const size_t len = std::char_traits<char>::length(ext);
		if (lower.size() >= len &&
			lower.compare(lower.size() - len, len, ext) == 0) {
			return 0.3f;
		}
	}
	for (const auto * ext : configExt) {
		const size_t len = std::char_traits<char>::length(ext);
		if (lower.size() >= len &&
			lower.compare(lower.size() - len, len, ext) == 0) {
			return 0.5f;
		}
	}
	for (const auto * ext : coreExt) {
		const size_t len = std::char_traits<char>::length(ext);
		if (lower.size() >= len &&
			lower.compare(lower.size() - len, len, ext) == 0) {
			return 1.2f;
		}
	}
	return 0.6f;
}

std::string slidingWindowText(const std::string & content, size_t maxChars) {
	if (content.size() <= maxChars || maxChars == 0) return content;
	const size_t half = maxChars / 2;
	return content.substr(0, half) + "\n...\n" + content.substr(content.size() - half);
}

float computeRecencyScore(const std::string & fullPath) {
	std::error_code ec;
	const auto timestamp = std::filesystem::last_write_time(fullPath, ec);
	if (ec) {
		return 0.2f;
	}
	const auto now = std::filesystem::file_time_type::clock::now();
	const auto ageHours = std::chrono::duration_cast<std::chrono::hours>(now - timestamp).count();
	const float ageDays = static_cast<float>(ageHours) / 24.0f;
	return 1.0f / (1.0f + (ageDays / 30.0f));
}

std::string toFixedString(float value, int decimals = 2) {
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(decimals) << value;
	return oss.str();
}

std::string toLowerCopy(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return value;
}

std::vector<std::string> tokenizeSearchTerms(const std::string & text) {
	std::vector<std::string> tokens;
	std::string current;
	current.reserve(32);
	auto flush = [&]() {
		if (current.size() >= 3) {
			tokens.push_back(current);
		}
		current.clear();
	};
	for (unsigned char ch : text) {
		if (std::isalnum(ch) || ch == '_' || ch == '-') {
			current.push_back(static_cast<char>(std::tolower(ch)));
		} else {
			flush();
		}
	}
	flush();
	return tokens;
}

bool isSearchStopWord(const std::string & token) {
	static const std::unordered_set<std::string> stopWords = {
		"the", "and", "for", "with", "that", "this", "from", "into", "only", "file",
		"files", "code", "review", "repository", "comprehensive", "focus", "look",
		"looking", "issues", "issue", "bugs", "tests", "test", "architecture",
		"integration", "security", "missing", "coverage", "current", "mode", "use"
	};
	return stopWords.find(token) != stopWords.end();
}

std::vector<std::string> buildQueryTerms(const std::string & query) {
	std::vector<std::string> terms;
	std::unordered_set<std::string> seen;
	for (const auto & token : tokenizeSearchTerms(query)) {
		if (isSearchStopWord(token)) {
			continue;
		}
		if (seen.insert(token).second) {
			terms.push_back(token);
		}
	}
	if (terms.empty()) {
		static const std::array<const char *, 10> genericReviewTerms = {{
			"build", "runtime", "config", "resource", "entry", "main", "app", "state", "dependency", "project"
		}};
		for (const char * token : genericReviewTerms) {
			if (seen.insert(token).second) {
				terms.emplace_back(token);
			}
		}
	}
	return terms;
}

float computeLexicalRelevance(
	const std::vector<std::string> & queryTerms,
	const ofxGgmlCodeReviewFileInfo & file) {
	if (queryTerms.empty()) {
		return 0.0f;
	}
	const std::string lowerName = toLowerCopy(file.name);
	std::string searchCorpus = lowerName;
	searchCorpus += "\n";
	searchCorpus += toLowerCopy(file.truncatedContent.empty() ? file.content : file.truncatedContent);
	searchCorpus += "\n";
	for (const auto & dep : file.dependencies) {
		searchCorpus += toLowerCopy(dep);
		searchCorpus += "\n";
	}

	float score = 0.0f;
	for (const auto & term : queryTerms) {
		if (term.empty()) {
			continue;
		}
		if (lowerName.find(term) != std::string::npos) {
			score += 2.0f;
			continue;
		}
		if (searchCorpus.find(term) != std::string::npos) {
			score += 1.0f;
		}
	}

	return score / static_cast<float>(queryTerms.size() * 2.0f);
}

bool containsAny(const std::string & haystack, std::initializer_list<const char *> needles) {
	for (const auto * needle : needles) {
		if (haystack.find(needle) != std::string::npos) {
			return true;
		}
	}
	return false;
}

bool containsConcreteIssueSignal(const std::string & text) {
	const std::string lower = toLowerCopy(text);
	return containsAny(lower, {
		"bug", "crash", "race", "deadlock", "leak", "unsafe", "invalid", "incorrect",
		"wrong", "mismatch", "fragile", "hardcoded", "cleanup", "stale", "overflow",
		"underflow", "missing check", "missing validation", "null", "nullptr", "out-of-bounds",
		"use-after", "regression", "break", "fails", "failure", "risk:", "risk ", "vulnerability"
	});
}

bool looksLikeLowSignalReviewText(const std::string & text) {
	const std::string lower = toLowerCopy(text);
	if (containsAny(lower, {
		"no material findings in this file",
		"no material architecture findings",
		"no material integration findings"
	})) {
		return false;
	}
	if (containsConcreteIssueSignal(lower)) {
		return false;
	}

	int genericHits = 0;
	if (containsAny(lower, {"this file is", "the provided code snippet", "this file contains"})) ++genericHits;
	if (containsAny(lower, {"fan-in", "fan-out", "recency score", "importance score", "complexity of"})) ++genericHits;
	if (containsAny(lower, {"there are no specific tests", "none specific to this file", "no specific tests provided"})) ++genericHits;
	if (containsAny(lower, {"well-organized", "easy to read", "clear separation of concerns"})) ++genericHits;
	if (containsAny(lower, {"the file contains", "the solution file", "the project references"})) ++genericHits;
	if (containsAny(lower, {"findings:\n- the ", "findings:\n1. the ", "findings: 1. the "} )) ++genericHits;
	if (containsAny(lower, {"tests: - there are no", "tests: - none", "tests: none"})) ++genericHits;

	return genericHits >= 2;
}

bool isNoMaterialFindingsSection(const std::string & text) {
	const std::string lower = toLowerCopy(text);
	return containsAny(lower, {
		"no material findings in this file.",
		"findings: no material findings in this file."
	});
}

bool looksLikeCodeFragmentSummary(const std::string & summary) {
	const std::string trimmed = trimCopy(summary);
	if (trimmed.empty()) return false;
	const std::string lower = toLowerCopy(trimmed);
	if (trimmed.find(';') != std::string::npos) return true;
	if (trimmed.find('{') != std::string::npos || trimmed.find('}') != std::string::npos) return true;
	if (trimmed.find(" = ") != std::string::npos) return true;
	if (trimmed.find(" == ") != std::string::npos) return true;
	if (trimmed.find("nullptr") != std::string::npos) return true;
	if (trimmed.find("->") != std::string::npos) return true;
	if (trimmed.find("::") != std::string::npos) return true;
	if (trimmed.find('(') != std::string::npos && trimmed.find(')') != std::string::npos) return true;
	if (trimmed.find('(') != std::string::npos &&
		(trimmed.back() == ',' || trimmed.find(')') == std::string::npos)) {
		return true;
	}
	if (trimmed.rfind('#', 0) == 0) return true;
	if (containsAny(lower, {
		"case ",
		"default:",
		"switch (",
		"if (",
		"for (",
		"while (",
		"return ",
		"test_case(",
		"require(",
		"check(",
		"section(",
		"int main(",
		"void ",
		"class ",
		"struct "
	})) {
		return true;
	}
	if (trimmed.find(':') != std::string::npos && trimmed.find("//") != std::string::npos) return true;
	return false;
}

bool looksLikeTrivialSummary(const std::string & summary) {
	const std::string trimmed = trimCopy(summary);
	if (trimmed.empty()) return true;
	if (trimmed.rfind("```", 0) == 0) return true;
	if (trimmed.find('\n') != std::string::npos) return true;
	if (looksLikeCodeFragmentSummary(trimmed)) return true;

	const std::string lower = toLowerCopy(trimmed);
	if (containsAny(lower, {
		"```",
		"summary:",
		"findings:",
		"tests:",
		"project file included in the hierarchical review"
	})) {
		return true;
	}

	const size_t slashPos = trimmed.find_last_of("/\\");
	const std::string basename = slashPos == std::string::npos
		? trimmed
		: trimmed.substr(slashPos + 1);
	if (!basename.empty()) {
		const size_t dotPos = basename.find('.');
		if (dotPos != std::string::npos && dotPos > 0 && dotPos < basename.size() - 1) {
			return true;
		}
	}

	return false;
}

std::string fallbackSummaryForFile(const ofxGgmlCodeReviewFileInfo & file) {
	const std::string lowerName = toLowerCopy(file.name);
	if (lowerName.find("main.cpp") != std::string::npos ||
		lowerName.find("main.cxx") != std::string::npos) {
		return "Application entry point that boots the OpenFrameworks app.";
	}
	if (lowerName.find("ofapp.cpp") != std::string::npos) {
		return "Main application implementation containing UI, state management, and runtime behavior.";
	}
	if (lowerName.size() >= 11 && lowerName.compare(lowerName.size() - 11, 11, ".vcxproj") == 0) {
		return "Visual Studio project definition for the example application and its build settings.";
	}
	if (lowerName.size() >= 19 && lowerName.compare(lowerName.size() - 19, 19, ".vcxproj.filters") == 0) {
		return "Visual Studio filters file that organizes source and header files in the IDE.";
	}
	if (lowerName.size() >= 4 && lowerName.compare(lowerName.size() - 4, 4, ".sln") == 0) {
		return "Visual Studio solution file that ties the example project and dependencies together.";
	}
	if (lowerName.size() >= 3 && lowerName.compare(lowerName.size() - 3, 3, ".rc") == 0) {
		return "Windows resource script that selects the application icon for debug and release builds.";
	}
	if (lowerName == "addons.make" || lowerName.size() >= 11 &&
		lowerName.compare(lowerName.size() - 11, 11, "addons.make") == 0) {
		return "Addon manifest listing the OpenFrameworks addons required by the example.";
	}

	const size_t dotPos = lowerName.find_last_of('.');
	if (dotPos != std::string::npos) {
		const std::string ext = lowerName.substr(dotPos);
		if (ext == ".cpp" || ext == ".cc" || ext == ".cxx") {
			return "C++ implementation file in the example project.";
		}
		if (ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx") {
			return "C++ header file in the example project.";
		}
		if (ext == ".json" || ext == ".yaml" || ext == ".yml" || ext == ".toml" || ext == ".ini") {
			return "Configuration file used by the example project.";
		}
	}

	return "Project file included in the hierarchical review.";
}

std::string extractSingleLineSummary(const std::string & text) {
	std::istringstream iss(text);
	std::string line;
	while (std::getline(iss, line)) {
		const std::string trimmed = trimCopy(line);
		if (trimmed.empty()) continue;
		if (trimmed.rfind("Summary:", 0) == 0) {
			const std::string summary = trimCopy(trimmed.substr(std::string("Summary:").size()));
			if (!looksLikeTrivialSummary(summary)) {
				return summary;
			}
			continue;
		}
		if (trimmed[0] != '-' && trimmed[0] != '#' && !looksLikeTrivialSummary(trimmed)) {
			return trimmed;
		}
	}
	return {};
}

std::vector<std::string> extractSectionBullets(
	const std::string & text,
	const std::string & header,
	const std::string & nextHeader = {}) {
	std::vector<std::string> bullets;
	const std::string lower = toLowerCopy(text);
	const std::string headerLower = toLowerCopy(header);
	const size_t headerPos = lower.find(headerLower);
	if (headerPos == std::string::npos) {
		return bullets;
	}

	size_t sectionStart = text.find('\n', headerPos);
	if (sectionStart == std::string::npos) {
		return bullets;
	}
	++sectionStart;

	size_t sectionEnd = text.size();
	if (!nextHeader.empty()) {
		const size_t nextPos = lower.find(toLowerCopy(nextHeader), sectionStart);
		if (nextPos != std::string::npos) {
			sectionEnd = nextPos;
		}
	}

	std::istringstream iss(text.substr(sectionStart, sectionEnd - sectionStart));
	std::string line;
	while (std::getline(iss, line)) {
		const std::string trimmed = trimCopy(line);
		if (trimmed.empty()) continue;
		if (trimmed[0] == '-' || trimmed[0] == '*' ||
			(std::isdigit(static_cast<unsigned char>(trimmed[0])) &&
			 trimmed.size() > 2 && trimmed[1] == '.')) {
			bullets.push_back(trimmed);
		}
	}
	return bullets;
}

std::vector<std::string> extractEvidenceAnchors(const std::string & text) {
	std::vector<std::string> anchors;
	auto addAnchor = [&](std::string anchor) {
		anchor = trimCopy(anchor);
		if (anchor.size() >= 2 &&
			((anchor.front() == '`' && anchor.back() == '`') ||
			 (anchor.front() == '"' && anchor.back() == '"') ||
			 (anchor.front() == '\'' && anchor.back() == '\''))) {
			anchor = anchor.substr(1, anchor.size() - 2);
		}
		anchor = trimCopy(anchor);
		if (anchor.size() >= 2 && anchor.size() <= 120) {
			anchors.push_back(anchor);
		}
	};

	size_t pos = 0;
	while ((pos = text.find("Evidence:", pos)) != std::string::npos) {
		size_t start = pos + std::string("Evidence:").size();
		size_t end = text.find_first_of("\r\n", start);
		addAnchor(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
		pos = start;
	}

	pos = 0;
	while ((pos = text.find('`', pos)) != std::string::npos) {
		const size_t end = text.find('`', pos + 1);
		if (end == std::string::npos) break;
		addAnchor(text.substr(pos, end - pos + 1));
		pos = end + 1;
	}

	return anchors;
}

bool contentContainsAnchor(
	const std::string & content,
	const std::string & fileName,
	const std::string & anchor) {
	if (anchor.empty()) return false;
	if (content.find(anchor) != std::string::npos) return true;
	return fileName.find(anchor) != std::string::npos;
}

bool looksLikeWeakFindingClaim(const std::string & bullet) {
	const std::string lower = toLowerCopy(bullet);
	int weakHits = 0;
	if (containsAny(lower, {
		"could be a security risk",
		"security risk if",
		"user-supplied data",
		"phishing",
		"malicious purposes",
		"might be too low",
		"depending on the requirements",
		"depending on the hardware",
		"could be improved",
		"ensure that this macro is consistently defined",
		"common practice",
		"no specific tests are required"
	})) {
		++weakHits;
	}
	if (containsAny(lower, {
		"could",
		"might",
		"may",
		"consider using",
		"would be beneficial"
	})) {
		++weakHits;
	}
	return weakHits >= 2;
}

bool isStrictEvidenceFile(const ofxGgmlCodeReviewFileInfo & file) {
	const std::string lowerName = toLowerCopy(file.name);
	const auto hasSuffix = [&](const std::string & suffix) {
		return lowerName.size() >= suffix.size() &&
			lowerName.compare(lowerName.size() - suffix.size(), suffix.size(), suffix) == 0;
	};
	if (file.loc > 0 && file.loc <= 40) {
		return true;
	}
	return hasSuffix(".rc") ||
		hasSuffix(".sln") ||
		hasSuffix(".vcxproj") ||
		hasSuffix(".vcxproj.filters") ||
		hasSuffix("addons.make") ||
		hasSuffix(".json") ||
		hasSuffix(".yaml") ||
		hasSuffix(".yml") ||
		hasSuffix(".toml") ||
		hasSuffix(".ini");
}

bool looksLikeWeakTestBullet(const std::string & bullet) {
	const std::string lower = toLowerCopy(bullet);
	if (containsAny(lower, {
		"none beyond current coverage",
		"none specific",
		"no specific tests",
		"no file-specific tests",
		"no tests are required",
		"would catch the listed findings",
		"verify that the file exists",
		"ensure that the build system",
		"inspect the output of the build process",
		"dependency walker",
		"dumpbin"
	})) {
		return true;
	}
	int weakHits = 0;
	if (containsAny(lower, {
		"ensure that",
		"verify that",
		"consider adding",
		"if there are any associated code"
	})) {
		++weakHits;
	}
	if (!containsAny(lower, {
		"test",
		"assert",
		"exercise",
		"cover",
		"startup",
		"load",
		"render",
		"parse"
	})) {
		++weakHits;
	}
	return weakHits >= 2;
}

bool isHighConfidenceFindingBullet(
	const std::string & bullet,
	const ofxGgmlCodeReviewFileInfo & file) {
	const auto anchors = extractEvidenceAnchors(bullet);
	bool supported = false;
	for (const auto & anchor : anchors) {
		if (contentContainsAnchor(file.truncatedContent, file.name, anchor) ||
			contentContainsAnchor(file.content, file.name, anchor)) {
			supported = true;
			break;
		}
	}
	if (!supported) {
		return false;
	}

	const std::string lower = toLowerCopy(bullet);
	if (!containsConcreteIssueSignal(lower)) {
		return false;
	}
	if (looksLikeWeakFindingClaim(lower)) {
		return false;
	}
	if (isStrictEvidenceFile(file) && !containsAny(lower, {
		"leak",
		"race",
		"crash",
		"corrupt",
		"mismatch",
		"missing",
		"invalid",
		"fails",
		"incorrect",
		"wrong",
		"unsafe",
		"stale",
		"broken",
		"duplicate",
		"unreachable",
		"not found",
		"inconsistent"
	})) {
		return false;
	}
	return true;
}

std::string normalizeFirstPassReviewSection(
	const std::string & text,
	const ofxGgmlCodeReviewFileInfo & file) {
	if (looksLikeLowSignalReviewText(text)) {
		const std::string summary = extractSingleLineSummary(text);
		if (!summary.empty()) {
			return "Summary: " + summary +
				"\nFindings: No material findings in this file.\nTests: None beyond current coverage.";
		}
		return "Summary: " + fallbackSummaryForFile(file) +
			"\nFindings: No material findings in this file.\nTests: None beyond current coverage.";
	}

	const std::string summary = extractSingleLineSummary(text);
	const auto rawFindings = extractSectionBullets(text, "Findings:", "Tests:");
	std::vector<std::string> supportedFindings;
	for (const auto & bullet : rawFindings) {
		if (isHighConfidenceFindingBullet(bullet, file)) {
			supportedFindings.push_back(bullet);
		}
	}

	std::string normalized = summary.empty()
		? "Summary: " + fallbackSummaryForFile(file)
		: "Summary: " + summary;

	if (supportedFindings.empty()) {
		normalized += "\nFindings: No material findings in this file.";
		normalized += "\nTests: None beyond current coverage.";
		return normalized;
	}

	normalized += "\nFindings:";
	for (const auto & bullet : supportedFindings) {
		normalized += "\n" + bullet;
	}

	const auto testBullets = extractSectionBullets(text, "Tests:");
	std::vector<std::string> supportedTests;
	for (const auto & bullet : testBullets) {
		if (!looksLikeWeakTestBullet(bullet)) {
			supportedTests.push_back(bullet);
		}
	}
	if (supportedTests.empty()) {
		normalized += "\nTests: Add coverage for the supported findings above.";
	} else {
		normalized += "\nTests:";
		for (const auto & bullet : supportedTests) {
			normalized += "\n" + bullet;
		}
	}
	return normalized;
}

std::string normalizeLowSignalReviewSection(
	const std::string & text,
	const std::string & passLabel) {
	if (!looksLikeLowSignalReviewText(text)) {
		return text;
	}

	if (passLabel == "Architecture review") {
		return "No material architecture findings.";
	}
	if (passLabel == "Integration review") {
		return "No material integration findings.";
	}

	const std::string summary = extractSingleLineSummary(text);
	if (!summary.empty()) {
		return "Summary: " + summary +
			"\nFindings: No material findings in this file.\nTests: None beyond current coverage.";
	}
	return "No material findings in this file.";
}

std::string normalizeAggregateReviewSection(
	const std::string & text,
	const std::string & passLabel) {
	const std::string normalized = normalizeLowSignalReviewSection(text, passLabel);
	const std::string lower = toLowerCopy(normalized);
	if (passLabel == "Architecture review" &&
		lower.find("no material architecture findings") != std::string::npos) {
		return "No material architecture findings.";
	}
	if (passLabel == "Integration review" &&
		lower.find("no material integration findings") != std::string::npos) {
		return "No material integration findings.";
	}
	return normalized;
}

bool reportProgress(
	const std::function<bool(const ofxGgmlCodeReviewProgress &)> & onProgress,
	const std::string & stage,
	size_t completed = 0,
	size_t total = 0) {
	if (!onProgress) return true;
	return onProgress({stage, completed, total});
}

std::string finalizedReviewSection(
	const ofxGgmlInferenceResult & inferenceResult,
	const std::string & passLabel,
	const ofxGgmlCodeReviewFileInfo * fileInfo = nullptr) {
	if (!inferenceResult.success) {
		return "[error] " + inferenceResult.error;
	}
	const std::string text = trimCopy(inferenceResult.text);
	if (!text.empty()) {
		if (fileInfo != nullptr) {
			return normalizeFirstPassReviewSection(text, *fileInfo);
		}
		return normalizeAggregateReviewSection(text, passLabel);
	}
	return "[warning] " + passLabel +
		" returned no findings. The model produced an empty response.";
}

ofxGgmlInferenceResult generateReviewPassWithRetry(
	ofxGgmlInference & inference,
	const ofxGgmlCodeReview::GenerateFallback & fallback,
	const std::string & modelPath,
	const std::string & primaryPrompt,
	const std::string & retryPrompt,
	const ofxGgmlInferenceSettings & settings) {
	auto runPass = [&](const std::string & prompt, const ofxGgmlInferenceSettings & passSettings) {
		auto result = inference.generate(modelPath, prompt, passSettings);
		if ((!result.success || trimCopy(result.text).empty()) && fallback) {
			auto fallbackResult = fallback(modelPath, prompt, passSettings);
			if (fallbackResult.success && !trimCopy(fallbackResult.text).empty()) {
				return fallbackResult;
			}
			if (!result.success && !fallbackResult.error.empty()) {
				return fallbackResult;
			}
		}
		return result;
	};

	auto result = runPass(primaryPrompt, settings);
	if (!result.success || !trimCopy(result.text).empty() || retryPrompt.empty()) {
		return result;
	}

	auto retrySettings = settings;
	retrySettings.maxTokens = std::max(96, settings.maxTokens / 2);
	return runPass(retryPrompt, retrySettings);
}

} // namespace

void ofxGgmlCodeReview::setCompletionExecutable(const std::string & path) {
	m_inference.setCompletionExecutable(path);
}

void ofxGgmlCodeReview::setEmbeddingExecutable(const std::string & path) {
	m_inference.setEmbeddingExecutable(path);
}

void ofxGgmlCodeReview::setGenerationFallback(GenerateFallback fallback) {
	m_generationFallback = std::move(fallback);
}

void ofxGgmlCodeReview::clearGenerationFallback() {
	m_generationFallback = nullptr;
}

ofxGgmlInference & ofxGgmlCodeReview::getInference() {
	return m_inference;
}

const ofxGgmlInference & ofxGgmlCodeReview::getInference() const {
	return m_inference;
}

std::string ofxGgmlCodeReview::defaultReviewQuery() {
	return "Comprehensive repository code review. Focus on bugs, security, architecture, and missing tests.";
}

ofxGgmlCodeReviewResult ofxGgmlCodeReview::reviewScriptSource(
	const std::string & modelPath,
	ofxGgmlScriptSource & scriptSource,
	const std::string & reviewQuery,
	const ofxGgmlCodeReviewSettings & settings,
	std::function<bool(const ofxGgmlCodeReviewProgress &)> onProgress) {
	ofxGgmlCodeReviewResult result;
	if (modelPath.empty() &&
		!settings.useServerBackend &&
		trimCopy(settings.serverUrl).empty()) {
		result.error = "model path is empty";
		return result;
	}

	const std::string effectiveQuery = trimCopy(reviewQuery.empty()
		? defaultReviewQuery()
		: reviewQuery);
	if (!reportProgress(onProgress, "Loading files")) {
		result.error = "cancelled";
		return result;
	}

	const auto entries = scriptSource.getFiles();
	for (size_t i = 0; i < entries.size(); ++i) {
		if (entries[i].isDirectory) continue;
		std::string content;
		if (!scriptSource.loadFileContent(static_cast<int>(i), content)) continue;

		ofxGgmlCodeReviewFileInfo info;
		info.name = entries[i].name;
		info.fullPath = entries[i].fullPath;
		info.content = std::move(content);
		info.truncatedContent = info.content;
		result.files.push_back(std::move(info));
	}
	if (result.files.empty()) {
		result.error = "no source files available for review";
		return result;
	}
	result.status = "Loaded " + std::to_string(result.files.size()) + " files";

	for (auto & file : result.files) {
		file.loc = countLines(file.content);
		file.complexity = estimateCyclomaticComplexity(file.content);
		file.dependencies = extractDependencies(file.content);
		file.dependencyFanOut = file.dependencies.size();
		file.importanceScore = importanceFromExtension(file.name);
		file.recencyScore = computeRecencyScore(file.fullPath);
		file.tokenCount = std::max(1, static_cast<int>(file.content.size() / 4 + 1));
	}

	std::unordered_map<std::string, size_t> fanIn;
	for (const auto & file : result.files) {
		for (const auto & dep : file.dependencies) {
			++fanIn[dep];
			++fanIn[std::filesystem::path(dep).filename().string()];
		}
	}
	for (auto & file : result.files) {
		auto it = fanIn.find(file.name);
		if (it != fanIn.end()) file.dependencyFanIn = std::max(file.dependencyFanIn, it->second);
		const std::string base = std::filesystem::path(file.name).filename().string();
		auto baseIt = fanIn.find(base);
		if (baseIt != fanIn.end()) file.dependencyFanIn = std::max(file.dependencyFanIn, baseIt->second);
	}

	std::vector<std::string> names;
	names.reserve(result.files.size());
	for (const auto & file : result.files) {
		names.push_back(file.name);
	}
	std::sort(names.begin(), names.end());
	result.tableOfContents = "Repository files (table of contents):\n";
	for (size_t i = 0; i < names.size() && i < settings.maxRepoTocFiles; ++i) {
		result.tableOfContents += "  - " + names[i] + "\n";
	}
	if (names.size() > settings.maxRepoTocFiles) {
		result.tableOfContents += "  ... and " +
			std::to_string(names.size() - settings.maxRepoTocFiles) + " more\n";
	}

	result.repoTree = "Repository tree:\n";
	for (const auto & name : names) {
		const size_t depth = std::count(name.begin(), name.end(), '/');
		result.repoTree += std::string(depth * 2, ' ') + "- " + name + "\n";
	}

	if (!reportProgress(onProgress, "Embedding query")) {
		result.error = "cancelled";
		return result;
	}
	std::vector<float> queryEmbedding;
	ofxGgmlEmbeddingSettings embeddingSettings;
	embeddingSettings.useServerBackend = settings.useServerBackend;
	embeddingSettings.serverUrl = settings.serverUrl;
	embeddingSettings.serverModel = settings.serverModel;
	const auto queryEmbed = m_inference.embed(modelPath, effectiveQuery, embeddingSettings);
	if (queryEmbed.success) {
		queryEmbedding = queryEmbed.embedding;
	}
	const auto queryTerms = buildQueryTerms(effectiveQuery);

	if (!reportProgress(onProgress, "Embedding files", 0, result.files.size())) {
		result.error = "cancelled";
		return result;
	}
	const size_t maxEmbedParallel = std::max<size_t>(1, settings.maxEmbedParallelTasks);
	std::atomic<size_t> embeddedCount{0};
	std::vector<std::future<void>> embedTasks;
	for (size_t i = 0; i < result.files.size(); ++i) {
		embedTasks.push_back(std::async(std::launch::async, [&, this, i]() {
			auto & file = result.files[i];
			std::string snippet = file.content;
			if (snippet.size() > settings.maxEmbeddingSnippetChars) {
				snippet = slidingWindowText(snippet, settings.maxEmbeddingSnippetChars);
			}
			const auto embedding = m_inference.embed(modelPath, snippet, embeddingSettings);
			if (embedding.success) {
				file.embedding = embedding.embedding;
			}
			++embeddedCount;
		}));
		if (embedTasks.size() >= maxEmbedParallel) {
			embedTasks.front().get();
			embedTasks.erase(embedTasks.begin());
			if (!reportProgress(onProgress, "Embedding files", embeddedCount.load(), result.files.size())) {
				result.error = "cancelled";
				return result;
			}
		}
	}
	for (auto & task : embedTasks) {
		task.get();
	}

	size_t maxLoc = 0;
	size_t maxComplexity = 0;
	size_t maxFanIn = 0;
	size_t maxFanOut = 0;
	float maxSimilarity = 0.0f;
	float maxLexical = 0.0f;
	for (auto & file : result.files) {
		file.lexicalScore = computeLexicalRelevance(queryTerms, file);
		maxLexical = std::max(maxLexical, file.lexicalScore);
		if (!queryEmbedding.empty() && !file.embedding.empty()) {
			file.similarityScore = ofxGgmlEmbeddingIndex::cosineSimilarity(
				queryEmbedding,
				file.embedding);
			maxSimilarity = std::max(maxSimilarity, file.similarityScore);
		}
		maxLoc = std::max(maxLoc, file.loc);
		maxComplexity = std::max(maxComplexity, file.complexity);
		maxFanIn = std::max(maxFanIn, file.dependencyFanIn);
		maxFanOut = std::max(maxFanOut, file.dependencyFanOut);
	}

	for (size_t i = 0; i < result.files.size(); ++i) {
		auto & file = result.files[i];
		const float normComplexity = maxComplexity > 0
			? static_cast<float>(file.complexity) / static_cast<float>(maxComplexity) : 0.0f;
		const float normLoc = maxLoc > 0
			? static_cast<float>(file.loc) / static_cast<float>(maxLoc) : 0.0f;
		const float normFan = (maxFanIn + maxFanOut) > 0
			? static_cast<float>(file.dependencyFanIn + file.dependencyFanOut) /
				static_cast<float>(maxFanIn + maxFanOut) : 0.0f;
		const float normSim = maxSimilarity > 0.0f
			? file.similarityScore / maxSimilarity : file.similarityScore;
		const float normLexical = maxLexical > 0.0f
			? file.lexicalScore / maxLexical : file.lexicalScore;
		const float relevanceScore = std::max(
			std::clamp(normSim, 0.0f, 1.0f),
			std::clamp(normLexical, 0.0f, 1.0f));

		file.priorityScore =
			0.30f * file.importanceScore +
			0.20f * normComplexity +
			0.15f * normLoc +
			0.15f * normFan +
			0.20f * relevanceScore +
			0.10f * std::clamp(file.recencyScore, 0.0f, 1.5f);
		file.similarityScore = relevanceScore;
	}

	std::vector<size_t> ordered(result.files.size());
	for (size_t i = 0; i < ordered.size(); ++i) {
		ordered[i] = i;
	}
	std::sort(ordered.begin(), ordered.end(), [&](size_t a, size_t b) {
		return result.files[a].priorityScore > result.files[b].priorityScore;
	});

	const int aggregateTokenBudget = std::clamp(std::min(settings.maxTokens, 384), 128, 384);
	const int firstPassTokenCeiling = std::clamp(std::min(settings.maxTokens, 512), 160, 512);
	const int responseReserve = std::max(aggregateTokenBudget, settings.contextSize / 8);
	int remaining = std::max(128, settings.contextSize - responseReserve);
	for (const size_t index : ordered) {
		auto & file = result.files[index];
		int tokens = file.tokenCount > 0 ? file.tokenCount
			: std::max(1, static_cast<int>(file.content.size() / 4 + 1));
		const int maxPerFile = std::max(96, settings.contextSize / 6);
		if (tokens > maxPerFile) {
			file.truncatedContent = slidingWindowText(file.content, static_cast<size_t>(maxPerFile * 4));
			file.truncated = true;
			tokens = std::max(1, static_cast<int>(file.truncatedContent.size() / 4 + 1));
		}
		if (tokens <= remaining) {
			file.selected = true;
			file.tokenCount = tokens;
			result.selectedFileIndices.push_back(index);
			remaining -= tokens;
		}
	}
	if (result.selectedFileIndices.empty() && !ordered.empty()) {
		auto & file = result.files[ordered.front()];
		file.selected = true;
		file.truncated = true;
		file.truncatedContent = slidingWindowText(file.content, static_cast<size_t>(std::max(256, responseReserve * 2)));
		file.tokenCount = std::max(1, static_cast<int>(file.truncatedContent.size() / 4 + 1));
		result.selectedFileIndices.push_back(ordered.front());
	}

	auto makeInferenceSettings = [&]() {
		ofxGgmlInferenceSettings inferenceSettings;
		inferenceSettings.maxTokens = firstPassTokenCeiling;
		inferenceSettings.temperature = 0.25f;
		inferenceSettings.topP = 0.92f;
		inferenceSettings.repeatPenalty = 1.05f;
		inferenceSettings.contextSize = settings.contextSize;
		inferenceSettings.batchSize = settings.batchSize;
		inferenceSettings.gpuLayers = settings.gpuLayers;
		inferenceSettings.threads = settings.threads;
		inferenceSettings.simpleIo = false;
		inferenceSettings.autoPromptCache = false;
		inferenceSettings.autoContinueCutoff = settings.autoContinueCutoff;
		inferenceSettings.trimPromptToContext = true;
		inferenceSettings.useServerBackend = settings.useServerBackend;
		inferenceSettings.serverUrl = trimCopy(settings.serverUrl);
		inferenceSettings.serverModel = trimCopy(settings.serverModel);
		return inferenceSettings;
	};

	if (!reportProgress(onProgress, "Summarizing files", 0, result.selectedFileIndices.size())) {
		result.error = "cancelled";
		return result;
	}
	const size_t maxSummaryParallel = std::max<size_t>(1, settings.maxSummaryParallelTasks);
	std::atomic<size_t> summarizedCount{0};
	std::vector<std::future<void>> summaryTasks;
	for (const size_t index : result.selectedFileIndices) {
		summaryTasks.push_back(std::async(std::launch::async, [&, this, index]() {
			auto & file = result.files[index];
			auto inferenceSettings = makeInferenceSettings();
			const int fileTokenEstimate = std::max(1, file.tokenCount);
			inferenceSettings.maxTokens = std::clamp(
				std::max(160, fileTokenEstimate / 3),
				160,
				firstPassTokenCeiling);

			std::ostringstream prompt;
			prompt << "First pass: Review this single file in isolation as a senior code reviewer.\n";
			prompt << "Use only the provided content. Do not invent vulnerabilities, architecture problems, "
				"or tests without evidence in the file.\n";
			prompt << "If there are no concrete issues, say exactly: No material findings in this file.\n";
			prompt << "Every finding must cite exact evidence copied verbatim from the file using backticks.\n";
			prompt << "Requested focus: " << effectiveQuery << "\n";
			prompt << "File: " << file.name << "\n";
			prompt << "Metrics: LOC=" << file.loc
				<< " complexity~" << file.complexity
				<< " fan-in=" << file.dependencyFanIn
				<< " fan-out=" << file.dependencyFanOut
				<< " recencyScore=" << toFixedString(file.recencyScore)
				<< " importance=" << toFixedString(file.importanceScore) << "\n";
			if (file.truncated) {
				prompt << "Note: content truncated with a sliding window to fit context.\n";
			}
			prompt << "\nContent:\n" << file.truncatedContent << "\n";
			prompt << "\nFormat:\n"
				<< "- Summary: one sentence about what this file does\n"
				<< "- Findings: 0-3 bullets, each tied to concrete code in this file with `exact evidence`\n"
				<< "- Tests: only file-specific tests that would catch the listed findings\n";

			std::ostringstream retryPrompt;
			retryPrompt << "Review this file and return exactly three bullets.\n";
			retryPrompt << "- Summary: one sentence\n";
			retryPrompt << "- Findings: concrete issue with `exact evidence`, or 'No material findings in this file.'\n";
			retryPrompt << "- Tests: file-specific test or 'None needed beyond current coverage.'\n";
			retryPrompt << "Request: " << effectiveQuery << "\n";
			retryPrompt << "File: " << file.name << "\n";
			retryPrompt << "Content:\n" << file.truncatedContent << "\n";

			const auto summary = generateReviewPassWithRetry(
				m_inference,
				m_generationFallback,
				modelPath,
				prompt.str(),
				retryPrompt.str(),
				inferenceSettings);
			file.summary = finalizedReviewSection(
				summary,
				"First-pass review for " + file.name,
				&file);
			++summarizedCount;
		}));
		if (summaryTasks.size() >= maxSummaryParallel) {
			summaryTasks.front().get();
			summaryTasks.erase(summaryTasks.begin());
			if (!reportProgress(onProgress, "Summarizing files", summarizedCount.load(), result.selectedFileIndices.size())) {
				result.error = "cancelled";
				return result;
			}
		}
	}
	for (auto & task : summaryTasks) {
		task.get();
	}

	std::ostringstream firstPass;
	for (const size_t index : result.selectedFileIndices) {
		firstPass << "### " << result.files[index].name << "\n";
		firstPass << result.files[index].summary << "\n\n";
	}
	result.firstPassSummary = firstPass.str();

	auto aggregateSettings = makeInferenceSettings();
	aggregateSettings.maxTokens = aggregateTokenBudget;
	std::string summaryList;
	for (size_t i = 0; i < result.selectedFileIndices.size() && i < 24; ++i) {
		const auto & file = result.files[result.selectedFileIndices[i]];
		summaryList += "- " + file.name + ": " + file.summary + "\n";
	}
	const bool allFirstPassesNoMaterial = !result.selectedFileIndices.empty() &&
		std::all_of(result.selectedFileIndices.begin(), result.selectedFileIndices.end(),
			[&](size_t index) {
				return isNoMaterialFindingsSection(result.files[index].summary);
			});

	if (allFirstPassesNoMaterial) {
		result.architectureReview = "No material architecture findings.";
		result.integrationReview = "No material integration findings.";
		reportProgress(onProgress, "Architecture review (skipped)");
		reportProgress(onProgress, "Integration review (skipped)");
	} else {
		if (!reportProgress(onProgress, "Architecture review")) {
			result.error = "cancelled";
			return result;
		}
		{
			std::string prompt = "Second pass: Architectural review using only the summaries below.\n";
			prompt += "Do not repeat file summaries. Only report cross-cutting architecture or layering issues "
				"that are supported by the provided summaries.\n";
			prompt += "If no concrete architecture issues are evident, say exactly: No material architecture findings.\n";
			prompt += "Request: " + effectiveQuery + "\n\n";
			prompt += result.repoTree + "\n";
			prompt += "File summaries:\n" + summaryList;
			prompt += "\nIdentify architecture, layering, and dependency issues. "
				"Highlight risky boundaries, missing invariants, and testing gaps. "
				"Keep output concise, evidence-based, and repository-specific.\n";
			std::string retryPrompt =
				"Architecture review. Return 3-5 concise bullets covering only concrete layering, "
				"dependency-boundary, shared-state, or testing issues visible in these summaries.\n"
				"If none are evident, say: No material architecture findings.\n"
				"Request: " + effectiveQuery + "\n\nSummaries:\n" + summaryList;
			const auto architecture = generateReviewPassWithRetry(
				m_inference,
				m_generationFallback,
				modelPath,
				prompt,
				retryPrompt,
				aggregateSettings);
			result.architectureReview = finalizedReviewSection(
				architecture,
				"Architecture review");
		}

		if (!reportProgress(onProgress, "Integration review")) {
			result.error = "cancelled";
			return result;
		}
		{
			std::string prompt = "Third pass: Cross-file dependency and integration analysis.\n";
			prompt += "Only report mismatches or integration risks that are supported by the provided per-file findings.\n";
			prompt += "Do not give generic cleanup advice. If no concrete integration issue is evident, "
				"say exactly: No material integration findings.\n";
			prompt += "Request: " + effectiveQuery + "\n\n";
			prompt += result.repoTree + "\nPer-file findings:\n";
			for (size_t i = 0; i < result.selectedFileIndices.size() && i < 24; ++i) {
				const auto & file = result.files[result.selectedFileIndices[i]];
				prompt += "- " + file.name + " (fan-in " + std::to_string(file.dependencyFanIn) +
					", fan-out " + std::to_string(file.dependencyFanOut) + "): " +
					file.summary + "\n";
			}
			prompt += "\nFocus on contract mismatches, API misuse, inconsistent assumptions, "
				"shared state, and missing integration tests. "
				"Propose cross-file actions only when grounded in the summaries.\n";
			std::string retryPrompt =
				"Cross-file integration review. Return 3-5 concise bullets covering only concrete "
				"interface mismatches, shared-state risks, or missing integration tests.\n"
				"If none are evident, say: No material integration findings.\n"
				"Request: " + effectiveQuery + "\n\nFindings:\n";
			for (size_t i = 0; i < result.selectedFileIndices.size() && i < 12; ++i) {
				const auto & file = result.files[result.selectedFileIndices[i]];
				retryPrompt += "- " + file.name + ": " + file.summary + "\n";
			}
			const auto integration = generateReviewPassWithRetry(
				m_inference,
				m_generationFallback,
				modelPath,
				prompt,
				retryPrompt,
				aggregateSettings);
			result.integrationReview = finalizedReviewSection(
				integration,
				"Integration review");
		}
	}

	std::ostringstream combined;
	combined << "Hierarchical code review (embeddings + multi-pass)\n\n";
	combined << "Generation backend: "
		<< (settings.useServerBackend ? "llama-server (persistent)" : "CLI (llama-completion)")
		<< "\n";
	if (settings.useServerBackend) {
		const std::string serverUrl = trimCopy(settings.serverUrl);
		const std::string serverModel = trimCopy(settings.serverModel);
		if (!serverUrl.empty()) {
			combined << "Server URL: " << serverUrl << "\n";
		}
		if (!serverModel.empty()) {
			combined << "Server model: " << serverModel << "\n";
		}
	}
	combined << "\n";
	combined << result.tableOfContents << "\n";
	combined << "Selected files (priority + similarity):\n";
	for (const size_t index : result.selectedFileIndices) {
		const auto & file = result.files[index];
		combined << "- " << file.name
			<< " | priority " << toFixedString(file.priorityScore)
			<< " | sim " << toFixedString(file.similarityScore)
			<< " | loc " << file.loc
			<< (file.truncated ? " (truncated)" : "") << "\n";
	}
	combined << "\nFirst pass - per-file summaries and issues:\n"
		<< result.firstPassSummary;
	combined << "Second pass - architecture issues:\n"
		<< result.architectureReview << "\n\n";
	combined << "Third pass - cross-file integration:\n"
		<< result.integrationReview << "\n\n";
	combined << "Context management: reserved " << responseReserve
		<< " tokens for responses; fallback token estimation used when tokenizer output is unavailable.\n";
	result.combinedReport = combined.str();

	if (settings.projectMemory != nullptr) {
		settings.projectMemory->addInteraction("First-pass summaries", result.firstPassSummary);
		settings.projectMemory->addInteraction("Architecture review", result.architectureReview);
		settings.projectMemory->addInteraction("Integration review", result.integrationReview);
	}

	result.success = true;
	result.status = "reviewed " + std::to_string(result.selectedFileIndices.size()) + " files";
	reportProgress(onProgress, "Complete", result.selectedFileIndices.size(), result.selectedFileIndices.size());
	return result;
}
