#include "PathHelpers.h"
#include "ImGuiHelpers.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

std::filesystem::path normalizeExistingPath(const std::filesystem::path & path) {
	std::error_code ec;
	if (path.empty()) {
		return {};
	}
	if (std::filesystem::exists(path, ec) && !ec) {
		return std::filesystem::weakly_canonical(path, ec);
	}
	ec.clear();
	return path.lexically_normal();
}

std::filesystem::path currentExecutableDir() {
	return std::filesystem::path(ofFilePath::getCurrentExeDir()).lexically_normal();
}

std::string normalizedModelKey(const std::string & value) {
	std::string normalized;
	normalized.reserve(value.size());
	for (const unsigned char c : value) {
		if (std::isalnum(c)) {
			normalized.push_back(static_cast<char>(std::tolower(c)));
		}
	}
	return normalized;
}

std::vector<std::string> tokenizeModelName(const std::string & value) {
	std::vector<std::string> tokens;
	std::string current;
	current.reserve(value.size());
	auto flush = [&]() {
		if (current.size() >= 2) {
			tokens.push_back(current);
		}
		current.clear();
	};
	for (const unsigned char c : value) {
		if (std::isalnum(c)) {
			current.push_back(static_cast<char>(std::tolower(c)));
		} else {
			flush();
		}
	}
	flush();
	return tokens;
}

#ifdef _WIN32
std::wstring toWidePath(const std::filesystem::path & path) {
	return path.wstring();
}
#endif

} // namespace

bool pathExists(const std::string & path) {
	if (path.empty()) {
		return false;
	}
	std::error_code ec;
	return std::filesystem::exists(path, ec) && !ec;
}

std::filesystem::path addonRootPath() {
	std::error_code ec;

	// Preferred workspace layout:
	// add-ons/ofxGgml/ofxGgmlGuiExample/bin -> add-ons/ofxGgml
	const std::filesystem::path exeDir = currentExecutableDir();
	const std::vector<std::filesystem::path> candidates = {
		exeDir / ".." / "..",
		exeDir / "..",
		std::filesystem::path(__FILE__).parent_path() / ".." / "..",
		std::filesystem::current_path(ec)
	};

	for (const auto & candidate : candidates) {
		ec.clear();
		const std::filesystem::path normalized = normalizeExistingPath(candidate);
		if (normalized.empty()) {
			continue;
		}
		const auto marker = normalized / "src" / "ofxGgml.h";
		if (std::filesystem::exists(marker, ec) && !ec) {
			return normalized;
		}
	}

	return {};
}

std::string sharedModelsDir() {
	const auto root = addonRootPath();
	if (root.empty()) {
		return {};
	}
	return (root / "models").lexically_normal().string();
}

std::string bundledModelsDir() {
	return ofToDataPath("models", true);
}

std::string resolveModelPathHint(const std::string & modelFileHint) {
	const std::string trimmedFileHint = trim(modelFileHint);
	if (trimmedFileHint.empty()) {
		return {};
	}

	const std::vector<std::filesystem::path> modelRoots = {
		std::filesystem::path(sharedModelsDir()),
		std::filesystem::path(bundledModelsDir())
	};
	const std::vector<std::filesystem::path> candidates = {
		modelRoots[0] / trimmedFileHint,
		modelRoots[1] / trimmedFileHint
	};

	for (const auto & candidate : candidates) {
		if (candidate.empty()) {
			continue;
		}
		std::error_code ec;
		if (std::filesystem::exists(candidate, ec) && !ec) {
			return candidate.lexically_normal().string();
		}
	}

	// Return the shared path as the default suggestion for development setups.
	if (!sharedModelsDir().empty()) {
		return (std::filesystem::path(sharedModelsDir()) / trimmedFileHint)
			.lexically_normal()
			.string();
	}
	return (std::filesystem::path(bundledModelsDir()) / trimmedFileHint)
		.lexically_normal()
		.string();
}

void configureCentralRuntimeSearchPaths() {
#ifdef _WIN32
	static bool configured = false;
	if (configured) {
		return;
	}
	configured = true;

	SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);

	const std::filesystem::path addonRoot = addonRootPath();
	if (addonRoot.empty()) {
		return;
	}

	const std::vector<std::filesystem::path> searchDirs = {
		addonRoot / "libs" / "llama" / "bin",
		addonRoot / "libs" / "whisper" / "bin",
		addonRoot / "libs" / "chatllm" / "bin",
		addonRoot / "libs" / "ggml" / "build" / "src" / "Release",
		addonRoot / "libs" / "ggml" / "build" / "src" / "ggml-cuda" / "Release",
		addonRoot / "libs" / "ggml" / "build" / "src" / "ggml-vulkan" / "Release",
		addonRoot.parent_path() / "ofxStableDiffusion" / "libs" / "stable-diffusion" / "build" / "bin" / "Release",
		addonRoot.parent_path() / "ofxStableDiffusion" / "libs" / "stable-diffusion" / "build" / "Release",
		addonRoot.parent_path() / "ofxStableDiffusion" / "libs" / "stable-diffusion" / "lib" / "vs"
	};

	for (const auto & dir : searchDirs) {
		std::error_code ec;
		if (!std::filesystem::exists(dir, ec) || ec) {
			continue;
		}
		AddDllDirectory(toWidePath(dir).c_str());
	}
#endif
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
	return resolveModelPathHint(trimmedFileHint);
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
