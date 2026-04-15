#include "ofxGgmlInference.h"

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
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <string_view>

#ifdef _WIN32
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

/// Strip common llama.cpp warning messages from output.
/// Even with --log-disable, some warnings may still appear in stderr
/// which gets captured alongside stdout by runCommandCapture.
static std::string stripLlamaWarnings(const std::string & text) {
	if (text.empty()) return text;

	std::ostringstream filtered;
	std::istringstream lines(text);
	std::string line;

	while (std::getline(lines, line)) {
		// Skip lines that are common llama.cpp warnings and backend initialization messages
		// Use more specific patterns to avoid filtering legitimate model output
		if (line.find("warning: no usable GPU found") != std::string::npos || line.find("warning: one possible reason is that llama.cpp was compiled without GPU support") != std::string::npos || line.find("warning: consult docs/build.md for compilation instructions") != std::string::npos || line.find("--gpu-layers option will be ignored") != std::string::npos || line.find("ggml_cuda_init") != std::string::npos || line.find("ggml_vulkan_init") != std::string::npos || line.find("ggml_metal_init") != std::string::npos) {
			continue;
		}

		// Filter backend/GPU log lines that start with specific prefixes
		// to avoid filtering model output that might mention these terms
		const std::string_view trimmedLine = trimView(line);

		if (trimmedLine.rfind("ofxGgml [INFO]", 0) == 0 || trimmedLine.rfind("ofxGgml [WARN]", 0) == 0 || trimmedLine.rfind("ofxGgml [ERROR]", 0) == 0) {
			continue;
		}

		// Keep non-warning lines
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
	if (trimmedLine.find("ggml_cuda_init") != std::string::npos) return true;
	if (trimmedLine.find("ggml_vulkan_init") != std::string::npos) return true;
	if (trimmedLine.find("ggml_metal_init") != std::string::npos) return true;
	if (trimmedLine.find("warning: no usable GPU found") != std::string::npos) return true;
	if (trimmedLine.find("--gpu-layers option will be ignored") != std::string::npos) return true;
	if (trimmedLine.find("Total VRAM:") != std::string::npos) return true;
	if (trimmedLine.find("compute capability") != std::string::npos) return true;
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

	std::error_code ec;
	std::filesystem::path fsPath(path);

	// For security, normalize the path to resolve any symlinks
	std::filesystem::path canonical = std::filesystem::weakly_canonical(fsPath, ec);
	if (ec) {
		// If we can't canonicalize, try basic existence check
		if (!std::filesystem::exists(fsPath, ec) || ec) return false;
		if (!std::filesystem::is_regular_file(fsPath, ec) || ec) return false;
		return true;
	}

	// Check if the canonical path exists
	if (!std::filesystem::exists(canonical, ec) || ec) return false;
	if (!std::filesystem::is_regular_file(canonical, ec) || ec) return false;
	return true;
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
	bool mergeStderr = true) {
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
		output.append(buf.data(), bytesRead);
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
		output.append(buf.data(), static_cast<size_t>(bytesRead));
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

ofxGgmlInferenceResult ofxGgmlInference::generate(
	const std::string & modelPath,
	const std::string & prompt,
	const ofxGgmlInferenceSettings & settings) const {
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

	// Security: Sanitize prompt
	std::string sanitizedPrompt = sanitizeArgument(prompt);
	if (sanitizedPrompt.empty() && !prompt.empty()) {
		result.error = "prompt contains only invalid characters";
		return result;
	}

	const auto t0 = std::chrono::steady_clock::now();
	static thread_local ThreadLocalTempFile promptTempFile;
	std::string promptPath;
	if (!writeReusableTempTextFile(promptTempFile, "ofxggml_prompt_", sanitizedPrompt, promptPath)) {
		result.error = "failed to write temp prompt file";
		return result;
	}

	std::vector<std::string> args;
	args.reserve(32); // Pre-reserve for typical number of arguments
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
	args.emplace_back(std::to_string(std::clamp(settings.batchSize, 1, 8192)));
	args.emplace_back("-ngl");
	args.emplace_back(std::to_string(std::max(0, settings.gpuLayers)));
	args.emplace_back("--temp");
	args.emplace_back(std::to_string(std::clamp(settings.temperature, 0.0f, 3.0f)));
	args.emplace_back("--top-p");
	args.emplace_back(std::to_string(std::clamp(settings.topP, 0.0f, 1.0f)));
	args.emplace_back("--repeat-penalty");
	args.emplace_back(std::to_string(std::clamp(settings.repeatPenalty, 1.0f, 3.0f)));
	args.emplace_back("--no-display-prompt");
	args.emplace_back("--log-disable");

	if (settings.simpleIo) {
		args.emplace_back("--simple-io");
	}
	if (settings.threads > 0) {
		args.emplace_back("--threads");
		args.emplace_back(std::to_string(std::clamp(settings.threads, 1, 256)));
	}
	if (settings.seed >= 0) {
		args.emplace_back("--seed");
		args.emplace_back(std::to_string(settings.seed));
	}
	if (!settings.promptCachePath.empty()) {
		// Security: Validate cache path if it exists, or allow creation of new file
		std::error_code ec;
		std::filesystem::path cachePath(settings.promptCachePath);
		if (std::filesystem::exists(cachePath, ec)) {
			if (!isValidFilePath(settings.promptCachePath)) {
				result.error = "invalid prompt cache path: " + settings.promptCachePath;
				return result;
			}
		}
		args.emplace_back("--prompt-cache");
		args.emplace_back(settings.promptCachePath);
		if (settings.promptCacheAll) {
			args.emplace_back("--prompt-cache-all");
		}
	}
	if (!settings.jsonSchema.empty()) {
		// Security: Sanitize JSON schema
		std::string sanitizedSchema = sanitizeArgument(settings.jsonSchema);
		args.emplace_back("--json-schema");
		args.emplace_back(std::move(sanitizedSchema));
	}
	if (!settings.grammarPath.empty()) {
		// Security: Validate grammar file path
		if (!isValidFilePath(settings.grammarPath)) {
			result.error = "invalid grammar file path: " + settings.grammarPath;
			return result;
		}
		args.emplace_back("--grammar-file");
		args.emplace_back(settings.grammarPath);
	}

	std::string raw;
	int exitCode = -1;
	const bool started = runCommandCapture(args, raw, exitCode, false);

	if (!started) {
		result.error = "failed to start llama completion process";
		return result;
	}

	std::string cleaned = cleanCompletionOutput(raw, sanitizedPrompt);
	if (exitCode == 130 && settings.simpleIo) {
		std::vector<std::string> retryArgs = args;
		retryArgs.erase(
			std::remove(retryArgs.begin(), retryArgs.end(), std::string("--simple-io")),
			retryArgs.end());

		std::string retryRaw;
		int retryExitCode = -1;
		if (runCommandCapture(retryArgs, retryRaw, retryExitCode, false)) {
			std::string retryCleaned = cleanCompletionOutput(retryRaw, sanitizedPrompt);
			if (retryExitCode == 0 && !retryCleaned.empty()) {
				raw = std::move(retryRaw);
				cleaned = std::move(retryCleaned);
				exitCode = 0;
			} else if (retryCleaned.size() > cleaned.size()) {
				raw = std::move(retryRaw);
				cleaned = std::move(retryCleaned);
				exitCode = retryExitCode;
			}
		}
	}
	if (exitCode != 0 && cleaned.empty()) {
		std::string diagRaw;
		int diagExitCode = -1;
		if (runCommandCapture(args, diagRaw, diagExitCode, true)) {
			const std::string diagCleaned = cleanCompletionOutput(diagRaw, sanitizedPrompt);
			if (!diagCleaned.empty()) {
				cleaned = diagCleaned;
			} else {
				raw = cleanStructuredOutput(diagRaw);
			}
		}
	}

	// Exit code 130 (128 + SIGINT) specifically indicates SIGINT, often from EOF on stdin
	// or Ctrl+C. This is frequently benign even without output (e.g., initialization only).
	// For other exit codes, only treat as benign if we have actual output.
	if (exitCode != 0) {
		const bool isSigint = (exitCode == 130);
		const bool hasOutput = !cleaned.empty();
		const bool interruptedMarker =
			raw.find("EOF by user") != std::string::npos ||
			raw.find("Interrupted by user") != std::string::npos;
		const bool benignExit = (isSigint && hasOutput && !interruptedMarker)
			|| (hasOutput && (exitCode == -1073740791 // Windows STATUS_STACK_BUFFER_OVERRUN (0xC0000409)
					|| exitCode == -1073741819 // Windows STATUS_ACCESS_VIOLATION (0xC0000005)
					|| exitCode == 1 // generic error (may occur during cleanup)
					|| exitCode == -1 // signal-killed on POSIX
					|| (exitCode >= 128 && exitCode < 160) // POSIX signal exits (128+signal)
					));
		if (benignExit) {
			exitCode = 0;
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
	result.text = trimToNaturalBoundary(cleaned);
	const auto t1 = std::chrono::steady_clock::now();
	result.elapsedMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
	return result;
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

	// Exit code 130 (128 + SIGINT) specifically indicates SIGINT, often from EOF on stdin
	// or Ctrl+C. This is frequently benign even without output (e.g., initialization only).
	// For other exit codes, only treat as benign if we have actual output.
	if (exitCode != 0) {
		const bool isSigint = (exitCode == 130);
		const bool hasOutput = !trim(raw).empty();
		const bool benignExit = isSigint // SIGINT (EOF on stdin) - benign even without output
			|| (hasOutput && (exitCode == -1073740791 // Windows STATUS_STACK_BUFFER_OVERRUN (0xC0000409)
					|| exitCode == -1073741819 // Windows STATUS_ACCESS_VIOLATION (0xC0000005)
					|| exitCode == 1 // generic error (may occur during cleanup)
					|| exitCode == -1 // signal-killed on POSIX
					|| (exitCode >= 128 && exitCode < 160) // POSIX signal exits (128+signal)
					));
		if (benignExit) {
			exitCode = 0;
		}
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
	return parseVerbosePromptTokenCount(raw);
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
