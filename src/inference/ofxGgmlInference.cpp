#include "ofxGgmlInference.h"
#include "ofxGgmlInferenceSourceInternals.h"
#include "ofxGgmlInferenceServerInternals.h"
#include "ofxGgmlInferenceTextCleanup.h"
#include "core/ofxGgmlWindowsUtf8.h"
#include "core/ofxGgmlMetrics.h"
#include "support/ofxGgmlProcessSecurity.h"
#include "support/ofxGgmlScriptSource.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <numeric>
#include <random>
#include <regex>
#include <sstream>
#include <string_view>
#include <thread>
#include <unordered_set>

#ifdef _WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
	#include <winhttp.h>
	#pragma comment(lib, "winhttp.lib")
#else
	#include <fcntl.h>
	#include <sys/wait.h>
	#include <unistd.h>
#endif

#if defined(_WIN32) && !defined(OFXGGML_HEADLESS_STUBS)
	#define OFXGGML_HAS_SERVER_STREAMING 1
#else
	#define OFXGGML_HAS_SERVER_STREAMING 0
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

static bool isValidFilePath(const std::string & path) {
	if (path.empty()) return false;
	if (path.find('\0') != std::string::npos) return false;

	std::error_code ec;
	std::filesystem::path fsPath(path);
	if (!std::filesystem::exists(fsPath, ec)) return false;
	if (ec) return false;
	if (!std::filesystem::is_regular_file(fsPath, ec)) return false;
	if (ec) return false;
	return true;
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
	return false;
}

static std::string sanitizeArgument(const std::string & arg) {
	std::string result;
	result.reserve(arg.size());

	for (char c : arg) {
		if (c == '\0' || (std::iscntrl(static_cast<unsigned char>(c)) && c != '\t' && c != '\n' && c != '\r')) {
			continue;
		}
		result += c;
	}

	return result;
}

static bool runCommandCapture(
	const std::vector<std::string> & args,
	std::string & output,
	int & exitCode,
	bool mergeStderr = true,
	std::function<bool(const std::string &)> onChunk = nullptr) {
	output.clear();
	exitCode = -1;
	if (args.empty() || args.front().empty()) return false;
	std::string pendingChunk;
	auto dispatchChunk = [&](const std::string & chunk) -> bool {
		if (!onChunk || chunk.empty()) {
			return true;
		}
		pendingChunk.append(chunk);
		size_t newlinePos = std::string::npos;
		while ((newlinePos = pendingChunk.find('\n')) != std::string::npos) {
			const std::string segment = pendingChunk.substr(0, newlinePos);
			pendingChunk.erase(0, newlinePos + 1);
			if (!onChunk(segment)) {
				return false;
			}
		}
		if (!pendingChunk.empty()) {
			const std::string segment = pendingChunk;
			pendingChunk.clear();
			return onChunk(segment);
		}
		return true;
	};
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

	STARTUPINFOW si {};
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
	const std::string cmdLine = ofxGgmlProcessSecurity::buildWindowsCommandLine(args);
	if (cmdLine.empty()) {
		CloseHandle(readPipe);
		CloseHandle(writePipe);
		if (nullInput != INVALID_HANDLE_VALUE) {
			CloseHandle(nullInput);
		}
		if (nullErr != INVALID_HANDLE_VALUE) {
			CloseHandle(nullErr);
		}
		return false;
	}
	std::wstring wideCmdLine = ofxGgmlWideFromUtf8(cmdLine);
	std::vector<wchar_t> mutableCmd(wideCmdLine.begin(), wideCmdLine.end());
	mutableCmd.push_back(L'\0');

	BOOL ok = CreateProcessW(
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
		if (!dispatchChunk(chunk)) {
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
		if (!dispatchChunk(chunk)) {
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

	std::random_device rd;
	std::mt19937_64 rng(rd());
	std::uniform_int_distribution<uint64_t> dist;

	for (int attempts = 0; attempts < 1000; ++attempts) {
		const uint64_t random1 = dist(rng);
		const uint64_t random2 = dist(rng);
		std::ostringstream oss;
		oss << prefix << std::hex << random1 << "_" << random2 << ext;

		std::filesystem::path candidate = base / oss.str();

#ifdef _WIN32
		HANDLE hFile = CreateFileW(
			candidate.c_str(),
			GENERIC_WRITE,
			0,
			NULL,
			CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL,
			NULL);
		if (hFile != INVALID_HANDLE_VALUE) {
			CloseHandle(hFile);
			return candidate.string();
		}
#else
		const int fd = open(candidate.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
		if (fd >= 0) {
			close(fd);
			return candidate.string();
		}
#endif
	}

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

static size_t normalizedConcurrencyLimit(size_t requestedLimit) {
	return std::max<size_t>(1, requestedLimit);
}

static size_t recommendedServerEmbeddingConcurrency(size_t requestCount) {
	if (requestCount <= 1) return requestCount;
	const unsigned int hardwareThreads =
		std::max(1u, std::thread::hardware_concurrency());
	return std::min<size_t>(requestCount, std::min<size_t>(4, hardwareThreads));
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
	if (m_completionExe.empty() ||
		!ofxGgmlProcessSecurity::isValidExecutablePath(m_completionExe)) {
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
	return ofxGgmlInferenceSourceInternals::fetchUrlSources(urls, sourceSettings);
}

ofxGgmlServerProbeResult ofxGgmlInference::probeServer(
	const std::string & serverUrl,
	bool fetchModels) {
	ofxGgmlServerProbeResult result;
	result.baseUrl = ofxGgmlInferenceServerInternals::serverBaseUrlFromConfiguredUrl(serverUrl);
	result.chatCompletionsUrl =
		ofxGgmlInferenceServerInternals::normalizeServerUrl(serverUrl);
	result.embeddingsUrl =
		ofxGgmlInferenceServerInternals::normalizeServerEmbeddingsUrl(serverUrl);

#ifdef OFXGGML_HEADLESS_STUBS
	result.error = "server probing requires openFrameworks HTTP runtime";
	return result;
#else
	ofHttpResponse response;
	response = ofLoadURL(result.baseUrl + "/health");
	if (response.status >= 200 && response.status < 300) {
		result.reachable = true;
		result.healthOk = true;
	}

	if (fetchModels) {
		const ofHttpResponse modelsResponse =
			ofLoadURL(ofxGgmlInferenceServerInternals::normalizeServerModelsUrl(serverUrl));
		if (modelsResponse.status >= 200 && modelsResponse.status < 300) {
			result.reachable = true;
			result.modelsOk = true;
			result.embeddingsRouteLikely = true;
			result.modelIds =
				ofxGgmlInferenceServerInternals::extractModelIdsFromServerResponse(
					modelsResponse.data.getText());
			result.visionCapable =
				ofxGgmlInferenceServerInternals::detectVisionCapabilityFromServerResponse(
					modelsResponse.data.getText());
			result.routerLikely = result.modelIds.size() > 1;
			if (!result.modelIds.empty()) {
				result.activeModel = result.modelIds.front();
			}
		} else if (!result.reachable) {
			response = modelsResponse;
		}
	}

	if (!result.reachable) {
		result.error = trim(response.error);
		if (result.error.empty()) {
			result.error = trim(response.data.getText());
		}
		if (result.error.empty() && response.status > 0) {
			result.error = "HTTP status " + std::to_string(response.status);
		}
		if (result.error.empty()) {
			result.error = "server probe failed";
		}
	}
	result.capabilitySummary =
		ofxGgmlInferenceServerInternals::buildServerCapabilitySummary(result);

	return result;
#endif
}

std::vector<ofxGgmlPromptSource> ofxGgmlInference::collectScriptSourceDocuments(
	ofxGgmlScriptSource & scriptSource,
	const ofxGgmlPromptSourceSettings & sourceSettings) {
	return ofxGgmlInferenceSourceInternals::collectScriptSourceDocuments(
		scriptSource,
		sourceSettings);
}

std::string ofxGgmlInference::buildPromptWithSources(
	const std::string & prompt,
	const std::vector<ofxGgmlPromptSource> & sources,
	const ofxGgmlPromptSourceSettings & sourceSettings,
	std::vector<ofxGgmlPromptSource> * usedSources) {
	return ofxGgmlInferenceSourceInternals::buildPromptWithSources(
		prompt,
		sources,
		sourceSettings,
		usedSources);
}

std::vector<ofxGgmlPromptSource> ofxGgmlInference::fetchRealtimeSources(
	const std::string & queryOrPrompt,
	const ofxGgmlRealtimeInfoSettings & realtimeSettings) {
	return ofxGgmlInferenceSourceInternals::fetchRealtimeSources(
		queryOrPrompt,
		realtimeSettings);
}

std::string ofxGgmlInference::buildPromptWithRealtimeInfo(
	const std::string & prompt,
	const std::string & queryOrPrompt,
	const ofxGgmlRealtimeInfoSettings & realtimeSettings,
	std::vector<ofxGgmlPromptSource> * usedSources) {
	return ofxGgmlInferenceSourceInternals::buildPromptWithRealtimeInfo(
		prompt,
		queryOrPrompt,
		realtimeSettings,
		usedSources);
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

std::string ofxGgmlInference::sanitizeGeneratedText(
	const std::string & raw,
	const std::string & prompt) {
	return ofxGgmlInferenceTextCleanup::sanitizeGeneratedText(raw, prompt);
}

std::string ofxGgmlInference::sanitizeStructuredText(
	const std::string & raw) {
	return ofxGgmlInferenceTextCleanup::sanitizeStructuredText(raw);
}

ofxGgmlInferenceResult ofxGgmlInference::generate(
	const std::string & modelPath,
const std::string & prompt,
const ofxGgmlInferenceSettings & settings,
std::function<bool(const std::string&)> onChunk) const {
	ofxGgmlInferenceResult result;
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

	if (settings.useServerBackend || !trim(settings.serverUrl).empty()) {
#ifdef OFXGGML_HEADLESS_STUBS
		result.error = "server-backed inference requires openFrameworks HTTP runtime";
		return result;
#else
		const auto t0 = std::chrono::steady_clock::now();
	const std::string requestUrl =
		ofxGgmlInferenceServerInternals::normalizeServerUrl(settings.serverUrl);
		try {
			ofJson payload;
			const bool requestStreaming = (onChunk != nullptr);
			payload["messages"] = ofJson::array({
				{
					{"role", "user"},
					{"content", sanitizedPrompt}
				}
			});
			payload["max_tokens"] = std::max(1, settings.maxTokens);
			payload["temperature"] = std::clamp(settings.temperature, 0.0f, 2.0f);
			payload["top_p"] = std::clamp(settings.topP, 0.0f, 1.0f);
			payload["stream"] = requestStreaming;
			if (settings.topK > 0) {
				payload["top_k"] = settings.topK;
			}
			if (settings.minP > 0.0f) {
				payload["min_p"] = settings.minP;
			}
			if (settings.repeatPenalty > 0.0f) {
				payload["repeat_penalty"] = settings.repeatPenalty;
			}
			if (settings.seed >= 0) {
				payload["seed"] = settings.seed;
			}
			std::string serverModel = trim(settings.serverModel);
			if (serverModel.empty()) {
			serverModel =
				ofxGgmlInferenceServerInternals::resolveCachedActiveServerModel(
					settings.serverUrl);
			}
			if (!serverModel.empty()) {
				payload["model"] = serverModel;
			}

			auto performNonStreamingRequest = [&](const ofJson & requestPayload) -> ofxGgmlInferenceResult {
				ofxGgmlInferenceResult serverResult;
				ofHttpRequest request(requestUrl, "text-inference");
				request.method = ofHttpRequest::POST;
				request.body = requestPayload.dump();
				request.contentType = "application/json";
				request.headers["Accept"] = "application/json";
				request.timeoutSeconds = 180;

				ofURLFileLoader loader;
				const ofHttpResponse response = loader.handleRequest(request);
				const std::string responseText = response.data.getText();
				if (response.status < 200 || response.status >= 300) {
				std::string detail = trim(
					ofxGgmlInferenceServerInternals::extractTextFromOpenAiResponse(
						responseText));
					if (detail.empty()) {
						detail = trim(responseText);
					}
					if (detail.empty()) {
						detail = trim(response.error);
					}
					serverResult.error = "server-backed inference failed with HTTP " +
						ofToString(response.status) + ": " + detail;
					return serverResult;
				}

			serverResult.text = trim(
				ofxGgmlInferenceServerInternals::extractTextFromOpenAiResponse(
					responseText));
				serverResult.success = !serverResult.text.empty();
				if (!serverResult.success) {
					serverResult.error = "server-backed inference returned empty output";
				}
				return serverResult;
			};

			if (!requestStreaming || !OFXGGML_HAS_SERVER_STREAMING) {
				payload["stream"] = false;
				result = performNonStreamingRequest(payload);
				if (onChunk && !result.text.empty()) {
					onChunk(result.text);
				}
			} else {
				const std::string requestBody = payload.dump();
				const std::wstring wideUrl = ofxGgmlWideFromUtf8(requestUrl);
				URL_COMPONENTS components{};
				components.dwStructSize = sizeof(components);
				components.dwSchemeLength = static_cast<DWORD>(-1);
				components.dwHostNameLength = static_cast<DWORD>(-1);
				components.dwUrlPathLength = static_cast<DWORD>(-1);
				components.dwExtraInfoLength = static_cast<DWORD>(-1);
				if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components)) {
					result.error = "server-backed inference failed: invalid server URL";
					return result;
				}

				const std::wstring host(components.lpszHostName, components.dwHostNameLength);
				std::wstring path(
					components.lpszUrlPath ? components.lpszUrlPath : L"/",
					components.dwUrlPathLength);
				if (path.empty()) {
					path = L"/";
				}
				if (components.lpszExtraInfo && components.dwExtraInfoLength > 0) {
					path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
				}

				HINTERNET session = WinHttpOpen(
					L"ofxGgml/1.0",
					WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
					WINHTTP_NO_PROXY_NAME,
					WINHTTP_NO_PROXY_BYPASS,
					0);
				HINTERNET connect = nullptr;
				HINTERNET request = nullptr;
				auto closeHandle = [](HINTERNET & handle) {
					if (handle) {
						WinHttpCloseHandle(handle);
						handle = nullptr;
					}
				};

				if (!session) {
					result.error = "server-backed inference failed: unable to open WinHTTP session";
					return result;
				}
				WinHttpSetTimeouts(session, 180000, 180000, 180000, 180000);

				connect = WinHttpConnect(session, host.c_str(), components.nPort, 0);
				if (!connect) {
					closeHandle(session);
					result.error = "server-backed inference failed: unable to connect to server";
					return result;
				}

				const DWORD requestFlags =
					components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
				request = WinHttpOpenRequest(
					connect,
					L"POST",
					path.c_str(),
					nullptr,
					WINHTTP_NO_REFERER,
					WINHTTP_DEFAULT_ACCEPT_TYPES,
					requestFlags);
				if (!request) {
					closeHandle(connect);
					closeHandle(session);
					result.error = "server-backed inference failed: unable to open request";
					return result;
				}

				static const wchar_t * headers =
					L"Content-Type: application/json\r\nAccept: text/event-stream\r\n";
				const BOOL sent = WinHttpSendRequest(
					request,
					headers,
					static_cast<DWORD>(-1L),
					reinterpret_cast<LPVOID>(const_cast<char *>(requestBody.data())),
					static_cast<DWORD>(requestBody.size()),
					static_cast<DWORD>(requestBody.size()),
					0);
				if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
					closeHandle(request);
					closeHandle(connect);
					closeHandle(session);
					result.error = "server-backed inference failed: request transmission failed";
					return result;
				}

				DWORD statusCode = 0;
				DWORD statusCodeSize = sizeof(statusCode);
				WinHttpQueryHeaders(
					request,
					WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
					WINHTTP_HEADER_NAME_BY_INDEX,
					&statusCode,
					&statusCodeSize,
					WINHTTP_NO_HEADER_INDEX);

				auto readRemainingBody = [&](std::string & bodyOut) {
					bodyOut.clear();
					for (;;) {
						DWORD available = 0;
						if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
							break;
						}
						std::string chunk(static_cast<size_t>(available), '\0');
						DWORD bytesRead = 0;
						if (!WinHttpReadData(request, chunk.data(), available, &bytesRead) ||
							bytesRead == 0) {
							break;
						}
						chunk.resize(bytesRead);
						bodyOut += chunk;
					}
				};

				if (statusCode < 200 || statusCode >= 300) {
					std::string body;
					readRemainingBody(body);
					closeHandle(request);
					closeHandle(connect);
					closeHandle(session);
			std::string detail = trim(
				ofxGgmlInferenceServerInternals::extractTextFromOpenAiResponse(body));
					if (detail.empty()) {
						detail = trim(body);
					}
					result.error = "server-backed inference failed with HTTP " +
						ofToString(static_cast<int>(statusCode)) + ": " + detail;
					return result;
				}

				std::string accumulated;
				std::string pending;
				for (;;) {
					DWORD available = 0;
					if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
						break;
					}

					std::string chunk(static_cast<size_t>(available), '\0');
					DWORD bytesRead = 0;
					if (!WinHttpReadData(request, chunk.data(), available, &bytesRead) ||
						bytesRead == 0) {
						break;
					}
					chunk.resize(bytesRead);
					pending += chunk;

					size_t newlinePos = std::string::npos;
					while ((newlinePos = pending.find('\n')) != std::string::npos) {
						std::string line = pending.substr(0, newlinePos);
						pending.erase(0, newlinePos + 1);
						if (!line.empty() && line.back() == '\r') {
							line.pop_back();
						}
						const std::string trimmedLine = trim(line);
						if (trimmedLine.empty() || trimmedLine.rfind(":", 0) == 0) {
							continue;
						}
						if (trimmedLine.rfind("data:", 0) != 0) {
							continue;
						}

						const std::string eventPayload = trim(trimmedLine.substr(5));
						if (eventPayload.empty()) {
							continue;
						}
						if (eventPayload == "[DONE]") {
							pending.clear();
							break;
						}

					const std::string delta =
						ofxGgmlInferenceServerInternals::extractDeltaTextFromOpenAiStreamEvent(
							eventPayload);
						if (delta.empty()) {
							continue;
						}
						accumulated += delta;
						if (onChunk && !onChunk(delta)) {
							closeHandle(request);
							closeHandle(connect);
							closeHandle(session);
							result.error = "server-backed inference cancelled";
							result.text = trim(accumulated);
							return result;
						}
					}
				}
				closeHandle(request);
				closeHandle(connect);
				closeHandle(session);
				result.text = trim(accumulated);
				if (result.text.empty()) {
					ofJson retryPayload = payload;
					retryPayload["stream"] = false;
					result = performNonStreamingRequest(retryPayload);
					if (onChunk && !result.text.empty()) {
						onChunk(result.text);
					}
				}
			}
			if (result.text.empty()) {
				result.error = "server-backed inference returned empty output";
				return result;
			}
			result.success = true;
			result.outputLikelyCutoff = isLikelyCutoffOutput(
				result.text,
				looksLikeCodeOutput(result.text));
			result.elapsedMs = std::chrono::duration<float, std::milli>(
				std::chrono::steady_clock::now() - t0).count();
			return result;
		} catch (const std::exception & e) {
			result.error = std::string("server-backed inference failed: ") + e.what();
			return result;
		}
#endif
	}

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
	if (!ofxGgmlProcessSecurity::isValidExecutablePath(m_completionExe)) {
		result.error = "invalid or inaccessible completion executable: " + m_completionExe;
		return result;
	}

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
		args.emplace_back("--color");
		args.emplace_back("off");

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
		passCleaned =
			ofxGgmlInferenceTextCleanup::sanitizeGeneratedText(
				passRaw,
				sanitizedPrompt);
		if (passCleaned.empty() && !trim(passRaw).empty()) {
			passCleaned =
				ofxGgmlInferenceTextCleanup::sanitizeGeneratedText(passRaw);
		}
		if (passExitCode != 0 && passCleaned.empty()) {
			std::string diagRaw;
			int diagExitCode = -1;
			if (runCommandCapture(args, diagRaw, diagExitCode, true)) {
				const std::string diagCleaned =
					ofxGgmlInferenceTextCleanup::sanitizeGeneratedText(
						diagRaw,
						sanitizedPrompt);
				if (!diagCleaned.empty()) {
					passCleaned = diagCleaned;
				} else {
					passRaw = ofxGgmlInferenceTextCleanup::sanitizeStructuredText(diagRaw);
					if (passCleaned.empty() && !trim(passRaw).empty()) {
						passCleaned =
							ofxGgmlInferenceTextCleanup::sanitizeGeneratedText(passRaw);
					}
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

	if (exitCode == 0 && cleaned.empty() &&
		(settings.simpleIo || !effectivePromptCachePath.empty())) {
		const std::string originalPromptCachePath = effectivePromptCachePath;
		std::string retryRaw;
		std::string retryCleaned;
		int retryExitCode = -1;

		// Some model / CLI combinations return an empty success when simple-io or
		// prompt-cache reuse is enabled. Retry once with the more conservative path.
		effectivePromptCachePath.clear();
		if (tryRun(effectiveBatch, false, retryRaw, retryCleaned, retryExitCode, nullptr)) {
			if ((retryExitCode == 0 && !retryCleaned.empty()) ||
				retryCleaned.size() > cleaned.size()) {
				raw = std::move(retryRaw);
				cleaned = std::move(retryCleaned);
				exitCode = retryExitCode;
			}
		}
		effectivePromptCachePath = originalPromptCachePath;
	}

	if (exitCode != 0) {
		result.error = !raw.empty() ? trim(raw) : cleaned;
		if (result.error.empty()) {
			result.error = "llama completion failed with exit code " + std::to_string(exitCode);
		}
		return result;
	}

	if (cleaned.empty()) {
		result.error = "llama completion returned empty output";
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

Result<ofxGgmlInferenceResult> ofxGgmlInference::generateEx(
	const std::string & modelPath,
	const std::string & prompt,
	const ofxGgmlInferenceSettings & settings,
	std::function<bool(const std::string &)> onChunk) const {
	const ofxGgmlInferenceResult result = generate(
		modelPath,
		prompt,
		settings,
		std::move(onChunk));
	if (result.success) {
		return result;
	}
	const std::string error = trim(result.error);
	if (error.find("executable path is empty") != std::string::npos ||
		error.find("invalid or inaccessible") != std::string::npos) {
		return ofxGgmlError(ofxGgmlErrorCode::InferenceExecutableMissing, error);
	}
	if (error.find("model path is empty") != std::string::npos ||
		error.find("prompt is empty") != std::string::npos ||
		error.find("invalid model path") != std::string::npos) {
		return ofxGgmlError(ofxGgmlErrorCode::InvalidArgument, error);
	}
	return ofxGgmlError(ofxGgmlErrorCode::InferenceProcessFailed, error);
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

ofxGgmlInferenceResult ofxGgmlInference::generateWithRealtimeInfo(
	const std::string & modelPath,
	const std::string & prompt,
	const std::string & queryOrPrompt,
	const ofxGgmlInferenceSettings & settings,
	const ofxGgmlRealtimeInfoSettings & realtimeSettings,
	std::function<bool(const std::string &)> onChunk) const {
	std::vector<ofxGgmlPromptSource> usedSources;
	const std::string promptWithSources = buildPromptWithRealtimeInfo(
		prompt,
		queryOrPrompt,
		realtimeSettings,
		&usedSources);
	ofxGgmlInferenceResult result = generate(
		modelPath,
		promptWithSources,
		settings,
		onChunk);
	result.sourcesUsed = std::move(usedSources);
	return result;
}

ofxGgmlEmbeddingResult ofxGgmlInference::embed(
	const std::string & modelPath,
	const std::string & text,
	const ofxGgmlEmbeddingSettings & settings) const {
	ofxGgmlEmbeddingResult result;
	const bool shouldTryLocalEmbeddingFallback =
		settings.allowLocalFallback &&
		!modelPath.empty() &&
		!m_embeddingExe.empty();
	if (settings.useServerBackend || !trim(settings.serverUrl).empty()) {
#ifdef OFXGGML_HEADLESS_STUBS
		result.error = "server-backed embeddings require openFrameworks HTTP runtime";
		return result;
#else
		std::string sanitizedText = sanitizeArgument(text);
		if (sanitizedText.empty() && !text.empty()) {
			result.error = "text contains only invalid characters";
			return result;
		}

	const std::string requestUrl =
		ofxGgmlInferenceServerInternals::normalizeServerEmbeddingsUrl(
			settings.serverUrl);
		try {
			ofJson payload;
			payload["input"] = sanitizedText;
			const std::string pooling = trim(settings.pooling);
			if (!pooling.empty()) {
				payload["pooling"] = pooling;
			}
			if (!settings.normalize) {
				payload["embd_normalize"] = -1;
			}
			std::string serverModel = trim(settings.serverModel);
			if (serverModel.empty()) {
			serverModel =
				ofxGgmlInferenceServerInternals::resolveCachedActiveServerModel(
					settings.serverUrl);
			}
			if (!serverModel.empty()) {
				payload["model"] = serverModel;
			}

			ofHttpRequest request(requestUrl, "embedding-inference");
			request.method = ofHttpRequest::POST;
			request.body = payload.dump();
			request.contentType = "application/json";
			request.headers["Accept"] = "application/json";
			request.timeoutSeconds = 180;

			ofURLFileLoader loader;
			const ofHttpResponse response = loader.handleRequest(request);
			const std::string responseText = response.data.getText();
			if (response.status < 200 || response.status >= 300) {
				result.error = trim(responseText);
				if (result.error.empty()) {
					result.error =
						"server-backed embedding failed with HTTP status " +
					std::to_string(response.status);
				}
				if (!shouldTryLocalEmbeddingFallback) {
					return result;
				}
			}
			else {
				ofJson parsed = ofJson::parse(responseText, nullptr, false);
				if (!parsed.is_discarded() && parseEmbeddingJson(parsed, result.embedding)) {
					result.success = true;
					return result;
				}
				result.error = "failed to parse server embedding output";
				if (!shouldTryLocalEmbeddingFallback) {
					return result;
				}
			}
		} catch (const std::exception & e) {
			result.error = std::string("server-backed embedding failed: ") + e.what();
			if (!shouldTryLocalEmbeddingFallback) {
				return result;
			}
		} catch (...) {
			result.error = "server-backed embedding failed: unknown error";
			if (!shouldTryLocalEmbeddingFallback) {
				return result;
			}
		}
#endif
	}
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
	if (!ofxGgmlProcessSecurity::isValidExecutablePath(m_embeddingExe)) {
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

	raw = ofxGgmlInferenceTextCleanup::sanitizeStructuredText(raw);
	if (exitCode != 0 && raw.empty()) {
		std::string diagRaw;
		int diagExitCode = -1;
		if (runCommandCapture(args, diagRaw, diagExitCode, true)) {
			raw = ofxGgmlInferenceTextCleanup::sanitizeStructuredText(diagRaw);
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

Result<ofxGgmlEmbeddingResult> ofxGgmlInference::embedEx(
	const std::string & modelPath,
	const std::string & text,
	const ofxGgmlEmbeddingSettings & settings) const {
	const ofxGgmlEmbeddingResult result = embed(modelPath, text, settings);
	if (result.success) {
		return result;
	}
	const std::string error = trim(result.error);
	if (error.find("executable path is empty") != std::string::npos ||
		error.find("invalid or inaccessible") != std::string::npos) {
		return ofxGgmlError(ofxGgmlErrorCode::InferenceExecutableMissing, error);
	}
	if (error.find("model path is empty") != std::string::npos ||
		error.find("text is empty") != std::string::npos ||
		error.find("invalid model path") != std::string::npos) {
		return ofxGgmlError(ofxGgmlErrorCode::InvalidArgument, error);
	}
	return ofxGgmlError(ofxGgmlErrorCode::InferenceProcessFailed, error);
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
	if (!ofxGgmlProcessSecurity::isValidExecutablePath(m_completionExe)) {
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

	raw = ofxGgmlInferenceTextCleanup::sanitizeStructuredText(raw);
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
// Batch Inference Implementation
// -----------------------------------------------------------------------------

ofxGgmlBatchResult ofxGgmlInference::generateBatch(
	const std::string & modelPath,
	const std::vector<ofxGgmlBatchRequest> & requests,
	const ofxGgmlBatchSettings & batchSettings) const {

	ofxGgmlBatchResult batchResult;
	batchResult.results.reserve(requests.size());

	if (requests.empty()) {
		batchResult.success = true;
		return batchResult;
	}

	// Record batch start
	auto& metrics = ofxGgmlMetrics::getInstance();
	metrics.recordBatchStart(modelPath, requests.size());

	const auto startTime = ofGetElapsedTimeMillis();
	const bool allowParallelBatchProcessing =
		batchSettings.allowParallelProcessing &&
		normalizedConcurrencyLimit(batchSettings.maxConcurrentRequests) > 1;

	// Check if all requests share compatible settings for server batch mode
	bool allUseServer = true;
	bool settingsCompatible = true;
	ofxGgmlInferenceSettings sharedSettings;

	if (!requests.empty()) {
		sharedSettings = requests[0].settings;
		for (const auto & req : requests) {
			if (!req.settings.useServerBackend) {
				allUseServer = false;
			}
			// Check key parameters that affect batching compatibility
			if (req.settings.maxTokens != sharedSettings.maxTokens ||
				req.settings.temperature != sharedSettings.temperature ||
				req.settings.contextSize != sharedSettings.contextSize) {
				settingsCompatible = false;
			}
		}
	}

	// Try server-based batch processing first if conditions are met
	if (allowParallelBatchProcessing &&
		batchSettings.preferServerBatch &&
		allUseServer &&
		settingsCompatible &&
		!sharedSettings.serverUrl.empty()) {
		batchResult = processBatchViaServer(modelPath, requests, batchSettings);

		// If server batch succeeded or we shouldn't fallback, return
		if (batchResult.success || !batchSettings.fallbackToSequential) {
			batchResult.totalElapsedMs = ofGetElapsedTimeMillis() - startTime;
			// Record batch end
			metrics.recordBatchEnd(modelPath, batchResult.processedCount,
				batchResult.failedCount, batchResult.totalElapsedMs, batchResult.success);
			return batchResult;
		}
	}

	// Fall back to sequential processing
	batchResult = processBatchSequentially(modelPath, requests, batchSettings);
	batchResult.totalElapsedMs = ofGetElapsedTimeMillis() - startTime;

	// Record batch end
	metrics.recordBatchEnd(modelPath, batchResult.processedCount,
		batchResult.failedCount, batchResult.totalElapsedMs, batchResult.success);

	return batchResult;
}

ofxGgmlBatchResult ofxGgmlInference::generateBatchSimple(
	const std::string & modelPath,
	const std::vector<std::string> & prompts,
	const ofxGgmlInferenceSettings & settings,
	const ofxGgmlBatchSettings & batchSettings) const {

	std::vector<ofxGgmlBatchRequest> requests;
	requests.reserve(prompts.size());

	for (size_t i = 0; i < prompts.size(); ++i) {
		std::string id = "batch_" + std::to_string(i);
		requests.emplace_back(id, prompts[i], settings);
	}

	return generateBatch(modelPath, requests, batchSettings);
}

std::vector<ofxGgmlEmbeddingResult> ofxGgmlInference::embedBatch(
	const std::string & modelPath,
	const std::vector<std::string> & texts,
	const ofxGgmlEmbeddingSettings & settings) const {

	std::vector<ofxGgmlEmbeddingResult> results(texts.size());
	if (texts.empty()) return results;

	if (settings.useServerBackend && !settings.serverUrl.empty()) {
		const size_t maxConcurrent =
			recommendedServerEmbeddingConcurrency(texts.size());
		std::vector<std::thread> workers;
		workers.reserve(maxConcurrent);

		for (size_t startIdx = 0; startIdx < texts.size(); startIdx += maxConcurrent) {
			workers.clear();
			const size_t endIdx = std::min(startIdx + maxConcurrent, texts.size());
			for (size_t i = startIdx; i < endIdx; ++i) {
				workers.emplace_back([&, i]() {
					results[i] = embed(modelPath, texts[i], settings);
				});
			}
			for (auto & worker : workers) {
				if (worker.joinable()) {
					worker.join();
				}
			}
		}
	} else {
		for (size_t i = 0; i < texts.size(); ++i) {
			results[i] = embed(modelPath, texts[i], settings);
		}
	}

	return results;
}

ofxGgmlBatchResult ofxGgmlInference::processBatchViaServer(
	const std::string & modelPath,
	const std::vector<ofxGgmlBatchRequest> & requests,
	const ofxGgmlBatchSettings & batchSettings) const {

	ofxGgmlBatchResult batchResult;
	batchResult.results.resize(requests.size());

	// For now, we use a small worker pool rather than a true /v1/batch call,
	// since not all llama-server implementations expose that endpoint.
	// Reusing workers avoids the repeated thread-burst overhead from the old
	// chunked implementation while keeping the behavior predictable.
	std::atomic<bool> shouldStop(false);
	std::atomic<size_t> nextIndex(0);
	std::atomic<size_t> processedCount(0);
	std::atomic<size_t> failedCount(0);
	const size_t maxConcurrent =
		normalizedConcurrencyLimit(batchSettings.maxConcurrentRequests);

	std::vector<std::thread> workers;
	workers.reserve(maxConcurrent);
	for (size_t workerIndex = 0; workerIndex < maxConcurrent; ++workerIndex) {
		workers.emplace_back([&]() {
			while (!shouldStop.load(std::memory_order_relaxed)) {
				const size_t i = nextIndex.fetch_add(1, std::memory_order_relaxed);
				if (i >= requests.size()) {
					return;
				}

				const auto & req = requests[i];
				ofxGgmlInferenceResult result = generate(
					modelPath,
					req.prompt,
					req.settings,
					req.onChunk);

				const bool success = result.success;
				ofxGgmlBatchItemResult item;
				item.id = req.id;
				item.result = std::move(result);
				item.batchIndex = i;
				batchResult.results[i] = std::move(item);

				if (success) {
					processedCount.fetch_add(1, std::memory_order_relaxed);
				} else {
					failedCount.fetch_add(1, std::memory_order_relaxed);
					if (batchSettings.stopOnFirstError) {
						shouldStop.store(true, std::memory_order_relaxed);
					}
				}
			}
		});
	}

	for (auto & worker : workers) {
		if (worker.joinable()) {
			worker.join();
		}
	}

	batchResult.processedCount = processedCount.load();
	batchResult.failedCount = failedCount.load();
	if (batchSettings.stopOnFirstError && batchResult.failedCount > 0) {
		batchResult.error = "Batch processing stopped due to server request failure.";
	}
	batchResult.success = (batchResult.failedCount == 0);
	return batchResult;
}

ofxGgmlBatchResult ofxGgmlInference::processBatchSequentially(
	const std::string & modelPath,
	const std::vector<ofxGgmlBatchRequest> & requests,
	const ofxGgmlBatchSettings & batchSettings) const {

	ofxGgmlBatchResult batchResult;
	batchResult.results.reserve(requests.size());

	for (size_t i = 0; i < requests.size(); ++i) {
		const auto & req = requests[i];

		ofxGgmlInferenceResult result = generate(
			modelPath,
			req.prompt,
			req.settings,
			req.onChunk);
		const bool success = result.success;

		ofxGgmlBatchItemResult item;
		item.id = req.id;
		item.result = std::move(result);
		item.batchIndex = i;
		batchResult.results.push_back(std::move(item));

		if (success) {
			batchResult.processedCount++;
		} else {
			batchResult.failedCount++;
			if (batchSettings.stopOnFirstError) {
				batchResult.error = "Batch processing stopped due to error in request: " + req.id;
				break;
			}
		}
	}

	batchResult.success = (batchResult.failedCount == 0);
	return batchResult;
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
