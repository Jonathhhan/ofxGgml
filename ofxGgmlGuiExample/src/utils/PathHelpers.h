#pragma once

#include "ofMain.h"

#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

enum class AiMode;

// Check if path exists
bool pathExists(const std::string & path);

// Get suggested model path (explicit or default data path)
std::string suggestedModelPath(
	const std::string & explicitPath,
	const std::string & modelFileHint);

// Generate suggested model download URL
std::string suggestedModelDownloadUrl(
	const std::string & modelRepoHint,
	const std::string & modelFileHint);

// Get effective suggested model download URL (explicit or generated)
std::string effectiveSuggestedModelDownloadUrl(
	const std::string & explicitUrl,
	const std::string & modelRepoHint,
	const std::string & modelFileHint);

// Check if executable hint is default whisper-cli
bool isDefaultWhisperCliExecutableHint(const std::string & executable);

// Extract HTTP URLs from text
std::vector<std::string> extractHttpUrls(const std::string & text);

// Extract path list from text (newline/semicolon separated)
std::vector<std::string> extractPathList(const std::string & text);

// Split stored script source URLs
std::vector<std::string> splitStoredScriptSourceUrls(const std::string & packedUrls);

// Get prompt cache path for model and mode
std::string promptCachePathFor(const std::string & modelPath, AiMode mode);
