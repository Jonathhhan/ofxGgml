#include "ofxGgmlCodeReview.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <iomanip>
#include <mutex>
#include <sstream>
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

bool reportProgress(
	const std::function<bool(const ofxGgmlCodeReviewProgress &)> & onProgress,
	const std::string & stage,
	size_t completed = 0,
	size_t total = 0) {
	if (!onProgress) return true;
	return onProgress({stage, completed, total});
}

} // namespace

void ofxGgmlCodeReview::setCompletionExecutable(const std::string & path) {
	m_inference.setCompletionExecutable(path);
}

void ofxGgmlCodeReview::setEmbeddingExecutable(const std::string & path) {
	m_inference.setEmbeddingExecutable(path);
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
	if (modelPath.empty()) {
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
	const auto queryEmbed = m_inference.embed(modelPath, effectiveQuery);
	if (queryEmbed.success) {
		queryEmbedding = queryEmbed.embedding;
	}

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
			const auto embedding = m_inference.embed(modelPath, snippet);
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
	for (auto & file : result.files) {
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

		file.priorityScore =
			0.30f * file.importanceScore +
			0.20f * normComplexity +
			0.15f * normLoc +
			0.15f * normFan +
			0.20f * std::clamp(normSim, 0.0f, 1.0f) +
			0.10f * std::clamp(file.recencyScore, 0.0f, 1.5f);
	}

	std::vector<size_t> ordered(result.files.size());
	for (size_t i = 0; i < ordered.size(); ++i) {
		ordered[i] = i;
	}
	std::sort(ordered.begin(), ordered.end(), [&](size_t a, size_t b) {
		return result.files[a].priorityScore > result.files[b].priorityScore;
	});

	const int responseReserve = std::max(settings.maxTokens, settings.contextSize / 3);
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
		inferenceSettings.maxTokens = std::clamp(settings.maxTokens, 96, 4096);
		inferenceSettings.temperature = 0.25f;
		inferenceSettings.topP = 0.92f;
		inferenceSettings.repeatPenalty = 1.05f;
		inferenceSettings.contextSize = settings.contextSize;
		inferenceSettings.batchSize = settings.batchSize;
		inferenceSettings.gpuLayers = settings.gpuLayers;
		inferenceSettings.threads = settings.threads;
		inferenceSettings.simpleIo = true;
		inferenceSettings.autoPromptCache = settings.usePromptCache;
		inferenceSettings.autoContinueCutoff = settings.autoContinueCutoff;
		inferenceSettings.trimPromptToContext = true;
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
			inferenceSettings.maxTokens = std::max(96, std::min(responseReserve, settings.maxTokens));

			std::ostringstream prompt;
			prompt << "First pass: Review this single file in isolation. "
				"Return a concise summary plus concrete issues (bugs, security, tests, readability).\n";
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
			prompt << "\nFormat:\n- Summary\n- Risks\n- Tests to add\n";

			const auto summary = m_inference.generate(modelPath, prompt.str(), inferenceSettings);
			file.summary = summary.success ? trimCopy(summary.text) : "[error] " + summary.error;
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
	std::string summaryList;
	for (size_t i = 0; i < result.selectedFileIndices.size() && i < 24; ++i) {
		const auto & file = result.files[result.selectedFileIndices[i]];
		summaryList += "- " + file.name + ": " + file.summary + "\n";
	}

	if (!reportProgress(onProgress, "Architecture review")) {
		result.error = "cancelled";
		return result;
	}
	{
		std::string prompt = "Second pass: Architectural review using only the summaries below.\n";
		prompt += "Request: " + effectiveQuery + "\n\n";
		prompt += result.repoTree + "\n";
		prompt += "File summaries:\n" + summaryList;
		prompt += "\nIdentify architecture, layering, and dependency issues. "
			"Highlight risky boundaries, missing invariants, and testing gaps. "
			"Keep output concise and actionable.\n";
		const auto architecture = m_inference.generate(modelPath, prompt, aggregateSettings);
		result.architectureReview = architecture.success
			? trimCopy(architecture.text)
			: "[error] " + architecture.error;
	}

	if (!reportProgress(onProgress, "Integration review")) {
		result.error = "cancelled";
		return result;
	}
	{
		std::string prompt = "Third pass: Cross-file dependency and integration analysis.\n";
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
			"Propose cross-file actions and dependency trims.\n";
		const auto integration = m_inference.generate(modelPath, prompt, aggregateSettings);
		result.integrationReview = integration.success
			? trimCopy(integration.text)
			: "[error] " + integration.error;
	}

	std::ostringstream combined;
	combined << "Hierarchical code review (embeddings + multi-pass)\n\n";
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
