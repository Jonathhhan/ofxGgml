#include "ofxGgmlInference.h"
#include "ofxGgmlScriptSource.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <string_view>

#ifdef _WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#else
	#include <fcntl.h>
	#include <sys/wait.h>
	#include <unistd.h>
#endif

namespace {

struct TokenLiteral {
	const char * text;
	size_t len;
};

static constexpr TokenLiteral kRoleLabels[] = {
	{ "user", 4 },
	{ "assistant", 9 },
	{ "system", 6 },
	{ "User", 4 },
	{ "Assistant", 9 },
	{ "System", 6 },
	{ "A:", 2 },
	{ "> ", 2 },
};

static constexpr TokenLiteral kTrailingArtifacts[] = {
	{ "> EOF by user", 13 },
	{ "> EOF", 5 },
	{ "EOF", 3 },
	{ "Interrupted by user", 19 },
};

static constexpr size_t kMaxSourceLabelChars = 96;

static std::string trim(const std::string & s) {
	size_t b = 0;
	while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
		++b;
	size_t e = s.size();
	while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
		--e;
	return s.substr(b, e - b);
}

static std::string_view trimView(const std::string & s) {
	size_t b = 0;
	while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
		++b;
	size_t e = s.size();
	while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
		--e;
	return std::string_view(s).substr(b, e - b);
}

static std::string getEnvVarString(const char * name) {
	if (!name || *name == '\0') {
		return "";
	}
#ifdef _WIN32
	char * value = nullptr;
	size_t len = 0;
	const errno_t err = _dupenv_s(&value, &len, name);
	if (err != 0 || value == nullptr || len == 0) {
		free(value);
		return "";
	}
	std::string result(value);
	free(value);
	return result;
#else
	const char * value = std::getenv(name);
	return value ? std::string(value) : std::string();
#endif
}

static std::string clipTextWithMarker(
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

static std::string stripHtmlComments(const std::string & html) {
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

static std::string stripHtmlBlocks(
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

static std::string decodeBasicHtmlEntities(const std::string & text) {
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

static bool looksLikeHtmlDocument(const std::string & text) {
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

static std::string extractPlainTextFromHtml(const std::string & html) {
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

	return trim(collapsed);
}

static std::string normalizeSourceContent(
	const ofxGgmlPromptSource & source,
	const ofxGgmlPromptSourceSettings & settings) {
	std::string content = trim(source.content);
	if (content.empty()) {
		return {};
	}
	if (settings.normalizeWebText && source.isWebSource && looksLikeHtmlDocument(content)) {
		content = extractPlainTextFromHtml(content);
	}
	return trim(content);
}

static std::string formatSourceLabel(const ofxGgmlPromptSource & source) {
	std::string label = trim(source.label);
	if (label.empty()) {
		label = trim(source.uri);
	}
	if (label.empty()) {
		label = "Source";
	}
	if (label.size() > kMaxSourceLabelChars) {
		label = label.substr(0, kMaxSourceLabelChars - 3) + "...";
	}
	return label;
}

static bool isLikelyWebUri(const std::string & uri) {
	return uri.rfind("http://", 0) == 0 || uri.rfind("https://", 0) == 0;
}

static bool isRuntimeNoiseLine(std::string_view trimmedLine);

/// Strip common llama.cpp warning messages from output.
/// Even with --log-disable, some warnings may still appear in stderr
/// which gets captured alongside stdout by runCommandCapture.
static std::string stripLlamaWarnings(const std::string & text) {
	if (text.empty()) return text;

	std::ostringstream filtered;
	std::istringstream lines(text);
	std::string line;

	while (std::getline(lines, line)) {
		const std::string_view trimmedLine = trimView(line);
		if (isRuntimeNoiseLine(trimmedLine)) {
			continue;
		}

		filtered << line << '\n';
	}

	std::string result = filtered.str();
	// Remove trailing newline if original didn't have one
	if (!result.empty() && result.back() == '\n' && (text.empty() || text.back() != '\n')) {
		result.pop_back();
	}
	return result;
}

static bool isRuntimeNoiseLine(std::string_view trimmedLine) {
	if (trimmedLine.empty()) return true;
	if (trimmedLine.rfind("ofxGgml [", 0) == 0) return true;
	if (trimmedLine.find("warning: no usable GPU found") != std::string::npos) return true;
	if (trimmedLine.find("warning: one possible reason is that llama.cpp was compiled without GPU support") != std::string::npos) return true;
	if (trimmedLine.find("warning: consult docs/build.md for compilation instructions") != std::string::npos) return true;
	if (trimmedLine.find("ggml_cuda_init") != std::string::npos) return true;
	if (trimmedLine.find("ggml_vulkan_init") != std::string::npos) return true;
	if (trimmedLine.find("ggml_metal_init") != std::string::npos) return true;
	if (trimmedLine.find("--gpu-layers option will be ignored") != std::string::npos) return true;
	if (trimmedLine.find("Total VRAM:") != std::string::npos) return true;
	if (trimmedLine.find("compute capability") != std::string::npos) return true;
	if (trimmedLine.find("backend = ") != std::string::npos) return true;
	if (trimmedLine.find("VMM:") != std::string::npos) return true;
	if (trimmedLine.rfind("Device ", 0) == 0) {
		const size_t colon = trimmedLine.find(':');
		if (colon != std::string::npos && colon > 7) {
			bool digitsOnly = true;
			for (size_t i = 7; i < colon; ++i) {
				if (!std::isdigit(static_cast<unsigned char>(trimmedLine[i]))) {
					digitsOnly = false;
					break;
				}
			}
			if (digitsOnly) return true;
		}
	}
	return false;
}

static std::string stripLeadingRuntimeNoise(const std::string & text) {
	std::ostringstream filtered;
	std::istringstream lines(text);
	std::string line;
	bool skipping = true;

	while (std::getline(lines, line)) {
		const std::string_view trimmedLine = trimView(line);
		if (skipping) {
			if (isRuntimeNoiseLine(trimmedLine)) {
				continue;
			}
			skipping = false;
		}

		filtered << line;
		if (!lines.eof()) {
			filtered << '\n';
		}
	}

	return trim(filtered.str());
}

static std::string stripChatTemplateMarkers(const std::string & text) {
	std::string out;
	out.reserve(text.size());
	size_t i = 0;
	while (i < text.size()) {
		if (i + 1 < text.size() && text[i] == '<' && text[i + 1] == '<') {
			out.push_back(text[i]);
			++i;
			continue;
		}
		if (i + 1 < text.size() && text[i] == '<' && text[i + 1] == '|') {
			const size_t end = text.find("|>", i + 2);
			if (end != std::string::npos) {
				i = end + 2;
				continue;
			}
		}
		out.push_back(text[i]);
		++i;
	}
	return out;
}

static std::string stripPromptEcho(const std::string & text, const std::string & prompt) {
	std::string out = trim(text);
	const std::string trimmedPrompt = trim(prompt);
	if (trimmedPrompt.empty()) return out;
	if (out.size() >= trimmedPrompt.size() &&
		out.compare(0, trimmedPrompt.size(), trimmedPrompt) == 0) {
		return trim(out.substr(trimmedPrompt.size()));
	}
	return out;
}

static std::string stripLeadingRoleLabels(const std::string & text) {
	std::string out = trim(text);
	bool changed = true;
	while (changed) {
		changed = false;
		for (const auto & label : kRoleLabels) {
			if (out.size() >= label.len && out.compare(0, label.len, label.text) == 0 &&
				(out.size() == label.len || out[label.len] == '\n' || out[label.len] == '\r' || out[label.len] == ':' || out[label.len] == ' ')) {
				out = trim(out.substr(label.len));
				if (!out.empty() && out.front() == ':') {
					out = trim(out.substr(1));
				}
				changed = true;
				break;
			}
		}
		if (!out.empty() && out.front() == '>') {
			out = trim(out.substr(1));
			changed = true;
		}
	}
	return out;
}

static std::string stripTrailingArtifacts(const std::string & text) {
	std::string out = trim(text);
	bool stripped = true;
	while (stripped) {
		stripped = false;
		for (const auto & artifact : kTrailingArtifacts) {
			if (out.size() >= artifact.len &&
				out.compare(out.size() - artifact.len, artifact.len, artifact.text) == 0) {
				out = trim(out.substr(0, out.size() - artifact.len));
				stripped = true;
				break;
			}
		}
	}
	return out;
}

static std::string cleanCompletionOutput(const std::string & raw, const std::string & prompt) {
	std::string cleaned = stripLlamaWarnings(raw);
	cleaned = stripLeadingRuntimeNoise(cleaned);
	cleaned = stripChatTemplateMarkers(cleaned);
	cleaned = stripPromptEcho(cleaned, prompt);
	cleaned = stripLeadingRoleLabels(cleaned);
	cleaned = stripTrailingArtifacts(cleaned);
	for (const char * marker : {"[1m", "[32m", "[0m", "> "}) {
		size_t pos = 0;
		const size_t len = std::strlen(marker);
		while ((pos = cleaned.find(marker, pos)) != std::string::npos) {
			cleaned.erase(pos, len);
		}
	}
	cleaned = stripLeadingRuntimeNoise(cleaned);
	return trim(cleaned);
}

static std::string cleanStructuredOutput(const std::string & raw) {
	return trim(stripLeadingRuntimeNoise(stripLlamaWarnings(raw)));
}

static std::string defaultPromptCachePathForModel(const std::string & modelPath) {
	if (modelPath.empty()) return {};
	std::error_code ec;
	const std::filesystem::path tempDir = std::filesystem::temp_directory_path(ec);
	if (ec) return {};
	const size_t modelHash = std::hash<std::string>{}(modelPath);
	return (tempDir / ("ofxggml_prompt_cache_" + std::to_string(modelHash) + ".bin")).string();
}

static bool looksLikeCodeOutput(const std::string & text) {
	if (text.find("```") != std::string::npos) return true;
	const bool hasBraces = text.find('{') != std::string::npos || text.find('}') != std::string::npos;
	const bool hasSemicolon = text.find(';') != std::string::npos;
	const bool hasIncludeOrImport =
		text.find("#include") != std::string::npos ||
		text.find("import ") != std::string::npos ||
		text.find("from ") != std::string::npos;
	return (hasBraces && hasSemicolon) || hasIncludeOrImport;
}

static std::string trimToNaturalBoundary(const std::string & text) {
	std::string out = trim(text);
	if (out.empty()) return out;

	if (looksLikeCodeOutput(out)) {
		if (out.back() != '\n') {
			const size_t cut = out.find_last_of('\n');
			if (cut != std::string::npos && cut > out.size() / 2) {
				out = trim(out.substr(0, cut));
			}
		}
		return out;
	}

	size_t sentenceEnd = std::string::npos;
	for (size_t i = 0; i < out.size(); i++) {
		const char c = out[i];
		if (c == '.' || c == '!' || c == '?') {
			if (i + 1 == out.size() || std::isspace(static_cast<unsigned char>(out[i + 1])) || out[i + 1] == '"' || out[i + 1] == '\'') {
				sentenceEnd = i + 1;
			}
		}
	}
	if (sentenceEnd != std::string::npos && sentenceEnd > out.size() / 2) {
		out = trim(out.substr(0, sentenceEnd));
	}
	return out;
}

/// Validate that a file path exists and is a regular file.
/// Returns true if the path is valid and safe to use.
static bool isValidFilePath(const std::string & path) {
	if (path.empty()) return false;

	// Check for null bytes (security: path injection)
	if (path.find('\0') != std::string::npos) return false;

	std::error_code ec;
	std::filesystem::path fsPath(path);

	// Check if file exists
	if (!std::filesystem::exists(fsPath, ec)) return false;
	if (ec) return false;

	// Ensure it's a regular file, not a device or special file
	if (!std::filesystem::is_regular_file(fsPath, ec)) return false;
	if (ec) return false;

	return true;
}

/// Validate an executable path for security.
/// Checks that the executable exists and is not a suspicious path.
static bool isValidExecutablePath(const std::string & path) {
	if (path.empty()) return false;

	// Check for null bytes
	if (path.find('\0') != std::string::npos) return false;

	auto containsPathSeparator = [](const std::string & value) {
		return value.find('/') != std::string::npos || value.find('\\') != std::string::npos;
	};
	auto isLikelyPath = [&](const std::string & value) {
		std::filesystem::path fsPath(value);
		return fsPath.is_absolute() || fsPath.has_parent_path() || containsPathSeparator(value);
	};
	auto isRegularExecutableFile = [](const std::filesystem::path & candidate) {
		std::error_code ec;
		if (!std::filesystem::exists(candidate, ec) || ec) return false;
		if (!std::filesystem::is_regular_file(candidate, ec) || ec) return false;
#ifndef _WIN32
		return access(candidate.c_str(), X_OK) == 0;
#else
		return true;
#endif
	};

	// Explicit path: validate this path directly.
	if (isLikelyPath(path)) {
		std::error_code ec;
		const std::filesystem::path fsPath(path);
		const std::filesystem::path canonical = std::filesystem::weakly_canonical(fsPath, ec);
		if (!ec && isRegularExecutableFile(canonical)) return true;
		return isRegularExecutableFile(fsPath);
	}

	// Command name: search PATH (same contract as execvp/CreateProcess).
	for (char c : path) {
		const unsigned char uc = static_cast<unsigned char>(c);
		if (std::iscntrl(uc) || std::isspace(uc)) return false;
	}
	const std::string envPath = getEnvVarString("PATH");
	if (envPath.empty()) return false;

#ifdef _WIN32
	const char pathSep = ';';
	std::vector<std::string> executableExtensions;
	const std::string envPathext = getEnvVarString("PATHEXT");
	if (!envPathext.empty()) {
		std::istringstream extStream(envPathext);
		std::string ext;
		while (std::getline(extStream, ext, ';')) {
			if (!ext.empty()) executableExtensions.push_back(ext);
		}
	}
	if (executableExtensions.empty()) {
		executableExtensions = { ".exe", ".bat", ".cmd", ".com" };
	}
#else
	const char pathSep = ':';
#endif

	std::istringstream pathEntries(envPath);
	std::string dir;
	while (std::getline(pathEntries, dir, pathSep)) {
		if (dir.empty()) continue;
		const std::filesystem::path base(dir);
		if (!std::filesystem::is_directory(base)) continue;
#ifdef _WIN32
		std::filesystem::path candidate = base / path;
		if (isRegularExecutableFile(candidate)) return true;
		for (const auto & ext : executableExtensions) {
			candidate = base / (path + ext);
			if (isRegularExecutableFile(candidate)) return true;
		}
#else
		if (isRegularExecutableFile(base / path)) return true;
#endif
	}
	return false;
}

static bool shouldTreatNonZeroExitAsSuccess(
	int exitCode,
	bool hasOutput,
	const std::string & rawOutput) {
	if (exitCode == 0) return true;
	if (exitCode == 130) return true;
	if (!hasOutput) return false;

	const bool interruptedMarker =
		rawOutput.find("EOF by user") != std::string::npos ||
		rawOutput.find("Interrupted by user") != std::string::npos;
	if (interruptedMarker) return false;

	return exitCode == -1073740791 // Windows STATUS_STACK_BUFFER_OVERRUN (0xC0000409)
		|| exitCode == -1073741819 // Windows STATUS_ACCESS_VIOLATION (0xC0000005)
		|| exitCode == 1 // generic error (may occur during cleanup)
		|| exitCode == -1 // signal-killed on POSIX
		|| (exitCode >= 128 && exitCode < 160); // POSIX signal exits (128+signal)
}

/// Sanitize a string for safe use in command arguments.
/// Removes or escapes potentially dangerous characters.
static std::string sanitizeArgument(const std::string & arg) {
	std::string result;
	result.reserve(arg.size());

	for (char c : arg) {
		// Remove null bytes and control characters (except common whitespace)
		if (c == '\0' || (std::iscntrl(static_cast<unsigned char>(c)) && c != '\t' && c != '\n' && c != '\r')) {
			continue;
		}
		result += c;
	}

	return result;
}

#ifdef _WIN32
static std::string quoteWindowsArg(const std::string & arg) {
	bool needsQuotes = arg.find_first_of(" \t\"%^&|<>") != std::string::npos;
	if (!needsQuotes) return arg;
	std::string out;
	out.push_back('"');
	size_t backslashes = 0;
	for (char c : arg) {
		if (c == '\\') {
			backslashes++;
			continue;
		}
		if (c == '"') {
			out.append(backslashes * 2 + 1, '\\');
			out.push_back('"');
			backslashes = 0;
			continue;
		}
		if (backslashes > 0) {
			out.append(backslashes, '\\');
			backslashes = 0;
		}
		out.push_back(c);
	}
	if (backslashes > 0) {
		out.append(backslashes * 2, '\\');
	}
	out.push_back('"');
	return out;
}
#endif

static bool runCommandCapture(
const std::vector<std::string> & args,
std::string & output,
int & exitCode,
bool mergeStderr = true,
std::function<bool(const std::string&)> onChunk = nullptr) {
output.clear();
exitCode = -1;
if (args.empty() || args.front().empty()) return false;
#ifdef _WIN32
	SECURITY_ATTRIBUTES sa {};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = nullptr;

	HANDLE readPipe = nullptr;
	HANDLE writePipe = nullptr;
	if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return false;
	if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0)) {
		CloseHandle(readPipe);
		CloseHandle(writePipe);
		return false;
	}

	STARTUPINFOA si {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	HANDLE nullInput = CreateFileA("NUL", GENERIC_READ, 0, &sa,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	si.hStdInput = (nullInput != INVALID_HANDLE_VALUE)
		? nullInput
		: GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = writePipe;
	HANDLE nullErr = INVALID_HANDLE_VALUE;
	if (mergeStderr) {
		si.hStdError = writePipe;
	} else {
		nullErr = CreateFileA("NUL", GENERIC_WRITE, 0, &sa,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		si.hStdError = (nullErr != INVALID_HANDLE_VALUE)
			? nullErr
			: GetStdHandle(STD_ERROR_HANDLE);
	}

	PROCESS_INFORMATION pi {};
	std::string cmdLine;
	size_t cmdReserve = 0;
	for (const auto & arg : args) {
		cmdReserve += arg.size() + 3;
	}
	cmdLine.reserve(cmdReserve);
	for (size_t i = 0; i < args.size(); ++i) {
		if (i > 0) cmdLine.push_back(' ');
		cmdLine += quoteWindowsArg(args[i]);
	}
	std::vector<char> mutableCmd(cmdLine.begin(), cmdLine.end());
	mutableCmd.push_back('\0');

	BOOL ok = CreateProcessA(
		nullptr,
		mutableCmd.data(),
		nullptr,
		nullptr,
		TRUE,
		CREATE_NO_WINDOW,
		nullptr,
		nullptr,
		&si,
		&pi);
	CloseHandle(writePipe);
	if (nullInput != INVALID_HANDLE_VALUE) {
		CloseHandle(nullInput);
	}
	if (nullErr != INVALID_HANDLE_VALUE) {
		CloseHandle(nullErr);
	}
	if (!ok) {
		CloseHandle(readPipe);
		return false;
	}

	std::array<char, 4096> buf {};
	DWORD bytesRead = 0;
	while (ReadFile(readPipe, buf.data(), static_cast<DWORD>(buf.size()), &bytesRead, nullptr) && bytesRead > 0) {
		std::string chunk(buf.data(), bytesRead);
		output.append(chunk);
		if (onChunk && !onChunk(chunk)) {
			// Terminate process if callback requested early exit
			TerminateProcess(pi.hProcess, 1);
			break;
		}
	}
	CloseHandle(readPipe);

	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD code = 1;
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	exitCode = static_cast<int>(code);
#else
	int pipeFds[2] = { -1, -1 };
	if (pipe(pipeFds) != 0) {
		return false;
	}

	pid_t pid = fork();
	if (pid < 0) {
		close(pipeFds[0]);
		close(pipeFds[1]);
		return false;
	}

	if (pid == 0) {
		dup2(pipeFds[1], STDOUT_FILENO);
		if (mergeStderr) {
			dup2(pipeFds[1], STDERR_FILENO);
		} else {
			const int devNull = open("/dev/null", O_WRONLY);
			if (devNull >= 0) {
				dup2(devNull, STDERR_FILENO);
				close(devNull);
			} else {
				close(STDERR_FILENO);
			}
		}
		close(pipeFds[0]);
		close(pipeFds[1]);

		std::vector<char *> argv;
		argv.reserve(args.size() + 1);
		for (const auto & arg : args) {
			argv.push_back(const_cast<char *>(arg.c_str()));
		}
		argv.push_back(nullptr);

		execvp(argv[0], argv.data());
		_exit(127);
	}

	close(pipeFds[1]);
	std::array<char, 4096> buf {};
	ssize_t bytesRead = 0;
	while ((bytesRead = read(pipeFds[0], buf.data(), buf.size())) > 0) {
		std::string chunk(buf.data(), static_cast<size_t>(bytesRead));
		output.append(chunk);
		if (onChunk && !onChunk(chunk)) {
			// Terminate process if callback requested early exit
			kill(pid, SIGTERM);
			break;
		}
	}
	close(pipeFds[0]);

	int status = 0;
	if (waitpid(pid, &status, 0) < 0) {
		return false;
	}
	if (WIFEXITED(status)) {
		exitCode = WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		exitCode = 128 + WTERMSIG(status);
	}
#endif
	return true;
}

static std::string makeTempPath(const char * prefix, const char * ext) {
	std::error_code ec;
	std::filesystem::path base = std::filesystem::temp_directory_path(ec);
	if (ec) base = std::filesystem::current_path();

	// Generate a cryptographically random filename component
	std::random_device rd;
	std::mt19937_64 rng(rd());
	std::uniform_int_distribution<uint64_t> dist;

	// Try up to 1000 times to create a unique file (prevents race conditions)
	for (int attempts = 0; attempts < 1000; ++attempts) {
		const uint64_t random1 = dist(rng);
		const uint64_t random2 = dist(rng);
		std::ostringstream oss;
		oss << prefix << std::hex << random1 << "_" << random2 << ext;

		std::filesystem::path candidate = base / oss.str();

		// Try to create the file exclusively (atomic check-and-create)
		// Use platform-specific approach since std::ios::excl is not standard
#ifdef _WIN32
		// On Windows, use CreateFile with CREATE_NEW for atomic exclusive creation
		HANDLE hFile = CreateFileW(
			candidate.c_str(),
			GENERIC_WRITE,
			0, // No sharing
			NULL,
			CREATE_NEW, // Fails if file exists (atomic check-and-create)
			FILE_ATTRIBUTE_NORMAL,
			NULL);
		if (hFile != INVALID_HANDLE_VALUE) {
			CloseHandle(hFile);
			return candidate.string();
		}
#else
		// On POSIX systems, create atomically with O_EXCL.
		const int fd = open(candidate.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
		if (fd >= 0) {
			close(fd);
			return candidate.string();
		}
#endif
	}

	// Fallback: use time-based name with random component
	const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	const uint64_t nonce = dist(rng);
	return (base / (std::string(prefix) + std::to_string(now) + "_" + std::to_string(nonce) + ext)).string();
}

static uint32_t makeRandomSeed() {
	try {
		std::random_device rd;
		return rd();
	} catch (...) {
		const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
		return static_cast<uint32_t>(now ^ (now >> 32));
	}
}

static bool writeTextFile(const std::string & path, const std::string & text) {
	std::ofstream out(path, std::ios::binary);
	if (!out.is_open()) return false;
	out << text;
	return out.good();
}

struct ThreadLocalTempFile {
	std::string path;
	~ThreadLocalTempFile() {
		if (path.empty()) return;
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}
};

static bool writeReusableTempTextFile(
	ThreadLocalTempFile & slot,
	const char * prefix,
	const std::string & text,
	std::string & outPath) {
	if (slot.path.empty()) {
		slot.path = makeTempPath(prefix, ".txt");
	}
	if (!writeTextFile(slot.path, text)) {
		slot.path = makeTempPath(prefix, ".txt");
		if (!writeTextFile(slot.path, text)) {
			return false;
		}
	}
	outPath = slot.path;
	return true;
}

/// Extracts float values from a JSON array into the output vector.
/// Only finite values are included. Returns true if at least one value was extracted.
static bool extractEmbeddingArray(const ofJson & value, std::vector<float> & out) {
	out.clear();
	if (!value.is_array()) return false;
	for (const auto & item : value) {
		if (!item.is_number()) continue;
		const float v = item.get<float>();
		if (std::isfinite(v)) out.push_back(v);
	}
	return !out.empty();
}

/// Recursively searches JSON object for embedding data in common field names.
/// Checks: "embedding", "embeddings", "result", and "data" array.
/// Returns true if valid embedding array was found and extracted.
static bool parseEmbeddingJson(const ofJson & json, std::vector<float> & out) {
	if (json.is_array()) {
		return extractEmbeddingArray(json, out);
	}
	if (!json.is_object()) return false;

	if (json.contains("embedding")) {
		if (parseEmbeddingJson(json["embedding"], out)) return true;
	}
	if (json.contains("embeddings")) {
		if (parseEmbeddingJson(json["embeddings"], out)) return true;
	}
	if (json.contains("result")) {
		if (parseEmbeddingJson(json["result"], out)) return true;
	}
	if (json.contains("data") && json["data"].is_array()) {
		for (const auto & item : json["data"]) {
			if (parseEmbeddingJson(item, out)) return true;
		}
	}
	return false;
}

/// Helper: Attempts to parse a single string as JSON embedding.
static bool tryParseJsonString(const std::string & str, std::vector<float> & out) {
	ofJson parsed = ofJson::parse(str, nullptr, false);
	return !parsed.is_discarded() && parseEmbeddingJson(parsed, out);
}

/// Helper: Extracts bracketed float array from raw text (fallback parser).
/// Expects format like "[1.0, 2.0, 3.0]" and converts commas to spaces for parsing.
static bool tryParseBracketedArray(const std::string & raw, std::vector<float> & out) {
	const size_t begin = raw.find('[');
	const size_t end = raw.find(']', begin == std::string::npos ? 0 : begin + 1);
	// Validate that brackets exist and are properly positioned to avoid underflow
	if (begin == std::string::npos || end == std::string::npos || end <= begin || (end - begin) < 2) {
		return false;
	}

	std::string body = raw.substr(begin + 1, end - begin - 1);
	for (char & c : body) {
		if (c == ',') c = ' ';
	}

	std::istringstream iss(body);
	float v = 0.0f;
	while (iss >> v) {
		if (std::isfinite(v)) out.push_back(v);
	}
	return !out.empty();
}

/// Parses embedding vector from llama-cli output text.
/// Tries multiple strategies: (1) JSON parse whole text, (2) JSON parse per line (reverse order),
/// (3) Fallback to bracketed array format. Returns true if parsing succeeded.
static bool parseEmbeddingVector(const std::string & raw, std::vector<float> & out) {
	out.clear();

	// Strategy 1: Try parsing entire normalized text as JSON
	const std::string normalized = trim(raw);
	if (!normalized.empty() && tryParseJsonString(normalized, out)) {
		return true;
	}

	// Strategy 2: Try parsing each line as JSON (reverse order - last line likely has result)
	std::istringstream lines(raw);
	std::vector<std::string> candidates;
	std::string line;
	while (std::getline(lines, line)) {
		line = trim(line);
		if (!line.empty()) candidates.push_back(std::move(line));
	}
	for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
		if (tryParseJsonString(*it, out)) {
			return true;
		}
	}

	// Strategy 3: Fallback to bracketed array format
	return tryParseBracketedArray(raw, out);
}

/// Parses token count from verbose llama-cli output.
/// Uses three strategies: (1) Extract count from lines containing "token" keyword,
/// (2) Parse bracket notation like "[42]" and return max+1, (3) Count bracket lines.
/// Returns -1 if no token count found. Performs single-pass parsing for efficiency.
static int parseVerbosePromptTokenCount(const std::string & raw) {
	if (raw.empty()) return -1;

	int explicitCount = -1;
	int bracketMax = -1;
	int bracketLines = 0;

	// Single-pass parsing with case-insensitive comparison inline
	std::istringstream iss(raw);
	std::string line;
	line.reserve(256); // Pre-allocate typical line size

	while (std::getline(iss, line)) {
		if (line.empty()) continue;

		// Check for "token" keyword (case-insensitive) without allocating lowercase copies.
		const std::string_view tokenKeyword = "token";
		const auto tokenIt = std::search(
			line.begin(), line.end(),
			tokenKeyword.begin(), tokenKeyword.end(),
			[](char a, char b) {
				return std::tolower(static_cast<unsigned char>(a)) == b;
			});
		const bool foundToken = tokenIt != line.end();

		if (foundToken) {
			// Extract signed integer-like numbers from this line.
			const char * p = line.c_str();
			char * end = nullptr;
			while (*p != '\0') {
				if (!std::isdigit(static_cast<unsigned char>(*p)) && *p != '-' && *p != '+') {
					++p;
					continue;
				}
				errno = 0;
				const long value = std::strtol(p, &end, 10);
				if (end == p) {
					++p;
					continue;
				}
				if (errno == 0 && value > explicitCount && value < std::numeric_limits<int>::max()) {
					explicitCount = static_cast<int>(value);
				}
				p = end;
			}
		}

		// Check for bracket notation
		if (line.front() == '[') {
			const size_t end = line.find(']');
			if (end != std::string::npos && end > 1) {
				try {
					int idx = std::stoi(line.substr(1, end - 1));
					if (idx > bracketMax) bracketMax = idx;
				} catch (...) {
					// ignore parse errors
				}
			}
			++bracketLines;
		}
	}

	if (explicitCount >= 0) return explicitCount;
	if (bracketMax >= 0) return bracketMax + 1; // assume zero-based indices
	if (bracketLines > 0) return bracketLines;
	return -1;
}

} // namespace

ofxGgmlInference::ofxGgmlInference()
	: m_completionExe("llama-completion")
	, m_embeddingExe("llama-embedding") { }

void ofxGgmlInference::setCompletionExecutable(const std::string & path) {
	m_completionExe = path;
	std::lock_guard<std::mutex> lock(m_completionCapabilitiesMutex);
	m_completionCapabilitiesValid = false;
	m_completionCapabilities = {};
}

void ofxGgmlInference::setEmbeddingExecutable(const std::string & path) {
	m_embeddingExe = path;
}

const std::string & ofxGgmlInference::getCompletionExecutable() const {
	return m_completionExe;
}

const std::string & ofxGgmlInference::getEmbeddingExecutable() const {
	return m_embeddingExe;
}

ofxGgmlInferenceCapabilities ofxGgmlInference::probeCompletionCapabilities(
	bool forceRefresh) const {
	std::lock_guard<std::mutex> lock(m_completionCapabilitiesMutex);
	if (m_completionCapabilitiesValid && !forceRefresh) {
		return m_completionCapabilities;
	}

	m_completionCapabilities = {};
	if (m_completionExe.empty() || !isValidExecutablePath(m_completionExe)) {
		m_completionCapabilitiesValid = false;
		return m_completionCapabilities;
	}

	std::string helpText;
	int exitCode = -1;
	if (!runCommandCapture({m_completionExe, "--help"}, helpText, exitCode, true) ||
		helpText.empty()) {
		m_completionCapabilitiesValid = false;
		return m_completionCapabilities;
	}

	m_completionCapabilities.probed = true;
	m_completionCapabilities.helpText = helpText;
	m_completionCapabilities.supportsTopK =
		helpText.find("--top-k") != std::string::npos;
	m_completionCapabilities.supportsMinP =
		helpText.find("--min-p") != std::string::npos;
	m_completionCapabilities.supportsMirostat =
		helpText.find("--mirostat") != std::string::npos &&
		helpText.find("--mirostat-lr") != std::string::npos &&
		helpText.find("--mirostat-ent") != std::string::npos;
	m_completionCapabilities.supportsSingleTurn =
		helpText.find("--single-turn") != std::string::npos;
	m_completionCapabilitiesValid = true;
	return m_completionCapabilities;
}

ofxGgmlInferenceCapabilities ofxGgmlInference::getCompletionCapabilities() const {
	std::lock_guard<std::mutex> lock(m_completionCapabilitiesMutex);
	return m_completionCapabilities;
}

std::vector<ofxGgmlPromptSource> ofxGgmlInference::fetchUrlSources(
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

		ofHttpResponse response = ofLoadURL(url);
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

		size_t remainingChars = sourceSettings.maxTotalChars - usedChars;
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

std::vector<ofxGgmlPromptSource> ofxGgmlInference::collectScriptSourceDocuments(
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

std::string ofxGgmlInference::buildPromptWithSources(
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
			if (!trim(source.uri).empty() && trim(source.uri) != formatSourceLabel(source)) {
				ctx << "\nURI: " << trim(source.uri);
			}
		}
		ctx << "\n" << source.content << "\n";
	}

	if (usedSources) {
		*usedSources = std::move(normalizedSources);
	}
	return ctx.str();
}

std::string ofxGgmlInference::clampPromptToContext(
	const std::string & prompt,
	size_t contextTokens,
	bool * trimmed) {
	bool wasTrimmed = false;
	if (contextTokens > 0) {
		const size_t charBudget = std::max<size_t>(512, contextTokens * 3);
		if (prompt.size() > charBudget) {
			wasTrimmed = true;
			const size_t head = std::min<size_t>(2048, charBudget / 4);
			if (charBudget <= head + 96) {
				if (trimmed) {
					*trimmed = true;
				}
				return prompt.substr(prompt.size() - charBudget);
			}
			const size_t tail = charBudget - head - 32;
			if (trimmed) {
				*trimmed = true;
			}
			return prompt.substr(0, head) +
				"\n...[context trimmed to fit window]...\n" +
				prompt.substr(prompt.size() - tail);
		}
	}
	if (trimmed) {
		*trimmed = wasTrimmed;
	}
	return prompt;
}

bool ofxGgmlInference::isLikelyCutoffOutput(
	const std::string & text,
	bool codeLike) {
	const std::string trimmedText = trim(text);
	if (trimmedText.empty()) return false;
	if (trimmedText.rfind("[Error]", 0) == 0) return false;

	const char last = trimmedText.back();
	if (codeLike) {
		if (last == '\n' || last == '}' || last == ')' ||
			last == ']' || last == ';') {
			return false;
		}
		return trimmedText.size() > 80;
	}

	if (last == '.' || last == '!' || last == '?' ||
		last == '"' || last == '\'') {
		return false;
	}
	return trimmedText.size() > 80;
}

std::string ofxGgmlInference::buildCutoffContinuationRequest(
	const std::string & tailText) {
	return
		"Continue exactly from where the previous answer stopped. "
		"Do not restart. Finish the incomplete thought/code naturally.\n\n"
		"Tail of previous output:\n" + tailText;
}

ofxGgmlInferenceResult ofxGgmlInference::generate(
const std::string & modelPath,
const std::string & prompt,
const ofxGgmlInferenceSettings & settings,
std::function<bool(const std::string&)> onChunk) const {
	ofxGgmlInferenceResult result;
	if (modelPath.empty()) {
		result.error = "model path is empty";
		return result;
	}
	if (m_completionExe.empty()) {
		result.error = "completion executable path is empty";
		return result;
	}

	// Security: Validate model path
	if (!isValidFilePath(modelPath)) {
		result.error = "invalid or inaccessible model path: " + modelPath;
		return result;
	}

	// Security: Validate executable path
	if (!isValidExecutablePath(m_completionExe)) {
		result.error = "invalid or inaccessible completion executable: " + m_completionExe;
		return result;
	}

	bool promptWasTrimmed = false;
	std::string effectivePrompt = settings.trimPromptToContext
		? clampPromptToContext(prompt, static_cast<size_t>(std::max(settings.contextSize, 0)), &promptWasTrimmed)
		: prompt;

	// Security: Sanitize prompt
	std::string sanitizedPrompt = sanitizeArgument(effectivePrompt);
	if (sanitizedPrompt.empty() && !prompt.empty()) {
		result.error = "prompt contains only invalid characters";
		return result;
	}
	result.promptWasTrimmed = promptWasTrimmed;

	ofxGgmlInferenceCapabilities capabilities;
	if (settings.autoProbeCliCapabilities) {
		capabilities = probeCompletionCapabilities();
	}

	const auto t0 = std::chrono::steady_clock::now();
	static thread_local ThreadLocalTempFile promptTempFile;
	std::string promptPath;
	if (!writeReusableTempTextFile(promptTempFile, "ofxggml_prompt_", sanitizedPrompt, promptPath)) {
		result.error = "failed to write temp prompt file";
		return result;
	}

	std::string effectivePromptCachePath = settings.promptCachePath;
	if (effectivePromptCachePath.empty() && settings.autoPromptCache) {
		effectivePromptCachePath = defaultPromptCachePathForModel(modelPath);
	}
	if (!effectivePromptCachePath.empty()) {
		std::error_code ec;
		std::filesystem::path cachePath(effectivePromptCachePath);
		if (std::filesystem::exists(cachePath, ec) &&
			!isValidFilePath(effectivePromptCachePath)) {
			result.error = "invalid prompt cache path: " + effectivePromptCachePath;
			return result;
		}
	}
	if (!settings.grammarPath.empty()) {
		if (!isValidFilePath(settings.grammarPath)) {
			result.error = "invalid grammar file path: " + settings.grammarPath;
			return result;
		}
	}

	int effectiveBatch = std::clamp(settings.batchSize, 1, 8192);
	if ((!settings.device.empty() || settings.gpuLayers > 0) && effectiveBatch > 256) {
		effectiveBatch = 256;
	}

	auto buildArgs = [&](int batchOverride, bool simpleIoEnabled) {
		std::vector<std::string> args;
		args.reserve(48);
		args.emplace_back(m_completionExe);
		args.emplace_back("-m");
		args.emplace_back(modelPath);
		args.emplace_back("--file");
		args.emplace_back(promptPath);
		args.emplace_back("-n");
		args.emplace_back(std::to_string(std::clamp(settings.maxTokens, 1, 8192)));
		args.emplace_back("-c");
		args.emplace_back(std::to_string(std::clamp(settings.contextSize, 128, 131072)));
		args.emplace_back("-b");
		args.emplace_back(std::to_string(batchOverride));
		args.emplace_back("-ub");
		args.emplace_back(std::to_string(std::clamp(settings.ubatchSize, 1, 8192)));
		args.emplace_back("-ngl");
		args.emplace_back(std::to_string(std::max(0, settings.gpuLayers)));
		if (!settings.device.empty()) {
			args.emplace_back("--device");
			args.emplace_back(settings.device);
			args.emplace_back("--split-mode");
			args.emplace_back("none");
		}
		args.emplace_back("--temp");
		args.emplace_back(std::to_string(std::clamp(settings.temperature, 0.0f, 3.0f)));
		args.emplace_back("--top-p");
		args.emplace_back(std::to_string(std::clamp(settings.topP, 0.0f, 1.0f)));
		if (!capabilities.probed || capabilities.supportsMinP) {
			args.emplace_back("--min-p");
			args.emplace_back(std::to_string(std::clamp(settings.minP, 0.0f, 1.0f)));
		}
		if (!capabilities.probed || capabilities.supportsTopK) {
			args.emplace_back("--top-k");
			args.emplace_back(std::to_string(std::clamp(settings.topK, 0, 100)));
		}
		args.emplace_back("--presence-penalty");
		args.emplace_back(std::to_string(std::clamp(settings.presencePenalty, -2.0f, 2.0f)));
		args.emplace_back("--frequency-penalty");
		args.emplace_back(std::to_string(std::clamp(settings.frequencyPenalty, -2.0f, 2.0f)));
		args.emplace_back("--repeat-penalty");
		args.emplace_back(std::to_string(std::clamp(settings.repeatPenalty, 1.0f, 3.0f)));
		args.emplace_back("--no-display-prompt");
		args.emplace_back("--log-disable");

		if (simpleIoEnabled) {
			args.emplace_back("--simple-io");
		}
		if (settings.singleTurn &&
			(!capabilities.probed || capabilities.supportsSingleTurn)) {
			args.emplace_back("--single-turn");
		}
		if ((settings.mirostat == 1 || settings.mirostat == 2) &&
			(!capabilities.probed || capabilities.supportsMirostat)) {
			args.emplace_back("--mirostat");
			args.emplace_back(std::to_string(settings.mirostat));
			args.emplace_back("--mirostat-lr");
			args.emplace_back(std::to_string(std::clamp(settings.mirostatEta, 0.0f, 1.0f)));
			args.emplace_back("--mirostat-ent");
			args.emplace_back(std::to_string(std::clamp(settings.mirostatTau, 0.0f, 20.0f)));
		}
		if (settings.flashAttn) {
			args.emplace_back("-fa");
		}
		if (settings.mlock) {
			args.emplace_back("--mlock");
		}
		if (settings.threads > 0) {
			args.emplace_back("--threads");
			args.emplace_back(std::to_string(std::clamp(settings.threads, 1, 256)));
		}
		if (settings.threadsBatch > 0) {
			args.emplace_back("--threads-batch");
			args.emplace_back(std::to_string(std::clamp(settings.threadsBatch, 1, 256)));
		}
		if (settings.seed >= 0) {
			args.emplace_back("--seed");
			args.emplace_back(std::to_string(settings.seed));
		}
		if (!settings.chatTemplate.empty()) {
			args.emplace_back("--chat-template");
			args.emplace_back(settings.chatTemplate);
		}
		if (!effectivePromptCachePath.empty()) {
			args.emplace_back("--prompt-cache");
			args.emplace_back(effectivePromptCachePath);
			if (settings.promptCacheAll) {
				args.emplace_back("--prompt-cache-all");
			}
		}
		if (!settings.jsonSchema.empty()) {
			args.emplace_back("--json-schema");
			args.emplace_back(sanitizeArgument(settings.jsonSchema));
		}
		if (!settings.grammarPath.empty()) {
			args.emplace_back("--grammar-file");
			args.emplace_back(settings.grammarPath);
		}
		return args;
	};

	std::string raw;
	std::string cleaned;
	int exitCode = -1;
	auto tryRun = [&](int batchOverride, bool simpleIoEnabled, std::string & passRaw, std::string & passCleaned, int & passExitCode, std::function<bool(const std::string &)> chunkHandler) {
		const auto args = buildArgs(batchOverride, simpleIoEnabled);
		const bool started = runCommandCapture(args, passRaw, passExitCode, false, chunkHandler);
		if (!started) {
			return false;
		}
		passCleaned = cleanCompletionOutput(passRaw, sanitizedPrompt);
		if (passExitCode != 0 && passCleaned.empty()) {
			std::string diagRaw;
			int diagExitCode = -1;
			if (runCommandCapture(args, diagRaw, diagExitCode, true)) {
				const std::string diagCleaned = cleanCompletionOutput(diagRaw, sanitizedPrompt);
				if (!diagCleaned.empty()) {
					passCleaned = diagCleaned;
				} else {
					passRaw = cleanStructuredOutput(diagRaw);
				}
			}
		}
		if (passExitCode != 0 &&
			shouldTreatNonZeroExitAsSuccess(passExitCode, !passCleaned.empty(), passRaw)) {
			passExitCode = 0;
		}
		return true;
	};

	if (!tryRun(effectiveBatch, settings.simpleIo, raw, cleaned, exitCode, onChunk)) {
		result.error = "failed to start llama completion process";
		return result;
	}

	if (exitCode == 130 && settings.simpleIo) {
		std::string retryRaw;
		std::string retryCleaned;
		int retryExitCode = -1;
		if (tryRun(effectiveBatch, false, retryRaw, retryCleaned, retryExitCode, nullptr)) {
			if ((retryExitCode == 0 && !retryCleaned.empty()) ||
				retryCleaned.size() > cleaned.size()) {
				raw = std::move(retryRaw);
				cleaned = std::move(retryCleaned);
				exitCode = retryExitCode;
			}
		}
	}

	if (settings.allowBatchFallback && exitCode != 0 && cleaned.empty() && effectiveBatch > 128) {
		std::string retryRaw;
		std::string retryCleaned;
		int retryExitCode = -1;
		const int fallbackBatch = std::min(effectiveBatch, 128);
		if (tryRun(fallbackBatch, settings.simpleIo, retryRaw, retryCleaned, retryExitCode, nullptr)) {
			if (retryExitCode == 0 || !retryCleaned.empty()) {
				raw = std::move(retryRaw);
				cleaned = std::move(retryCleaned);
				exitCode = retryExitCode;
			}
		}
	}

	if (exitCode != 0) {
		result.error = !raw.empty() ? trim(raw) : cleaned;
		if (result.error.empty()) {
			result.error = "llama completion failed with exit code " + std::to_string(exitCode);
		}
		return result;
	}

	result.success = true;
	result.text = settings.stopAtNaturalBoundary
		? trimToNaturalBoundary(cleaned)
		: trim(cleaned);
	result.outputLikelyCutoff = isLikelyCutoffOutput(
		result.text,
		looksLikeCodeOutput(result.text));

	if (settings.autoContinueCutoff &&
		result.outputLikelyCutoff &&
		!result.text.empty()) {
		ofxGgmlInferenceSettings continuationSettings = settings;
		continuationSettings.autoContinueCutoff = false;
		const size_t tailChars = std::min<size_t>(result.text.size(), 600);
		const std::string continuationRequest = buildCutoffContinuationRequest(
			result.text.substr(result.text.size() - tailChars));
		ofxGgmlInferenceResult continuation = generate(
			modelPath,
			continuationRequest,
			continuationSettings,
			nullptr);
		if (continuation.success && !continuation.text.empty()) {
			result.text += "\n" + continuation.text;
			result.outputLikelyCutoff = isLikelyCutoffOutput(
				continuation.text,
				looksLikeCodeOutput(continuation.text));
			result.continuationCount = continuation.continuationCount + 1;
		}
	}

	const auto t1 = std::chrono::steady_clock::now();
	result.elapsedMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
	return result;
}

ofxGgmlInferenceResult ofxGgmlInference::generateWithSources(
	const std::string & modelPath,
	const std::string & prompt,
	const std::vector<ofxGgmlPromptSource> & sources,
	const ofxGgmlInferenceSettings & settings,
	const ofxGgmlPromptSourceSettings & sourceSettings,
	std::function<bool(const std::string &)> onChunk) const {
	std::vector<ofxGgmlPromptSource> usedSources;
	const std::string promptWithSources = buildPromptWithSources(
		prompt,
		sources,
		sourceSettings,
		&usedSources);

	ofxGgmlInferenceResult result = generate(
		modelPath,
		promptWithSources,
		settings,
		onChunk);
	result.sourcesUsed = std::move(usedSources);
	return result;
}

ofxGgmlInferenceResult ofxGgmlInference::generateWithUrls(
	const std::string & modelPath,
	const std::string & prompt,
	const std::vector<std::string> & urls,
	const ofxGgmlInferenceSettings & settings,
	const ofxGgmlPromptSourceSettings & sourceSettings,
	std::function<bool(const std::string &)> onChunk) const {
	const auto sources = fetchUrlSources(urls, sourceSettings);
	return generateWithSources(
		modelPath,
		prompt,
		sources,
		settings,
		sourceSettings,
		onChunk);
}

ofxGgmlInferenceResult ofxGgmlInference::generateWithScriptSource(
	const std::string & modelPath,
	const std::string & prompt,
	ofxGgmlScriptSource & scriptSource,
	const ofxGgmlInferenceSettings & settings,
	const ofxGgmlPromptSourceSettings & sourceSettings,
	std::function<bool(const std::string &)> onChunk) const {
	const auto sources = collectScriptSourceDocuments(scriptSource, sourceSettings);
	return generateWithSources(
		modelPath,
		prompt,
		sources,
		settings,
		sourceSettings,
		onChunk);
}

ofxGgmlEmbeddingResult ofxGgmlInference::embed(
	const std::string & modelPath,
	const std::string & text,
	const ofxGgmlEmbeddingSettings & settings) const {
	ofxGgmlEmbeddingResult result;
	if (modelPath.empty()) {
		result.error = "model path is empty";
		return result;
	}
	if (m_embeddingExe.empty()) {
		result.error = "embedding executable path is empty";
		return result;
	}

	// Security: Validate model path
	if (!isValidFilePath(modelPath)) {
		result.error = "invalid or inaccessible model path: " + modelPath;
		return result;
	}

	// Security: Validate executable path
	if (!isValidExecutablePath(m_embeddingExe)) {
		result.error = "invalid or inaccessible embedding executable: " + m_embeddingExe;
		return result;
	}

	// Security: Sanitize input text
	std::string sanitizedText = sanitizeArgument(text);
	if (sanitizedText.empty() && !text.empty()) {
		result.error = "text contains only invalid characters";
		return result;
	}

	static thread_local ThreadLocalTempFile promptTempFile;
	std::string promptPath;
	if (!writeReusableTempTextFile(promptTempFile, "ofxggml_embed_", sanitizedText, promptPath)) {
		result.error = "failed to write temp embedding prompt file";
		return result;
	}

	std::vector<std::string> args;
	args.reserve(16);
	args.emplace_back(m_embeddingExe);
	args.emplace_back("-m");
	args.emplace_back(modelPath);
	args.emplace_back("--file");
	args.emplace_back(promptPath);
	args.emplace_back("--embd-output-format");
	args.emplace_back("json");
	args.emplace_back("--pooling");
	args.emplace_back(settings.pooling);
	args.emplace_back("--log-disable");
	if (settings.normalize) {
		args.emplace_back("--embd-normalize");
	}

	std::string raw;
	int exitCode = -1;
	const bool started = runCommandCapture(args, raw, exitCode, false);

	if (!started) {
		result.error = "failed to start llama embedding process";
		return result;
	}

	raw = cleanStructuredOutput(raw);
	if (exitCode != 0 && raw.empty()) {
		std::string diagRaw;
		int diagExitCode = -1;
		if (runCommandCapture(args, diagRaw, diagExitCode, true)) {
			raw = cleanStructuredOutput(diagRaw);
		}
	}

	if (exitCode != 0 && shouldTreatNonZeroExitAsSuccess(exitCode, !trim(raw).empty(), raw)) {
		exitCode = 0;
	}

	if (exitCode != 0) {
		result.error = trim(raw);
		if (result.error.empty()) {
			result.error = "llama embedding failed with exit code " + std::to_string(exitCode);
		}
		return result;
	}

	if (!parseEmbeddingVector(raw, result.embedding)) {
		result.error = "failed to parse embedding output";
		return result;
	}

	result.success = true;
	return result;
}

int ofxGgmlInference::countPromptTokens(
	const std::string & modelPath,
	const std::string & text) const {
	if (modelPath.empty() || m_completionExe.empty()) return -1;

	const size_t textHash = std::hash<std::string>{}(text);
	const std::string cacheKey = modelPath + "|" + std::to_string(text.size()) + "|" + std::to_string(textHash);
	{
		std::lock_guard<std::mutex> lock(m_tokenCountCacheMutex);
		auto it = m_tokenCountCache.find(cacheKey);
		if (it != m_tokenCountCache.end()) {
			return it->second;
		}
	}

	if (!isValidFilePath(modelPath)) {
		return -1;
	}
	if (!isValidExecutablePath(m_completionExe)) {
		return -1;
	}

	std::string sanitized = sanitizeArgument(text);
	if (sanitized.empty() && !text.empty()) {
		return -1;
	}

	static thread_local ThreadLocalTempFile promptTempFile;
	std::string promptPath;
	if (!writeReusableTempTextFile(promptTempFile, "ofxggml_tok_", sanitized, promptPath)) {
		return -1;
	}

	std::vector<std::string> args;
	args.reserve(12);
	args.emplace_back(m_completionExe);
	args.emplace_back("-m");
	args.emplace_back(modelPath);
	args.emplace_back("--file");
	args.emplace_back(promptPath);
	args.emplace_back("--vocab-only");
	args.emplace_back("-n");
	args.emplace_back("0");
	args.emplace_back("--verbose-prompt");
	args.emplace_back("--no-display-prompt");
	args.emplace_back("--log-disable");

	std::string raw;
	int exitCode = -1;
	if (!runCommandCapture(args, raw, exitCode, false) || exitCode != 0) {
		return -1;
	}

	raw = cleanStructuredOutput(raw);
	const int tokenCount = parseVerbosePromptTokenCount(raw);
	if (tokenCount >= 0) {
		std::lock_guard<std::mutex> lock(m_tokenCountCacheMutex);
		if (m_tokenCountCache.size() > 2000) {
			m_tokenCountCache.clear();
		}
		m_tokenCountCache[cacheKey] = tokenCount;
	}
	return tokenCount;
}

std::vector<std::string> ofxGgmlInference::tokenize(const std::string & text) {
	std::vector<std::string> tokens;
	std::istringstream iss(text);
	std::string tok;
	while (iss >> tok) {
		tokens.push_back(tok);
	}
	return tokens;
}

std::string ofxGgmlInference::detokenize(const std::vector<std::string> & tokens) {
	std::ostringstream oss;
	for (size_t i = 0; i < tokens.size(); ++i) {
		if (i > 0) oss << ' ';
		oss << tokens[i];
	}
	return oss.str();
}

int ofxGgmlInference::sampleFromLogits(
	const std::vector<float> & logits,
	float temperature,
	float topP,
	uint32_t seed) {
	if (logits.empty()) return -1;
	if (!std::isfinite(temperature) || temperature <= 0.0f) {
		return static_cast<int>(std::distance(logits.begin(), std::max_element(logits.begin(), logits.end())));
	}

	const float maxLogit = *std::max_element(logits.begin(), logits.end());
	std::vector<std::pair<float, size_t>> prob_idx;
	prob_idx.reserve(logits.size());
	float sum = 0.0f;
	for (size_t i = 0; i < logits.size(); ++i) {
		const float z = (logits[i] - maxLogit) / temperature;
		const float p = std::exp(z);
		const float valid_p = std::isfinite(p) ? p : 0.0f;
		prob_idx.emplace_back(valid_p, i);
		sum += valid_p;
	}
	if (sum <= 0.0f) {
		return static_cast<int>(std::distance(logits.begin(), std::max_element(logits.begin(), logits.end())));
	}
	for (auto & pi : prob_idx) {
		pi.first /= sum;
	}

	std::sort(prob_idx.begin(), prob_idx.end(), [](const std::pair<float, size_t>& a, const std::pair<float, size_t>& b) {
		return a.first > b.first;
	});

	topP = std::clamp(topP, 0.0f, 1.0f);
	if (topP <= 0.0f) {
		return static_cast<int>(prob_idx.front().second);
	}

	float selectedMass = 0.0f;
	size_t selectedCount = 0;
	for (const auto & pi : prob_idx) {
		selectedMass += pi.first;
		++selectedCount;
		if (selectedMass >= topP) break;
	}
	if (selectedCount == 0) {
		return static_cast<int>(prob_idx.front().second);
	}

	std::mt19937 rng(seed == 0 ? makeRandomSeed() : seed);
	std::uniform_real_distribution<float> dist(0.0f, selectedMass);
	const float target = dist(rng);

	float running = 0.0f;
	for (size_t i = 0; i < selectedCount; ++i) {
		running += prob_idx[i].first;
		if (target <= running) {
			return static_cast<int>(prob_idx[i].second);
		}
	}

	return static_cast<int>(prob_idx[selectedCount - 1].second);
}

void ofxGgmlEmbeddingIndex::clear() {
	m_entries.clear();
}

void ofxGgmlEmbeddingIndex::add(const std::string & id, const std::string & text, const std::vector<float> & embedding) {
	if (embedding.empty()) return;
	m_entries.push_back({ id, text, embedding });
}

std::vector<ofxGgmlSimilarityHit> ofxGgmlEmbeddingIndex::search(const std::vector<float> & queryEmbedding, size_t topK) const {
	std::vector<ofxGgmlSimilarityHit> hits;
	if (queryEmbedding.empty() || m_entries.empty() || topK == 0) return hits;

	hits.reserve(m_entries.size());
	for (size_t i = 0; i < m_entries.size(); ++i) {
		const auto & e = m_entries[i];
		hits.push_back({ e.id, e.text, cosineSimilarity(queryEmbedding, e.embedding), i });
	}

	const size_t limit = std::min(topK, hits.size());
	auto byScoreDesc = [](const ofxGgmlSimilarityHit & a, const ofxGgmlSimilarityHit & b) {
		return a.score > b.score;
	};
	if (limit < hits.size()) {
		std::partial_sort(hits.begin(), hits.begin() + limit, hits.end(), byScoreDesc);
		hits.resize(limit);
	} else {
		std::sort(hits.begin(), hits.end(), byScoreDesc);
	}
	return hits;
}

float ofxGgmlEmbeddingIndex::cosineSimilarity(const std::vector<float> & a, const std::vector<float> & b) {
	if (a.empty() || b.empty() || a.size() != b.size()) return 0.0f;
	double dot = 0.0;
	double na = 0.0;
	double nb = 0.0;
	const float* pa = a.data();
	const float* pb = b.data();
	const size_t sz = a.size();
	for (size_t i = 0; i < sz; ++i) {
		const double da = pa[i];
		const double db = pb[i];
		dot += da * db;
		na += da * da;
		nb += db * db;
	}
	if (na <= 0.0 || nb <= 0.0) return 0.0f;
	return static_cast<float>(dot / (std::sqrt(na) * std::sqrt(nb)));
}

// -----------------------------------------------------------------------------
// ofxGgmlInferenceAsync
// -----------------------------------------------------------------------------

ofxGgmlInferenceAsync::ofxGgmlInferenceAsync() {}

ofxGgmlInferenceAsync::~ofxGgmlInferenceAsync() {
	stopInference();
	waitForThread(true);
}

void ofxGgmlInferenceAsync::startInference(
	const std::string & modelPath,
	const std::string & prompt,
	const ofxGgmlInferenceSettings & settings) {
	
	if (isThreadRunning()) {
		ofLogWarning("ofxGgmlInferenceAsync") << "Inference is already running.";
		return;
	}

	m_modelPath = modelPath;
	m_prompt = prompt;
	m_settings = settings;
	m_fullResponse.clear();

	// Drain the queue of any stale tokens from a previous run
	std::string stale;
	while (m_tokenQueue.tryReceive(stale)) {}

	startThread();
}

void ofxGgmlInferenceAsync::stopInference() {
	if (isThreadRunning()) {
		stopThread();
	}
}

void ofxGgmlInferenceAsync::update() {
	std::string chunk;
	// Process all queued tokens this frame
	while (m_tokenQueue.tryReceive(chunk)) {
		m_fullResponse += chunk;
		ofNotifyEvent(onTokenStream, chunk, this);
	}
}

void ofxGgmlInferenceAsync::threadedFunction() {
	ofxGgmlInference localInference;
	// Since we are running in an async thread, let's inject our own token handler 
	// into the generate call to capture tokens as they stream from the CLI pipe.
	auto chunkCallback = [this](const std::string& tokenChunk) -> bool {
		// Stop thread request received
		if (!isThreadRunning()) return false;
		
		if (!tokenChunk.empty()) {
			m_tokenQueue.send(tokenChunk);
		}
		return true;
	};

	ofxGgmlInferenceResult result = localInference.generate(
		m_modelPath,
		m_prompt,
		m_settings,
		chunkCallback);

	// In async context, the text may be the joined cleaned response,
	// but the UI typically relies on the actual stream. We still include
	// the cleaned text inside the final result.
	ofNotifyEvent(onInferenceComplete, result, this);
}
