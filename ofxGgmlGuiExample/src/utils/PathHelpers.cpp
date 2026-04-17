#include "PathHelpers.h"
#include "ImGuiHelpers.h"

#include <algorithm>
#include <filesystem>
#include <regex>
#include <string>
#include <unordered_set>

bool pathExists(const std::string & path) {
	if (path.empty()) {
		return false;
	}
	std::error_code ec;
	return std::filesystem::exists(path, ec) && !ec;
}

std::string suggestedModelPath(
	const std::string & explicitPath,
	const std::string & modelFileHint) {
	const std::string trimmedExplicit = trim(explicitPath);
	if (!trimmedExplicit.empty()) {
		return trimmedExplicit;
	}
	const std::string trimmedFileHint = trim(modelFileHint);
	if (trimmedFileHint.empty()) {
		return "";
	}
	return ofToDataPath(ofFilePath::join("models", trimmedFileHint), true);
}

std::string suggestedModelDownloadUrl(
	const std::string & modelRepoHint,
	const std::string & modelFileHint) {
	const std::string trimmedRepoHint = trim(modelRepoHint);
	const std::string trimmedFileHint = trim(modelFileHint);
	if (trimmedRepoHint.empty() || trimmedFileHint.empty()) {
		return "";
	}
	return "https://huggingface.co/" + trimmedRepoHint +
		"/resolve/main/" + trimmedFileHint;
}

std::string effectiveSuggestedModelDownloadUrl(
	const std::string & explicitUrl,
	const std::string & modelRepoHint,
	const std::string & modelFileHint) {
	const std::string trimmedExplicitUrl = trim(explicitUrl);
	if (!trimmedExplicitUrl.empty()) {
		return trimmedExplicitUrl;
	}
	return suggestedModelDownloadUrl(modelRepoHint, modelFileHint);
}

bool isDefaultWhisperCliExecutableHint(const std::string & executable) {
	const std::string trimmed = trim(executable);
	if (trimmed.empty()) {
		return true;
	}
	if (trimmed.find('\\') != std::string::npos ||
		trimmed.find('/') != std::string::npos) {
		return false;
	}
	std::string fileName = std::filesystem::path(trimmed).filename().string();
	std::transform(
		fileName.begin(),
		fileName.end(),
		fileName.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return fileName == "whisper-cli" ||
		fileName == "whisper-cli.exe" ||
		fileName == "main" ||
		fileName == "main.exe";
}

std::vector<std::string> extractHttpUrls(const std::string & text) {
	static const std::regex urlRegex(R"(https?://[^\s<>\"]+)", std::regex::icase);
	std::vector<std::string> urls;
	std::unordered_set<std::string> seen;
	for (std::sregex_iterator it(text.begin(), text.end(), urlRegex), end; it != end; ++it) {
		const std::string url = it->str();
		if (seen.insert(url).second) {
			urls.push_back(url);
		}
	}
	return urls;
}

std::vector<std::string> extractPathList(const std::string & text) {
	std::vector<std::string> paths;
	std::unordered_set<std::string> seen;
	std::string current;
	auto flush = [&]() {
		const std::string value = trim(current);
		current.clear();
		if (!value.empty() && seen.insert(value).second) {
			paths.push_back(value);
		}
	};
	for (const char c : text) {
		if (c == '\r' || c == '\n' || c == ';') {
			flush();
		} else {
			current.push_back(c);
		}
	}
	flush();
	return paths;
}

std::vector<std::string> splitStoredScriptSourceUrls(const std::string & packedUrls) {
	if (packedUrls.empty()) {
		return {};
	}
	return ofSplitString(packedUrls, "\n", true, true);
}

std::string promptCachePathFor(const std::string & modelPath, AiMode mode) {
	const std::string cacheDir = ofToDataPath("cache", true);
	std::error_code ec;
	std::filesystem::create_directories(cacheDir, ec);
	const size_t modelHash = std::hash<std::string>{}(modelPath);
	const std::string filename = "prompt_cache_"
		+ std::to_string(static_cast<int>(mode)) + "_"
		+ std::to_string(modelHash) + ".bin";
	return ofFilePath::join(cacheDir, filename);
}
