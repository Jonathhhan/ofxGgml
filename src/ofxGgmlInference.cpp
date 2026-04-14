#include "ofxGgmlInference.h"

#include <algorithm>
#include <array>
#include <cctype>
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

#ifdef _WIN32
	#include <windows.h>
#else
	#include <fcntl.h>
	#include <sys/wait.h>
	#include <unistd.h>
#endif

namespace {

static std::string trim(const std::string & s) {
	size_t b = 0;
	while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
		++b;
	size_t e = s.size();
	while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
		--e;
	return s.substr(b, e - b);
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
		const std::string trimmedLine = [&line]() {
			size_t start = 0;
			while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) {
				start++;
			}
			return line.substr(start);
		}();

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

static bool startsWithWord(const std::string & text, const std::string & word) {
	if (text.size() < word.size()) return false;
	if (text.compare(0, word.size(), word) != 0) return false;
	if (text.size() == word.size()) return true;
	const char after = text[word.size()];
	return after == '\n' || after == '\r' || after == ':' || after == ' ';
}

static bool isRuntimeNoiseLine(const std::string & trimmedLine) {
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
		const std::string trimmedLine = trim(line);
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
		for (const auto & label : {
			std::string("user"),
			std::string("assistant"),
			std::string("system"),
			std::string("User"),
			std::string("Assistant"),
			std::string("System"),
			std::string("A:"),
			std::string("> ")
		}) {
			if (startsWithWord(out, label)) {
				out = trim(out.substr(label.size()));
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
		for (const auto & artifact : {
			std::string("> EOF by user"),
			std::string("> EOF"),
			std::string("EOF"),
			std::string("Interrupted by user")
		}) {
			if (out.size() >= artifact.size() &&
				out.compare(out.size() - artifact.size(), artifact.size(), artifact) == 0) {
				out = trim(out.substr(0, out.size() - artifact.size()));
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
	cleaned = stripLeadingRuntimeNoise(cleaned);
	return trim(cleaned);
}

static std::string cleanStructuredOutput(const std::string & raw) {
	return trim(stripLeadingRuntimeNoise(stripLlamaWarnings(raw)));
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

	// Check for null bytes and suspicious characters
	if (path.find('\0') != std::string::npos) return false;
	if (path.find("..") != std::string::npos) return false; // Path traversal

	std::error_code ec;
	std::filesystem::path fsPath(path);

	// For security, normalize the path to resolve any symlinks
	std::filesystem::path canonical = std::filesystem::weakly_canonical(fsPath, ec);
	if (ec) {
		// If we can't canonicalize, try basic existence check
		return std::filesystem::exists(fsPath, ec) && !ec;
	}

	// Check if the canonical path exists
	return std::filesystem::exists(canonical, ec) && !ec;
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
		// On POSIX systems, std::ios::noreplace is C++23, so use filesystem approach
		std::error_code ec;
		if (!std::filesystem::exists(candidate, ec) && !ec) {
			std::ofstream test(candidate, std::ios::out);
			if (test.is_open()) {
				test.close();
				return candidate.string();
			}
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

		// Check for "token" keyword (case-insensitive, inline comparison)
		// More efficient than creating a lowercase copy of entire line
		const char * tokenKeyword = "token";
		size_t pos = 0;
		bool foundToken = false;
		while (pos + 5 <= line.size()) {
			bool match = true;
			for (size_t i = 0; i < 5 && match; ++i) {
				const char c = line[pos + i];
				const char k = tokenKeyword[i];
				match = (c == k || std::tolower(static_cast<unsigned char>(c)) == k);
			}
			if (match) {
				foundToken = true;
				break;
			}
			++pos;
		}

		if (foundToken) {
			// Extract numbers from this line
			std::istringstream ls(line);
			long value = 0;
			while (ls >> value) {
				if (value > explicitCount && value < std::numeric_limits<int>::max()) {
					explicitCount = static_cast<int>(value);
				}
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
	: m_completionExe("llama-cli")
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

	std::vector<std::string> args = {
		m_completionExe,
		"-m", modelPath,
		"--file", promptPath,
		"-n", std::to_string(std::clamp(settings.maxTokens, 1, 8192)),
		"-c", std::to_string(std::clamp(settings.contextSize, 128, 131072)),
		"-b", std::to_string(std::clamp(settings.batchSize, 1, 8192)),
		"-ngl", std::to_string(std::max(0, settings.gpuLayers)),
		"--temp", std::to_string(std::clamp(settings.temperature, 0.0f, 3.0f)),
		"--top-p", std::to_string(std::clamp(settings.topP, 0.0f, 1.0f)),
		"--repeat-penalty", std::to_string(std::clamp(settings.repeatPenalty, 1.0f, 3.0f)),
		"--no-display-prompt",
		"--log-disable"
	};

	if (settings.simpleIo) {
		args.push_back("--simple-io");
	}
	if (settings.threads > 0) {
		args.push_back("--threads");
		args.push_back(std::to_string(std::clamp(settings.threads, 1, 256)));
	}
	if (settings.seed >= 0) {
		args.push_back("--seed");
		args.push_back(std::to_string(settings.seed));
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
		args.push_back("--prompt-cache");
		args.push_back(settings.promptCachePath);
		if (settings.promptCacheAll) {
			args.push_back("--prompt-cache-all");
		}
	}
	if (!settings.jsonSchema.empty()) {
		// Security: Sanitize JSON schema
		std::string sanitizedSchema = sanitizeArgument(settings.jsonSchema);
		args.push_back("--json-schema");
		args.push_back(sanitizedSchema);
	}
	if (!settings.grammarPath.empty()) {
		// Security: Validate grammar file path
		if (!isValidFilePath(settings.grammarPath)) {
			result.error = "invalid grammar file path: " + settings.grammarPath;
			return result;
		}
		args.push_back("--grammar-file");
		args.push_back(settings.grammarPath);
	}

	std::string raw;
	int exitCode = -1;
	const bool started = runCommandCapture(args, raw, exitCode, false);

	if (!started) {
		result.error = "failed to start llama completion process";
		return result;
	}

	std::string cleaned = cleanCompletionOutput(raw, sanitizedPrompt);
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
		result.error = !raw.empty() ? trim(raw) : cleaned;
		if (result.error.empty()) {
			result.error = "llama completion failed with exit code " + std::to_string(exitCode);
		}
		return result;
	}

	result.success = true;
	result.text = cleaned;
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

	std::vector<std::string> args = {
		m_embeddingExe,
		"-m", modelPath,
		"--file", promptPath,
		"--embd-output-format", "json",
		"--pooling", settings.pooling,
		"--log-disable"
	};
	if (settings.normalize) {
		args.push_back("--embd-normalize");
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

	std::vector<std::string> args = {
		m_completionExe,
		"-m", modelPath,
		"--file", promptPath,
		"--vocab-only",
		"-n", "0",
		"--verbose-prompt",
		"--no-display-prompt",
		"--log-disable"
	};

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
	std::vector<float> probs(logits.size(), 0.0f);
	float sum = 0.0f;
	for (size_t i = 0; i < logits.size(); ++i) {
		const float z = (logits[i] - maxLogit) / temperature;
		const float p = std::exp(z);
		probs[i] = std::isfinite(p) ? p : 0.0f;
		sum += probs[i];
	}
	if (sum <= 0.0f) {
		return static_cast<int>(std::distance(logits.begin(), std::max_element(logits.begin(), logits.end())));
	}
	for (float & p : probs)
		p /= sum;

	std::vector<size_t> idx(probs.size());
	std::iota(idx.begin(), idx.end(), static_cast<size_t>(0));
	std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return probs[a] > probs[b]; });

	topP = std::clamp(topP, 0.0f, 1.0f);
	if (topP <= 0.0f) {
		return static_cast<int>(idx.front());
	}

	std::vector<double> filtered;
	std::vector<size_t> filteredIdx;
	filtered.reserve(probs.size());
	filteredIdx.reserve(probs.size());
	float cum = 0.0f;
	for (size_t i : idx) {
		filtered.push_back(probs[i]);
		filteredIdx.push_back(i);
		cum += probs[i];
		if (cum >= topP) break;
	}

	std::mt19937 rng(seed == 0 ? makeRandomSeed() : seed);
	std::discrete_distribution<size_t> dist(filtered.begin(), filtered.end());
	return static_cast<int>(filteredIdx[dist(rng)]);
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
		std::nth_element(hits.begin(), hits.begin() + limit, hits.end(), byScoreDesc);
		hits.resize(limit);
	}
	std::sort(hits.begin(), hits.end(), byScoreDesc);
	return hits;
}

float ofxGgmlEmbeddingIndex::cosineSimilarity(const std::vector<float> & a, const std::vector<float> & b) {
	if (a.empty() || b.empty() || a.size() != b.size()) return 0.0f;
	double dot = 0.0;
	double na = 0.0;
	double nb = 0.0;
	for (size_t i = 0; i < a.size(); ++i) {
		const double da = a[i];
		const double db = b[i];
		dot += da * db;
		na += da * da;
		nb += db * db;
	}
	if (na <= 0.0 || nb <= 0.0) return 0.0f;
	return static_cast<float>(dot / (std::sqrt(na) * std::sqrt(nb)));
}
