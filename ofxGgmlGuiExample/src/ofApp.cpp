#include "ofApp.h"

#include "ofJson.h"
#include "core/ofxGgmlWindowsUtf8.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <numeric>
#include <regex>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

// ---------------------------------------------------------------------------
// Static data
// ---------------------------------------------------------------------------

const char * ofApp::modeLabels[kModeCount] = {
	"Chat", "Script", "Summarize", "Write", "Translate", "Custom", "Vision", "Speech"
};

const char * const kTextBackendLabels[] = {
	"CLI fallback",
	"llama-server"
};

const char * const kDefaultTextServerUrl = "http://127.0.0.1:8080";

namespace {
struct TokenLiteral {
	const char * text;
	size_t len;
};

struct LogLevelOption {
	const char * label;
	ofLogLevel level;
};

void drawPanelHeader(const char * title, const char * subtitle) {
	ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", title);
	ImGui::SameLine();
	ImGui::TextDisabled("(%s)", subtitle);
	ImGui::Separator();
}

void drawWrappedDisabledText(const std::string & text) {
	if (text.empty()) {
		return;
	}
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextDisabled("%s", text.c_str());
	ImGui::PopTextWrapPos();
}

static const std::array<LogLevelOption, 5> kLogLevelOptions = {{
	{"Silent",   OF_LOG_SILENT},
	{"Errors",   OF_LOG_ERROR},
	{"Warnings", OF_LOG_WARNING},
	{"Notices",  OF_LOG_NOTICE},
	{"Verbose",  OF_LOG_VERBOSE}
}};

std::string getEnvVarString(const char * name) {
	if (!name || *name == '\0') return {};
#ifdef _WIN32
	char * value = nullptr;
	size_t len = 0;
	const errno_t err = _dupenv_s(&value, &len, name);
	if (err != 0 || value == nullptr || len == 0) {
		free(value);
		return {};
	}
	std::string result(value);
	free(value);
	return result;
#else
	const char * value = std::getenv(name);
	return value ? std::string(value) : std::string();
#endif
}

void copyStringToBuffer(char * buffer, size_t bufferSize, const std::string & value) {
	if (!buffer || bufferSize == 0) return;
	const size_t copyLen = std::min(bufferSize - 1, value.size());
	std::memcpy(buffer, value.data(), copyLen);
	buffer[copyLen] = '\0';
}

void setVulkanRuntimeDisabled(bool disabled) {
#ifdef _WIN32
	_putenv_s("GGML_DISABLE_VULKAN", disabled ? "1" : "");
#else
	if (disabled) {
		setenv("GGML_DISABLE_VULKAN", "1", 1);
	} else {
		unsetenv("GGML_DISABLE_VULKAN");
	}
#endif
}

bool shouldDisableVulkanForCurrentSelection(
	const std::vector<std::string> & names,
	int selectedIndex) {
	if (getEnvVarString("OFXGGML_DISABLE_VULKAN") == "1") {
		return true;
	}
	if (selectedIndex >= 0 && selectedIndex < static_cast<int>(names.size())) {
		const std::string & sel = names[static_cast<size_t>(selectedIndex)];
	if (sel.rfind("CUDA", 0) == 0) {
			return true;
		}
	}
	return false;
}

const char * logLevelLabel(ofLogLevel level) noexcept {
	switch (level) {
	case OF_LOG_VERBOSE:      return "verbose";
	case OF_LOG_NOTICE:       return "notice";
	case OF_LOG_WARNING:      return "warn";
	case OF_LOG_ERROR:        return "error";
	case OF_LOG_FATAL_ERROR:  return "fatal";
	case OF_LOG_SILENT:       return "silent";
	default:                  return "log";
	}
}

int logLevelIndex(ofLogLevel level) noexcept {
	if (level == OF_LOG_FATAL_ERROR) {
		return 1; // treat fatal as errors for UI selection
	}
	for (size_t i = 0; i < kLogLevelOptions.size(); i++) {
		if (kLogLevelOptions[i].level == level) {
			return static_cast<int>(i);
		}
	}
	// Default to "Notices" when the level isn't found.
	return 3;
}

std::string stripAnsi(const std::string & text) {
	std::string out;
	out.reserve(text.size());
	size_t i = 0;
	while (i < text.size()) {
		if (text[i] == '\x1b') {
			i++; // skip ESC
			if (i < text.size() && text[i] == '[') {
				// CSI sequence: ESC [ <params> <final_byte>
				// Skip parameter bytes (0x30-0x3F) and intermediate
				// bytes (0x20-0x2F), then the final byte (0x40-0x7E).
				i++; // skip '['
				while (i < text.size()
					&& static_cast<unsigned char>(text[i]) < 0x40) {
					i++;
				}
				if (i < text.size()) i++; // skip the final byte (e.g. 'm')
			} else if (i < text.size()) {
				i++; // two-byte escape sequence (e.g. ESC =), skip second byte
			}
		} else {
			out += text[i];
			i++;
		}
	}
	return out;
}

std::string stripLiteralAnsiMarkers(const std::string & text) {
	static const std::regex markerRegex(R"(\[(?:\d{1,3})(?:;\d{1,3})*m)");
	return std::regex_replace(text, markerRegex, "");
}

std::string trim(const std::string & s) {
	size_t start = 0;
	while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
		start++;
	}
	size_t end = s.size();
	while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
		end--;
	}
	return s.substr(start, end - start);
}

std::string effectiveTextServerUrl(const char * buffer) {
	const std::string value = trim(buffer ? std::string(buffer) : std::string());
	return value.empty() ? std::string(kDefaultTextServerUrl) : value;
}

std::string buildStructuredTextPrompt(
	const std::string & systemPrompt,
	const std::string & instruction,
	const std::string & inputHeading,
	const std::string & inputText,
	const std::string & outputHeading) {
	std::ostringstream prompt;
	const std::string system = trim(systemPrompt);
	if (!system.empty()) {
		prompt << "System:\n" << system << "\n\n";
	}
	prompt << trim(instruction) << "\n";
	if (!trim(inputHeading).empty()) {
		prompt << trim(inputHeading) << ":\n";
	}
	prompt << inputText << "\n\n";
	if (!trim(outputHeading).empty()) {
		prompt << trim(outputHeading) << ":\n";
	}
	return prompt.str();
}

std::string formatSpeechTimestamp(double seconds) {
	if (seconds < 0.0) {
		seconds = 0.0;
	}
	const int totalMillis = static_cast<int>(std::round(seconds * 1000.0));
	const int hours = totalMillis / 3600000;
	const int minutes = (totalMillis / 60000) % 60;
	const int secs = (totalMillis / 1000) % 60;
	const int millis = totalMillis % 1000;
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d",
		hours, minutes, secs, millis);
	return buffer;
}

std::string formatSpeechSegments(
	const std::vector<ofxGgmlSpeechSegment> & segments) {
	if (segments.empty()) {
		return {};
	}
	std::ostringstream out;
	for (const auto & segment : segments) {
		out << "[" << formatSpeechTimestamp(segment.startSeconds)
			<< " - " << formatSpeechTimestamp(segment.endSeconds) << "] "
			<< segment.text << "\n";
	}
	return trim(out.str());
}

std::string makeTempMicRecordingPath() {
	std::error_code ec;
	std::filesystem::path base = std::filesystem::temp_directory_path(ec);
	if (ec) {
		base = std::filesystem::current_path();
	}
	const auto now = std::chrono::system_clock::now();
	const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
		now.time_since_epoch()).count();
	const uint64_t nonceHi = static_cast<uint64_t>(std::random_device{}());
	const uint64_t nonceLo = static_cast<uint64_t>(std::random_device{}());
	const uint64_t nonce = (nonceHi << 32) | nonceLo;
	std::ostringstream name;
	name << "ofxggml_mic_" << millis << "_" << std::hex << nonce << ".wav";
	return (base / name.str()).string();
}

bool writeMonoWavFile(
	const std::string & path,
	const std::vector<float> & samples,
	int sampleRate) {
	if (path.empty() || samples.empty() || sampleRate <= 0) {
		return false;
	}
	std::ofstream out(path, std::ios::binary);
	if (!out.is_open()) {
		return false;
	}

	const uint16_t channels = 1;
	const uint16_t bitsPerSample = 16;
	const uint32_t byteRate = static_cast<uint32_t>(sampleRate) * channels * (bitsPerSample / 8);
	const uint16_t blockAlign = channels * (bitsPerSample / 8);
	const uint32_t dataSize = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
	const uint32_t riffSize = 36u + dataSize;

	auto writeU16 = [&](uint16_t value) {
		out.write(reinterpret_cast<const char *>(&value), sizeof(value));
	};
	auto writeU32 = [&](uint32_t value) {
		out.write(reinterpret_cast<const char *>(&value), sizeof(value));
	};

	out.write("RIFF", 4);
	writeU32(riffSize);
	out.write("WAVE", 4);
	out.write("fmt ", 4);
	writeU32(16);
	writeU16(1);
	writeU16(channels);
	writeU32(static_cast<uint32_t>(sampleRate));
	writeU32(byteRate);
	writeU16(blockAlign);
	writeU16(bitsPerSample);
	out.write("data", 4);
	writeU32(dataSize);

	for (float sample : samples) {
		const float clamped = std::clamp(sample, -1.0f, 1.0f);
		const int16_t pcm = static_cast<int16_t>(std::lrint(clamped * 32767.0f));
		out.write(reinterpret_cast<const char *>(&pcm), sizeof(pcm));
	}

	return out.good();
}

std::string serverBaseUrlFromConfiguredUrl(const std::string & configuredUrl) {
	std::string value = trim(configuredUrl);
	if (value.empty()) {
		return kDefaultTextServerUrl;
	}
	auto stripSuffix = [&](const std::string & suffix) {
		if (value.size() >= suffix.size() &&
			value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0) {
			value.erase(value.size() - suffix.size());
		}
	};
	stripSuffix("/v1/chat/completions");
	stripSuffix("/chat/completions");
	if (!value.empty() && value.back() == '/') {
		value.pop_back();
	}
	return value.empty() ? std::string(kDefaultTextServerUrl) : value;
}

std::pair<std::string, int> parseServerHostPort(const std::string & configuredUrl) {
	const std::string baseUrl = serverBaseUrlFromConfiguredUrl(configuredUrl);
	static const std::regex hostPortRe(R"(^https?://([^/:]+)(?::(\d+))?.*$)", std::regex::icase);
	std::smatch match;
	if (std::regex_match(baseUrl, match, hostPortRe)) {
		const std::string host = match[1].str();
		const int port = (match.size() >= 3 && match[2].matched)
			? std::max(1, std::stoi(match[2].str()))
			: 8080;
		return {host, port};
	}
	return {"127.0.0.1", 8080};
}

bool shouldManageLocalTextServer(const std::string & configuredUrl) {
	const auto [host, port] = parseServerHostPort(configuredUrl);
	(void)port;
	std::string normalizedHost = trim(host);
	std::transform(
		normalizedHost.begin(),
		normalizedHost.end(),
		normalizedHost.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return normalizedHost.empty() ||
		normalizedHost == "127.0.0.1" ||
		normalizedHost == "localhost";
}

bool aiModeSupportsTextBackend(AiMode mode) {
	switch (mode) {
	case AiMode::Chat:
	case AiMode::Script:
	case AiMode::Summarize:
	case AiMode::Write:
	case AiMode::Translate:
	case AiMode::Custom:
		return true;
	case AiMode::Vision:
	case AiMode::Speech:
		return false;
	}
	return false;
}

TextInferenceBackend clampTextInferenceBackend(int value) {
	return value == static_cast<int>(TextInferenceBackend::LlamaServer)
		? TextInferenceBackend::LlamaServer
		: TextInferenceBackend::Cli;
}

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

bool isEuRestrictedVisionProfile(const ofxGgmlVisionModelProfile & profile) {
	const std::string repoHint = trim(profile.modelRepoHint);
	return repoHint.find("meta-llama/Llama-3.2-11B-Vision") != std::string::npos;
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

// Strip common chat-template role markers and prompt artefacts that
// llama-completion may emit around the actual generated text.
// Examples of markers removed: "user", "assistant", "system",
// "<|...|>" ChatML tokens, and leading/trailing ">" prompt chars.
std::string cleanChatOutput(const std::string & text) {
	return ofxGgmlInference::sanitizeGeneratedText(text);
}

std::string formatConsoleLogText(const std::string & text, bool chatLike = false) {
	std::string out = stripLiteralAnsiMarkers(stripAnsi(text));
	if (chatLike) {
		out = ofxGgmlInference::sanitizeGeneratedText(out);
	}

	std::string flattened;
	flattened.reserve(out.size());
	bool lastWasSpace = false;
	for (unsigned char ch : out) {
		if (ch == '\r' || ch == '\n' || ch == '\t') {
			if (!flattened.empty() && !lastWasSpace) {
				flattened.push_back(' ');
				lastWasSpace = true;
			}
			continue;
		}
		if (std::iscntrl(ch)) {
			continue;
		}
		flattened.push_back(static_cast<char>(ch));
		lastWasSpace = (ch == ' ');
	}

	return trim(flattened);
}

// Translate well-known process crash/error exit codes into a short
// human-readable description.  Returns an empty string for codes that
// are not recognised so the caller can fall back to the numeric value.
std::string describeExitCode(int code) {
#ifdef _WIN32
	switch (static_cast<unsigned int>(code)) {
	case 0xC0000409: return "stack buffer overrun (0xC0000409)";
	case 0xC0000005: return "access violation (0xC0000005)";
	case 0xC00000FD: return "stack overflow (0xC00000FD)";
	case 0xC0000135: return "DLL not found (0xC0000135)";
	case 0xC000001D: return "illegal instruction (0xC000001D) — CPU may not support required features";
	case 0xC0000374: return "heap corruption (0xC0000374)";
	default: break;
	}
	// DWORD exit codes are stored as a signed int; match negative
	// representations of the same NTSTATUS values.
	switch (code) {
	case -1073740791: return "stack buffer overrun (0xC0000409)";
	case -1073741819: return "access violation (0xC0000005)";
	case -1073741571: return "stack overflow (0xC00000FD)";
	case -1073741515: return "DLL not found (0xC0000135)";
	case -1073741795: return "illegal instruction (0xC000001D) — CPU may not support required features";
	case -1073741676: return "heap corruption (0xC0000374)";
	default: break;
	}
#endif
	if (code >= 129 && code <= 159) {
		int sig = code - 128;
		return "killed by signal " + std::to_string(sig);
	}
	return "";
}

bool isLikelyCutoffOutput(const std::string & text, AiMode mode) {
	const std::string t = trim(text);
	if (t.empty()) return false;
	if (t.rfind("[Error]", 0) == 0) return false;

	const char last = t.back();
	if (mode == AiMode::Script) {
		if (last == '\n' || last == '}' || last == ')' || last == ']' || last == ';') {
			return false;
		}
		return t.size() > 80;
	}

	if (last == '.' || last == '!' || last == '?' || last == '"' || last == '\'') {
		return false;
	}
	return t.size() > 80;
}

std::string clampPromptToContext(const std::string & prompt, size_t contextTokens, bool & trimmed) {
	trimmed = false;
	if (contextTokens == 0) return prompt;
	const size_t charBudget = std::max<size_t>(512, contextTokens * 3);
	if (prompt.size() <= charBudget) return prompt;

	trimmed = true;
	const size_t head = std::min<size_t>(2048, charBudget / 4);
	if (charBudget <= head + 96) {
		return prompt.substr(prompt.size() - charBudget);
	}
	const size_t tail = charBudget - head - 32;
	return prompt.substr(0, head)
		+ "\n...[context trimmed to fit window]...\n"
		+ prompt.substr(prompt.size() - tail);
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

constexpr size_t kMaxLogMessages = 500;
constexpr size_t kExePathBufSize = 4096; // buffer for resolving the executable path
constexpr float kDefaultTemp = 0.7f;
constexpr float kDefaultTopP = 0.9f;
constexpr float kDefaultRepeatPenalty = 1.1f;
constexpr int kExecNotFound = 127; // POSIX convention when execvp fails
constexpr float kSpinnerInterval = 0.15f;       // seconds per spinner frame
constexpr float kDotsAnimationSpeed = 3.0f;     // dots cycle speed multiplier
constexpr size_t kMaxScriptContextFiles = 50;
constexpr size_t kMaxFocusedFileSnippetChars = 2000;
constexpr auto kStreamUiUpdateInterval = std::chrono::milliseconds(50);
constexpr size_t kStreamUiMinGrowth = 256;
const char * const kWaitingLabels[] = {"generating", "generating.", "generating..", "generating..."};

// Llama CLI detection state shared between probe and UI.
// -1 = unknown / needs probe, 0 = probed but not found, 1 = available.
std::atomic<int> llamaCliState{-1};
std::string llamaCliCommand = "llama-completion";

// Child process tracking — allows stopGeneration() to kill a running
// llama-completion process instead of blocking until it finishes.
#ifdef _WIN32
std::atomic<HANDLE> inferenceProcessHandle{nullptr};
#else
std::atomic<pid_t> inferenceProcessPid{0};
#endif

// Kill the currently running inference child process, if any.
void killInferenceProcess() {
#ifdef _WIN32
	HANDLE h = inferenceProcessHandle.exchange(nullptr);
	if (h) {
		TerminateProcess(h, 1);
		// Do not close the handle here — runProcessCapture owns it.
	}
#else
	pid_t pid = inferenceProcessPid.exchange(0);
	if (pid > 0) {
		kill(pid, SIGKILL);
	}
#endif
}

#ifdef _WIN32
std::string quoteWindowsArg(const std::string & arg) {
	bool needsQuotes = arg.find_first_of(" \t\"%^&|<>") != std::string::npos;
	if (!needsQuotes) return arg;
	std::string out = "\"";
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

bool runProcessCapture(const std::vector<std::string> & args, std::string & output, int & exitCode,
                       bool trackProcess = false,
                       std::function<void(const std::string &)> onStreamData = nullptr,
                       bool mergeStderr = true) {
	output.clear();
	exitCode = -1;
	if (args.empty() || args[0].empty()) return false;

#ifdef _WIN32
	SECURITY_ATTRIBUTES sa{};
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

	STARTUPINFOA si{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	// Redirect stdin to NUL so that interactive / conversation modes
	// receive EOF and exit automatically after generation.
	HANDLE nullInput = CreateFileA("NUL", GENERIC_READ, 0, &sa,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	si.hStdInput = (nullInput != INVALID_HANDLE_VALUE)
		? nullInput : GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = writePipe;
	HANDLE nullErr = INVALID_HANDLE_VALUE;
	if (mergeStderr) {
		si.hStdError = writePipe;
	} else {
		// Discard stderr so verbose log output from the child process
		// does not pollute the captured stdout.
		nullErr = CreateFileA("NUL", GENERIC_WRITE, 0, &sa,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		si.hStdError = (nullErr != INVALID_HANDLE_VALUE)
			? nullErr : GetStdHandle(STD_ERROR_HANDLE);
	}

	PROCESS_INFORMATION pi{};
	std::string cmdLine;
	for (size_t i = 0; i < args.size(); i++) {
		if (i > 0) cmdLine += " ";
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
		&pi
	);
	CloseHandle(writePipe);
	if (nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);
	if (nullErr != INVALID_HANDLE_VALUE) CloseHandle(nullErr);
	if (!ok) {
		CloseHandle(readPipe);
		return false;
	}

	if (trackProcess) {
		inferenceProcessHandle.store(pi.hProcess);
	}

	char buffer[4096];
	DWORD bytesRead = 0;
	while (ReadFile(readPipe, buffer, static_cast<DWORD>(sizeof(buffer)), &bytesRead, nullptr) && bytesRead > 0) {
		output.append(buffer, bytesRead);
		if (onStreamData) onStreamData(output);
	}
	CloseHandle(readPipe);

	WaitForSingleObject(pi.hProcess, INFINITE);

	if (trackProcess) {
		inferenceProcessHandle.store(nullptr);
	}

	DWORD code = 1;
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	exitCode = static_cast<int>(code);
	return true;
#else
	int pipefd[2];
	if (pipe(pipefd) != 0) return false;

	pid_t pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return false;
	}

	if (pid == 0) {
		// Redirect stdin to /dev/null so that interactive / conversation
		// modes (e.g. llama-cli) receive EOF and exit automatically
		// after generation instead of blocking for more input.
		int devNull = open("/dev/null", O_RDONLY);
		if (devNull >= 0) {
			dup2(devNull, STDIN_FILENO);
			if (devNull != STDIN_FILENO) close(devNull);
		}
		dup2(pipefd[1], STDOUT_FILENO);
		if (mergeStderr) {
			dup2(pipefd[1], STDERR_FILENO);
		} else {
			// Discard stderr so verbose log output from the child
			// process does not pollute the captured stdout.
			int devNullW = open("/dev/null", O_WRONLY);
			if (devNullW >= 0) {
				dup2(devNullW, STDERR_FILENO);
				close(devNullW);
			} else {
				// Last resort: close stderr so nothing leaks into the pipe.
				close(STDERR_FILENO);
			}
		}
		close(pipefd[0]);
		close(pipefd[1]);

		std::vector<std::string> mutableArgs = args;
		std::vector<char *> argv;
		argv.reserve(mutableArgs.size() + 1);
		for (auto & a : mutableArgs) {
			argv.push_back(a.empty() ? const_cast<char *>("") : &a[0]);
		}
		argv.push_back(nullptr);
		execvp(argv[0], argv.data());
		_exit(127);
	}

	if (trackProcess) {
		inferenceProcessPid.store(pid);
	}

	close(pipefd[1]);
	char buffer[4096];
	ssize_t n = 0;
	while ((n = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
		output.append(buffer, static_cast<size_t>(n));
		if (onStreamData) onStreamData(output);
	}
	close(pipefd[0]);

	int status = 0;
	pid_t wp = waitpid(pid, &status, 0);

	if (trackProcess) {
		inferenceProcessPid.store(0);
	}

	if (wp < 0) {
		exitCode = -1;
		return false;
	}

	if (WIFEXITED(status)) {
		exitCode = WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		exitCode = -1;
	} else {
		exitCode = -1;
	}
	return true;
#endif
}

enum class ScriptSlashCommandKind {
	None = 0,
	Help,
	ReviewAll,
	ReviewFix,
	NextEdit,
	SummarizeChanges,
	Tests,
	FixPlan,
	Explain,
	Docs
};

struct ScriptSlashCommand {
	ScriptSlashCommandKind kind = ScriptSlashCommandKind::None;
	std::string argument;
};

struct WorkspaceDiffSnapshot {
	bool success = false;
	bool hasChanges = false;
	std::string workspaceRoot;
	std::string repoRoot;
	std::string statusText;
	std::string diffText;
	std::string error;
};

std::string normalizeSlashCommandName(const std::string & raw) {
	std::string normalized;
	normalized.reserve(raw.size());
	for (char ch : raw) {
		if (ch == '-' || ch == '_') {
			continue;
		}
		normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
	}
	return normalized;
}

ScriptSlashCommand parseScriptSlashCommand(const std::string & rawInput) {
	const std::string trimmedInput = trim(rawInput);
	if (trimmedInput.size() < 2 || trimmedInput.front() != '/') {
		return {};
	}

	const size_t splitPos = trimmedInput.find_first_of(" \t\r\n");
	const std::string commandName = normalizeSlashCommandName(
		trimmedInput.substr(1, splitPos == std::string::npos
			? std::string::npos
			: splitPos - 1));
	const std::string argument = splitPos == std::string::npos
		? std::string()
		: trim(trimmedInput.substr(splitPos + 1));

	ScriptSlashCommand command;
	command.argument = argument;
	if (commandName == "help" || commandName == "commands") {
		command.kind = ScriptSlashCommandKind::Help;
	} else if (commandName == "review" || commandName == "reviewall") {
		command.kind = ScriptSlashCommandKind::ReviewAll;
	} else if (commandName == "reviewfix" || commandName == "fixreview") {
		command.kind = ScriptSlashCommandKind::ReviewFix;
	} else if (commandName == "nextedit" || commandName == "next") {
		command.kind = ScriptSlashCommandKind::NextEdit;
	} else if (commandName == "summary" || commandName == "summarize" ||
		commandName == "changes" || commandName == "prsummary") {
		command.kind = ScriptSlashCommandKind::SummarizeChanges;
	} else if (commandName == "tests" || commandName == "testplan") {
		command.kind = ScriptSlashCommandKind::Tests;
	} else if (commandName == "fix" || commandName == "edit") {
		command.kind = ScriptSlashCommandKind::FixPlan;
	} else if (commandName == "explain") {
		command.kind = ScriptSlashCommandKind::Explain;
	} else if (commandName == "docs") {
		command.kind = ScriptSlashCommandKind::Docs;
	}
	return command;
}

std::string buildScriptCommandHelpText() {
	return
		"Slash commands:\n"
		"- /review [focus] -> hierarchical workspace review\n"
		"- /reviewfix [focus] -> review with an actionable fix plan\n"
		"- /nextedit [focus] -> predict the most likely next change\n"
		"- /summary [focus] -> summarize local git changes for reviewers\n"
		"- /tests [focus] -> propose the highest-value tests\n"
		"- /fix [focus] -> produce a structured fix/edit plan\n"
		"- /explain [focus] -> explain code or architecture\n"
		"- /docs [focus] -> answer with grounded docs\n";
}

std::string truncatePromptPayload(const std::string & text, size_t maxChars) {
	if (text.size() <= maxChars) {
		return text;
	}
	return text.substr(0, maxChars) +
		"\n...[truncated " + std::to_string(text.size() - maxChars) + " chars]";
}

WorkspaceDiffSnapshot captureWorkspaceDiffSnapshot(const std::string & workspaceRoot) {
	WorkspaceDiffSnapshot snapshot;
	snapshot.workspaceRoot = trim(workspaceRoot);
	if (snapshot.workspaceRoot.empty()) {
		snapshot.error = "Workspace change summary requires a loaded local folder.";
		return snapshot;
	}

	std::string output;
	int exitCode = -1;
	if (!runProcessCapture(
			{"git", "-C", snapshot.workspaceRoot, "rev-parse", "--show-toplevel"},
			output,
			exitCode) ||
		exitCode != 0) {
		snapshot.error =
			"Workspace change summary requires a local Git repository.";
		return snapshot;
	}
	snapshot.repoRoot = trim(stripAnsi(output));
	if (snapshot.repoRoot.empty()) {
		snapshot.error = "Unable to determine the Git repository root for this workspace.";
		return snapshot;
	}

	output.clear();
	exitCode = -1;
	if (!runProcessCapture(
			{"git", "-C", snapshot.repoRoot, "status", "--short", "--branch"},
			output,
			exitCode) ||
		exitCode != 0) {
		snapshot.error = "Failed to read Git status for the current workspace.";
		return snapshot;
	}
	snapshot.statusText = trim(stripAnsi(output));

	output.clear();
	exitCode = -1;
	const bool diffOk = runProcessCapture(
		{"git", "-C", snapshot.repoRoot, "diff", "--no-ext-diff", "--minimal", "HEAD", "--"},
		output,
		exitCode);
	if (diffOk && exitCode == 0) {
		snapshot.diffText = trim(stripAnsi(output));
	} else {
		output.clear();
		exitCode = -1;
		if (runProcessCapture(
				{"git", "-C", snapshot.repoRoot, "diff", "--no-ext-diff", "--minimal", "--"},
				output,
				exitCode) &&
			exitCode == 0) {
			snapshot.diffText = trim(stripAnsi(output));
		}
	}

	snapshot.hasChanges =
		(snapshot.statusText.find("##") != std::string::npos &&
			snapshot.statusText.find('\n') != std::string::npos) ||
		(!snapshot.statusText.empty() &&
			snapshot.statusText.rfind("##", 0) != 0) ||
		!snapshot.diffText.empty();
	snapshot.success = true;
	return snapshot;
}

}


// ---------------------------------------------------------------------------
// Probe for llama-completion / llama-cli / llama.
// Checks a user-supplied custom path first, then PATH, then common
// installation directories.  Updates llamaCliState and llamaCliCommand.
// Prefers llama-completion (one-shot text completion) over llama-cli
// (interactive chat mode since llama.cpp PR #17824).
// ---------------------------------------------------------------------------

static bool probeCandidate(const std::string & candidate,
                           const std::vector<std::string> & flags,
                           std::string & probeOut, int & probeExit) {
	for (const auto & flag : flags) {
		if (runProcessCapture({candidate, flag}, probeOut, probeExit)
			&& probeExit != kExecNotFound) {
			return true;
		}
	}
	return false;
}

static void probeLlamaCliImpl(
	const std::function<void(ofLogLevel, const std::string &)> & logger,
	const std::string & customPath = "") {
	std::string probeOut;
	int probeExit = -1;
	const std::vector<std::string> probeFlags = {"--version", "--help"};
	bool found = false;

	// 1. Try user-supplied custom path first.
	if (!customPath.empty()) {
		std::error_code ec;
		if (std::filesystem::exists(customPath, ec) && !ec) {
			if (probeCandidate(customPath, probeFlags, probeOut, probeExit)) {
				llamaCliCommand = customPath;
				found = true;
			}
		} else {
			if (logger) logger(OF_LOG_WARNING, "custom CLI path not found: " + customPath);
		}
	}

	// 2. Prefer addon-local installs first (libs/llama/bin), then PATH.
	// This avoids accidentally picking an older CPU-only llama executable
	// from PATH when a GPU-enabled bundled build is available.
	if (!found) {
		const std::vector<std::string> exeNames = {"llama-completion", "llama-cli", "llama"};
		std::vector<std::string> preferredDirs;

		// Addon-local libs/llama/bin (default install target for build script).
		{
			std::error_code srcEc;
			auto srcPath = std::filesystem::path(__FILE__).parent_path();
			auto addonRoot = std::filesystem::weakly_canonical(srcPath / ".." / "..", srcEc);
			if (!srcEc) {
				preferredDirs.push_back((addonRoot / "libs" / "llama" / "bin").string());
			}
		}

		for (const auto & dir : preferredDirs) {
			for (const auto & exe : exeNames) {
				std::string fullPath = dir +
#ifdef _WIN32
					"\\" + exe + ".exe";
#else
					"/" + exe;
#endif
				std::error_code ec;
				if (!std::filesystem::exists(fullPath, ec) || ec) continue;
				if (probeCandidate(fullPath, probeFlags, probeOut, probeExit)) {
					llamaCliCommand = fullPath;
					found = true;
					break;
				}
			}
			if (found) break;
		}
	}

	// 3. Try bare names via PATH (execvp search).
	// Prefer llama-completion (one-shot text completion) over llama-cli
	// (interactive chat, server-based since llama.cpp PR #17824).
	if (!found) {
		const std::vector<std::string> bareNames = {"llama-completion", "llama-cli", "llama"};
		for (const auto & name : bareNames) {
			if (probeCandidate(name, probeFlags, probeOut, probeExit)) {
				llamaCliCommand = name;
				found = true;
				break;
			}
		}
	}

	// 4. Try common installation directories.
	if (!found) {
		std::vector<std::string> searchDirs;

		// Check the directory of the running executable.
		// This handles the case where llama-completion was installed
		// next to the application binary (common deployment layout).
		{
#ifdef _WIN32
			std::vector<char> exeBuf(kExePathBufSize);
			DWORD len = GetModuleFileNameA(nullptr, exeBuf.data(), static_cast<DWORD>(exeBuf.size()));
			if (len > 0 && len < exeBuf.size()) {
				auto exeDir = std::filesystem::path(std::string(exeBuf.data(), len)).parent_path();
				searchDirs.push_back(exeDir.string());
			}
#elif defined(__linux__) || defined(__FreeBSD__)
			std::vector<char> exeBuf(kExePathBufSize);
			ssize_t exeLen = readlink("/proc/self/exe", exeBuf.data(), exeBuf.size());
			if (exeLen > 0 && static_cast<size_t>(exeLen) < exeBuf.size()) {
				auto exeDir = std::filesystem::path(std::string(exeBuf.data(), static_cast<size_t>(exeLen))).parent_path();
				searchDirs.push_back(exeDir.string());
			}
#elif defined(__APPLE__)
			// On macOS, use _NSGetExecutablePath or the current working
			// directory; the latter is usually the bundle's Contents/MacOS.
			char macBuf[kExePathBufSize];
			uint32_t macBufSize = sizeof(macBuf);
			if (_NSGetExecutablePath(macBuf, &macBufSize) == 0) {
				auto exeDir = std::filesystem::path(macBuf).parent_path();
				searchDirs.push_back(exeDir.string());
			}
#endif
		}

		// Check addon-local libs/llama/bin directory.  This is the
		// default install prefix used by scripts/build-llama-cli.sh on
		// Windows-like shells and can be used on any platform by
		// passing --prefix <addon>/libs/llama to the script.
		{
			std::error_code srcEc;
			auto srcPath = std::filesystem::path(__FILE__).parent_path();  // .../ofxGgmlGuiExample/src
			auto addonRoot = std::filesystem::weakly_canonical(srcPath / ".." / "..", srcEc);
			if (!srcEc) {
				searchDirs.push_back((addonRoot / "libs" / "llama" / "bin").string());
			}
		}

		// Also check relative to the current working directory in
		// case __FILE__ resolved to a path that no longer exists at
		// runtime (common with out-of-tree builds).
		{
			std::error_code ec;
			auto cwd = std::filesystem::current_path(ec);
			if (!ec) {
				searchDirs.push_back(cwd.string());
				// Try one level up from cwd (e.g., cwd is bin/,
				// llama-completion is in ../libs/llama/bin/).
				auto cwdParent = std::filesystem::weakly_canonical(cwd / "..", ec);
				if (!ec) {
					searchDirs.push_back((cwdParent / "libs" / "llama" / "bin").string());
				}
			}
		}

#ifdef _WIN32
		// On Windows, also check common Program Files locations.
		const std::string progFiles = getEnvVarString("ProgramFiles");
		if (!progFiles.empty()) {
			searchDirs.push_back(progFiles + "\\llama.cpp");
			searchDirs.push_back(progFiles + "\\LlamaCpp");
		}
		const std::string localAppData = getEnvVarString("LOCALAPPDATA");
		if (!localAppData.empty()) {
			searchDirs.push_back(localAppData + "\\llama.cpp");
		}
#else
		// Common POSIX install directories that may not be in PATH
		// when launched from a GUI / IDE.
		searchDirs.push_back("/usr/local/bin");
		searchDirs.push_back("/usr/bin");
#ifdef __APPLE__
		searchDirs.push_back("/opt/homebrew/bin");
#endif
		const std::string home = getEnvVarString("HOME");
		if (!home.empty()) {
			searchDirs.push_back(home + "/.local/bin");
		}
		searchDirs.push_back("/snap/bin");
#endif
		const std::vector<std::string> exeNames = {"llama-completion", "llama-cli", "llama"};
		for (const auto & dir : searchDirs) {
			for (const auto & exe : exeNames) {
				std::string fullPath = dir +
#ifdef _WIN32
					"\\" + exe + ".exe";
#else
					"/" + exe;
#endif
				std::error_code ec;
				if (!std::filesystem::exists(fullPath, ec) || ec) continue;
				if (probeCandidate(fullPath, probeFlags, probeOut, probeExit)) {
					llamaCliCommand = fullPath;
					found = true;
					break;
				}
			}
			if (found) break;
		}
	}

	if (!found) {
		llamaCliState.store(0, std::memory_order_relaxed);
		{
			if (logger) logger(
				OF_LOG_VERBOSE,
				"Optional CLI fallback (llama-completion/llama-cli/llama) not found in PATH or common directories.");
		}
		return;
	}
	{
		if (logger) logger(OF_LOG_NOTICE, "detected CLI: " + llamaCliCommand);
	}
	llamaCliState.store(1, std::memory_order_relaxed);
}

void ofApp::applyLogLevel(ofLogLevel level) {
	logLevel = level;
	ofSetLogLevel(level);
}

bool ofApp::shouldLog(ofLogLevel level) const {
	return logLevel != OF_LOG_SILENT && level >= logLevel;
}

void ofApp::logWithLevel(ofLogLevel level, const std::string & message) {
	if (!shouldLog(level) || message.empty()) return;
	ofLog(level, message);
	std::lock_guard<std::mutex> lock(logMutex);
	std::string entry = "[" + std::string(logLevelLabel(level)) + "] " + message;
	logMessages.push_back(entry);
	if (logMessages.size() > kMaxLogMessages) {
		logMessages.pop_front();
	}
}

void ofApp::announceTextBackendChange() {
	const bool useServerBackend =
		(textInferenceBackend == TextInferenceBackend::LlamaServer);
	const std::string modePrefix = aiModeSupportsTextBackend(activeMode)
		? std::string("Text backend for ") + modeLabels[static_cast<int>(activeMode)] + " switched to "
		: std::string("Text backend switched to ");
	std::string message = useServerBackend
		? modePrefix + "llama-server (persistent)."
		: modePrefix + "CLI fallback (optional local llama-completion).";
	if (useServerBackend) {
		const std::string serverUrl = effectiveTextServerUrl(textServerUrl);
		message += " Server: " + serverUrl + ".";
	} else {
		message += " This optional fallback uses the selected local GGUF model when the server path is not preferred.";
	}

	logWithLevel(OF_LOG_NOTICE, message);

	const Message notice{"system", message, ofGetElapsedTimef()};
	if (activeMode == AiMode::Chat) {
		chatMessages.push_back(notice);
	} else if (activeGenerationMode == AiMode::Script || activeMode == AiMode::Script) {
		scriptMessages.push_back(notice);
	}

	if (useServerBackend) {
		applyServerFriendlyDefaultsForMode(activeMode);
		ensureTextServerReady(
			false,
			shouldManageLocalTextServer(effectiveTextServerUrl(textServerUrl)));
	} else {
		textServerStatus = ServerStatusState::Unknown;
		textServerStatusMessage.clear();
		textServerCapabilityHint.clear();
	}
}

TextInferenceBackend ofApp::preferredTextBackendForMode(AiMode mode) const {
	if (!aiModeSupportsTextBackend(mode)) {
		return textInferenceBackend;
	}
	const int index = modeTextBackendIndices[static_cast<size_t>(mode)];
	return clampTextInferenceBackend(index);
}

void ofApp::rememberTextBackendForMode(AiMode mode, TextInferenceBackend backend) {
	if (!aiModeSupportsTextBackend(mode)) {
		return;
	}
	modeTextBackendIndices[static_cast<size_t>(mode)] = static_cast<int>(backend);
}

void ofApp::applyServerFriendlyDefaultsForMode(AiMode mode) {
	int tunedMaxTokens = 512;
	switch (mode) {
	case AiMode::Chat: tunedMaxTokens = 384; break;
	case AiMode::Script: tunedMaxTokens = 768; break;
	case AiMode::Summarize: tunedMaxTokens = 512; break;
	case AiMode::Write: tunedMaxTokens = 640; break;
	case AiMode::Translate: tunedMaxTokens = 384; break;
	case AiMode::Custom: tunedMaxTokens = 512; break;
	case AiMode::Vision:
	case AiMode::Speech:
		tunedMaxTokens = std::clamp(maxTokens, 256, 768);
		break;
	}
	maxTokens = tunedMaxTokens;
	modeMaxTokens[static_cast<size_t>(mode)] = maxTokens;
	temperature = (mode == AiMode::Chat || mode == AiMode::Write) ? 0.6f : 0.25f;
	topP = 0.9f;
	topK = 50;
	minP = 0.05f;
	repeatPenalty = 1.03f;
	contextSize = std::clamp(contextSize, 2048, 8192);
	stopAtNaturalBoundary = true;
	autoContinueCutoff = (mode == AiMode::Script);
	usePromptCache = false;
}

void ofApp::syncTextBackendForActiveMode(bool announce) {
	if (!aiModeSupportsTextBackend(activeMode)) {
		return;
	}
	const TextInferenceBackend preferred = preferredTextBackendForMode(activeMode);
	if (textInferenceBackend == preferred) {
		if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
			if (trim(textServerUrl).empty()) {
				copyStringToBuffer(textServerUrl, sizeof(textServerUrl), kDefaultTextServerUrl);
			}
			applyServerFriendlyDefaultsForMode(activeMode);
			ensureTextServerReady(
				false,
				shouldManageLocalTextServer(effectiveTextServerUrl(textServerUrl)));
		}
		return;
	}
	textInferenceBackend = preferred;
	if (textInferenceBackend == TextInferenceBackend::LlamaServer &&
		trim(textServerUrl).empty()) {
		copyStringToBuffer(textServerUrl, sizeof(textServerUrl), kDefaultTextServerUrl);
	}
	if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
		applyServerFriendlyDefaultsForMode(activeMode);
	}
	if (announce) {
		announceTextBackendChange();
	} else if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
		ensureTextServerReady(
			false,
			shouldManageLocalTextServer(effectiveTextServerUrl(textServerUrl)));
	} else {
		textServerStatus = ServerStatusState::Unknown;
		textServerStatusMessage.clear();
		textServerCapabilityHint.clear();
	}
}

void ofApp::checkTextServerStatus(bool logResult) {
	const std::string configuredUrl = effectiveTextServerUrl(textServerUrl);
	const ofxGgmlServerProbeResult probe =
		ofxGgmlInference::probeServer(configuredUrl, true);
	if (probe.reachable) {
		textServerStatus = ServerStatusState::Reachable;
		textServerStatusMessage = "Server reachable at " + probe.baseUrl + ".";
		if (!probe.activeModel.empty()) {
			textServerStatusMessage += " Model: " + probe.activeModel + ".";
		}
		textServerCapabilityHint = probe.capabilitySummary.empty()
			? std::string()
			: "Server profile: " + probe.capabilitySummary + ".";
		if (logResult) {
			logWithLevel(OF_LOG_NOTICE, textServerStatusMessage);
		}
		return;
	}

	textServerStatus = ServerStatusState::Unreachable;
	textServerStatusMessage = "Server not reachable at " + probe.baseUrl + ".";
	textServerCapabilityHint.clear();
	if (!probe.error.empty()) {
		textServerStatusMessage += " " + probe.error;
	}
	if (logResult) {
		logWithLevel(OF_LOG_WARNING, textServerStatusMessage);
	}
}

bool ofApp::ensureTextServerReady(bool logResult, bool allowLaunch) {
	if (textInferenceBackend != TextInferenceBackend::LlamaServer) {
		return true;
	}

	checkTextServerStatus(logResult);
	if (textServerStatus == ServerStatusState::Reachable) {
		return true;
	}

	if (!allowLaunch) {
		return false;
	}

	const std::string serverExe = findLocalTextServerExecutable();
	const std::string modelPath = getSelectedModelPath();
	if (serverExe.empty() || modelPath.empty() || !std::filesystem::exists(modelPath)) {
		return false;
	}

	if (!isManagedTextServerRunning()) {
		startLocalTextServer();
	}

	for (int attempt = 0; attempt < 20; ++attempt) {
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
		checkTextServerStatus(false);
		if (textServerStatus == ServerStatusState::Reachable) {
			if (logResult) {
				logWithLevel(OF_LOG_NOTICE, textServerStatusMessage);
			}
			return true;
		}
	}

	if (logResult && !textServerStatusMessage.empty()) {
		logWithLevel(OF_LOG_WARNING, textServerStatusMessage);
	}
	return false;
}

std::string ofApp::findLocalTextServerExecutable() const {
	std::vector<std::filesystem::path> candidates;
	const std::filesystem::path exeDir(ofFilePath::getCurrentExeDir());
	candidates.push_back(exeDir / ".." / ".." / "libs" / "llama" / "bin" / "llama-server.exe");
	candidates.push_back(exeDir / ".." / ".." / "build" / "llama.cpp-build" / "bin" / "Release" / "llama-server.exe");
	candidates.push_back(exeDir / "llama-server.exe");

	for (const auto & candidate : candidates) {
		std::error_code ec;
		const auto normalized = std::filesystem::weakly_canonical(candidate, ec);
		const auto & checkPath = ec ? candidate : normalized;
		if (std::filesystem::exists(checkPath, ec) && !ec) {
			return checkPath.string();
		}
	}
	return {};
}

bool ofApp::isManagedTextServerRunning() {
	if (!textServerManagedByApp) {
		return false;
	}
#ifdef _WIN32
	if (!textServerProcessHandle) {
		textServerManagedByApp = false;
		textServerProcessId = 0;
		return false;
	}
	const DWORD waitCode = WaitForSingleObject(textServerProcessHandle, 0);
	if (waitCode == WAIT_TIMEOUT) {
		return true;
	}
	CloseHandle(textServerProcessHandle);
	textServerProcessHandle = nullptr;
	textServerProcessId = 0;
	textServerManagedByApp = false;
	return false;
#else
	if (textServerProcessId <= 0) {
		textServerManagedByApp = false;
		return false;
	}
	if (kill(textServerProcessId, 0) == 0) {
		return true;
	}
	textServerProcessId = 0;
	textServerManagedByApp = false;
	return false;
#endif
}

void ofApp::startLocalTextServer() {
	if (isManagedTextServerRunning()) {
		logWithLevel(OF_LOG_NOTICE, "Local llama-server is already running.");
		checkTextServerStatus(false);
		return;
	}

	const std::string serverExe = findLocalTextServerExecutable();
	if (serverExe.empty()) {
		logWithLevel(OF_LOG_ERROR, "No local llama-server executable was found. Build or copy llama-server.exe into libs/llama/bin first.");
		textServerStatus = ServerStatusState::Unreachable;
		textServerStatusMessage = "Local llama-server executable not found.";
		textServerCapabilityHint.clear();
		return;
	}

	const std::string modelPath = getSelectedModelPath();
	if (modelPath.empty() || !std::filesystem::exists(modelPath)) {
		logWithLevel(OF_LOG_ERROR, "Cannot start local llama-server because the selected GGUF model is missing.");
		textServerStatus = ServerStatusState::Unreachable;
		textServerStatusMessage = "Selected GGUF model is missing; local server was not started.";
		textServerCapabilityHint.clear();
		return;
	}

	const auto [host, port] = parseServerHostPort(effectiveTextServerUrl(textServerUrl));
	const int gpuLayerCount = std::max(0, gpuLayers > 0 ? gpuLayers : detectedModelLayers);

#ifdef _WIN32
	std::vector<std::string> args = {
		serverExe,
		"-m", modelPath,
		"--host", host,
		"--port", ofToString(port),
		"-ngl", ofToString(gpuLayerCount)
	};
	if (contextSize > 0) {
		args.emplace_back("-c");
		args.emplace_back(ofToString(contextSize));
	}

	std::string cmdLine;
	for (size_t i = 0; i < args.size(); ++i) {
		if (i > 0) cmdLine += " ";
		const bool needsQuotes = args[i].find_first_of(" \t\"") != std::string::npos;
		if (!needsQuotes) {
			cmdLine += args[i];
			continue;
		}
		cmdLine += "\"";
		for (char c : args[i]) {
			if (c == '"') cmdLine += '\\';
			cmdLine += c;
		}
		cmdLine += "\"";
	}

	std::wstring wideCmd = ofxGgmlWideFromUtf8(cmdLine);
	std::wstring wideCwd = ofxGgmlWideFromUtf8(std::filesystem::path(serverExe).parent_path().string());
	if (wideCmd.empty()) {
		logWithLevel(OF_LOG_ERROR, "Failed to prepare local llama-server command line.");
		return;
	}

	std::vector<wchar_t> mutableCmd(wideCmd.begin(), wideCmd.end());
	mutableCmd.push_back(L'\0');

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	const BOOL ok = CreateProcessW(
		nullptr,
		mutableCmd.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
		nullptr,
		wideCwd.empty() ? nullptr : wideCwd.c_str(),
		&si,
		&pi);
	if (!ok) {
		logWithLevel(OF_LOG_ERROR, "Failed to start local llama-server. Windows error: " + ofToString(static_cast<int>(GetLastError())));
		textServerStatus = ServerStatusState::Unreachable;
		textServerStatusMessage = "Failed to launch local llama-server.";
		textServerCapabilityHint.clear();
		return;
	}

	if (textServerProcessHandle) {
		CloseHandle(textServerProcessHandle);
	}
	textServerProcessHandle = pi.hProcess;
	textServerProcessId = pi.dwProcessId;
	CloseHandle(pi.hThread);
#else
	pid_t pid = fork();
	if (pid == 0) {
		chdir(std::filesystem::path(serverExe).parent_path().string().c_str());
		execl(
			serverExe.c_str(),
			serverExe.c_str(),
			"-m", modelPath.c_str(),
			"--host", host.c_str(),
			"--port", ofToString(port).c_str(),
			"-ngl", ofToString(gpuLayerCount).c_str(),
			nullptr);
		_exit(127);
	}
	if (pid <= 0) {
		logWithLevel(OF_LOG_ERROR, "Failed to fork local llama-server process.");
		return;
	}
	textServerProcessId = pid;
#endif

	textServerManagedByApp = true;
	textServerStatus = ServerStatusState::Unknown;
	textServerStatusMessage = "Local llama-server started. The app will probe it automatically.";
	textServerCapabilityHint.clear();
	logWithLevel(
		OF_LOG_NOTICE,
		"Started local llama-server on " + host + ":" + ofToString(port) +
		" using model " + ofFilePath::getFileName(modelPath) + ".");
}

void ofApp::stopLocalTextServer(bool logResult) {
	if (!isManagedTextServerRunning()) {
		textServerManagedByApp = false;
		textServerStatus = ServerStatusState::Unknown;
		textServerCapabilityHint.clear();
		if (logResult) {
			logWithLevel(OF_LOG_NOTICE, "No app-managed local llama-server is currently running.");
		}
		return;
	}

#ifdef _WIN32
	TerminateProcess(textServerProcessHandle, 0);
	CloseHandle(textServerProcessHandle);
	textServerProcessHandle = nullptr;
	textServerProcessId = 0;
#else
	kill(textServerProcessId, SIGTERM);
	textServerProcessId = 0;
#endif
	textServerManagedByApp = false;
	textServerStatus = ServerStatusState::Unknown;
	textServerStatusMessage = "Local llama-server stopped.";
	textServerCapabilityHint.clear();
	if (logResult) {
		logWithLevel(OF_LOG_NOTICE, textServerStatusMessage);
	}
}

ofLogLevel ofApp::mapGgmlLogLevel(int level) const {
	switch (level) {
	case 4: return OF_LOG_ERROR;
	case 3: return OF_LOG_WARNING;
	case 2: return OF_LOG_NOTICE;
	case 1: return OF_LOG_VERBOSE;
	default: return OF_LOG_SILENT;
	}
}

void ofApp::probeLlamaCli(const std::string & customPath) {
	cliCapabilitiesProbed = false;
	probeLlamaCliImpl(
		[this](ofLogLevel lvl, const std::string & msg) { logWithLevel(lvl, msg); },
		customPath);
}

void ofApp::probeCliCapabilities() {
	if (cliCapabilitiesProbed) return;
	cliCapabilitiesProbed = true;
	cliSupportsTopK = true;
	cliSupportsMinP = true;
	cliSupportsMirostat = true;
	cliSupportsSingleTurn = true;

	std::string helpText;
	int exitCode = -1;
	if (!runProcessCapture({llamaCliCommand, "--help"}, helpText, exitCode) || helpText.empty()) {
		return;
	}

	const bool hasTopK = helpText.find("--top-k") != std::string::npos;
	const bool hasMinP = helpText.find("--min-p") != std::string::npos;
	const bool hasMirostat =
		helpText.find("--mirostat") != std::string::npos &&
		helpText.find("--mirostat-lr") != std::string::npos &&
		helpText.find("--mirostat-ent") != std::string::npos;
	const bool hasSingleTurn =
		helpText.find("--single-turn") != std::string::npos;

	cliSupportsTopK = hasTopK;
	cliSupportsMinP = hasMinP;
	cliSupportsMirostat = hasMirostat;
	cliSupportsSingleTurn = hasSingleTurn;

	if (!cliSupportsTopK) {
		logWithLevel(OF_LOG_WARNING, "Detected CLI does not support --top-k; Top-K will be ignored.");
	}
	if (!cliSupportsMinP) {
		logWithLevel(OF_LOG_WARNING, "Detected CLI does not support --min-p; Min-P will be ignored.");
	}
	if (!cliSupportsMirostat) {
		logWithLevel(OF_LOG_WARNING, "Detected CLI does not support Mirostat flags; Mirostat settings will be ignored.");
	}
	if (!cliSupportsSingleTurn) {
		logWithLevel(OF_LOG_WARNING, "Detected CLI does not support --single-turn; EOF shutdown path may be less stable.");
	}
}

// ---------------------------------------------------------------------------
// Presets — models
// ---------------------------------------------------------------------------

void ofApp::initModelPresets() {
	modelPresets.clear();
	taskDefaultModelIndices.fill(0);

	auto setFallbackPresets = [this]() {
		modelPresets = {
			{
				"Qwen2.5-1.5B Instruct Q4_K_M",
				"qwen2.5-1.5b-instruct-q4_k_m.gguf",
				"https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf",
				"Alibaba Qwen2.5 — balanced chat model",
				"~1.0 GB", "chat, general"
			},
			{
				"Qwen2.5-Coder-1.5B Instruct Q4_K_M",
				"qwen2.5-coder-1.5b-instruct-q4_k_m.gguf",
				"https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/main/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf",
				"Alibaba Qwen2.5-Coder — optimized for code generation",
				"~1.0 GB", "scripting, code generation"
			},
			{
				"Qwen2.5-Coder-7B Instruct Q4_K_M",
				"qwen2.5-coder-7b-instruct-q4_k_m.gguf",
				"https://huggingface.co/Qwen/Qwen2.5-Coder-7B-Instruct-GGUF/resolve/main/qwen2.5-coder-7b-instruct-q4_k_m.gguf",
				"Alibaba Qwen2.5-Coder — stronger local repo review and patch planning",
				"~4.7 GB", "repo review, larger code edits, architecture analysis"
			}
		};
		// Match the CLI defaults for most modes, but prefer a stronger coder model for Script.
		taskDefaultModelIndices = {0, 2, 0, 0, 0, 0, 0, 0};
	};

	// Try to load presets from scripts/model-catalog.json first.
	std::error_code ec;
	std::filesystem::path catalogPath = ofToDataPath("model-catalog.json", true);
	bool loaded = false;

	if (!std::filesystem::exists(catalogPath, ec) || ec) {
		// Fallback: resolve relative to addon root (…/ofxGgmlGuiExample/src -> addon root).
		auto srcPath = std::filesystem::path(__FILE__).parent_path();
		auto addonRoot = std::filesystem::weakly_canonical(srcPath / ".." / "..", ec);
		if (!ec) {
			catalogPath = addonRoot / "scripts" / "model-catalog.json";
		}
	}

	if (std::filesystem::exists(catalogPath, ec) && !ec) {
		try {
			std::ifstream in(catalogPath);
			ofJson json;
			in >> json;
			if (json.contains("models") && json["models"].is_array()) {
				for (const auto & model : json["models"]) {
					ModelPreset preset;
					preset.name = model.value("name", "");
					preset.filename = model.value("filename", "");
					preset.url = model.value("url", "");
					preset.sizeMB = model.value("size", "");
					preset.bestFor = model.value("best_for", "");
					preset.description = model.value("description", preset.bestFor);
					if (preset.description.empty()) {
						preset.description = "Recommended model";
					}
					if (preset.name.empty() || preset.filename.empty() || preset.url.empty()) {
						continue;
					}
					modelPresets.push_back(std::move(preset));
				}
			}
			if (!modelPresets.empty() &&
				json.contains("task_defaults") && json["task_defaults"].is_object()) {
				auto defaults = json["task_defaults"];
				auto setDefault = [&](const char * key, AiMode mode) {
					const int idxOneBased = defaults.value(key, 0);
					if (idxOneBased > 0) {
						const int idx = std::clamp(idxOneBased - 1, 0,
							static_cast<int>(modelPresets.size()) - 1);
						taskDefaultModelIndices[static_cast<int>(mode)] = idx;
					}
				};
				setDefault("chat", AiMode::Chat);
				setDefault("script", AiMode::Script);
				setDefault("summarize", AiMode::Summarize);
				setDefault("write", AiMode::Write);
				setDefault("translate", AiMode::Translate);
				setDefault("custom", AiMode::Custom);
				setDefault("vision", AiMode::Vision);
				setDefault("speech", AiMode::Speech);
			}
			loaded = !modelPresets.empty();
		} catch (const std::exception & e) {
			logWithLevel(OF_LOG_WARNING,
				std::string("Failed to load model-catalog.json: ") + e.what());
		} catch (...) {
			logWithLevel(OF_LOG_WARNING,
				"Failed to load model-catalog.json: unknown parse error");
		}
	}

	if (!loaded) {
		setFallbackPresets();
	}

	selectedModelIndex = std::clamp(selectedModelIndex, 0,
		std::max(0, static_cast<int>(modelPresets.size()) - 1));
}

// ---------------------------------------------------------------------------
// Presets — script languages
// ---------------------------------------------------------------------------

void ofApp::initScriptLanguages() {
scriptLanguages = ofxGgmlCodeAssistant::defaultLanguagePresets();
}

// ---------------------------------------------------------------------------
// Presets — prompt templates for Custom panel
// ---------------------------------------------------------------------------

void ofApp::initPromptTemplates() {
chatLanguages = ofxGgmlChatAssistant::defaultResponseLanguages();
translateLanguages = ofxGgmlTextAssistant::defaultTranslateLanguages();
promptTemplates.clear();
for (const auto & preset : ofxGgmlTextAssistant::defaultPromptTemplates()) {
	promptTemplates.push_back({preset.name, preset.systemPrompt});
}
promptTemplates.push_back({
	"Data Analyst",
	"You are a data analysis expert. Help interpret data, suggest statistical "
	"methods, write queries, and explain results in plain language."
});
promptTemplates.push_back({
	"System Architect",
	"You are a software architect. Design systems with clear component "
	"boundaries, data flows, and technology choices. Consider scalability, "
	"reliability, and maintainability."
});
promptTemplates.push_back({
	"Debugger",
	"You are an expert debugger. Analyze error messages, stack traces, and "
	"code to identify root causes. Suggest specific fixes and explain why "
	"the bug occurs."
});
promptTemplates.push_back({
	"Test Engineer",
	"You are a test engineering expert. Generate comprehensive test cases "
	"including unit tests, edge cases, error paths, and integration scenarios. "
	"Use the appropriate testing framework for the language."
});
promptTemplates.push_back({
	"Translator",
	"You are a code translator. Convert code between programming languages "
	"while preserving logic, idioms, and best practices of the target language."
});
promptTemplates.push_back({
	"Optimizer",
	"You are a performance optimization expert. Analyze code for bottlenecks, "
	"memory issues, and algorithmic improvements. Suggest concrete optimizations "
	"with expected impact."
});
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ofApp::setup() {
ofSetWindowTitle("ofxGgml AI Studio");
ofSetFrameRate(60);
ofSetBackgroundColor(ofColor(30, 30, 34));

gui.setup(nullptr, true, ImGuiConfigFlags_None, true);
ImGui::GetIO().IniFilename = "imgui_ggml_studio.ini";
applyLogLevel(logLevel);

// Initialize presets.
initModelPresets();
initScriptLanguages();
initPromptTemplates();
if (!scriptLanguages.empty()) {
	scriptSource.setPreferredExtension(
		scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExtension);
}

// Default branch for GitHub.
scriptSourceBranch[0] = '\0';

// Session directory.
sessionDir = ofToDataPath("sessions", true);
std::error_code ec;
std::filesystem::create_directories(sessionDir, ec);
lastSessionPath = ofFilePath::join(sessionDir, "autosave.session");

// Initialize ggml with the selected backend preference.
ofxGgmlSettings settings;
settings.threads = numThreads;
if (selectedBackendIndex >= 0 &&
	selectedBackendIndex < static_cast<int>(backendNames.size())) {
	settings.preferredBackendName = backendNames[selectedBackendIndex];
}
setVulkanRuntimeDisabled(
	shouldDisableVulkanForCurrentSelection(backendNames, selectedBackendIndex));
settings.graphSize = static_cast<size_t>(contextSize);
engineReady = ggml.setup(settings);
if (engineReady) {
engineStatus = "Ready (" + ggml.getBackendName() + ")";
devices = ggml.listDevices();
lastBackendUsed = ggml.getBackendName();
// Populate backend names from discovered devices.
backendNames.clear();
for (const auto & d : devices) {
	backendNames.push_back(d.name);
}
// Auto-select the device that matches the *actual* running backend.
// Previous code blindly picked the first GPU device, but the engine
// may have fallen back to CPU (e.g. when the GPU driver is present
// but cannot create a working context).  Matching by name avoids
// the mismatch where the dropdown says "GPU" but the engine is on
// CPU, and prevents a user from accidentally triggering a reinit
// that crashes.
syncSelectedBackendIndex();
} else {
engineStatus = "Failed to initialize ggml engine";
}

// Log callback.
ggml.setLogCallback([this](int level, const std::string & msg) {
	const ofLogLevel mapped = mapGgmlLogLevel(level);
	if (mapped != OF_LOG_SILENT) {
		logWithLevel(mapped, msg);
	}
});

// Auto-load last session if available.
autoLoadSession();

if (useModeTokenBudgets) {
	maxTokens = std::clamp(modeMaxTokens[static_cast<size_t>(activeMode)], 32, 4096);
}

// Detect llama-completion / llama-cli / llama at startup.
probeLlamaCli();

// Detect model layer count for GPU layers slider.
detectModelLayers();
{
	if (gpuLayers == 0 && detectedModelLayers > 0) {
		// Default GPU layers to all if a model is loaded and the user
		// hasn't explicitly set them to 0 from a previous session.
		gpuLayers = detectedModelLayers;
	}
}
syncTextBackendForActiveMode(false);
for (int i = 0; i < kModeCount; ++i) {
	const AiMode mode = static_cast<AiMode>(i);
	if (!aiModeSupportsTextBackend(mode)) {
		continue;
	}
	if (preferredTextBackendForMode(mode) == TextInferenceBackend::LlamaServer) {
		if (trim(textServerUrl).empty()) {
			copyStringToBuffer(textServerUrl, sizeof(textServerUrl), kDefaultTextServerUrl);
		}
		ensureTextServerReady(
			false,
			shouldManageLocalTextServer(effectiveTextServerUrl(textServerUrl)));
		break;
	}
}

// Pre-fill example system prompt only if not restored from session.
if (customSystemPrompt[0] == '\0') {
copyStringToBuffer(
	customSystemPrompt,
	sizeof(customSystemPrompt),
	"You are a helpful assistant. Respond concisely.");
}

// Apply default theme.
applyTheme(themeIndex);
}

void ofApp::update() {
applyPendingOutput();
}

void ofApp::draw() {
ofBackground(30, 30, 34);
gui.begin();
drawMenuBar();

const float windowW = static_cast<float>(ofGetWidth());
const float windowH = static_cast<float>(ofGetHeight());
const float menuBarH = ImGui::GetFrameHeight() + 4.0f;
const float statusBarH = 28.0f;
const float sidebarW = 220.0f;

// Sidebar.
ImGui::SetNextWindowPos(ImVec2(0.0f, menuBarH), ImGuiCond_Always);
ImGui::SetNextWindowSize(ImVec2(sidebarW, windowH - menuBarH - statusBarH), ImGuiCond_Always);
drawSidebar();

// Main panel.
ImGui::SetNextWindowPos(ImVec2(sidebarW, menuBarH), ImGuiCond_Always);
ImGui::SetNextWindowSize(ImVec2(windowW - sidebarW, windowH - menuBarH - statusBarH), ImGuiCond_Always);
drawMainPanel();

// Status bar.
ImGui::SetNextWindowPos(ImVec2(0.0f, windowH - statusBarH), ImGuiCond_Always);
ImGui::SetNextWindowSize(ImVec2(windowW, statusBarH), ImGuiCond_Always);
drawStatusBar();

// Optional floating windows.
if (showDeviceInfo) drawDeviceInfoWindow();
if (showLog) drawLogWindow();
if (showPerformance) drawPerformanceWindow();

gui.end();
}

void ofApp::exit() {
autoSaveSession();
stopLocalTextServer(false);
stopGeneration();
stopSpeechRecording(false);
ggml.close();
gui.exit();
}

void ofApp::keyPressed(int key) {
// Global shortcuts.
if (key == OF_KEY_F1) showDeviceInfo = !showDeviceInfo;
if (key == OF_KEY_F2) showLog = !showLog;
if (key == OF_KEY_F3) showPerformance = !showPerformance;
if (key == 27) stopGeneration();  // Escape cancels generation
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------

void ofApp::drawMenuBar() {
if (ImGui::BeginMainMenuBar()) {
if (ImGui::BeginMenu("File")) {
if (ImGui::MenuItem("Save Session", "Ctrl+S")) {
ofFileDialogResult result = ofSystemSaveDialog(
"session.txt", "Save Session");
if (result.bSuccess) {
saveSession(result.getPath());
}
}
if (ImGui::MenuItem("Load Session", "Ctrl+L")) {
ofFileDialogResult result = ofSystemLoadDialog(
"Load Session", false, sessionDir);
if (result.bSuccess) {
loadSession(result.getPath());
}
}
ImGui::Separator();
if (ImGui::MenuItem("Clear All Output")) {
chatMessages.clear();
scriptMessages.clear();
scriptOutput.clear();
summarizeOutput.clear();
writeOutput.clear();
translateOutput.clear();
customOutput.clear();
visionOutput.clear();
speechOutput.clear();
speechDetectedLanguage.clear();
speechTranscriptPath.clear();
speechSrtPath.clear();
speechSegmentCount = 0;
}
ImGui::Separator();
if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
ofExit(0);
}
ImGui::EndMenu();
}
if (ImGui::BeginMenu("View")) {
ImGui::MenuItem("Device Info    (F1)", nullptr, &showDeviceInfo);
ImGui::MenuItem("Engine Log     (F2)", nullptr, &showLog);
ImGui::MenuItem("Performance    (F3)", nullptr, &showPerformance);
ImGui::EndMenu();
}
ImGui::EndMainMenuBar();
}
}

// ---------------------------------------------------------------------------
// Sidebar — mode selection + model preset + quick settings
// ---------------------------------------------------------------------------

void ofApp::drawSidebar() {
ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

if (ImGui::Begin("##Sidebar", nullptr, flags)) {
ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "AI Studio");
ImGui::Separator();
ImGui::Spacing();
ImGui::Text("Mode:");
ImGui::Spacing();

for (int i = 0; i < kModeCount; i++) {
bool selected = (static_cast<int>(activeMode) == i);
if (ImGui::Selectable(modeLabels[i], selected, ImGuiSelectableFlags_None, ImVec2(0, 28))) {
activeMode = static_cast<AiMode>(i);
	if (useModeTokenBudgets) {
		maxTokens = std::clamp(modeMaxTokens[static_cast<size_t>(i)], 32, 4096);
	}
	syncTextBackendForActiveMode(false);
}
}

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();

// Model preset selector.
ImGui::Text("Model:");
ImGui::SetNextItemWidth(-1);
const bool useServerBackend =
	(textInferenceBackend == TextInferenceBackend::LlamaServer);
if (!modelPresets.empty()) {
if (ImGui::BeginCombo("##ModelSel", modelPresets[static_cast<size_t>(selectedModelIndex)].name.c_str())) {
for (int i = 0; i < static_cast<int>(modelPresets.size()); i++) {
bool isSelected = (selectedModelIndex == i);
const auto & preset = modelPresets[static_cast<size_t>(i)];
std::string label = preset.name + "  " + preset.sizeMB;
if (ImGui::Selectable(label.c_str(), isSelected)) {
selectedModelIndex = i;
detectModelLayers();
// Default to all GPU layers when switching models.
if (detectedModelLayers > 0) {
gpuLayers = detectedModelLayers;
}
}
if (ImGui::IsItemHovered()) {
ImGui::SetTooltip("%s\nBest for: %s\nFile: %s",
preset.description.c_str(),
preset.bestFor.c_str(),
preset.filename.c_str());
}
if (isSelected) ImGui::SetItemDefaultFocus();
}
ImGui::EndCombo();
}
}

if (!modelPresets.empty()) {
	const int recommendedIdx = std::clamp(
		taskDefaultModelIndices[static_cast<int>(activeMode)],
		0, static_cast<int>(modelPresets.size()) - 1);
	ImGui::BeginDisabled(recommendedIdx == selectedModelIndex);
	if (ImGui::Button("Use recommended", ImVec2(-1, 0))) {
		selectedModelIndex = recommendedIdx;
		detectModelLayers();
		if (detectedModelLayers > 0) {
			gpuLayers = detectedModelLayers;
		}
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Switch to the catalog default for this mode.");
	}

	const auto & selectedPreset = modelPresets[static_cast<size_t>(selectedModelIndex)];
	const std::string modelPath = getSelectedModelPath();
	if (!modelPath.empty()) {
		std::error_code modelEc;
		if (std::filesystem::exists(modelPath, modelEc) && !modelEc) {
			drawWrappedDisabledText("File: " + modelPath);
			if (useServerBackend) {
				drawWrappedDisabledText("Server mode active: this local GGUF remains useful for reviews and embeddings.");
			}
		} else if (useServerBackend) {
			drawWrappedDisabledText("Server mode active: a local GGUF is optional for normal text generation.");
			drawWrappedDisabledText("Suggested local file: " + modelPath);
			ImGui::BeginDisabled(selectedPreset.url.empty());
			if (ImGui::SmallButton("Download in browser")) {
				ofLaunchBrowser(selectedPreset.url);
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Opens the preset download URL in your browser.");
			}
		} else {
			ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f),
				"Model missing: %s", ofFilePath::getFileName(modelPath).c_str());
			drawWrappedDisabledText("Place file at: " + modelPath);
			ImGui::BeginDisabled(selectedPreset.url.empty());
			if (ImGui::SmallButton("Download in browser")) {
				ofLaunchBrowser(selectedPreset.url);
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Opens the preset download URL in your browser.");
			}
			ImGui::SameLine();
			const int presetNumber = selectedModelIndex + 1;
			std::string downloadCmd = "./scripts/download-model.sh --preset " + ofToString(presetNumber);
			if (ImGui::SmallButton("Copy download command")) {
				copyToClipboard(downloadCmd);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Copies a shell command to fetch preset %d into bin/data/models/", presetNumber);
			}
		}
	}
}
ImGui::Spacing();
const bool modeSupportsTextBackend = aiModeSupportsTextBackend(activeMode);
ImGui::Text(modeSupportsTextBackend ? "Text Backend (this mode):" : "Text Backend:");
ImGui::SetNextItemWidth(-1);
ImGui::BeginDisabled(!modeSupportsTextBackend);
int textBackendIndex = static_cast<int>(textInferenceBackend);
if (ImGui::Combo("##TextBackend", &textBackendIndex,
	kTextBackendLabels, IM_ARRAYSIZE(kTextBackendLabels))) {
	textInferenceBackend = clampTextInferenceBackend(textBackendIndex);
	rememberTextBackendForMode(activeMode, textInferenceBackend);
	if (textInferenceBackend == TextInferenceBackend::LlamaServer &&
		trim(textServerUrl).empty()) {
		copyStringToBuffer(
			textServerUrl,
			sizeof(textServerUrl),
			kDefaultTextServerUrl);
	}
	announceTextBackendChange();
}
ImGui::EndDisabled();
if (modeSupportsTextBackend) {
	drawWrappedDisabledText("Stored separately per text mode. Switching tabs restores that mode's backend.");
} else {
	drawWrappedDisabledText("Vision and Speech use their own pipelines. Switch to a text mode to change its backend.");
}
if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
	const bool serverUrlChanged = ImGui::InputText("Server URL", textServerUrl, sizeof(textServerUrl));
	ImGui::InputText("Server model", textServerModel, sizeof(textServerModel));
	if (serverUrlChanged) {
		textServerStatus = ServerStatusState::Unknown;
		textServerStatusMessage.clear();
		textServerCapabilityHint.clear();
		ensureTextServerReady(
			false,
			shouldManageLocalTextServer(effectiveTextServerUrl(textServerUrl)));
	}
	drawWrappedDisabledText("Uses a warm OpenAI-compatible llama-server for Chat, Script, and text modes.");
	drawWrappedDisabledText("Leave Server model empty to use the model already loaded by the server.");
	drawWrappedDisabledText("Server-friendly defaults are applied automatically, and the app starts the local server during setup.");
	drawWrappedDisabledText("CLI binaries are optional and only used as a local fallback path.");
	const std::string localServerExe = findLocalTextServerExecutable();
	if (!localServerExe.empty()) {
		drawWrappedDisabledText("Local server executable: " + ofFilePath::getFileName(localServerExe));
	} else {
		ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.35f, 1.0f), "Local server executable not found.");
		drawWrappedDisabledText("Build it with scripts/build-llama-server.ps1 when you want a managed local server.");
	}
	if (textServerStatus != ServerStatusState::Unknown && !textServerStatusMessage.empty()) {
		const ImVec4 statusColor =
			(textServerStatus == ServerStatusState::Reachable)
				? ImVec4(0.35f, 0.8f, 0.45f, 1.0f)
				: ImVec4(0.9f, 0.45f, 0.35f, 1.0f);
		ImGui::TextColored(statusColor, "%s", textServerStatusMessage.c_str());
		if (!textServerCapabilityHint.empty()) {
			drawWrappedDisabledText(textServerCapabilityHint);
		}
	}
} else {
	if (llamaCliState.load(std::memory_order_relaxed) == 1) {
		drawWrappedDisabledText("Optional CLI fallback detected: " + llamaCliCommand);
	} else {
		drawWrappedDisabledText("Optional CLI fallback is not installed. Build it only if you want a local non-server fallback.");
	}
}

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();
ImGui::Text("Settings");
ImGui::Spacing();

if (ImGui::Button("Balanced", ImVec2(ImGui::GetContentRegionAvail().x / 3.0f - 3.0f, 0))) {
	maxTokens = 512;
	temperature = 0.7f;
	topP = 0.9f;
	topK = 40;
	minP = 0.0f;
	repeatPenalty = 1.1f;
	mirostatMode = 0;
}
ImGui::SameLine();
if (ImGui::Button("Code", ImVec2(ImGui::GetContentRegionAvail().x / 2.0f - 2.0f, 0))) {
	maxTokens = 1024;
	temperature = 0.25f;
	topP = 0.92f;
	topK = 60;
	minP = 0.05f;
	repeatPenalty = 1.05f;
	mirostatMode = 0;
}
ImGui::SameLine();
if (ImGui::Button("Creative", ImVec2(-1, 0))) {
	maxTokens = 768;
	temperature = 1.0f;
	topP = 0.95f;
	topK = 80;
	minP = 0.0f;
	repeatPenalty = 1.05f;
	mirostatMode = 0;
}

ImGui::SetNextItemWidth(-1);
if (ImGui::SliderInt("##MaxTok", &maxTokens, 32, 4096, "Tokens: %d")) {
	modeMaxTokens[static_cast<size_t>(activeMode)] = maxTokens;
}
ImGui::Checkbox("Per-mode token budgets", &useModeTokenBudgets);
if (ImGui::IsItemHovered()) {
	ImGui::SetTooltip("Remember and auto-apply a separate Max Tokens value per mode.");
}
ImGui::SetNextItemWidth(-1);
ImGui::SliderFloat("##Temp", &temperature, 0.0f, 2.0f, "Temp: %.2f");
ImGui::SetNextItemWidth(-1);
ImGui::SliderFloat("##TopP", &topP, 0.0f, 1.0f, "Top-P: %.2f");
ImGui::SetNextItemWidth(-1);
ImGui::SliderInt("##TopK", &topK, 0, 200, "Top-K: %d");
ImGui::SetNextItemWidth(-1);
ImGui::SliderFloat("##MinP", &minP, 0.0f, 1.0f, "Min-P: %.2f");
ImGui::SetNextItemWidth(-1);
ImGui::SliderFloat("##RepeatPenalty", &repeatPenalty, 1.0f, 2.0f, "Repeat Penalty: %.2f");
ImGui::SetNextItemWidth(-1);
ImGui::SliderInt("##Seed", &seed, -1, 99999, "Seed: %d");
ImGui::Checkbox("Stop on natural boundary", &stopAtNaturalBoundary);
if (ImGui::IsItemHovered()) {
	ImGui::SetTooltip("Trims cut-off output to sentence or line boundaries when generation ends abruptly.");
}
ImGui::Checkbox("Auto-continue cutoffs (Script)", &autoContinueCutoff);
if (ImGui::IsItemHovered()) {
	ImGui::SetTooltip("When Script output appears cut off, run one automatic continuation pass.");
}
ImGui::BeginDisabled(useServerBackend);
ImGui::Checkbox("Use prompt cache", &usePromptCache);
ImGui::EndDisabled();
if (ImGui::IsItemHovered()) {
	ImGui::SetTooltip(useServerBackend
		? "Prompt cache is a local CLI optimization and is ignored for llama-server."
		: "Reuse llama prompt cache between requests for faster follow-up responses.");
}
const char * liveContextLabels[] = {
	"Live context: Offline",
	"Live context: Loaded sources only",
	"Live context: Live context",
	"Live context: Strict citations"
};
int liveContextModeIndex = static_cast<int>(liveContextMode);
ImGui::SetNextItemWidth(-1);
if (ImGui::Combo("##LiveContextMode", &liveContextModeIndex, liveContextLabels, 4)) {
	liveContextMode = static_cast<LiveContextMode>(liveContextModeIndex);
}
if (ImGui::IsItemHovered()) {
	ImGui::SetTooltip(
		"Offline disables external context. Loaded sources only uses the URLs you provide. "
		"Live context also allows automatic news, weather, and search grounding.");
}
const bool autoLiveLookupsEnabled =
	liveContextMode == LiveContextMode::LiveContext ||
	liveContextMode == LiveContextMode::LiveContextStrictCitations;
ImGui::BeginDisabled(!autoLiveLookupsEnabled);
ImGui::Checkbox("Allow prompt URLs", &liveContextAllowPromptUrls);
ImGui::Checkbox("Allow domain providers", &liveContextAllowDomainProviders);
ImGui::Checkbox("Allow generic search", &liveContextAllowGenericSearch);
ImGui::EndDisabled();

const char * mirostatLabels[] = { "Mirostat: Off", "Mirostat", "Mirostat 2.0" };
ImGui::SetNextItemWidth(-1);
ImGui::Combo("##MirostatMode", &mirostatMode, mirostatLabels, 3);
if (mirostatMode > 0) {
	ImGui::SetNextItemWidth(-1);
	ImGui::SliderFloat("##MirostatTau", &mirostatTau, 0.0f, 10.0f, "Mirostat Tau: %.1f");
	ImGui::SetNextItemWidth(-1);
	ImGui::SliderFloat("##MirostatEta", &mirostatEta, 0.0f, 1.0f, "Mirostat Eta: %.2f");
}

ImGui::SetNextItemWidth(-1);
ImGui::BeginDisabled(useServerBackend);
ImGui::SliderInt("##Threads", &numThreads, 1, 32, "Threads: %d");
ImGui::EndDisabled();
if (useServerBackend && ImGui::IsItemHovered()) {
	ImGui::SetTooltip("Thread count is only used by the local CLI path.");
}
ImGui::SetNextItemWidth(-1);
ImGui::SliderInt(
	"##ContextSize",
	&contextSize,
	256,
	16384,
	useServerBackend ? "Prompt budget: %d" : "Context: %d");
if (ImGui::IsItemHovered()) {
	ImGui::SetTooltip(useServerBackend
		? "Used as a local prompt-trimming heuristic before sending text to llama-server."
		: "Maximum local context window requested for llama-completion.");
}
ImGui::SetNextItemWidth(-1);
ImGui::BeginDisabled(useServerBackend);
ImGui::SliderInt("##BatchSize", &batchSize, 32, 4096, "Batch: %d");
ImGui::EndDisabled();
if (useServerBackend && ImGui::IsItemHovered()) {
	ImGui::SetTooltip("Batch size is only used by the local CLI path.");
}

if (!backendNames.empty()) {
	ImGui::BeginDisabled(useServerBackend);
	selectedBackendIndex = std::clamp(selectedBackendIndex, 0, static_cast<int>(backendNames.size()) - 1);
	const std::string currentBackendLabel = backendNames[static_cast<size_t>(selectedBackendIndex)];
	if (ImGui::BeginCombo("Backend", currentBackendLabel.c_str())) {
		for (int i = 0; i < static_cast<int>(backendNames.size()); i++) {
			const bool isSelected = (selectedBackendIndex == i);
			if (ImGui::Selectable(backendNames[static_cast<size_t>(i)].c_str(), isSelected)) {
				if (selectedBackendIndex != i) {
					selectedBackendIndex = i;
					reinitBackend();
				}
			}
			if (isSelected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();
}

ImGui::SetNextItemWidth(-1);
{
// GPU layers control the llama-completion CLI process, which has
// its own GPU support — always allow the user to adjust them.
int sliderMax = detectedModelLayers > 0 ? detectedModelLayers : 128;
ImGui::BeginDisabled(useServerBackend);
ImGui::SliderInt("##GPULayers", &gpuLayers, 0, sliderMax, "GPU Layers: %d");
if (ImGui::IsItemHovered()) {
if (detectedModelLayers > 0) {
ImGui::SetTooltip("Model has %d layers\nGPU offloading via llama-completion", detectedModelLayers);
}
}
// None / All quick buttons.
if (ImGui::Button("None##gpu", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 2, 0))) {
gpuLayers = 0;
}
ImGui::SameLine();
if (ImGui::Button("All##gpu", ImVec2(-1, 0))) {
gpuLayers = detectedModelLayers > 0 ? detectedModelLayers : 128;
}
ImGui::EndDisabled();
}

ImGui::Separator();
int currentLogIdx = std::clamp(logLevelIndex(logLevel), 0,
	static_cast<int>(kLogLevelOptions.size()) - 1);
const char * currentLogLabel = kLogLevelOptions[static_cast<size_t>(currentLogIdx)].label;
if (ImGui::BeginCombo("Log Level", currentLogLabel)) {
	for (size_t i = 0; i < kLogLevelOptions.size(); i++) {
		const bool isSelected = (currentLogIdx == static_cast<int>(i));
		if (ImGui::Selectable(kLogLevelOptions[i].label, isSelected)) {
			if (!isSelected) {
				applyLogLevel(kLogLevelOptions[i].level);
				logWithLevel(OF_LOG_NOTICE,
					std::string("Log level set to ") + kLogLevelOptions[i].label);
				currentLogIdx = static_cast<int>(i);
			}
		}
		if (isSelected) ImGui::SetItemDefaultFocus();
	}
	ImGui::EndCombo();
}
ImGui::Checkbox("Show Engine Log", &showLog);

const char * themeLabels[] = { "Dark", "Light", "Classic" };
if (ImGui::Combo("Theme", &themeIndex, themeLabels, 3)) {
	applyTheme(themeIndex);
}

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();

// Engine status indicator.
if (engineReady) {
ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Engine: OK");
} else {
ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.2f, 1.0f), "Engine: Error");
}
ImGui::Text("Backend: %s", ggml.getBackendName().c_str());

if (generating.load()) {
ImGui::Spacing();
const char * spinner = "|/-\\";
int spinIdx = static_cast<int>(ImGui::GetTime() / kSpinnerInterval) % 4;
float elapsed = ofGetElapsedTimef() - generationStartTime;
char genLabel[64];
snprintf(genLabel, sizeof(genLabel), "%c Generating... (%.1fs)", spinner[spinIdx], elapsed);
ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", genLabel);
if (ImGui::Button("Stop", ImVec2(-1, 0))) {
stopGeneration();
}
}
}
ImGui::End();
}

// ---------------------------------------------------------------------------
// Main panel — dispatches to mode-specific panels
// ---------------------------------------------------------------------------

void ofApp::drawMainPanel() {
ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

if (ImGui::Begin("##MainPanel", nullptr, flags)) {
switch (activeMode) {
case AiMode::Chat:      drawChatPanel();      break;
case AiMode::Script:    drawScriptPanel();    break;
case AiMode::Summarize: drawSummarizePanel(); break;
case AiMode::Write:     drawWritePanel();     break;
case AiMode::Translate: drawTranslatePanel(); break;
case AiMode::Custom:    drawCustomPanel();    break;
case AiMode::Vision:    drawVisionPanel();    break;
case AiMode::Speech:    drawSpeechPanel();    break;
}
}
ImGui::End();
}

// ---------------------------------------------------------------------------
// Chat panel
// ---------------------------------------------------------------------------

void ofApp::drawChatPanel() {
	drawPanelHeader("Chat", "conversation with the ggml engine");
	if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
		if (!textServerCapabilityHint.empty()) {
			ImGui::TextDisabled("%s", textServerCapabilityHint.c_str());
		} else {
			ImGui::TextDisabled("Server-backed chat is active.");
		}
	}

	if (chatLanguages.empty()) {
		chatLanguages = ofxGgmlChatAssistant::defaultResponseLanguages();
	}
chatLanguageIndex = std::clamp(
	chatLanguageIndex,
	0,
	std::max(0, static_cast<int>(chatLanguages.size()) - 1));

ImGui::Text("Response language:");
ImGui::SameLine();
ImGui::SetNextItemWidth(170);
const std::string selectedChatLanguage =
	chatLanguages[static_cast<size_t>(chatLanguageIndex)].name;
if (ImGui::BeginCombo("##ChatLang", selectedChatLanguage.c_str())) {
	for (int i = 0; i < static_cast<int>(chatLanguages.size()); i++) {
		const bool selected = (chatLanguageIndex == i);
		if (ImGui::Selectable(
			chatLanguages[static_cast<size_t>(i)].name.c_str(),
			selected)) {
			chatLanguageIndex = i;
		}
		if (selected) ImGui::SetItemDefaultFocus();
	}
	ImGui::EndCombo();
}
if (ImGui::IsItemHovered()) {
	ImGui::SetTooltip("Auto lets the model decide. Choose a language to force chat replies.");
}

// Message history.
float inputH = 60.0f;
ImGui::BeginChild("##ChatHistory", ImVec2(0, -inputH), true);
for (size_t i = 0; i < chatMessages.size(); i++) {
auto & msg = chatMessages[i];
if (msg.role == "user") {
ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "You:");
} else if (msg.role == "assistant") {
ImGui::TextColored(ImVec4(0.6f, 0.7f, 1.0f, 1.0f), "AI:");
} else {
ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "System:");
}
ImGui::TextWrapped("%s", msg.text.c_str());
ImGui::Spacing();
}
// Show streaming output while generating in Chat mode.
if (generating.load() && activeGenerationMode == AiMode::Chat) {
ImGui::TextColored(ImVec4(0.6f, 0.7f, 1.0f, 1.0f), "AI:");
ImGui::SameLine();
std::string partial;
{
std::lock_guard<std::mutex> lock(streamMutex);
partial = streamingOutput;
}
if (partial.empty()) {
int dots = static_cast<int>(ImGui::GetTime() * kDotsAnimationSpeed) % 4;
const char * thinking[] = {"thinking", "thinking.", "thinking..", "thinking..."};
ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", thinking[dots]);
} else {
ImGui::TextWrapped("%s", partial.c_str());
}
ImGui::Spacing();
}
// Auto-scroll.
if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) {
ImGui::SetScrollHereY(1.0f);
}
ImGui::EndChild();

// Input.
float availW = ImGui::GetContentRegionAvail().x;
ImGui::SetNextItemWidth(availW - 190.0f);
bool submitted = ImGui::InputText("##ChatIn", chatInput, sizeof(chatInput),
ImGuiInputTextFlags_EnterReturnsTrue);
ImGui::SameLine();
bool sendClicked = ImGui::Button("Send", ImVec2(70, 0));
ImGui::SameLine();
bool sendWithSourcesClicked = ImGui::Button("Send with Sources", ImVec2(130, 0));
if (liveContextMode == LiveContextMode::Offline) {
	ImGui::SameLine();
	ImGui::TextDisabled("(offline)");
} else if (liveContextMode == LiveContextMode::LoadedSourcesOnly) {
	ImGui::SameLine();
	ImGui::TextDisabled("(loaded sources only)");
}

ImGui::InputTextMultiline(
	"Loaded Sources (URLs)",
	sourceUrlsInput,
	sizeof(sourceUrlsInput),
	ImVec2(-1, 48));
if (ImGui::IsItemHovered()) {
	ImGui::SetTooltip("Optional source URLs for grounded answers in Chat, Summarize, and Custom.");
}

	if ((submitted || sendClicked || sendWithSourcesClicked) && std::strlen(chatInput) > 0 && !generating.load()) {
std::string userText(chatInput);
chatMessages.push_back({"user", userText, ofGetElapsedTimef()});
fprintf(stderr, "[ChatWindow] You: %s\n", userText.c_str());
std::memset(chatInput, 0, sizeof(chatInput));
ofxGgmlRealtimeInfoSettings realtimeSettings = buildLiveContextSettings(
	"",
	"Live context for chat",
	true);
if (sendWithSourcesClicked) {
	realtimeSettings = buildLiveContextSettings(
		sourceUrlsInput,
		"Loaded sources for this chat reply",
		true);
}
runInference(AiMode::Chat, userText, "", "", realtimeSettings);
	}

	// Copy / Clear / Export row.
	if (!chatMessages.empty()) {
		if (ImGui::SmallButton("Summarize Chat")) {
			std::ostringstream transcript;
			size_t included = 0;
			for (auto it = chatMessages.rbegin(); it != chatMessages.rend() && included < 8; ++it, ++included) {
				transcript << it->role << ": " << it->text << "\n";
			}
			runInference(
				AiMode::Chat,
				"Summarize the recent conversation.",
				"",
				buildStructuredTextPrompt(
					"You are a concise conversation analyst.",
					"Summarize the recent conversation into decisions, open questions, and next actions.",
					"Recent transcript",
					transcript.str(),
					"Summary"));
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Copy Chat")) {
			std::string all;
			for (const auto & m : chatMessages) {
				all += m.role + ": " + m.text + "\n\n";
			}
copyToClipboard(all);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear Chat")) {
			chatMessages.clear();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Export Chat")) {
			ofFileDialogResult result = ofSystemSaveDialog(
				"chat_export.md", "Export Chat History");
			if (result.bSuccess) {
				exportChatHistory(result.getPath());
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Script panel — with language selector and source browser
// ---------------------------------------------------------------------------

void ofApp::drawScriptPanel() {
drawPanelHeader("Script Generation", "generate or explain code");

// Language selector and source controls on same row.
ImGui::Text("Language:");
ImGui::SameLine();
ImGui::SetNextItemWidth(140);
	if (!scriptLanguages.empty()) {
	if (ImGui::BeginCombo("##LangSel", scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].name.c_str())) {
	for (int i = 0; i < static_cast<int>(scriptLanguages.size()); i++) {
	bool isSelected = (selectedLanguageIndex == i);
	if (ImGui::Selectable(scriptLanguages[static_cast<size_t>(i)].name.c_str(), isSelected)) {
	if (selectedLanguageIndex != i) {
	selectedLanguageIndex = i;
	if (!scriptLanguages.empty()) {
		scriptSource.setPreferredExtension(
			scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExtension);
	}
	if (scriptSource.getSourceType() == ofxGgmlScriptSourceType::LocalFolder &&
		!scriptSource.getLocalFolderPath().empty()) {
		selectedScriptFileIndex = -1;
		scriptSource.rescan();
	}
	}
	}
	if (isSelected) ImGui::SetItemDefaultFocus();
	}
	ImGui::EndCombo();
	}
}

ImGui::SameLine();
ImGui::Text("Source:");
ImGui::SameLine();

// Source type buttons.
ofxGgmlScriptSourceType sourceType = scriptSource.getSourceType();
bool isNone = (sourceType == ofxGgmlScriptSourceType::None);
bool isLocal = (sourceType == ofxGgmlScriptSourceType::LocalFolder);
bool isGitHub = (sourceType == ofxGgmlScriptSourceType::GitHubRepo);
bool isInternet = (sourceType == ofxGgmlScriptSourceType::Internet);

if (isNone) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
if (ImGui::SmallButton("None")) {
	scriptSource.clear();
	selectedScriptFileIndex = -1;
}
if (isNone) ImGui::PopStyleColor();
ImGui::SameLine();

if (isLocal) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
if (ImGui::SmallButton("Local Folder")) {
ofFileDialogResult result = ofSystemLoadDialog("Select Script Folder", true);
if (result.bSuccess) {
	selectedScriptFileIndex = -1;
	if (!scriptLanguages.empty()) {
		scriptSource.setPreferredExtension(
			scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExtension);
	}
	scriptSource.setLocalFolder(result.getPath());
}
}
if (isLocal) ImGui::PopStyleColor();
ImGui::SameLine();

const auto localWorkspaceInfo = scriptSource.getWorkspaceInfo();
const bool isVisualStudioWorkspace =
	isLocal &&
	(localWorkspaceInfo.hasVisualStudioSolution ||
	 !localWorkspaceInfo.visualStudioProjectPaths.empty());
if (isVisualStudioWorkspace) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.35f, 0.7f, 1.0f));
if (ImGui::SmallButton("Visual Studio")) {
	ofFileDialogResult result = ofSystemLoadDialog("Select Visual Studio .sln or .vcxproj", false);
	if (result.bSuccess) {
		selectedScriptFileIndex = -1;
		if (!scriptLanguages.empty()) {
			scriptSource.setPreferredExtension(
				scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExtension);
		}
		scriptSource.setVisualStudioWorkspace(result.getPath());
	}
}
if (isVisualStudioWorkspace) ImGui::PopStyleColor();
ImGui::SameLine();

if (isGitHub) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.6f, 1.0f));
if (ImGui::SmallButton("GitHub")) {
	selectedScriptFileIndex = -1;
	scriptSource.setGitHubMode();
}
if (isGitHub) ImGui::PopStyleColor();
ImGui::SameLine();

if (isInternet) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.5f, 0.7f, 1.0f));
if (ImGui::SmallButton("Internet")) {
	selectedScriptFileIndex = -1;
	scriptSource.setInternetMode();
}
if (isInternet) ImGui::PopStyleColor();

// Script source file browser (inline when active).
const auto scriptSourceFiles = scriptSource.getFiles();
if (sourceType != ofxGgmlScriptSourceType::None && !scriptSourceFiles.empty()) {
ImGui::BeginChild("##ScriptFiles", ImVec2(-1, 80), true);
if (sourceType == ofxGgmlScriptSourceType::LocalFolder) {
const std::string localPath = scriptSource.getLocalFolderPath();
ImGui::TextDisabled("Folder: %s", localPath.c_str());
} else if (sourceType == ofxGgmlScriptSourceType::GitHubRepo) {
const std::string ownerRepo = scriptSource.getGitHubOwnerRepo();
const std::string branch = scriptSource.getGitHubBranch();
ImGui::TextDisabled("GitHub: %s (%s)", ownerRepo.c_str(), branch.c_str());
} else {
ImGui::TextDisabled("Internet sources");
}
for (int i = 0; i < static_cast<int>(scriptSourceFiles.size()); i++) {
const auto & entry = scriptSourceFiles[static_cast<size_t>(i)];
ImGui::PushID(i);
std::string icon = entry.isDirectory ? "[dir] " : "      ";
bool isSelected = (selectedScriptFileIndex == i);
if (ImGui::Selectable((icon + entry.name).c_str(), isSelected) && !entry.isDirectory) {
selectedScriptFileIndex = i;
}
ImGui::PopID();
}
ImGui::EndChild();
}

	if (sourceType != ofxGgmlScriptSourceType::None) {
		drawScriptSourcePanel();
	}

	ImGui::Spacing();

	// --- 3. Chat History (dynamic size) ---
	ImGui::Text("Coding Chat:");
	ImGui::BeginChild("##ScriptChatHistory", ImVec2(-1, -120), true);
	for (const auto & msg : scriptMessages) {
		if (msg.role == "user") {
			ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "You:");
		} else if (msg.role == "system") {
			ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "System:");
		} else {
			ImGui::TextColored(ImVec4(0.6f, 0.7f, 1.0f, 1.0f), "AI:");
		}
		ImGui::TextWrapped("%s", msg.text.c_str());
		ImGui::Spacing();
	}
	if (generating.load() && activeGenerationMode == AiMode::Script) {
		ImGui::TextColored(ImVec4(0.6f, 0.7f, 1.0f, 1.0f), "AI:");
		std::string partial;
		{
			std::lock_guard<std::mutex> lock(streamMutex);
			partial = streamingOutput;
		}
		if (partial.empty()) {
			int dots = static_cast<int>(ImGui::GetTime() * kDotsAnimationSpeed) % 4;
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", kWaitingLabels[dots]);
		} else {
			std::string preview;
			if (!scriptOutput.empty() && partial.size() > scriptOutput.size()) {
				preview = partial.substr(scriptOutput.size());
			} else {
				preview = partial;
			}
			ImGui::TextWrapped("%s", preview.c_str());
		}
	}

	if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40.0f) {
		ImGui::SetScrollHereY(1.0f);
	}
	ImGui::EndChild();

	// --- 4. Multiline Chat Input ---
	const bool scriptChatSubmitted = ImGui::InputTextMultiline(
		"##ScriptIn", scriptInput, sizeof(scriptInput), ImVec2(-1, 50),
		ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);

const bool hasSelectedFile =
	selectedScriptFileIndex >= 0 &&
	selectedScriptFileIndex < static_cast<int>(scriptSourceFiles.size()) &&
	!scriptSourceFiles[static_cast<size_t>(selectedScriptFileIndex)].isDirectory;
const bool hasUserInput = std::strlen(scriptInput) > 0;
static char scriptBuildErrors[8192] = {};
static bool restrictWorkspaceToFocusedFile = true;

auto buildScriptAssistantContext = [this]() {
	ofxGgmlCodeAssistantContext context;
	context.scriptSource = &scriptSource;
	context.projectMemory = &scriptProjectMemory;
	context.focusedFileIndex = selectedScriptFileIndex;
	context.includeRepoContext = scriptIncludeRepoContext;
	context.maxRepoFiles = kMaxScriptContextFiles;
	context.maxFocusedFileChars = kMaxFocusedFileSnippetChars;
	return context;
};

auto buildWorkspaceAllowedFiles = [&]() {
	std::vector<std::string> allowedFiles;
	if (restrictWorkspaceToFocusedFile && hasSelectedFile) {
		allowedFiles.push_back(
			scriptSourceFiles[static_cast<size_t>(selectedScriptFileIndex)].fullPath);
	}
	return allowedFiles;
};

auto appendScriptAssistantOutput = [&](const std::string & userLabel,
	const std::string & assistantText) {
	if (!userLabel.empty()) {
		scriptMessages.push_back({"user", userLabel, ofGetElapsedTimef()});
	}
	scriptOutput = assistantText;
	scriptMessages.push_back({"assistant", assistantText, ofGetElapsedTimef()});
};

auto formatSymbolContext = [](const ofxGgmlCodeAssistantSymbolContext & symbolContext) {
	std::ostringstream out;
	out << "Semantic context for: " << symbolContext.query << "\n\n";
	if (symbolContext.definitions.empty()) {
		out << "Definitions: none found\n";
	} else {
		out << "Definitions:\n";
		for (const auto & symbol : symbolContext.definitions) {
			out << "- " << symbol.name;
			if (!symbol.signature.empty()) {
				out << " :: " << symbol.signature;
			}
			out << "\n  " << symbol.filePath << ":" << symbol.line << "\n";
			if (!symbol.preview.empty()) {
				out << "  " << trim(symbol.preview) << "\n";
			}
		}
	}

	if (!symbolContext.relatedReferences.empty()) {
		out << "\nReferences and callers:\n";
		for (const auto & ref : symbolContext.relatedReferences) {
			out << "- " << ref.kind << " in " << ref.filePath << ":" << ref.line;
			if (!ref.callerSymbol.empty()) {
				out << " via " << ref.callerSymbol;
			}
			out << "\n";
			if (!ref.preview.empty()) {
				out << "  " << trim(ref.preview) << "\n";
			}
		}
	}

	return out.str();
};

auto previewWorkspacePlan = [&](const std::string & label) {
	if (sourceType != ofxGgmlScriptSourceType::LocalFolder ||
		scriptSource.getLocalFolderPath().empty()) {
		appendScriptAssistantOutput(label,
			"Workspace preview requires a loaded local folder source.");
		return;
	}

	auto structured = ofxGgmlCodeAssistant::parseStructuredResult(scriptOutput);
	if (structured.unifiedDiff.empty()) {
		structured.unifiedDiff =
			ofxGgmlCodeAssistant::buildUnifiedDiffFromStructuredResult(structured);
	}

	const std::string workspaceRoot = scriptSource.getLocalFolderPath();
	const auto allowedFiles = buildWorkspaceAllowedFiles();

	ofxGgmlWorkspacePatchValidationResult validation;
	ofxGgmlWorkspaceApplyResult applyResult;
	if (!structured.unifiedDiff.empty()) {
		validation = scriptWorkspaceAssistant.validateUnifiedDiff(
			structured.unifiedDiff,
			workspaceRoot,
			allowedFiles);
		applyResult = scriptWorkspaceAssistant.applyUnifiedDiff(
			structured.unifiedDiff,
			workspaceRoot,
			allowedFiles,
			true);
	} else if (!structured.patchOperations.empty()) {
		validation = scriptWorkspaceAssistant.validatePatchOperations(
			structured.patchOperations,
			workspaceRoot,
			allowedFiles);
		applyResult = scriptWorkspaceAssistant.applyPatchOperations(
			structured.patchOperations,
			workspaceRoot,
			allowedFiles,
			true);
	} else {
		appendScriptAssistantOutput(label,
			"No structured patch plan was found in the latest script output.");
		return;
	}

	std::vector<std::string> changedFiles = applyResult.touchedFiles;
	if (changedFiles.empty()) {
		changedFiles = validation.validatedFiles;
	}
	if (changedFiles.empty()) {
		for (const auto & fileIntent : structured.filesToTouch) {
			if (!fileIntent.filePath.empty()) {
				changedFiles.push_back(fileIntent.filePath);
			}
		}
	}
	const auto workspaceInfoSnapshot = scriptSource.getWorkspaceInfo();
	const auto verificationCommands = structured.verificationCommands.empty()
		? scriptWorkspaceAssistant.suggestVerificationCommands(
			changedFiles,
			workspaceRoot,
			&workspaceInfoSnapshot)
		: structured.verificationCommands;

	std::ostringstream out;
	out << label << "\n\n";
	out << "Workspace root: " << workspaceRoot << "\n";
	out << "Validation: " << (validation.success ? "passed" : "failed") << "\n";
	if (!allowedFiles.empty()) {
		out << "Allow-list:\n";
		for (const auto & file : allowedFiles) {
			out << "- " << file << "\n";
		}
	}
	if (!validation.messages.empty()) {
		out << "\nValidation notes:\n";
		for (const auto & message : validation.messages) {
			out << "- " << message << "\n";
		}
	}
	if (!applyResult.messages.empty()) {
		out << "\nDry-run result:\n";
		for (const auto & message : applyResult.messages) {
			out << "- " << message << "\n";
		}
	}
	if (!applyResult.unifiedDiffPreview.empty()) {
		out << "\nUnified diff preview:\n" << applyResult.unifiedDiffPreview << "\n";
	}
	if (!verificationCommands.empty()) {
		out << "\nSuggested verification commands:\n";
		for (const auto & command : verificationCommands) {
			out << "- " << command.label << ": " << command.executable;
			for (const auto & arg : command.arguments) {
				out << " " << arg;
			}
			if (!command.workingDirectory.empty()) {
				out << "  (cwd: " << command.workingDirectory << ")";
			}
			out << "\n";
		}
	}

	appendScriptAssistantOutput(label, out.str());
};

auto submitScriptRequest = [&](ofxGgmlCodeAssistantAction action,
	const std::string & userInput = std::string(),
	const std::string & bodyOverride = std::string(),
	const std::string & labelOverride = std::string(),
	bool clearInputAfter = false,
	bool requestStructuredResult = false,
	bool requestUnifiedDiff = false,
	const std::string & buildErrors = std::string(),
	const std::vector<std::string> & allowedFiles = {}) {
	ofxGgmlCodeAssistantRequest request;
	request.action = action;
	request.userInput = userInput;
	request.lastTask = lastScriptRequest;
	request.lastOutput = scriptOutput;
	request.bodyOverride = bodyOverride;
	request.labelOverride = labelOverride;
	request.requestStructuredResult = requestStructuredResult;
	request.requestUnifiedDiff = requestUnifiedDiff;
	request.buildErrors = buildErrors;
	request.allowedFiles = allowedFiles;
	if (!scriptLanguages.empty() &&
		selectedLanguageIndex >= 0 &&
		selectedLanguageIndex < static_cast<int>(scriptLanguages.size())) {
		request.language = scriptLanguages[static_cast<size_t>(selectedLanguageIndex)];
	}

	const auto prepared = scriptAssistant.preparePrompt(
		request,
		buildScriptAssistantContext());
	const std::string taskText = prepared.body.empty()
		? userInput
		: prepared.body;
	const std::string requestLabel = prepared.requestLabel.empty()
		? taskText
		: prepared.requestLabel;

	scriptMessages.push_back({"user", requestLabel, ofGetElapsedTimef()});
	runInference(AiMode::Script, taskText, "", prepared.prompt);
	if (clearInputAfter) {
		std::memset(scriptInput, 0, sizeof(scriptInput));
	}
};

auto summarizeLocalChanges = [&](const std::string & focusText) {
	if (sourceType != ofxGgmlScriptSourceType::LocalFolder ||
		scriptSource.getLocalFolderPath().empty()) {
		appendScriptAssistantOutput(
			"Summarize local changes",
			"Local change summaries require a loaded local folder workspace.");
		return;
	}

	const auto snapshot = captureWorkspaceDiffSnapshot(scriptSource.getLocalFolderPath());
	if (!snapshot.success) {
		appendScriptAssistantOutput("Summarize local changes", snapshot.error);
		return;
	}
	if (!snapshot.hasChanges) {
		appendScriptAssistantOutput(
			"Summarize local changes",
			"No local Git changes were detected in the current workspace.");
		return;
	}

	std::ostringstream body;
	body << "Summarize the following local Git changes professionally for reviewers. "
		<< "Focus on user-visible impact, important files, notable risks, and verification notes.\n";
	if (!trim(focusText).empty()) {
		body << "Review focus: " << trim(focusText) << "\n";
	}
	body << "\nRepository root: " << snapshot.repoRoot << "\n";
	if (!snapshot.statusText.empty()) {
		body << "\nGit status:\n"
			<< truncatePromptPayload(snapshot.statusText, 2000) << "\n";
	}
	if (!snapshot.diffText.empty()) {
		body << "\nUnified diff:\n"
			<< truncatePromptPayload(snapshot.diffText, 12000) << "\n";
	}

	submitScriptRequest(
		ofxGgmlCodeAssistantAction::SummarizeChanges,
		focusText,
		body.str(),
		"Summarize local changes.",
		false);
};

auto executeScriptSlashCommand = [&](const ScriptSlashCommand & command) {
	switch (command.kind) {
	case ScriptSlashCommandKind::Help:
		appendScriptAssistantOutput("Slash command help", buildScriptCommandHelpText());
		return true;
	case ScriptSlashCommandKind::ReviewAll: {
		if (sourceType != ofxGgmlScriptSourceType::None && !scriptSourceFiles.empty()) {
			const std::string reviewQuery = trim(command.argument).empty()
				? ofxGgmlCodeReview::defaultReviewQuery()
				: trim(command.argument);
			scriptMessages.push_back({
				"user",
				"Review all files in loaded " +
					std::string(sourceType == ofxGgmlScriptSourceType::LocalFolder ? "folder" : "repo") +
					". Focus: " + reviewQuery,
				ofGetElapsedTimef()});
			runHierarchicalReview(reviewQuery);
			return true;
		}
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::Review,
			command.argument,
			"",
			"",
			false);
		return true;
	}
	case ScriptSlashCommandKind::ReviewFix: {
		std::ostringstream body;
		body << "Review the current workspace and return an actionable fix plan. "
			<< "Only include concrete, evidence-backed issues. "
			<< "Prefer a structured plan with a unified diff when a clear next fix is visible.";
		if (!trim(command.argument).empty()) {
			body << "\n\nFocus: " << trim(command.argument);
		}
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::Review,
			command.argument,
			body.str(),
			"Review with fix plan.",
			false,
			true,
			true,
			"",
			buildWorkspaceAllowedFiles());
		return true;
	}
	case ScriptSlashCommandKind::NextEdit:
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::NextEdit,
			command.argument,
			"",
			"",
			false,
			true,
			true,
			"",
			buildWorkspaceAllowedFiles());
		return true;
	case ScriptSlashCommandKind::SummarizeChanges:
		summarizeLocalChanges(command.argument);
		return true;
	case ScriptSlashCommandKind::Tests: {
		std::ostringstream body;
		body << "Propose the highest-value tests for this workspace and request. "
			<< "Prefer concrete test names, likely files, and verification commands. "
			<< "If no additional tests are needed, say so clearly.";
		if (!trim(command.argument).empty()) {
			body << "\n\nFocus: " << trim(command.argument);
		}
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::Generate,
			command.argument,
			body.str(),
			"Plan tests.",
			false,
			true,
			false,
			"",
			buildWorkspaceAllowedFiles());
		return true;
	}
	case ScriptSlashCommandKind::FixPlan:
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::Edit,
			command.argument,
			"",
			"",
			false,
			true,
			true,
			"",
			buildWorkspaceAllowedFiles());
		return true;
	case ScriptSlashCommandKind::Explain:
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::Explain,
			command.argument,
			"",
			"",
			false);
		return true;
	case ScriptSlashCommandKind::Docs:
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::GroundedDocs,
			command.argument,
			"",
			"",
			false);
		return true;
	case ScriptSlashCommandKind::None:
	default:
		return false;
	}
};

struct ScriptActionSpec {
	const char * label;
	ImVec2 size;
	ofxGgmlCodeAssistantAction action;
};

const ScriptActionSpec actionSpecs[] = {
	{ "Generate", ImVec2(100, 0), ofxGgmlCodeAssistantAction::Generate },
	{ "Explain", ImVec2(100, 0), ofxGgmlCodeAssistantAction::Explain },
	{ "Debug", ImVec2(100, 0), ofxGgmlCodeAssistantAction::Debug },
	{ "Optimize", ImVec2(100, 0), ofxGgmlCodeAssistantAction::Optimize },
	{ "Refactor", ImVec2(100, 0), ofxGgmlCodeAssistantAction::Refactor },
	{ "Next Edit", ImVec2(100, 0), ofxGgmlCodeAssistantAction::NextEdit },
	{ "Review Mode", ImVec2(100, 0), ofxGgmlCodeAssistantAction::Review },
};

bool canSendScriptChat = !generating.load() && hasUserInput;
const ScriptSlashCommand slashCommand = hasUserInput
	? parseScriptSlashCommand(scriptInput)
	: ScriptSlashCommand{};

ImGui::BeginDisabled(generating.load());
if (canSendScriptChat) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.3f, 1.0f));
if ((ImGui::Button("Send to Chat", ImVec2(110, 0)) || scriptChatSubmitted) && canSendScriptChat) {
	if (slashCommand.kind != ScriptSlashCommandKind::None) {
		if (executeScriptSlashCommand(slashCommand)) {
			std::memset(scriptInput, 0, sizeof(scriptInput));
		}
	} else {
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::Ask,
			scriptInput,
			"",
			"",
			true);
	}
}
if (canSendScriptChat) ImGui::PopStyleColor();
ImGui::SameLine();
if (ImGui::Button("Clear Chat", ImVec2(90, 0))) {
	scriptMessages.clear();
	scriptOutput.clear();
	lastScriptOutputLikelyCutoff = false;
	lastScriptOutputTail.clear();
}
ImGui::SameLine();

ImGui::BeginDisabled(generating.load() || !lastScriptOutputLikelyCutoff || lastScriptOutputTail.empty());
if (ImGui::Button("Continue cutoff", ImVec2(120, 0))) {
	submitScriptRequest(
		ofxGgmlCodeAssistantAction::ContinueCutoff,
		"",
		ofxGgmlInference::buildCutoffContinuationRequest(lastScriptOutputTail),
		"Continue from cutoff.");
}
ImGui::EndDisabled();
ImGui::SameLine();

// Review all files button (enabled when folder/repo is loaded)
if (sourceType != ofxGgmlScriptSourceType::None && !scriptSourceFiles.empty()) {
	if (ImGui::Button("Review All Files", ImVec2(120, 0))) {
		scriptMessages.push_back({"user", "Review all files in loaded " + std::string(sourceType == ofxGgmlScriptSourceType::LocalFolder ? "folder" : "repo") + ".", ofGetElapsedTimef()});
		runHierarchicalReview();
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Run embedding-powered, multi-pass review over the loaded folder/repository.\nRecommended: use the Script-mode recommended model plus Review Preset.");
	}
	ImGui::SameLine();
	if (ImGui::Button("Review Fix Plan", ImVec2(120, 0))) {
		std::ostringstream body;
		body << "Review the current workspace and return an actionable fix plan. "
			<< "Only include concrete, evidence-backed issues. Prefer a structured plan and unified diff.";
		if (hasUserInput) {
			body << "\n\nFocus: " << trim(scriptInput);
		}
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::Review,
			scriptInput,
			body.str(),
			"Review with fix plan.",
			true,
			true,
			true,
			"",
			buildWorkspaceAllowedFiles());
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Review the loaded workspace and return an actionable fix plan with structured edits.");
	}
	ImGui::SameLine();
	if (ImGui::Button("Review Preset", ImVec2(110, 0))) {
		if (!modelPresets.empty()) {
			selectedModelIndex = std::clamp(
				taskDefaultModelIndices[static_cast<int>(AiMode::Script)],
				0, static_cast<int>(modelPresets.size()) - 1);
			detectModelLayers();
			if (detectedModelLayers > 0) {
				gpuLayers = detectedModelLayers;
			}
		}
		maxTokens = std::clamp(maxTokens, 512, 768);
		contextSize = std::clamp(contextSize, 4096, 6144);
		batchSize = 256;
		temperature = 0.2f;
		topP = 0.9f;
		topK = std::max(topK, 50);
		minP = std::max(minP, 0.05f);
		repeatPenalty = 1.03f;
		autoContinueCutoff = true;
		usePromptCache = true;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Switch to the recommended Script model and review-tuned generation settings.");
	}
}

const bool hasScriptOutput = !scriptOutput.empty();
const bool hasLastTask = !lastScriptRequest.empty();

// Save output to source.
if (hasScriptOutput && sourceType == ofxGgmlScriptSourceType::LocalFolder) {
	ImGui::SameLine();
	if (ImGui::Button("Save Output", ImVec2(120, 0))) {
		std::string filename = buildScriptFilename();
		scriptSource.saveToLocalSource(filename, scriptOutput);
	}
}

ImGui::EndDisabled();

ImGui::Spacing();
ImGui::TextDisabled("Quick Actions:");
ImGui::TextDisabled("%s", "Tip: /review /reviewfix /nextedit /summary /tests /fix /docs");
if (ImGui::IsItemHovered()) {
	ImGui::SetTooltip("%s", buildScriptCommandHelpText().c_str());
}
ImGui::BeginDisabled(generating.load() || (!hasUserInput && !hasSelectedFile));
for (size_t i = 0; i < std::size(actionSpecs); i++) {
	const auto & spec = actionSpecs[i];
	if (ImGui::Button(spec.label, spec.size)) {
		if (spec.action == ofxGgmlCodeAssistantAction::NextEdit) {
			submitScriptRequest(
				spec.action,
				scriptInput,
				"",
				"",
				true,
				true,
				true,
				"",
				buildWorkspaceAllowedFiles());
		} else {
			submitScriptRequest(
				spec.action,
				scriptInput,
				"",
				"",
				true);
		}
	}
	if (i + 1 < std::size(actionSpecs)) {
		ImGui::SameLine();
	}
}
ImGui::EndDisabled();
ImGui::BeginDisabled(
	generating.load() ||
	sourceType != ofxGgmlScriptSourceType::LocalFolder ||
	scriptSource.getLocalFolderPath().empty());
if (ImGui::Button("Change Summary", ImVec2(120, 0))) {
	summarizeLocalChanges(scriptInput);
	std::memset(scriptInput, 0, sizeof(scriptInput));
}
if (ImGui::IsItemHovered()) {
	ImGui::SetTooltip("Summarize local Git changes professionally for reviewers.");
}
ImGui::EndDisabled();

if (ImGui::CollapsingHeader("Semantic & Workspace")) {
	ImGui::Checkbox("Restrict workspace previews to focused file", &restrictWorkspaceToFocusedFile);
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("When enabled, structured edit previews stay inside the currently selected file.");
	}

	ImGui::BeginDisabled(generating.load() || (!hasUserInput && !hasSelectedFile));
	if (ImGui::Button("Symbol Context", ImVec2(120, 0))) {
		ofxGgmlCodeAssistantSymbolQuery query;
		query.query = hasUserInput ? std::string(scriptInput) : lastScriptRequest;
		query.includeCallers = true;
		query.maxDefinitions = 6;
		query.maxReferences = 8;
		const auto symbolContext =
			scriptAssistant.buildSymbolContext(query, buildScriptAssistantContext());
		appendScriptAssistantOutput("Inspect symbol context", formatSymbolContext(symbolContext));
	}
	ImGui::SameLine();
	if (ImGui::Button("Edit Plan", ImVec2(100, 0))) {
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::Edit,
			scriptInput,
			"",
			"",
			true,
			true,
			true,
			"",
			buildWorkspaceAllowedFiles());
	}
	ImGui::SameLine();
	if (ImGui::Button("Grounded Docs", ImVec2(120, 0))) {
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::GroundedDocs,
			scriptInput,
			"",
			"",
			true);
	}
	ImGui::EndDisabled();

	ImGui::InputTextMultiline(
		"Build errors / compiler output",
		scriptBuildErrors,
		sizeof(scriptBuildErrors),
		ImVec2(-1, 90));
	ImGui::BeginDisabled(generating.load() || std::strlen(scriptBuildErrors) == 0);
	if (ImGui::Button("Fix Build Plan", ImVec2(120, 0))) {
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::FixBuild,
			scriptInput,
			"",
			"",
			false,
			true,
			true,
			scriptBuildErrors,
			buildWorkspaceAllowedFiles());
	}
	ImGui::SameLine();
	if (ImGui::Button("Workspace Dry Run", ImVec2(140, 0))) {
		previewWorkspacePlan("Workspace dry run");
	}
	ImGui::EndDisabled();
}

if (ImGui::CollapsingHeader("Tools & Memory")) {
	bool useProjectMemory = scriptProjectMemory.isEnabled();
	if (ImGui::Checkbox("Use project memory", &useProjectMemory)) {
		scriptProjectMemory.setEnabled(useProjectMemory);
	}
	ImGui::SameLine();
	ImGui::Checkbox("Include repo context", &scriptIncludeRepoContext);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Include loaded file list and selected file snippet in script prompts");

	ImGui::BeginDisabled(generating.load() || (!hasScriptOutput && !hasLastTask));
	if (ImGui::Button("Continue Task", ImVec2(120, 0))) {
		submitScriptRequest(ofxGgmlCodeAssistantAction::ContinueTask);
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Continue from the latest coding response without rewriting your full prompt.");
	ImGui::SameLine();
	if (ImGui::Button("Shorter", ImVec2(80, 0))) {
		submitScriptRequest(ofxGgmlCodeAssistantAction::Shorter);
	}
	ImGui::SameLine();
	if (ImGui::Button("More Detail", ImVec2(90, 0))) {
		submitScriptRequest(ofxGgmlCodeAssistantAction::MoreDetail);
	}
	ImGui::SameLine();
	if (ImGui::Button("Long Code Preset", ImVec2(120, 0))) {
		maxTokens = std::max(maxTokens, 2048);
		contextSize = std::max(contextSize, 8192);
		temperature = 0.35f;
		topP = 0.92f;
		topK = std::max(topK, 60);
		minP = std::max(minP, 0.05f);
		repeatPenalty = 1.05f;
	}

	if (ImGui::Button("Reuse Last Task", ImVec2(120, 0))) {
		if (hasLastTask) {
			copyStringToBuffer(scriptInput, sizeof(scriptInput), lastScriptRequest);
		}
	}
	ImGui::EndDisabled();

	if (ImGui::SmallButton("Clear Project Memory")) scriptProjectMemory.clear();
	ImGui::SameLine();
	ImGui::TextDisabled("Learned context (%d chars)", static_cast<int>(scriptProjectMemory.getMemoryText().size()));
	ImGui::BeginChild("##ProjectMemory", ImVec2(-1, 80), true);
	ImGui::TextWrapped("%s", scriptProjectMemory.getMemoryText().c_str());
	ImGui::EndChild();
}
}

// ---------------------------------------------------------------------------
// Script source panel — GitHub repo connection UI
// ---------------------------------------------------------------------------

void ofApp::drawScriptSourcePanel() {
ImGui::Spacing();
const ofxGgmlScriptSourceType sourceType = scriptSource.getSourceType();
const auto workspaceInfo = scriptSource.getWorkspaceInfo();

if (sourceType == ofxGgmlScriptSourceType::LocalFolder) {
	ImGui::TextColored(ImVec4(0.6f, 0.85f, 0.6f, 1.0f), "Local Workspace:");
	ImGui::SameLine();
	if (ImGui::Button("Rescan", ImVec2(70, 0))) {
		scriptSource.rescan();
	}
	ImGui::SameLine();
	if (ImGui::Button("Load VS Workspace", ImVec2(140, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select Visual Studio .sln or .vcxproj", false);
		if (result.bSuccess) {
			selectedScriptFileIndex = -1;
			scriptSource.setVisualStudioWorkspace(result.getPath());
		}
	}

	const std::string localRoot = scriptSource.getLocalFolderPath();
	if (!localRoot.empty()) {
		ImGui::TextWrapped("Root: %s", localRoot.c_str());
	}
	if (workspaceInfo.hasVisualStudioSolution) {
		ImGui::TextWrapped("Solution: %s", workspaceInfo.visualStudioSolutionPath.c_str());
	}
	if (!workspaceInfo.visualStudioProjectPaths.empty()) {
		ImGui::TextWrapped(
			"Visual Studio projects: %d",
			static_cast<int>(workspaceInfo.visualStudioProjectPaths.size()));
	}
	if (!workspaceInfo.visualStudioProjects.empty()) {
		if (ImGui::BeginCombo(
				"VS Project",
				workspaceInfo.selectedVisualStudioProjectPath.empty()
					? workspaceInfo.visualStudioProjects.front().relativePath.c_str()
					: workspaceInfo.selectedVisualStudioProjectPath.c_str())) {
			for (const auto & project : workspaceInfo.visualStudioProjects) {
				const bool isSelected =
					project.relativePath == workspaceInfo.selectedVisualStudioProjectPath;
				const std::string label = project.name.empty()
					? project.relativePath
					: (project.name + " (" + project.relativePath + ")");
				if (ImGui::Selectable(label.c_str(), isSelected)) {
					scriptSource.configureVisualStudioWorkspace(
						project.relativePath,
						workspaceInfo.selectedVisualStudioConfiguration,
						workspaceInfo.selectedVisualStudioPlatform);
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}
	if (!workspaceInfo.visualStudioConfigurations.empty()) {
		if (ImGui::BeginCombo(
				"Configuration",
				workspaceInfo.selectedVisualStudioConfiguration.c_str())) {
			for (const auto & configuration : workspaceInfo.visualStudioConfigurations) {
				const bool isSelected =
					configuration == workspaceInfo.selectedVisualStudioConfiguration;
				if (ImGui::Selectable(configuration.c_str(), isSelected)) {
					scriptSource.configureVisualStudioWorkspace(
						workspaceInfo.selectedVisualStudioProjectPath,
						configuration,
						workspaceInfo.selectedVisualStudioPlatform);
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}
	if (!workspaceInfo.visualStudioPlatforms.empty()) {
		if (ImGui::BeginCombo(
				"Platform",
				workspaceInfo.selectedVisualStudioPlatform.c_str())) {
			for (const auto & platform : workspaceInfo.visualStudioPlatforms) {
				const bool isSelected =
					platform == workspaceInfo.selectedVisualStudioPlatform;
				if (ImGui::Selectable(platform.c_str(), isSelected)) {
					scriptSource.configureVisualStudioWorkspace(
						workspaceInfo.selectedVisualStudioProjectPath,
						workspaceInfo.selectedVisualStudioConfiguration,
						platform);
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}
	if (workspaceInfo.hasCompilationDatabase) {
		ImGui::TextWrapped("compile_commands.json: %s", workspaceInfo.compilationDatabasePath.c_str());
	}
	if (workspaceInfo.hasCMakeProject) {
		ImGui::TextWrapped("CMakeLists.txt: %s", workspaceInfo.cmakeListsPath.c_str());
	}
	if (workspaceInfo.hasOpenFrameworksProject) {
		ImGui::TextWrapped("addons.make: %s", workspaceInfo.addonsMakePath.c_str());
	}
	if (!workspaceInfo.defaultBuildDirectory.empty()) {
		ImGui::TextWrapped("Preferred build dir: %s", workspaceInfo.defaultBuildDirectory.c_str());
	}
	if (!workspaceInfo.msbuildPath.empty()) {
		ImGui::TextWrapped("MSBuild: %s", workspaceInfo.msbuildPath.c_str());
	}
	ImGui::TextDisabled(
		"Auto-rescan: %s",
		workspaceInfo.localBackgroundMonitoringEnabled ? "active" : "off");

	if (cachedScriptVerificationRoot != localRoot ||
		cachedScriptVerificationGeneration != workspaceInfo.workspaceGeneration) {
		cachedScriptVerificationCommands =
			scriptWorkspaceAssistant.suggestVerificationCommands(
				{},
				localRoot,
				&workspaceInfo);
		cachedScriptVerificationRoot = localRoot;
		cachedScriptVerificationGeneration = workspaceInfo.workspaceGeneration;
	}
	if (!cachedScriptVerificationCommands.empty()) {
		ImGui::Spacing();
		ImGui::TextDisabled("Suggested verification:");
		for (const auto & command : cachedScriptVerificationCommands) {
			std::string line = command.executable;
			for (const auto & arg : command.arguments) {
				line += " " + arg;
			}
			ImGui::BulletText("%s", line.c_str());
		}
	}
} else if (sourceType == ofxGgmlScriptSourceType::GitHubRepo) {
	ImGui::TextColored(ImVec4(0.6f, 0.7f, 1.0f, 1.0f), "GitHub Repository:");
	ImGui::SetNextItemWidth(250);
	ImGui::InputText("##GHRepo", scriptSourceGitHub, sizeof(scriptSourceGitHub));
	ImGui::SameLine();
	ImGui::Text("Branch:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(100);
	ImGui::InputText("##GHBranch", scriptSourceBranch, sizeof(scriptSourceBranch));
	ImGui::SetNextItemWidth(240);
	ImGui::InputText(
		"Token (optional)##GHToken",
		scriptSourceGitHubToken,
		sizeof(scriptSourceGitHubToken),
		ImGuiInputTextFlags_Password);
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		scriptSource.setGitHubAuthToken(scriptSourceGitHubToken);
	}
	ImGui::SameLine();
	if (ImGui::Button("Fetch", ImVec2(60, 0))) {
		if (std::strlen(scriptSourceGitHub) > 0) {
			selectedScriptFileIndex = -1;
			if (!scriptLanguages.empty()) {
				scriptSource.setPreferredExtension(
					scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExtension);
			}
			scriptSource.setGitHubAuthToken(scriptSourceGitHubToken);
			if (scriptSource.setGitHubRepoFromInput(
				scriptSourceGitHub,
				scriptSourceBranch)) {
				scriptSource.fetchGitHubRepo();
			}
		}
	}
	ImGui::TextDisabled("Accepts owner/repo, GitHub repo URLs, /tree/<branch> URLs, and file URLs. Leave branch empty for auto-detect.");
	if (!workspaceInfo.gitHubDefaultBranch.empty()) {
		ImGui::TextWrapped("Default branch: %s", workspaceInfo.gitHubDefaultBranch.c_str());
	}
	if (!workspaceInfo.gitHubResolvedCommitSha.empty()) {
		ImGui::TextWrapped("Pinned commit: %s", workspaceInfo.gitHubResolvedCommitSha.c_str());
	}
	if (!workspaceInfo.gitHubFocusedPath.empty()) {
		ImGui::TextWrapped("Focused path: %s", workspaceInfo.gitHubFocusedPath.c_str());
	}
	if (!workspaceInfo.gitHubDiagnostic.empty()) {
		ImGui::TextColored(
			ImVec4(0.95f, 0.7f, 0.35f, 1.0f),
			"%s",
			workspaceInfo.gitHubDiagnostic.c_str());
	}
} else if (sourceType == ofxGgmlScriptSourceType::Internet) {
	ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.9f, 1.0f), "Internet Sources (URLs):");
	ImGui::SetNextItemWidth(360);
	ImGui::InputText("##InternetUrl", scriptSourceInternetUrl, sizeof(scriptSourceInternetUrl));
	ImGui::SameLine();
	if (ImGui::Button("Add URL", ImVec2(80, 0))) {
		if (std::strlen(scriptSourceInternetUrl) > 0) {
			scriptSource.addInternetUrl(scriptSourceInternetUrl);
			scriptSource.rescan();
			scriptSourceInternetUrl[0] = '\0';
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear URLs", ImVec2(90, 0))) {
		scriptSource.setInternetUrls({});
		selectedScriptFileIndex = -1;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Remove all added URLs.");
	}
	if (selectedScriptFileIndex >= 0) {
		ImGui::SameLine();
		if (ImGui::Button("Remove Selected", ImVec2(130, 0))) {
			if (scriptSource.removeInternetUrl(static_cast<size_t>(selectedScriptFileIndex))) {
				selectedScriptFileIndex = -1;
				scriptSource.rescan();
			}
		}
	}
}

const std::string status = scriptSource.getStatus();
if (!status.empty()) {
	ImGui::SameLine();
	ImGui::TextDisabled("%s", status.c_str());
}
ImGui::Spacing();
}

std::string ofApp::buildScriptFilename() const {
std::string ext = ".txt";
if (!scriptLanguages.empty()) {
ext = scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExtension;
}
auto now = std::chrono::system_clock::now();
auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
now.time_since_epoch()).count();
return "generated_" + ofToString(ms) + ext;
}

// ---------------------------------------------------------------------------
// Summarize panel
// ---------------------------------------------------------------------------

void ofApp::drawSummarizePanel() {
	drawPanelHeader("Summarize", "condense text into key points");
	if (textInferenceBackend == TextInferenceBackend::LlamaServer && !textServerCapabilityHint.empty()) {
		ImGui::TextDisabled("%s", textServerCapabilityHint.c_str());
	}

	ImGui::Text("Paste text to summarize:");
	ImGui::InputTextMultiline("##SumIn", summarizeInput, sizeof(summarizeInput),
		ImVec2(-1, 150));

	auto submitTextTask = [&](ofxGgmlTextTask task) {
		ofxGgmlTextAssistantRequest request;
		request.task = task;
		request.inputText = summarizeInput;
		const auto prepared = textAssistant.preparePrompt(request);
		runInference(AiMode::Summarize, request.inputText, "", prepared.prompt);
	};
	auto submitSummaryPrompt = [&](const std::string & label,
		const std::string & instruction,
		const std::string & outputHeading,
		bool useLoadedSources = false) {
		const std::string inputText = std::strlen(summarizeInput) > 0
			? std::string(summarizeInput)
			: std::string("Use the provided loaded sources.");
		const auto realtimeSettings = useLoadedSources
			? buildLiveContextSettings(sourceUrlsInput, "Loaded sources for summarization")
			: ofxGgmlRealtimeInfoSettings{};
		runInference(
			AiMode::Summarize,
			inputText,
			"",
			buildStructuredTextPrompt(
				"You are a precise analyst who writes crisp, professional summaries.",
				instruction,
				"Material",
				inputText,
				outputHeading),
			realtimeSettings);
	};

	const bool hasSummarizeInput = std::strlen(summarizeInput) > 0;
	const bool hasSummarizeUrls = !trim(sourceUrlsInput).empty();
	ImGui::BeginDisabled(generating.load() || (!hasSummarizeInput && !hasSummarizeUrls));
if (ImGui::Button("Summarize", ImVec2(140, 0))) {
submitTextTask(ofxGgmlTextTask::Summarize);
}
ImGui::SameLine();
if (ImGui::Button("Key Points", ImVec2(140, 0))) {
submitTextTask(ofxGgmlTextTask::KeyPoints);
}
ImGui::SameLine();
if (ImGui::Button("TL;DR", ImVec2(140, 0))) {
submitTextTask(ofxGgmlTextTask::TlDr);
}
ImGui::SameLine();
	if (ImGui::Button("Summarize URLs", ImVec2(150, 0))) {
	ofxGgmlTextAssistantRequest request;
	request.task = ofxGgmlTextTask::Summarize;
	request.inputText = std::strlen(summarizeInput) > 0
		? std::string(summarizeInput)
		: "Summarize the reference sources.";
	const auto prepared = textAssistant.preparePrompt(request);
	const auto realtimeSettings = buildLiveContextSettings(
		sourceUrlsInput,
		"Loaded sources for summarization");
	runInference(
		AiMode::Summarize,
		request.inputText,
		"",
		prepared.prompt,
		realtimeSettings);
	}
	ImGui::EndDisabled();
	ImGui::BeginDisabled(generating.load() || (!hasSummarizeInput && !hasSummarizeUrls));
	if (ImGui::Button("Executive Brief", ImVec2(140, 0))) {
		submitSummaryPrompt(
			"Executive brief",
			"Write an executive brief with headline, what matters, risks, and recommended next step.",
			"Executive brief");
	}
	ImGui::SameLine();
	if (ImGui::Button("Action Items", ImVec2(140, 0))) {
		submitSummaryPrompt(
			"Action items",
			"Extract concrete action items with owners or placeholders, dependencies, and urgency.",
			"Action items");
	}
	ImGui::SameLine();
	if (ImGui::Button("Meeting Notes", ImVec2(140, 0))) {
		submitSummaryPrompt(
			"Meeting notes",
			"Turn the material into professional meeting notes with decisions, blockers, and follow-ups.",
			"Meeting notes");
	}
	ImGui::SameLine();
	if (ImGui::Button("Source Brief", ImVec2(140, 0))) {
		submitSummaryPrompt(
			"Source brief",
			"Summarize the loaded material into a source-backed brief with key claims and supporting context.",
			"Source brief",
			hasSummarizeUrls);
	}
	ImGui::EndDisabled();

	ImGui::Separator();
	ImGui::Text("Summary:");
if (!summarizeOutput.empty()) {
ImGui::SameLine();
if (ImGui::SmallButton("Copy##SumCopy")) copyToClipboard(summarizeOutput);
ImGui::SameLine();
if (ImGui::SmallButton("Clear##SumClear")) summarizeOutput.clear();
ImGui::SameLine();
ImGui::TextDisabled("(%d chars)", static_cast<int>(summarizeOutput.size()));
}
if (generating.load() && activeGenerationMode == AiMode::Summarize) {
ImGui::BeginChild("##SumOut", ImVec2(0, 0), true);
std::string partial;
{
std::lock_guard<std::mutex> lock(streamMutex);
partial = streamingOutput;
}
if (partial.empty()) {
int dots = static_cast<int>(ImGui::GetTime() * kDotsAnimationSpeed) % 4;
ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", kWaitingLabels[dots]);
} else {
ImGui::TextWrapped("%s", partial.c_str());
}
ImGui::EndChild();
} else {
ImGui::BeginChild("##SumOut", ImVec2(0, 0), true);
ImGui::TextWrapped("%s", summarizeOutput.c_str());
ImGui::EndChild();
}
}

// ---------------------------------------------------------------------------
// Write panel
// ---------------------------------------------------------------------------

void ofApp::drawWritePanel() {
	drawPanelHeader("Writing Assistant", "rewrite, expand, polish text");
	if (textInferenceBackend == TextInferenceBackend::LlamaServer && !textServerCapabilityHint.empty()) {
		ImGui::TextDisabled("%s", textServerCapabilityHint.c_str());
	}

	ImGui::Text("Enter your text:");
	ImGui::InputTextMultiline("##WriteIn", writeInput, sizeof(writeInput),
		ImVec2(-1, 120));

	auto submitTextTask = [&](ofxGgmlTextTask task) {
		ofxGgmlTextAssistantRequest request;
		request.task = task;
		request.inputText = writeInput;
		const auto prepared = textAssistant.preparePrompt(request);
		runInference(AiMode::Write, request.inputText, "", prepared.prompt);
	};
	auto submitWritePrompt = [&](const std::string & userText,
		const std::string & instruction,
		const std::string & outputHeading) {
		runInference(
			AiMode::Write,
			userText,
			"",
			buildStructuredTextPrompt(
				"You are a strong professional editor.",
				instruction,
				"Text",
				userText,
				outputHeading));
	};

	ImGui::BeginDisabled(generating.load() || std::strlen(writeInput) == 0);
if (ImGui::Button("Rewrite", ImVec2(110, 0))) {
submitTextTask(ofxGgmlTextTask::Rewrite);
}
ImGui::SameLine();
if (ImGui::Button("Expand", ImVec2(110, 0))) {
submitTextTask(ofxGgmlTextTask::Expand);
}
ImGui::SameLine();
if (ImGui::Button("Make Formal", ImVec2(110, 0))) {
submitTextTask(ofxGgmlTextTask::MakeFormal);
}
ImGui::SameLine();
if (ImGui::Button("Make Casual", ImVec2(110, 0))) {
submitTextTask(ofxGgmlTextTask::MakeCasual);
}
ImGui::SameLine();
if (ImGui::Button("Fix Grammar", ImVec2(110, 0))) {
submitTextTask(ofxGgmlTextTask::FixGrammar);
}
ImGui::SameLine();
	if (ImGui::Button("Polish", ImVec2(110, 0))) {
		submitTextTask(ofxGgmlTextTask::Polish);
	}
	ImGui::EndDisabled();
	ImGui::BeginDisabled(generating.load() || std::strlen(writeInput) == 0);
	if (ImGui::Button("Shorten", ImVec2(110, 0))) {
		submitWritePrompt(
			writeInput,
			"Shorten this text while preserving meaning, key details, and professional tone.",
			"Shortened version");
	}
	ImGui::SameLine();
	if (ImGui::Button("Email Reply", ImVec2(110, 0))) {
		submitWritePrompt(
			writeInput,
			"Draft a professional email reply using the provided material. Keep it clear, tactful, and ready to send.",
			"Email reply");
	}
	ImGui::SameLine();
	if (ImGui::Button("Release Notes", ImVec2(110, 0))) {
		submitWritePrompt(
			writeInput,
			"Turn this material into concise release notes with highlights, fixes, and any user-facing caveats.",
			"Release notes");
	}
	ImGui::SameLine();
	if (ImGui::Button("Commit Message", ImVec2(110, 0))) {
		submitWritePrompt(
			writeInput,
			"Write a professional commit message with a short subject and a useful body when justified.",
			"Commit message");
	}
	ImGui::EndDisabled();

  ImGui::Separator();
  ImGui::Text("Output:");
if (!writeOutput.empty()) {
ImGui::SameLine();
if (ImGui::SmallButton("Copy##WriteCopy")) copyToClipboard(writeOutput);
ImGui::SameLine();
if (ImGui::SmallButton("Clear##WriteClear")) writeOutput.clear();
ImGui::SameLine();
ImGui::TextDisabled("(%d chars)", static_cast<int>(writeOutput.size()));
}
if (generating.load() && activeGenerationMode == AiMode::Write) {
ImGui::BeginChild("##WriteOut", ImVec2(0, 0), true);
std::string partial;
{
std::lock_guard<std::mutex> lock(streamMutex);
partial = streamingOutput;
}
if (partial.empty()) {
int dots = static_cast<int>(ImGui::GetTime() * kDotsAnimationSpeed) % 4;
ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", kWaitingLabels[dots]);
} else {
ImGui::TextWrapped("%s", partial.c_str());
}
ImGui::EndChild();
} else {
ImGui::BeginChild("##WriteOut", ImVec2(0, 0), true);
ImGui::TextWrapped("%s", writeOutput.c_str());
ImGui::EndChild();
}
}

void ofApp::drawTranslatePanel() {
	drawPanelHeader("Translate", "translate text between languages");
	if (textInferenceBackend == TextInferenceBackend::LlamaServer && !textServerCapabilityHint.empty()) {
		ImGui::TextDisabled("%s", textServerCapabilityHint.c_str());
	}

if (translateLanguages.empty()) {
	translateLanguages = ofxGgmlTextAssistant::defaultTranslateLanguages();
}
translateSourceLang = std::clamp(
	translateSourceLang,
	0,
	std::max(0, static_cast<int>(translateLanguages.size()) - 1));
translateTargetLang = std::clamp(
	translateTargetLang,
	0,
	std::max(0, static_cast<int>(translateLanguages.size()) - 1));

ImGui::Text("Source language:");
ImGui::SameLine();
ImGui::SetNextItemWidth(160);
if (ImGui::BeginCombo("##SrcLang", translateLanguages[static_cast<size_t>(translateSourceLang)].name.c_str())) {
	for (int i = 0; i < static_cast<int>(translateLanguages.size()); i++) {
		const bool selected = (translateSourceLang == i);
		if (ImGui::Selectable(translateLanguages[static_cast<size_t>(i)].name.c_str(), selected)) {
			translateSourceLang = i;
		}
		if (selected) ImGui::SetItemDefaultFocus();
	}
	ImGui::EndCombo();
}
ImGui::SameLine();
ImGui::Text("  Target language:");
ImGui::SameLine();
ImGui::SetNextItemWidth(160);
if (ImGui::BeginCombo("##TgtLang", translateLanguages[static_cast<size_t>(translateTargetLang)].name.c_str())) {
	for (int i = 0; i < static_cast<int>(translateLanguages.size()); i++) {
		const bool selected = (translateTargetLang == i);
		if (ImGui::Selectable(translateLanguages[static_cast<size_t>(i)].name.c_str(), selected)) {
			translateTargetLang = i;
		}
		if (selected) ImGui::SetItemDefaultFocus();
	}
	ImGui::EndCombo();
}
ImGui::SameLine();
if (ImGui::Button("Swap", ImVec2(60, 0))) {
std::swap(translateSourceLang, translateTargetLang);
}

ImGui::Text("Enter text to translate:");
ImGui::InputTextMultiline("##TransIn", translateInput, sizeof(translateInput),
ImVec2(-1, 120));

	bool hasInput = std::strlen(translateInput) > 0;
	auto submitTranslatePrompt = [&](const std::string & instruction, const std::string & outputHeading) {
		const std::string sourceLanguage =
			translateLanguages[static_cast<size_t>(translateSourceLang)].name;
		const std::string targetLanguage =
			translateLanguages[static_cast<size_t>(translateTargetLang)].name;
		runInference(
			AiMode::Translate,
			translateInput,
			"",
			buildStructuredTextPrompt(
				"You are a careful translator.",
				instruction + " Source language: " + sourceLanguage + ". Target language: " + targetLanguage + ".",
				"Text",
				translateInput,
				outputHeading));
	};
	ImGui::BeginDisabled(generating.load() || !hasInput);
	if (ImGui::Button("Translate", ImVec2(110, 0))) {
	ofxGgmlTextAssistantRequest request;
	request.task = ofxGgmlTextTask::Translate;
	request.inputText = translateInput;
	request.sourceLanguage = translateLanguages[static_cast<size_t>(translateSourceLang)].name;
	request.targetLanguage = translateLanguages[static_cast<size_t>(translateTargetLang)].name;
	const auto prepared = textAssistant.preparePrompt(request);
	runInference(AiMode::Translate, request.inputText, "", prepared.prompt);
}
ImGui::SameLine();
	if (ImGui::Button("Detect Language", ImVec2(140, 0))) {
	ofxGgmlTextAssistantRequest request;
	request.task = ofxGgmlTextTask::DetectLanguage;
	request.inputText = translateInput;
	const auto prepared = textAssistant.preparePrompt(request);
	runInference(AiMode::Translate, request.inputText, "", prepared.prompt);
	}
	ImGui::EndDisabled();
	ImGui::BeginDisabled(generating.load() || !hasInput);
	if (ImGui::Button("Natural", ImVec2(110, 0))) {
		submitTranslatePrompt(
			"Translate naturally and fluently for a native speaker while preserving intent and tone.",
			"Natural translation");
	}
	ImGui::SameLine();
	if (ImGui::Button("Literal", ImVec2(110, 0))) {
		submitTranslatePrompt(
			"Translate as literally as possible while remaining grammatical.",
			"Literal translation");
	}
	ImGui::SameLine();
	if (ImGui::Button("Detect + Translate", ImVec2(140, 0))) {
		submitTranslatePrompt(
			"Detect the source language first, then translate to the target language and mention the detected source language briefly.",
			"Detected translation");
	}
	ImGui::EndDisabled();

	ImGui::Separator();
	ImGui::Text("Translation:");
if (!translateOutput.empty()) {
ImGui::SameLine();
if (ImGui::SmallButton("Copy##TransCopy")) copyToClipboard(translateOutput);
ImGui::SameLine();
if (ImGui::SmallButton("Clear##TransClear")) translateOutput.clear();
ImGui::SameLine();
ImGui::TextDisabled("(%d chars)", static_cast<int>(translateOutput.size()));
}
if (generating.load() && activeGenerationMode == AiMode::Translate) {
ImGui::BeginChild("##TransOut", ImVec2(0, 0), true);
std::string partial;
{
std::lock_guard<std::mutex> lock(streamMutex);
partial = streamingOutput;
}
if (partial.empty()) {
int dots = static_cast<int>(ImGui::GetTime() * kDotsAnimationSpeed) % 4;
ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", kWaitingLabels[dots]);
} else {
ImGui::TextWrapped("%s", partial.c_str());
}
ImGui::EndChild();
} else {
ImGui::BeginChild("##TransOut", ImVec2(0, 0), true);
ImGui::TextWrapped("%s", translateOutput.c_str());
ImGui::EndChild();
}
}

// ---------------------------------------------------------------------------
// Custom panel
// ---------------------------------------------------------------------------

void ofApp::drawCustomPanel() {
	drawPanelHeader("Custom Prompt", "configure system prompt + user input");
	if (textInferenceBackend == TextInferenceBackend::LlamaServer && !textServerCapabilityHint.empty()) {
		ImGui::TextDisabled("%s", textServerCapabilityHint.c_str());
	}

// Prompt template selector.
if (!promptTemplates.empty()) {
ImGui::Text("Prompt Template:");
ImGui::SameLine();
ImGui::SetNextItemWidth(200);
const char * preview = (selectedPromptTemplateIndex >= 0 &&
selectedPromptTemplateIndex < static_cast<int>(promptTemplates.size()))
? promptTemplates[static_cast<size_t>(selectedPromptTemplateIndex)].name.c_str()
: "(select template)";
if (ImGui::BeginCombo("##PromptTpl", preview)) {
for (int i = 0; i < static_cast<int>(promptTemplates.size()); i++) {
bool sel = (selectedPromptTemplateIndex == i);
if (ImGui::Selectable(promptTemplates[static_cast<size_t>(i)].name.c_str(), sel)) {
selectedPromptTemplateIndex = i;
const auto & sp = promptTemplates[static_cast<size_t>(i)].systemPrompt;
copyStringToBuffer(customSystemPrompt, sizeof(customSystemPrompt), sp);
}
if (sel) ImGui::SetItemDefaultFocus();
}
ImGui::EndCombo();
}
}

ImGui::Text("System prompt:");
ImGui::InputTextMultiline("##CustSys", customSystemPrompt, sizeof(customSystemPrompt),
ImVec2(-1, 60));

ImGui::Text("Your input:");
ImGui::InputTextMultiline("##CustIn", customInput, sizeof(customInput),
ImVec2(-1, 100));

	ImGui::BeginDisabled(generating.load() || std::strlen(customInput) == 0);
	if (ImGui::Button("Run", ImVec2(100, 0))) {
	ofxGgmlTextAssistantRequest request;
	request.task = ofxGgmlTextTask::Custom;
	request.inputText = customInput;
	request.systemPrompt = customSystemPrompt;
	const auto prepared = textAssistant.preparePrompt(request);
	runInference(AiMode::Custom, request.inputText, customSystemPrompt, prepared.prompt);
	}
	ImGui::SameLine();
	if (ImGui::Button("Run with Sources", ImVec2(150, 0))) {
	ofxGgmlTextAssistantRequest request;
	request.task = ofxGgmlTextTask::Custom;
	request.inputText = customInput;
	request.systemPrompt = customSystemPrompt;
	const auto prepared = textAssistant.preparePrompt(request);
	const auto realtimeSettings = buildLiveContextSettings(
		sourceUrlsInput,
		"Loaded sources for this custom task");
	runInference(
		AiMode::Custom,
		request.inputText,
		customSystemPrompt,
		prepared.prompt,
		realtimeSettings);
	}
	ImGui::EndDisabled();
	ImGui::BeginDisabled(generating.load() || std::strlen(customInput) == 0);
	if (ImGui::Button("JSON Reply", ImVec2(100, 0))) {
		const std::string customPrompt = buildStructuredTextPrompt(
			std::string(customSystemPrompt) +
				(std::strlen(customSystemPrompt) > 0 ? "\n" : "") +
				"Return valid JSON only.",
			"Answer the user request with valid minified JSON only.",
			"User request",
			customInput,
			"JSON");
		runInference(AiMode::Custom, customInput, customSystemPrompt, customPrompt);
	}
	ImGui::SameLine();
	if (ImGui::Button("Professional Tone", ImVec2(130, 0))) {
		const std::string system = trim(customSystemPrompt).empty()
			? "You are a professional assistant. Be concise, concrete, and businesslike."
			: std::string(customSystemPrompt) +
				"\nPrefer concise, professional, high-signal answers.";
		const auto prompt = buildStructuredTextPrompt(
			system,
			"Answer the user request clearly and professionally.",
			"User request",
			customInput,
			"Answer");
		runInference(AiMode::Custom, customInput, system, prompt);
	}
	ImGui::EndDisabled();

ImGui::Separator();
ImGui::Text("Output:");
if (!customOutput.empty()) {
ImGui::SameLine();
if (ImGui::SmallButton("Copy##CustCopy")) copyToClipboard(customOutput);
ImGui::SameLine();
if (ImGui::SmallButton("Clear##CustClear")) customOutput.clear();
ImGui::SameLine();
ImGui::TextDisabled("(%d chars)", static_cast<int>(customOutput.size()));
}
if (generating.load() && activeGenerationMode == AiMode::Custom) {
ImGui::BeginChild("##CustOut", ImVec2(0, 0), true);
std::string partial;
{
std::lock_guard<std::mutex> lock(streamMutex);
partial = streamingOutput;
}
if (partial.empty()) {
int dots = static_cast<int>(ImGui::GetTime() * kDotsAnimationSpeed) % 4;
ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", kWaitingLabels[dots]);
} else {
ImGui::TextWrapped("%s", partial.c_str());
}
ImGui::EndChild();
} else {
ImGui::BeginChild("##CustOut", ImVec2(0, 0), true);
ImGui::TextWrapped("%s", customOutput.c_str());
ImGui::EndChild();
}
}

void ofApp::drawVisionPanel() {
drawPanelHeader("Vision", "image / video-to-text via llama-server multimodal models");

	const auto applyVisionProfileDefaults =
		[this](const ofxGgmlVisionModelProfile & profile, bool onlyWhenEmpty) {
			if (!profile.serverUrl.empty() &&
				(!onlyWhenEmpty || trim(visionServerUrl).empty())) {
				copyStringToBuffer(
					visionServerUrl,
					sizeof(visionServerUrl),
					profile.serverUrl);
			}
			const std::string suggestedPath =
				suggestedModelPath(profile.modelPath, profile.modelFileHint);
			if (!suggestedPath.empty() &&
				(!onlyWhenEmpty || trim(visionModelPath).empty())) {
				copyStringToBuffer(
					visionModelPath,
					sizeof(visionModelPath),
					suggestedPath);
			}
		};

	bool loadedVisionProfiles = false;
if (visionProfiles.empty()) {
	visionProfiles = ofxGgmlVisionInference::defaultProfiles();
	loadedVisionProfiles = !visionProfiles.empty();
}
selectedVisionProfileIndex = std::clamp(
	selectedVisionProfileIndex,
	0,
	std::max(0, static_cast<int>(visionProfiles.size()) - 1));
if (loadedVisionProfiles && !visionProfiles.empty()) {
	applyVisionProfileDefaults(
		visionProfiles[static_cast<size_t>(selectedVisionProfileIndex)],
		true);
}

ImGui::TextWrapped(
	"Use a llama-server instance that is already running with a multimodal GGUF model. "
	"This panel sends OpenAI-compatible image requests with local files encoded as data URLs, and can also route sampled video frames through the same server.");

if (!visionProfiles.empty()) {
	std::vector<const char *> profileNames;
	profileNames.reserve(visionProfiles.size());
	for (const auto & profile : visionProfiles) {
		profileNames.push_back(profile.name.c_str());
	}
	ImGui::SetNextItemWidth(280);
	if (ImGui::Combo("Vision profile", &selectedVisionProfileIndex, profileNames.data(), static_cast<int>(profileNames.size()))) {
		const auto & profile = visionProfiles[static_cast<size_t>(selectedVisionProfileIndex)];
		applyVisionProfileDefaults(profile, false);
	}
	const auto & profile = visionProfiles[static_cast<size_t>(selectedVisionProfileIndex)];
	const std::string recommendedModelPath =
		suggestedModelPath(profile.modelPath, profile.modelFileHint);
	const std::string recommendedDownloadUrl =
		effectiveSuggestedModelDownloadUrl(
			profile.modelDownloadUrl,
			profile.modelRepoHint,
			profile.modelFileHint);
	ImGui::TextDisabled("Architecture: %s", profile.architecture.c_str());
	if (!profile.modelRepoHint.empty()) {
		ImGui::TextDisabled("Recommended server model: %s", profile.modelRepoHint.c_str());
	}
	if (isEuRestrictedVisionProfile(profile)) {
		ImGui::TextColored(
			ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
			"EU note: this Meta model is gated and currently unavailable for download from the EU on Hugging Face.");
	}
	if (!profile.modelFileHint.empty()) {
		ImGui::TextDisabled("Recommended file: %s", profile.modelFileHint.c_str());
	}
	if (!recommendedModelPath.empty()) {
		ImGui::TextDisabled("Recommended local path: %s", recommendedModelPath.c_str());
		ImGui::TextDisabled(
			pathExists(recommendedModelPath)
				? "Recommended model is already present."
				: "Recommended model is not downloaded yet.");
		ImGui::BeginDisabled(trim(visionModelPath) == recommendedModelPath);
		if (ImGui::SmallButton("Use recommended path##Vision")) {
			copyStringToBuffer(
				visionModelPath,
				sizeof(visionModelPath),
				recommendedModelPath);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Sets the model path to the profile's recommended file under bin/data/models/.");
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(recommendedDownloadUrl.empty());
		const bool opensModelPage =
			recommendedDownloadUrl.find("/tree/") != std::string::npos ||
			recommendedDownloadUrl.find("/blob/") != std::string::npos;
		if (ImGui::SmallButton(opensModelPage ? "Open model page##Vision" : "Download model##Vision")) {
			ofLaunchBrowser(recommendedDownloadUrl);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered()) {
			if (isEuRestrictedVisionProfile(profile)) {
				ImGui::SetTooltip("Opens the model page in your browser. Meta currently blocks this download in the EU.");
			} else if (opensModelPage) {
				ImGui::SetTooltip("Opens the recommended model page in your browser so you can pick the exact GGUF file.");
			} else {
				ImGui::SetTooltip("Opens the recommended multimodal model in your browser.");
			}
		}
	}
	if (profile.mayRequireMmproj) {
		ImGui::TextDisabled("Note: some variants also need a matching mmproj file on the server side.");
	}
	ImGui::TextDisabled(
		"OCR: %s | Multi-image: %s",
		profile.supportsOcr ? "supported" : "not ideal",
		profile.supportsMultipleImages ? "supported" : "single-image oriented");
}

ImGui::InputText("Server URL", visionServerUrl, sizeof(visionServerUrl));
if (ImGui::IsItemHovered()) {
	ImGui::SetTooltip("Example: http://127.0.0.1:8080");
}

ImGui::InputText("Model path", visionModelPath, sizeof(visionModelPath));
ImGui::SameLine();
if (ImGui::Button("Browse model...", ImVec2(110, 0))) {
	ofFileDialogResult result = ofSystemLoadDialog("Select vision model", false);
	if (result.bSuccess) {
		copyStringToBuffer(visionModelPath, sizeof(visionModelPath), result.getPath());
	}
}

ImGui::InputText("Image path", visionImagePath, sizeof(visionImagePath));
ImGui::SameLine();
if (ImGui::Button("Browse...", ImVec2(90, 0))) {
	ofFileDialogResult result = ofSystemLoadDialog("Select image", false);
	if (result.bSuccess) {
		copyStringToBuffer(visionImagePath, sizeof(visionImagePath), result.getPath());
	}
}

static const char * visionTaskLabels[] = { "Describe", "OCR", "Ask" };
ImGui::SetNextItemWidth(180);
ImGui::Combo("Task", &visionTaskIndex, visionTaskLabels, 3);

	if (ImGui::Button("Scene Describe", ImVec2(130, 0))) {
		visionTaskIndex = static_cast<int>(ofxGgmlVisionTask::Describe);
		copyStringToBuffer(
			visionPrompt,
			sizeof(visionPrompt),
			"Describe the scene professionally. Cover the main subject, layout, visible text, state, and anything a teammate would need to know quickly.");
		copyStringToBuffer(
			visionSystemPrompt,
			sizeof(visionSystemPrompt),
			"You are a precise multimodal assistant. Report only what is visually supported and organize the answer cleanly.");
	}
	ImGui::SameLine();
	if (ImGui::Button("Screenshot Review", ImVec2(140, 0))) {
		visionTaskIndex = static_cast<int>(ofxGgmlVisionTask::Ask);
		copyStringToBuffer(
			visionPrompt,
			sizeof(visionPrompt),
			"Review this screenshot like a professional product teammate. Summarize the UI, key controls, current state, visible warnings, and likely user intent.");
		copyStringToBuffer(
			visionSystemPrompt,
			sizeof(visionSystemPrompt),
			"You are a grounded multimodal assistant. Stay anchored to the image and avoid guessing about hidden state.");
	}
	ImGui::SameLine();
	if (ImGui::Button("Document OCR", ImVec2(120, 0))) {
		visionTaskIndex = static_cast<int>(ofxGgmlVisionTask::Ocr);
		copyStringToBuffer(
			visionPrompt,
			sizeof(visionPrompt),
			"Extract the readable text from this document image. Preserve headings, paragraphs, and line breaks where they matter.");
		copyStringToBuffer(
			visionSystemPrompt,
			sizeof(visionSystemPrompt),
			"You are an OCR assistant. Preserve reading order when possible and do not invent unreadable text.");
	}

ImGui::InputTextMultiline(
	"Vision prompt",
	visionPrompt,
	sizeof(visionPrompt),
	ImVec2(-1, 100));
ImGui::InputTextMultiline(
	"Vision system prompt",
	visionSystemPrompt,
	sizeof(visionSystemPrompt),
	ImVec2(-1, 70));

	ImGui::Separator();
	ImGui::TextDisabled("Optional video workflow");
	ImGui::InputText("Video path", visionVideoPath, sizeof(visionVideoPath));
	ImGui::SameLine();
	if (ImGui::Button("Browse video...", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select video", false);
		if (result.bSuccess) {
			copyStringToBuffer(visionVideoPath, sizeof(visionVideoPath), result.getPath());
		}
	}
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Sampled frames", &visionVideoMaxFrames, 1, 12);
	if (!visionProfiles.empty()) {
		const auto & profile = visionProfiles[static_cast<size_t>(selectedVisionProfileIndex)];
		if (!profile.supportsMultipleImages) {
			ImGui::TextDisabled("This profile is single-image oriented. Video analysis will use one representative frame.");
		}
	}

ImGui::BeginDisabled(generating.load() || std::strlen(visionImagePath) == 0);
if (ImGui::Button("Run Vision", ImVec2(140, 0))) {
	runVisionInference();
}
ImGui::EndDisabled();
ImGui::SameLine();
ImGui::BeginDisabled(generating.load() || std::strlen(visionVideoPath) == 0);
if (ImGui::Button("Run Video", ImVec2(140, 0))) {
	runVideoInference();
}
ImGui::EndDisabled();

ImGui::Separator();
ImGui::Text("Output:");
if (!visionOutput.empty()) {
	ImGui::SameLine();
	if (ImGui::SmallButton("Copy##VisionCopy")) copyToClipboard(visionOutput);
	ImGui::SameLine();
	if (ImGui::SmallButton("Clear##VisionClear")) visionOutput.clear();
	ImGui::SameLine();
	ImGui::TextDisabled("(%d chars)", static_cast<int>(visionOutput.size()));
}
if (generating.load() && activeGenerationMode == AiMode::Vision) {
	ImGui::BeginChild("##VisionOut", ImVec2(0, 0), true);
	std::string partial;
	{
		std::lock_guard<std::mutex> lock(streamMutex);
		partial = streamingOutput;
	}
	if (partial.empty()) {
		int dots = static_cast<int>(ImGui::GetTime() * kDotsAnimationSpeed) % 4;
		ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", kWaitingLabels[dots]);
	} else {
		ImGui::TextWrapped("%s", partial.c_str());
	}
	ImGui::EndChild();
} else {
	ImGui::BeginChild("##VisionOut", ImVec2(0, 0), true);
	if (visionOutput.empty()) {
		ImGui::TextDisabled("Vision responses appear here.");
	} else {
		ImGui::TextWrapped("%s", visionOutput.c_str());
	}
	ImGui::EndChild();
	}
}

void ofApp::drawSpeechPanel() {
	drawPanelHeader("Speech", "audio transcription and translation via speech backends");

	ImGui::TextWrapped(
		"Use a local speech backend such as whisper-cli. The speech path now preserves transcript artifacts, "
		"timestamp segments, and detected language metadata when the backend provides them.");

	const auto applySpeechProfileDefaults =
		[this](const ofxGgmlSpeechModelProfile & profile, bool onlyWhenEmpty) {
			if (!profile.executable.empty() &&
				(!onlyWhenEmpty || trim(speechExecutable).empty())) {
				copyStringToBuffer(
					speechExecutable,
					sizeof(speechExecutable),
					profile.executable);
			}
			const std::string suggestedPath =
				suggestedModelPath(profile.modelPath, profile.modelFileHint);
			if (!suggestedPath.empty() &&
				(!onlyWhenEmpty || trim(speechModelPath).empty())) {
				copyStringToBuffer(
					speechModelPath,
					sizeof(speechModelPath),
					suggestedPath);
			}
		};

	bool loadedSpeechProfiles = false;
	if (speechProfiles.empty()) {
		speechProfiles = ofxGgmlSpeechInference::defaultProfiles();
		loadedSpeechProfiles = !speechProfiles.empty();
	}
	selectedSpeechProfileIndex = std::clamp(
		selectedSpeechProfileIndex,
		0,
		std::max(0, static_cast<int>(speechProfiles.size()) - 1));
	if (loadedSpeechProfiles && !speechProfiles.empty()) {
		applySpeechProfileDefaults(
			speechProfiles[static_cast<size_t>(selectedSpeechProfileIndex)],
			true);
	}

	const ofxGgmlSpeechModelProfile activeSpeechProfile =
		speechProfiles.empty()
			? ofxGgmlSpeechModelProfile{}
			: speechProfiles[static_cast<size_t>(selectedSpeechProfileIndex)];

	if (!speechProfiles.empty()) {
		std::vector<const char *> profileNames;
		profileNames.reserve(speechProfiles.size());
		for (const auto & profile : speechProfiles) {
			profileNames.push_back(profile.name.c_str());
		}
		ImGui::SetNextItemWidth(260);
		if (ImGui::Combo("Speech profile", &selectedSpeechProfileIndex, profileNames.data(),
			static_cast<int>(profileNames.size()))) {
			const auto & profile = speechProfiles[static_cast<size_t>(selectedSpeechProfileIndex)];
			applySpeechProfileDefaults(profile, false);
			if (!profile.supportsTranslate &&
				speechTaskIndex == static_cast<int>(ofxGgmlSpeechTask::Translate)) {
				speechTaskIndex = static_cast<int>(ofxGgmlSpeechTask::Transcribe);
			}
		}

		const std::string recommendedModelPath =
			suggestedModelPath(activeSpeechProfile.modelPath, activeSpeechProfile.modelFileHint);
		const std::string recommendedDownloadUrl =
			suggestedModelDownloadUrl(activeSpeechProfile.modelRepoHint, activeSpeechProfile.modelFileHint);
		if (!activeSpeechProfile.modelRepoHint.empty()) {
			ImGui::TextDisabled("Recommended repo: %s", activeSpeechProfile.modelRepoHint.c_str());
		}
		if (!activeSpeechProfile.modelFileHint.empty()) {
			ImGui::TextDisabled("Recommended file: %s", activeSpeechProfile.modelFileHint.c_str());
		}
		if (!recommendedModelPath.empty()) {
			ImGui::TextDisabled("Recommended local path: %s", recommendedModelPath.c_str());
			ImGui::TextDisabled(
				pathExists(recommendedModelPath)
					? "Recommended model is already present."
					: "Recommended model is not downloaded yet.");
			ImGui::BeginDisabled(trim(speechModelPath) == recommendedModelPath);
			if (ImGui::SmallButton("Use recommended path##Speech")) {
				copyStringToBuffer(
					speechModelPath,
					sizeof(speechModelPath),
					recommendedModelPath);
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Sets the speech model path to the recommended file under bin/data/models/.");
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(recommendedDownloadUrl.empty());
			if (ImGui::SmallButton("Download model##Speech")) {
				ofLaunchBrowser(recommendedDownloadUrl);
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Opens the recommended Whisper model in your browser.");
			}
		}
	}

	ImGui::InputText("Audio path", speechAudioPath, sizeof(speechAudioPath));
	ImGui::SameLine();
	if (ImGui::Button("Browse audio...", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select audio file", false);
		if (result.bSuccess) {
			copyStringToBuffer(speechAudioPath, sizeof(speechAudioPath), result.getPath());
		}
	}

	ImGui::InputText("Executable", speechExecutable, sizeof(speechExecutable));
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Example: whisper-cli or a full path to whisper-cli.exe");
	}

	ImGui::InputText("Model path", speechModelPath, sizeof(speechModelPath));
	ImGui::SameLine();
	if (ImGui::Button("Browse model...", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select Whisper model", false);
		if (result.bSuccess) {
			copyStringToBuffer(speechModelPath, sizeof(speechModelPath), result.getPath());
		}
	}

	static const char * speechTaskLabels[] = {"Transcribe", "Translate"};
	ImGui::SetNextItemWidth(180);
	ImGui::BeginDisabled(!activeSpeechProfile.supportsTranslate);
	ImGui::Combo("Speech task", &speechTaskIndex, speechTaskLabels, 2);
	ImGui::EndDisabled();
	if (!activeSpeechProfile.supportsTranslate) {
		ImGui::SameLine();
		ImGui::TextDisabled("(selected profile is transcription-only)");
		speechTaskIndex = static_cast<int>(ofxGgmlSpeechTask::Transcribe);
	}

	ImGui::InputText("Language hint", speechLanguageHint, sizeof(speechLanguageHint));
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Use auto to let the backend decide, or pass a language like en, de, fr.");
	}

	ImGui::Checkbox("Return timestamps", &speechReturnTimestamps);
	if (!activeSpeechProfile.supportsTimestamps) {
		ImGui::SameLine();
		ImGui::TextDisabled("(backend may still ignore timestamps)");
	}

	ImGui::InputTextMultiline(
		"Speech prompt",
		speechPrompt,
		sizeof(speechPrompt),
		ImVec2(-1, 90));

	const double recordedSeconds = [&]() {
		std::lock_guard<std::mutex> lock(speechRecordMutex);
		if (speechInputSampleRate <= 0) return 0.0;
		return static_cast<double>(speechRecordedSamples.size()) /
			static_cast<double>(speechInputSampleRate);
	}();
	if (speechRecording) {
		ImGui::TextColored(
			ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
			"Recording microphone... %.1f s", recordedSeconds);
	} else if (!speechRecordedTempPath.empty()) {
		ImGui::TextDisabled(
			"Last mic recording: %.1f s -> %s",
			recordedSeconds,
			speechRecordedTempPath.c_str());
	}

	if (!speechRecording) {
		if (ImGui::Button("Start Mic Recording", ImVec2(180, 0))) {
			startSpeechRecording();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Records from the default microphone into a temporary WAV file.");
		}
		if (!speechRecordedTempPath.empty() || recordedSeconds > 0.0) {
			ImGui::SameLine();
			if (ImGui::Button("Use Last Recording", ImVec2(160, 0))) {
				const std::string wavPath = flushSpeechRecordingToTempWav();
				if (!wavPath.empty()) {
					copyStringToBuffer(speechAudioPath, sizeof(speechAudioPath), wavPath);
					logWithLevel(OF_LOG_NOTICE, "Prepared microphone recording: " + wavPath);
				}
			}
		}
	} else {
		if (ImGui::Button("Stop Mic", ImVec2(120, 0))) {
			stopSpeechRecording(true);
			const std::string wavPath = flushSpeechRecordingToTempWav();
			if (!wavPath.empty()) {
				copyStringToBuffer(speechAudioPath, sizeof(speechAudioPath), wavPath);
				logWithLevel(OF_LOG_NOTICE, "Saved microphone recording: " + wavPath);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Stop + Run", ImVec2(140, 0))) {
			stopSpeechRecording(true);
			const std::string wavPath = flushSpeechRecordingToTempWav();
			if (!wavPath.empty()) {
				copyStringToBuffer(speechAudioPath, sizeof(speechAudioPath), wavPath);
				runSpeechInference();
			}
		}
	}

	const bool hasAudioInput = std::strlen(speechAudioPath) > 0;
	ImGui::BeginDisabled(generating.load() || !hasAudioInput);
	if (ImGui::Button("Run Speech", ImVec2(140, 0))) {
		runSpeechInference();
	}
	ImGui::EndDisabled();

	if (!speechDetectedLanguage.empty() || speechSegmentCount > 0 || !speechTranscriptPath.empty()) {
		ImGui::Separator();
		if (!speechDetectedLanguage.empty()) {
			ImGui::TextDisabled("Detected language: %s", speechDetectedLanguage.c_str());
		}
		if (speechSegmentCount > 0) {
			ImGui::TextDisabled("Timestamp segments: %d", speechSegmentCount);
		}
		if (!speechTranscriptPath.empty()) {
			ImGui::TextDisabled("Transcript file: %s", speechTranscriptPath.c_str());
		}
		if (!speechSrtPath.empty()) {
			ImGui::TextDisabled("Subtitle file: %s", speechSrtPath.c_str());
		}
	}

	ImGui::Separator();
	ImGui::Text("Transcript:");
	if (!speechOutput.empty()) {
		ImGui::SameLine();
		if (ImGui::SmallButton("Copy##SpeechCopy")) copyToClipboard(speechOutput);
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear##SpeechClear")) {
			speechOutput.clear();
			speechDetectedLanguage.clear();
			speechTranscriptPath.clear();
			speechSrtPath.clear();
			speechSegmentCount = 0;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(%d chars)", static_cast<int>(speechOutput.size()));
	}
	if (generating.load() && activeGenerationMode == AiMode::Speech) {
		ImGui::BeginChild("##SpeechOut", ImVec2(0, 0), true);
		std::string partial;
		{
			std::lock_guard<std::mutex> lock(streamMutex);
			partial = streamingOutput;
		}
		if (partial.empty()) {
			int dots = static_cast<int>(ImGui::GetTime() * kDotsAnimationSpeed) % 4;
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", kWaitingLabels[dots]);
		} else {
			ImGui::TextWrapped("%s", partial.c_str());
		}
		ImGui::EndChild();
	} else {
		ImGui::BeginChild("##SpeechOut", ImVec2(0, 0), true);
		if (speechOutput.empty()) {
			ImGui::TextDisabled("Speech transcription appears here.");
		} else {
			ImGui::TextWrapped("%s", speechOutput.c_str());
		}
		ImGui::EndChild();
	}
}

void ofApp::audioIn(ofSoundBuffer & input) {
	if (!speechRecording) {
		return;
	}
	const size_t channels = std::max<size_t>(1, input.getNumChannels());
	const size_t frames = input.getNumFrames();
	std::lock_guard<std::mutex> lock(speechRecordMutex);
	speechRecordedSamples.reserve(speechRecordedSamples.size() + frames);
	for (size_t frame = 0; frame < frames; ++frame) {
		float mono = 0.0f;
		for (size_t channel = 0; channel < channels; ++channel) {
			mono += input[frame * channels + channel];
		}
		mono /= static_cast<float>(channels);
		speechRecordedSamples.push_back(mono);
	}
}

bool ofApp::startSpeechRecording() {
	if (speechRecording) {
		return true;
	}
	try {
		stopSpeechRecording(false);
		{
			std::lock_guard<std::mutex> lock(speechRecordMutex);
			speechRecordedSamples.clear();
			speechRecordedTempPath.clear();
		}
		ofSoundStreamSettings settings;
		settings.setInListener(this);
		settings.sampleRate = speechInputSampleRate;
		settings.numInputChannels = speechInputChannels;
		settings.numOutputChannels = 0;
		settings.bufferSize = speechInputBufferSize;
		settings.numBuffers = 4;
		if (!speechInputStream.setup(settings)) {
			logWithLevel(OF_LOG_ERROR, "Failed to open microphone input stream for speech mode.");
			return false;
		}
		speechRecording = true;
		logWithLevel(OF_LOG_NOTICE, "Started microphone recording for speech mode.");
		return true;
	} catch (const std::exception & e) {
		logWithLevel(OF_LOG_ERROR, std::string("Failed to start microphone recording: ") + e.what());
		return false;
	} catch (...) {
		logWithLevel(OF_LOG_ERROR, "Failed to start microphone recording.");
		return false;
	}
}

void ofApp::stopSpeechRecording(bool keepBufferedAudio) {
	if (speechRecording || speechInputStream.getSoundStream() != nullptr) {
		speechRecording = false;
		speechInputStream.stop();
		speechInputStream.close();
	}
	if (!keepBufferedAudio) {
		std::lock_guard<std::mutex> lock(speechRecordMutex);
		speechRecordedSamples.clear();
		speechRecordedTempPath.clear();
	}
}

std::string ofApp::flushSpeechRecordingToTempWav() {
	std::vector<float> recorded;
	{
		std::lock_guard<std::mutex> lock(speechRecordMutex);
		recorded = speechRecordedSamples;
	}
	if (recorded.empty()) {
		return {};
	}
	if (speechRecordedTempPath.empty()) {
		speechRecordedTempPath = makeTempMicRecordingPath();
	}
	if (!writeMonoWavFile(speechRecordedTempPath, recorded, speechInputSampleRate)) {
		logWithLevel(OF_LOG_ERROR, "Failed to write microphone recording to WAV.");
		return {};
	}
	return speechRecordedTempPath;
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

void ofApp::drawStatusBar() {
ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
ImGuiWindowFlags_NoScrollbar;

if (ImGui::Begin("##StatusBar", nullptr, flags)) {
ImGui::Text("Engine: %s", engineStatus.c_str());
ImGui::SameLine();
if (!modelPresets.empty()) {
ImGui::Text(" | Model: %s", modelPresets[static_cast<size_t>(selectedModelIndex)].name.c_str());
ImGui::SameLine();
}
ImGui::Text(" | Mode: %s", modeLabels[static_cast<int>(activeMode)]);
if (activeMode == AiMode::Chat &&
	chatLanguageIndex > 0 &&
	chatLanguageIndex < static_cast<int>(chatLanguages.size())) {
	ImGui::SameLine();
	ImGui::Text(" | Chat Lang: %s",
		chatLanguages[static_cast<size_t>(chatLanguageIndex)].name.c_str());
}
if (activeMode == AiMode::Script && !scriptLanguages.empty()) {
ImGui::SameLine();
ImGui::Text(" | Lang: %s", scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].name.c_str());
}
ImGui::SameLine();
ImGui::Text(" | Tokens: %d  Temp: %.2f  Top-P: %.2f  Top-K: %d  Min-P: %.2f",
	maxTokens, temperature, topP, topK, minP);
if (liveContextMode == LiveContextMode::Offline) {
	ImGui::SameLine();
	ImGui::TextDisabled(" | Offline");
} else if (liveContextMode == LiveContextMode::LoadedSourcesOnly) {
	ImGui::SameLine();
	ImGui::TextDisabled(" | LoadedSourcesOnly");
} else if (liveContextMode == LiveContextMode::LiveContextStrictCitations) {
	ImGui::SameLine();
	ImGui::TextDisabled(" | LiveContextStrictCitations");
} else {
	ImGui::SameLine();
	ImGui::TextDisabled(" | LiveContext");
}
if (gpuLayers > 0) {
ImGui::SameLine();
if (detectedModelLayers > 0) {
ImGui::Text(" | GPU: %d/%d layers", gpuLayers, detectedModelLayers);
} else {
ImGui::Text(" | GPU: %d layers", gpuLayers);
}
}
if (generating.load()) {
ImGui::SameLine();
const char * spinner = "|/-\\";
int spinIdx = static_cast<int>(ImGui::GetTime() / kSpinnerInterval) % 4;
float elapsed = ofGetElapsedTimef() - generationStartTime;
char statusLabel[64];
snprintf(statusLabel, sizeof(statusLabel), " | %c Generating... (%.1fs)", spinner[spinIdx], elapsed);
ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", statusLabel);
	std::string partial;
	{
		std::lock_guard<std::mutex> lock(streamMutex);
		partial = streamingOutput;
	}
	if (elapsed > 0.2f && !partial.empty()) {
		const float cps = static_cast<float>(partial.size()) / elapsed;
		ImGui::SameLine();
		ImGui::TextDisabled(" | %.0f chars/s", cps);
	}
} else if (lastComputeMs > 0.0f) {
ImGui::SameLine();
ImGui::TextDisabled(" | Last: %.1f ms", lastComputeMs);
}
ImGui::SameLine();
ImGui::Text(" | FPS: %.0f", ofGetFrameRate());
}
ImGui::End();
}

// ---------------------------------------------------------------------------
// Device info window
// ---------------------------------------------------------------------------

void ofApp::drawDeviceInfoWindow() {
ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_FirstUseEver);
if (ImGui::Begin("Device Info", &showDeviceInfo)) {
ImGui::Text("Backend: %s", ggml.getBackendName().c_str());
ImGui::Text("State: %s", ofxGgmlHelpers::stateName(ggml.getState()).c_str());
ImGui::Separator();

if (devices.empty()) {
ImGui::TextDisabled("No devices discovered.");
} else {
for (size_t i = 0; i < devices.size(); i++) {
const auto & d = devices[i];
ImGui::PushID(static_cast<int>(i));
ImGui::Text("%s", d.name.c_str());
ImGui::SameLine();
ImGui::TextDisabled("(%s)", d.description.c_str());
ImGui::Text("  Type: %s  Memory: %s / %s",
ofxGgmlHelpers::backendTypeName(d.type).c_str(),
ofxGgmlHelpers::formatBytes(d.memoryFree).c_str(),
ofxGgmlHelpers::formatBytes(d.memoryTotal).c_str());
ImGui::Separator();
ImGui::PopID();
}
}
}
ImGui::End();
}

// ---------------------------------------------------------------------------
// Log window
// ---------------------------------------------------------------------------

void ofApp::drawLogWindow() {
ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_FirstUseEver);
if (ImGui::Begin("Engine Log", &showLog)) {
if (ImGui::Button("Clear")) {
std::lock_guard<std::mutex> lock(logMutex);
logMessages.clear();
}
ImGui::Separator();
ImGui::BeginChild("##LogScroll", ImVec2(0, 0), false);
std::lock_guard<std::mutex> lock(logMutex);
for (const auto & line : logMessages) {
ImGui::TextWrapped("%s", line.c_str());
}
if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f) {
ImGui::SetScrollHereY(1.0f);
}
ImGui::EndChild();
}
ImGui::End();
}

// ---------------------------------------------------------------------------
// Session persistence — escape / unescape helpers
// ---------------------------------------------------------------------------

std::string ofApp::escapeSessionText(const std::string & text) const {
std::string result;
result.reserve(text.size() + text.size() / 8);
for (char c : text) {
switch (c) {
case '\n': result += "\\n";  break;
case '\r': result += "\\r";  break;
case '\t': result += "\\t";  break;
case '\\': result += "\\\\"; break;
default:   result += c;      break;
}
}
return result;
}

std::string ofApp::unescapeSessionText(const std::string & text) const {
std::string result;
result.reserve(text.size());
for (size_t i = 0; i < text.size(); i++) {
if (text[i] == '\\' && i + 1 < text.size()) {
switch (text[i + 1]) {
case 'n':  result += '\n'; i++; break;
case 'r':  result += '\r'; i++; break;
case 't':  result += '\t'; i++; break;
case '\\': result += '\\'; i++; break;
default:   result += text[i];   break;
}
} else {
result += text[i];
}
}
return result;
}

// ---------------------------------------------------------------------------
// Session persistence — save
// ---------------------------------------------------------------------------

bool ofApp::saveSession(const std::string & path) {
std::ofstream out(path);
if (!out.is_open()) return false;

out << "[session_v1]\n";

// Settings.
out << "mode=" << static_cast<int>(activeMode) << "\n";
out << "chatLanguage="
	<< std::clamp(chatLanguageIndex, 0,
		std::max(0, static_cast<int>(chatLanguages.size()) - 1))
	<< "\n";
out << "model=" << selectedModelIndex << "\n";
out << "language=" << selectedLanguageIndex << "\n";
out << "maxTokens=" << maxTokens << "\n";
out << "temperature=" << ofToString(temperature, 4) << "\n";
out << "topP=" << ofToString(topP, 4) << "\n";
out << "topK=" << topK << "\n";
out << "minP=" << ofToString(minP, 4) << "\n";
out << "repeatPenalty=" << ofToString(repeatPenalty, 4) << "\n";
out << "contextSize=" << contextSize << "\n";
out << "batchSize=" << batchSize << "\n";
out << "gpuLayers=" << gpuLayers << "\n";
out << "seed=" << seed << "\n";
out << "numThreads=" << numThreads << "\n";
out << "selectedBackend=" << selectedBackendIndex << "\n";
{
std::string selectedName;
if (selectedBackendIndex >= 0 &&
	selectedBackendIndex < static_cast<int>(backendNames.size())) {
	selectedName = backendNames[selectedBackendIndex];
}
out << "selectedBackendName=" << escapeSessionText(selectedName) << "\n";
}
out << "theme=" << themeIndex << "\n";
out << "mirostatMode=" << mirostatMode << "\n";
out << "mirostatTau=" << ofToString(mirostatTau, 4) << "\n";
out << "mirostatEta=" << ofToString(mirostatEta, 4) << "\n";
out << "textInferenceBackend=" << static_cast<int>(textInferenceBackend) << "\n";
out << "textServerUrl=" << escapeSessionText(textServerUrl) << "\n";
out << "textServerModel=" << escapeSessionText(textServerModel) << "\n";
out << "useModeTokenBudgets=" << (useModeTokenBudgets ? 1 : 0) << "\n";
out << "autoContinueCutoff=" << (autoContinueCutoff ? 1 : 0) << "\n";
out << "usePromptCache=" << (usePromptCache ? 1 : 0) << "\n";
for (int i = 0; i < kModeCount; i++) {
	out << "modeTokenBudget" << i << "="
		<< std::clamp(modeMaxTokens[static_cast<size_t>(i)], 32, 4096) << "\n";
	out << "modeTextBackend" << i << "="
		<< std::clamp(modeTextBackendIndices[static_cast<size_t>(i)], 0, 1) << "\n";
}
out << "logLevel=" << static_cast<int>(logLevel) << "\n";

// Script source.
out << "scriptSourceType=" << static_cast<int>(scriptSource.getSourceType()) << "\n";
out << "scriptSourcePath=" << escapeSessionText(scriptSource.getLocalFolderPath()) << "\n";
out << "scriptSourceGitHub=" << escapeSessionText(scriptSourceGitHub) << "\n";
out << "scriptSourceBranch=" << escapeSessionText(scriptSourceBranch) << "\n";
{
	const auto internetUrls = scriptSource.getInternetUrls();
	std::string packed;
	for (size_t i = 0; i < internetUrls.size(); i++) {
		if (i > 0) packed += "\n";
		packed += internetUrls[i];
	}
	out << "scriptSourceInternetUrls=" << escapeSessionText(packed) << "\n";
}
out << "useProjectMemory=" << (scriptProjectMemory.isEnabled() ? 1 : 0) << "\n";
out << "projectMemory=" << escapeSessionText(scriptProjectMemory.getMemoryText()) << "\n";
	out << "scriptIncludeRepoContext=" << (scriptIncludeRepoContext ? 1 : 0) << "\n";

// Input buffers.
out << "chatInput=" << escapeSessionText(chatInput) << "\n";
out << "scriptInput=" << escapeSessionText(scriptInput) << "\n";
out << "summarizeInput=" << escapeSessionText(summarizeInput) << "\n";
out << "writeInput=" << escapeSessionText(writeInput) << "\n";
out << "translateInput=" << escapeSessionText(translateInput) << "\n";
out << "translateSourceLang=" << translateSourceLang << "\n";
out << "translateTargetLang=" << translateTargetLang << "\n";
out << "customInput=" << escapeSessionText(customInput) << "\n";
out << "customSystemPrompt=" << escapeSessionText(customSystemPrompt) << "\n";
out << "visionPrompt=" << escapeSessionText(visionPrompt) << "\n";
out << "visionImagePath=" << escapeSessionText(visionImagePath) << "\n";
out << "visionVideoPath=" << escapeSessionText(visionVideoPath) << "\n";
out << "visionModelPath=" << escapeSessionText(visionModelPath) << "\n";
out << "visionServerUrl=" << escapeSessionText(visionServerUrl) << "\n";
out << "visionSystemPrompt=" << escapeSessionText(visionSystemPrompt) << "\n";
out << "visionTaskIndex=" << visionTaskIndex << "\n";
out << "visionVideoMaxFrames=" << visionVideoMaxFrames << "\n";
out << "visionProfileIndex=" << selectedVisionProfileIndex << "\n";
out << "speechAudioPath=" << escapeSessionText(speechAudioPath) << "\n";
out << "speechExecutable=" << escapeSessionText(speechExecutable) << "\n";
out << "speechModelPath=" << escapeSessionText(speechModelPath) << "\n";
out << "speechPrompt=" << escapeSessionText(speechPrompt) << "\n";
out << "speechLanguageHint=" << escapeSessionText(speechLanguageHint) << "\n";
out << "speechTaskIndex=" << speechTaskIndex << "\n";
out << "speechProfileIndex=" << selectedSpeechProfileIndex << "\n";
out << "speechReturnTimestamps=" << (speechReturnTimestamps ? 1 : 0) << "\n";

// Outputs.
out << "scriptOutput=" << escapeSessionText(scriptOutput) << "\n";
out << "summarizeOutput=" << escapeSessionText(summarizeOutput) << "\n";
out << "writeOutput=" << escapeSessionText(writeOutput) << "\n";
out << "translateOutput=" << escapeSessionText(translateOutput) << "\n";
out << "customOutput=" << escapeSessionText(customOutput) << "\n";
out << "visionOutput=" << escapeSessionText(visionOutput) << "\n";
out << "speechOutput=" << escapeSessionText(speechOutput) << "\n";
out << "liveContextMode=" << static_cast<int>(liveContextMode) << "\n";
out << "liveContextAllowPromptUrls=" << (liveContextAllowPromptUrls ? 1 : 0) << "\n";
out << "liveContextAllowDomainProviders=" << (liveContextAllowDomainProviders ? 1 : 0) << "\n";
out << "liveContextAllowGenericSearch=" << (liveContextAllowGenericSearch ? 1 : 0) << "\n";
out << "stopAtNaturalBoundary=" << (stopAtNaturalBoundary ? 1 : 0) << "\n";

// Chat messages.
out << "chatMessageCount=" << chatMessages.size() << "\n";
for (const auto & msg : chatMessages) {
out << "msg=" << escapeSessionText(msg.role) << "|"
<< ofToString(msg.timestamp, 2) << "|"
<< escapeSessionText(msg.text) << "\n";
}

out << "[/session_v1]\n";
out.close();
return true;
}

// ---------------------------------------------------------------------------
// Session persistence — load
// ---------------------------------------------------------------------------

bool ofApp::loadSession(const std::string & path) {
	std::ifstream in(path);
	if (!in.is_open()) return false;

std::string line;
if (!std::getline(in, line) || line != "[session_v1]") {
return false;
}

chatMessages.clear();
int loadedScriptSourceType = static_cast<int>(ofxGgmlScriptSourceType::None);
	std::string loadedScriptSourcePath;
	std::string loadedScriptSourceInternetUrls;
	bool logLevelSpecified = false;
	bool legacyVerbose = false;
	bool legacyVerboseSeen = false;
	bool modeTextBackendsSpecified = false;

auto copyToBuf = [this](char * buf, size_t bufSize, const std::string & value) {
std::string text = unescapeSessionText(value);
copyStringToBuffer(buf, bufSize, text);
};

// Safe integer/float parsers — return default on invalid input.
auto safeStoi = [](const std::string & s, int fallback = 0) -> int {
try { return std::stoi(s); } catch (...) { return fallback; }
};
auto safeStof = [](const std::string & s, float fallback = 0.0f) -> float {
try { return std::stof(s); } catch (...) { return fallback; }
};

while (std::getline(in, line)) {
if (line == "[/session_v1]") break;

size_t eq = line.find('=');
if (eq == std::string::npos) continue;
std::string key = line.substr(0, eq);
std::string value = line.substr(eq + 1);

if (key == "mode") {
	int m = std::clamp(safeStoi(value), 0, kModeCount - 1);
	activeMode = static_cast<AiMode>(m);
}
else if (key == "chatLanguage") {
	chatLanguageIndex = std::clamp(
		safeStoi(value),
		0,
		std::max(0, static_cast<int>(chatLanguages.size()) - 1));
}
else if (key == "model") {
	int maxIdx = std::max(0, static_cast<int>(modelPresets.size()) - 1);
	selectedModelIndex = std::clamp(safeStoi(value), 0, maxIdx);
}
else if (key == "language") {
	int maxIdx = std::max(0, static_cast<int>(scriptLanguages.size()) - 1);
	selectedLanguageIndex = std::clamp(safeStoi(value), 0, maxIdx);
}
else if (key == "maxTokens") maxTokens = std::clamp(safeStoi(value, 256), 32, 4096);
else if (key == "temperature") temperature = std::clamp(safeStof(value, 0.7f), 0.0f, 2.0f);
else if (key == "topP") topP = std::clamp(safeStof(value, 0.9f), 0.0f, 1.0f);
else if (key == "topK") topK = std::clamp(safeStoi(value, 40), 0, 200);
else if (key == "minP") minP = std::clamp(safeStof(value, 0.0f), 0.0f, 1.0f);
else if (key == "repeatPenalty") repeatPenalty = std::clamp(safeStof(value, 1.1f), 1.0f, 2.0f);
else if (key == "contextSize") contextSize = std::clamp(safeStoi(value, 2048), 256, 16384);
else if (key == "batchSize") batchSize = std::clamp(safeStoi(value, 512), 32, 4096);
else if (key == "gpuLayers") gpuLayers = std::clamp(safeStoi(value), 0, 128);
else if (key == "seed") seed = std::clamp(safeStoi(value, -1), -1, 99999);
else if (key == "numThreads") numThreads = std::clamp(safeStoi(value, 4), 1, 32);
else if (key == "selectedBackend") {
	// Legacy numeric index — kept for backward compatibility but
	// overridden by selectedBackendName when present.
	int maxIdx = std::max(0, static_cast<int>(backendNames.size()) - 1);
	selectedBackendIndex = std::clamp(safeStoi(value), 0, maxIdx);
}
else if (key == "selectedBackendName") {
	std::string name = unescapeSessionText(value);
	if (name.empty()) {
		selectedBackendIndex = 0; // first device
	} else {
		int found = 0; // default to first device
		for (int i = 0; i < static_cast<int>(backendNames.size()); i++) {
			if (backendNames[i] == name) {
				found = i;
				break;
			}
		}
		selectedBackendIndex = found;
	}
}
else if (key == "theme") {
	themeIndex = std::clamp(safeStoi(value), 0, 2);
	applyTheme(themeIndex);
}
else if (key == "mirostatMode") mirostatMode = std::clamp(safeStoi(value), 0, 2);
else if (key == "mirostatTau") mirostatTau = std::clamp(safeStof(value, 5.0f), 0.0f, 10.0f);
else if (key == "mirostatEta") mirostatEta = std::clamp(safeStof(value, 0.1f), 0.0f, 1.0f);
else if (key == "textInferenceBackend") {
	textInferenceBackend = clampTextInferenceBackend(safeStoi(value, 0));
}
	else if (key == "textServerUrl") copyToBuf(textServerUrl, sizeof(textServerUrl), value);
	else if (key == "textServerModel") copyToBuf(textServerModel, sizeof(textServerModel), value);
	else if (key == "useModeTokenBudgets") useModeTokenBudgets = (safeStoi(value, 1) != 0);
	else if (key == "autoContinueCutoff") autoContinueCutoff = (safeStoi(value, 0) != 0);
	else if (key == "usePromptCache") usePromptCache = (safeStoi(value, 1) != 0);
else if (key.rfind("modeTokenBudget", 0) == 0) {
	try {
		int idx = std::stoi(key.substr(std::strlen("modeTokenBudget")));
		if (idx >= 0 && idx < kModeCount) {
			modeMaxTokens[static_cast<size_t>(idx)] = std::clamp(safeStoi(value, 512), 32, 4096);
		}
	} catch (...) {
	}
	}
	else if (key.rfind("modeTextBackend", 0) == 0) {
		try {
			int idx = std::stoi(key.substr(std::strlen("modeTextBackend")));
			if (idx >= 0 && idx < kModeCount) {
				modeTextBackendIndices[static_cast<size_t>(idx)] =
					std::clamp(safeStoi(value, 0), 0, 1);
				modeTextBackendsSpecified = true;
			}
		} catch (...) {
		}
	}
else if (key == "logLevel") {
	int lvl = std::clamp(
		safeStoi(value, static_cast<int>(OF_LOG_NOTICE)),
		static_cast<int>(OF_LOG_VERBOSE),
		static_cast<int>(OF_LOG_SILENT));
	logLevel = static_cast<ofLogLevel>(lvl);
	logLevelSpecified = true;
}
else if (key == "verbose") {
	legacyVerbose = (safeStoi(value) != 0);
	legacyVerboseSeen = true;
}
else if (key == "customCliPath") { /* ignored — CLI path option removed */ }
else if (key == "scriptSourceType") {
	loadedScriptSourceType = std::clamp(safeStoi(value), 0, 3);
}
else if (key == "scriptSourcePath") loadedScriptSourcePath = unescapeSessionText(value);
else if (key == "scriptSourceGitHub") copyToBuf(scriptSourceGitHub, sizeof(scriptSourceGitHub), value);
else if (key == "scriptSourceBranch") copyToBuf(scriptSourceBranch, sizeof(scriptSourceBranch), value);
else if (key == "scriptSourceInternetUrls") loadedScriptSourceInternetUrls = unescapeSessionText(value);
else if (key == "useProjectMemory") scriptProjectMemory.setEnabled(safeStoi(value, 1) != 0);
else if (key == "projectMemory") {
scriptProjectMemory.setMemoryText(unescapeSessionText(value));
}
else if (key == "scriptIncludeRepoContext") scriptIncludeRepoContext = (safeStoi(value, 1) != 0);
else if (key == "chatInput") copyToBuf(chatInput, sizeof(chatInput), value);
else if (key == "scriptInput") copyToBuf(scriptInput, sizeof(scriptInput), value);
else if (key == "summarizeInput") copyToBuf(summarizeInput, sizeof(summarizeInput), value);
else if (key == "writeInput") copyToBuf(writeInput, sizeof(writeInput), value);
else if (key == "translateInput") copyToBuf(translateInput, sizeof(translateInput), value);
else if (key == "translateSourceLang") {
	int maxIdx = std::max(0, static_cast<int>(translateLanguages.size()) - 1);
	translateSourceLang = std::clamp(safeStoi(value), 0, maxIdx);
}
else if (key == "translateTargetLang") {
	int maxIdx = std::max(0, static_cast<int>(translateLanguages.size()) - 1);
	translateTargetLang = std::clamp(safeStoi(value, 1), 0, maxIdx);
}
else if (key == "customInput") copyToBuf(customInput, sizeof(customInput), value);
else if (key == "customSystemPrompt") copyToBuf(customSystemPrompt, sizeof(customSystemPrompt), value);
else if (key == "visionPrompt") copyToBuf(visionPrompt, sizeof(visionPrompt), value);
else if (key == "visionImagePath") copyToBuf(visionImagePath, sizeof(visionImagePath), value);
else if (key == "visionVideoPath") copyToBuf(visionVideoPath, sizeof(visionVideoPath), value);
else if (key == "visionModelPath") copyToBuf(visionModelPath, sizeof(visionModelPath), value);
else if (key == "visionServerUrl") copyToBuf(visionServerUrl, sizeof(visionServerUrl), value);
else if (key == "visionSystemPrompt") copyToBuf(visionSystemPrompt, sizeof(visionSystemPrompt), value);
else if (key == "visionTaskIndex") visionTaskIndex = std::clamp(safeStoi(value), 0, 2);
else if (key == "visionVideoMaxFrames") visionVideoMaxFrames = std::clamp(safeStoi(value, 6), 1, 12);
else if (key == "visionProfileIndex") selectedVisionProfileIndex = std::max(0, safeStoi(value));
else if (key == "speechAudioPath") copyToBuf(speechAudioPath, sizeof(speechAudioPath), value);
else if (key == "speechExecutable") copyToBuf(speechExecutable, sizeof(speechExecutable), value);
else if (key == "speechModelPath") copyToBuf(speechModelPath, sizeof(speechModelPath), value);
else if (key == "speechPrompt") copyToBuf(speechPrompt, sizeof(speechPrompt), value);
else if (key == "speechLanguageHint") copyToBuf(speechLanguageHint, sizeof(speechLanguageHint), value);
else if (key == "speechTaskIndex") speechTaskIndex = std::clamp(safeStoi(value), 0, 1);
else if (key == "speechProfileIndex") selectedSpeechProfileIndex = std::max(0, safeStoi(value));
else if (key == "speechReturnTimestamps") speechReturnTimestamps = (safeStoi(value, 0) != 0);
else if (key == "scriptOutput") scriptOutput = unescapeSessionText(value);
else if (key == "summarizeOutput") summarizeOutput = unescapeSessionText(value);
else if (key == "writeOutput") writeOutput = unescapeSessionText(value);
else if (key == "translateOutput") translateOutput = unescapeSessionText(value);
else if (key == "customOutput") customOutput = unescapeSessionText(value);
else if (key == "visionOutput") visionOutput = unescapeSessionText(value);
else if (key == "speechOutput") speechOutput = unescapeSessionText(value);
else if (key == "liveContextMode") {
	liveContextMode = static_cast<LiveContextMode>(std::clamp(safeStoi(value, 0), 0, 3));
}
else if (key == "liveContextAllowPromptUrls") liveContextAllowPromptUrls = (safeStoi(value, 1) != 0);
else if (key == "liveContextAllowDomainProviders") liveContextAllowDomainProviders = (safeStoi(value, 1) != 0);
else if (key == "liveContextAllowGenericSearch") liveContextAllowGenericSearch = (safeStoi(value, 1) != 0);
else if (key == "stopAtNaturalBoundary") stopAtNaturalBoundary = (safeStoi(value, 1) != 0);
else if (key == "msg") {
// Parse: role|timestamp|text
size_t sep1 = value.find('|');
if (sep1 != std::string::npos) {
size_t sep2 = value.find('|', sep1 + 1);
if (sep2 != std::string::npos) {
Message msg;
msg.role = unescapeSessionText(value.substr(0, sep1));
msg.timestamp = safeStof(value.substr(sep1 + 1, sep2 - sep1 - 1));
msg.text = unescapeSessionText(value.substr(sep2 + 1));
chatMessages.push_back(msg);
}
}
}
}

if (!logLevelSpecified && legacyVerboseSeen && legacyVerbose) {
	logLevel = OF_LOG_VERBOSE;
}
applyLogLevel(logLevel);

in.close();

if (!scriptLanguages.empty()) {
	scriptSource.setPreferredExtension(
		scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExtension);
}
if (loadedScriptSourceType == static_cast<int>(ofxGgmlScriptSourceType::LocalFolder) &&
	!loadedScriptSourcePath.empty()) {
	scriptSource.setLocalFolder(loadedScriptSourcePath);
	selectedScriptFileIndex = -1;
} else if (loadedScriptSourceType == static_cast<int>(ofxGgmlScriptSourceType::GitHubRepo)) {
	scriptSource.setGitHubMode();
	std::string ownerRepo = scriptSourceGitHub;
	std::string branch = std::strlen(scriptSourceBranch) > 0
		? std::string(scriptSourceBranch) : std::string();
	if (!ownerRepo.empty()) {
		scriptSource.setGitHubRepoFromInput(ownerRepo, branch);
	}
	selectedScriptFileIndex = -1;
} else if (loadedScriptSourceType == static_cast<int>(ofxGgmlScriptSourceType::Internet)) {
	std::vector<std::string> urls;
	if (!loadedScriptSourceInternetUrls.empty()) {
		urls = ofSplitString(loadedScriptSourceInternetUrls, "\n", true, true);
	}
	scriptSource.setInternetUrls(urls);
	selectedScriptFileIndex = -1;
} else {
	scriptSource.clear();
	selectedScriptFileIndex = -1;
}

if (!modeTextBackendsSpecified) {
	for (int i = 0; i < kModeCount; i++) {
		if (aiModeSupportsTextBackend(static_cast<AiMode>(i))) {
			modeTextBackendIndices[static_cast<size_t>(i)] =
				static_cast<int>(textInferenceBackend);
		}
	}
}

if (useModeTokenBudgets) {
	maxTokens = std::clamp(modeMaxTokens[static_cast<size_t>(activeMode)], 32, 4096);
} else {
	modeMaxTokens[static_cast<size_t>(activeMode)] = std::clamp(maxTokens, 32, 4096);
}
syncTextBackendForActiveMode(false);

return true;
}

void ofApp::autoSaveSession() {
saveSession(lastSessionPath);
}

void ofApp::autoLoadSession() {
std::error_code ec;
if (std::filesystem::exists(lastSessionPath, ec) && !ec) {
loadSession(lastSessionPath);
}
}

// ---------------------------------------------------------------------------
// Inference — background thread
// ---------------------------------------------------------------------------

std::string ofApp::getSelectedModelPath() const {
	if (modelPresets.empty()) return "";
	if (selectedModelIndex < 0 || selectedModelIndex >= static_cast<int>(modelPresets.size())) return "";
	if (cachedModelPathIndex == selectedModelIndex && !cachedModelPath.empty()) {
		return cachedModelPath;
	}
	const auto & preset = modelPresets[static_cast<size_t>(selectedModelIndex)];
	cachedModelPath = ofToDataPath(ofFilePath::join("models", preset.filename), true);
	cachedModelPathIndex = selectedModelIndex;
	return cachedModelPath;
}

void ofApp::detectModelLayers() {
	detectedModelLayers = 0;
	const std::string modelPath = getSelectedModelPath();
	if (modelPath.empty()) return;

	std::error_code ec;
	if (!std::filesystem::exists(modelPath, ec) || ec) return;

	// Use ofxGgmlModel to read GGUF metadata without loading tensor data.
	// Common metadata keys for layer count:
	//   <arch>.block_count  (e.g., llama.block_count, qwen2.block_count)
	ofxGgmlModel model;
	if (!model.load(modelPath)) return;

	// Try to find the architecture name first.
	std::string arch = model.getMetadataString("general.architecture");
	if (!arch.empty()) {
		int32_t layers = model.getMetadataInt32(arch + ".block_count", 0);
		if (layers > 0) {
			detectedModelLayers = layers;
			if (shouldLog(OF_LOG_VERBOSE)) {
				logWithLevel(OF_LOG_VERBOSE,
					"Detected " + ofToString(detectedModelLayers) + " layers in model (" + arch + ")");
			}
		}
	}

	// Fallback: try common architecture names.
	// These are well-known GGUF architecture identifiers from llama.cpp.
	// Update this list when new architectures are added to llama.cpp.
	if (detectedModelLayers == 0) {
		const char * archNames[] = {
			"llama", "qwen2", "gemma", "phi", "starcoder",
			"gpt2", "mpt", "falcon", "bloom", "mistral"
		};
		for (const auto & name : archNames) {
			int32_t layers = model.getMetadataInt32(std::string(name) + ".block_count", 0);
			if (layers > 0) {
				detectedModelLayers = layers;
				if (shouldLog(OF_LOG_VERBOSE)) {
					logWithLevel(OF_LOG_VERBOSE,
						"Detected " + ofToString(detectedModelLayers) + " layers in model (" + name + ")");
				}
				break;
			}
		}
	}

	model.close();
}

void ofApp::runHierarchicalReview(const std::string & overrideQuery) {
	if (generating.load() || !engineReady) return;

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::Script;
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = "Building review plan...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	workerThread = std::thread([this, overrideQuery]() {
		auto setError = [this](const std::string & msg) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingOutput = msg;
			pendingRole = "assistant";
			pendingMode = AiMode::Script;
		};

		try {
			if (llamaCliState.load(std::memory_order_relaxed) != 1) {
				probeLlamaCli();
			}

			const bool useServerBackend =
				(textInferenceBackend == TextInferenceBackend::LlamaServer);
			const std::string modelPath = getSelectedModelPath();
			if (modelPath.empty() && !useServerBackend) {
				setError("[Error] No model selected for review.");
				generating.store(false);
				return;
			}

			scriptCodeReview.setCompletionExecutable(llamaCliCommand);
			{
				std::filesystem::path cliPath(llamaCliCommand);
				std::string embeddingExe;
				if (cliPath.has_parent_path()) {
					auto dir = cliPath.parent_path();
#ifdef _WIN32
					embeddingExe = (dir / "llama-embedding.exe").string();
#else
					embeddingExe = (dir / "llama-embedding").string();
#endif
					std::error_code ec;
					if (!std::filesystem::exists(embeddingExe, ec) || ec) {
						embeddingExe = "llama-embedding";
					}
				} else {
					embeddingExe = "llama-embedding";
				}
				scriptCodeReview.setEmbeddingExecutable(embeddingExe);
			}

			const std::string effectiveReviewQuery = !trim(overrideQuery).empty()
				? trim(overrideQuery)
				: (std::strlen(scriptInput) > 0
					? std::string(scriptInput)
					: ofxGgmlCodeReview::defaultReviewQuery());

			ofxGgmlCodeReviewSettings reviewSettings;
			reviewSettings.maxTokens = std::clamp(maxTokens, 384, 768);
			reviewSettings.contextSize = std::clamp(contextSize, 4096, 6144);
			reviewSettings.batchSize = std::clamp(batchSize, 128, 256);
			reviewSettings.gpuLayers = gpuLayers;
			reviewSettings.threads = numThreads;
			reviewSettings.useServerBackend = useServerBackend;
			reviewSettings.serverUrl = useServerBackend
				? effectiveTextServerUrl(textServerUrl)
				: trim(textServerUrl);
			reviewSettings.serverModel = trim(textServerModel);
			reviewSettings.maxEmbedParallelTasks = 2;
			reviewSettings.maxSummaryParallelTasks = 1;
			reviewSettings.usePromptCache = false;
			reviewSettings.autoContinueCutoff = true;
			reviewSettings.projectMemory = &scriptProjectMemory;

			std::mutex reviewFallbackMutex;
			scriptCodeReview.setGenerationFallback(
				[this, &reviewFallbackMutex](
					const std::string & /*fallbackModelPath*/,
					const std::string & prompt,
					const ofxGgmlInferenceSettings & inferenceSettings) {
					std::lock_guard<std::mutex> lock(reviewFallbackMutex);

					const int savedMaxTokens = maxTokens;
					const int savedContextSize = contextSize;
					const int savedBatchSize = batchSize;
					const int savedGpuLayers = gpuLayers;
					const int savedNumThreads = numThreads;
					const int savedTopK = topK;
					const int savedMirostatMode = mirostatMode;
					const float savedTemperature = temperature;
					const float savedTopP = topP;
					const float savedMinP = minP;
					const float savedRepeatPenalty = repeatPenalty;
					const float savedMirostatTau = mirostatTau;
					const float savedMirostatEta = mirostatEta;
					const TextInferenceBackend savedTextInferenceBackend =
						textInferenceBackend;
					const std::string savedTextServerUrl = textServerUrl;
					const std::string savedTextServerModel = textServerModel;
					const bool savedUsePromptCache = usePromptCache;
					const bool savedAutoContinueCutoff = autoContinueCutoff;

					maxTokens = inferenceSettings.maxTokens;
					contextSize = inferenceSettings.contextSize;
					batchSize = inferenceSettings.batchSize;
					gpuLayers = inferenceSettings.gpuLayers;
					if (inferenceSettings.threads > 0) {
						numThreads = inferenceSettings.threads;
					}
					temperature = inferenceSettings.temperature;
					topP = inferenceSettings.topP;
					topK = inferenceSettings.topK;
					minP = inferenceSettings.minP;
					repeatPenalty = inferenceSettings.repeatPenalty;
					mirostatMode = inferenceSettings.mirostat;
					mirostatTau = inferenceSettings.mirostatTau;
					mirostatEta = inferenceSettings.mirostatEta;
					textInferenceBackend = inferenceSettings.useServerBackend
						? TextInferenceBackend::LlamaServer
						: TextInferenceBackend::Cli;
					copyStringToBuffer(
						textServerUrl,
						sizeof(textServerUrl),
						inferenceSettings.serverUrl);
					copyStringToBuffer(
						textServerModel,
						sizeof(textServerModel),
						inferenceSettings.serverModel);
					usePromptCache = false;
					autoContinueCutoff = inferenceSettings.autoContinueCutoff;

					std::string output;
					std::string error;
					const bool success = runRealInference(
						AiMode::Script,
						prompt,
						output,
						error,
						nullptr,
						false,
						true);

					maxTokens = savedMaxTokens;
					contextSize = savedContextSize;
					batchSize = savedBatchSize;
					gpuLayers = savedGpuLayers;
					numThreads = savedNumThreads;
					topK = savedTopK;
					mirostatMode = savedMirostatMode;
					temperature = savedTemperature;
					topP = savedTopP;
					minP = savedMinP;
					repeatPenalty = savedRepeatPenalty;
					mirostatTau = savedMirostatTau;
					mirostatEta = savedMirostatEta;
					textInferenceBackend = savedTextInferenceBackend;
					copyStringToBuffer(
						textServerUrl,
						sizeof(textServerUrl),
						savedTextServerUrl);
					copyStringToBuffer(
						textServerModel,
						sizeof(textServerModel),
						savedTextServerModel);
					usePromptCache = savedUsePromptCache;
					autoContinueCutoff = savedAutoContinueCutoff;

					ofxGgmlInferenceResult result;
					result.success = success;
					result.text = output;
					result.error = error;
					return result;
				});

			const auto reviewResult = scriptCodeReview.reviewScriptSource(
				modelPath,
				scriptSource,
				effectiveReviewQuery,
				reviewSettings,
				[this](const ofxGgmlCodeReviewProgress & progress) {
					{
						std::lock_guard<std::mutex> lock(streamMutex);
						streamingOutput = progress.stage;
						if (progress.total > 0) {
							streamingOutput += " (" + ofToString(progress.completed) +
								"/" + ofToString(progress.total) + ")";
						}
					}
					return !cancelRequested.load();
				});
			scriptCodeReview.clearGenerationFallback();
			if (!reviewResult.success) {
				if (reviewResult.error == "cancelled") {
					setError("[Cancelled] Review cancelled.");
				} else {
					setError("[Error] Hierarchical review failed: " + reviewResult.error);
				}
				generating.store(false);
				return;
			}

			lastScriptRequest = effectiveReviewQuery;
			{
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingOutput = reviewResult.combinedReport;
				pendingRole = "assistant";
				pendingMode = AiMode::Script;
			}
			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput.clear();
			}
			generating.store(false);
			return;
		} catch (const std::exception & e) {
			setError(std::string("[Error] Hierarchical review failed: ") + e.what());
		} catch (...) {
			setError("[Error] Unknown failure during hierarchical review.");
		}

		generating.store(false);
	});
}

void ofApp::runVisionInference() {
	if (generating.load()) return;

	if (visionProfiles.empty()) {
		visionProfiles = ofxGgmlVisionInference::defaultProfiles();
	}
	if (visionProfiles.empty()) {
		visionOutput = "[Error] No vision profiles are available.";
		return;
	}

	selectedVisionProfileIndex = std::clamp(
		selectedVisionProfileIndex,
		0,
		std::max(0, static_cast<int>(visionProfiles.size()) - 1));

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::Vision;
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = "Preparing multimodal request...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	const ofxGgmlVisionModelProfile profileBase =
		visionProfiles[static_cast<size_t>(selectedVisionProfileIndex)];
	const std::string prompt = trim(visionPrompt);
	const std::string imagePath = trim(visionImagePath);
	const std::string modelPath = trim(visionModelPath);
	const std::string serverUrl = trim(visionServerUrl);
	const std::string systemPrompt = trim(visionSystemPrompt);
	const int taskIndex = std::clamp(visionTaskIndex, 0, 2);
	const int requestedMaxTokens = std::clamp(maxTokens, 64, 4096);
	const float requestedTemperature = std::isfinite(temperature)
		? std::clamp(temperature, 0.0f, 2.0f)
		: 0.2f;

	workerThread = std::thread([this, profileBase, prompt, imagePath, modelPath, serverUrl, systemPrompt, taskIndex, requestedMaxTokens, requestedTemperature]() {
		auto setPending = [this](const std::string & text) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingOutput = text;
			pendingRole = "assistant";
			pendingMode = AiMode::Vision;
		};

		try {
			if (imagePath.empty()) {
				setPending("[Error] Select an image first.");
				generating.store(false);
				return;
			}

			ofxGgmlVisionModelProfile profile = profileBase;
			if (!serverUrl.empty()) {
				profile.serverUrl = serverUrl;
			}
			if (!modelPath.empty()) {
				profile.modelPath = modelPath;
			} else if (trim(profile.modelPath).empty() &&
				!trim(profile.modelFileHint).empty()) {
				const std::filesystem::path suggested =
					std::filesystem::path(ofToDataPath("models", true)) /
					trim(profile.modelFileHint);
				std::error_code ec;
				if (std::filesystem::exists(suggested, ec) && !ec) {
					profile.modelPath = suggested.string();
				}
			}

			ofxGgmlVisionRequest request;
			request.task = static_cast<ofxGgmlVisionTask>(taskIndex);
			request.prompt = prompt;
			request.systemPrompt = systemPrompt;
			request.maxTokens = requestedMaxTokens;
			request.temperature = requestedTemperature;
			if (chatLanguageIndex > 0 &&
				chatLanguageIndex < static_cast<int>(chatLanguages.size())) {
				request.responseLanguage =
					chatLanguages[static_cast<size_t>(chatLanguageIndex)].name;
			}
			request.images.push_back({imagePath, "Input image", ""});

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput =
					"Contacting " + ofxGgmlVisionInference::normalizeServerUrl(profile.serverUrl);
			}

			const ofxGgmlVisionResult result = visionInference.runServerRequest(profile, request);
			if (cancelRequested.load()) {
				setPending("[Cancelled] Vision request cancelled.");
			} else if (result.success) {
				setPending(result.text);
				logWithLevel(
					OF_LOG_NOTICE,
					"Vision request completed in " +
						ofxGgmlHelpers::formatDurationMs(result.elapsedMs) +
						" via " + result.usedServerUrl);
			} else {
				setPending("[Error] " + result.error);
				if (!result.responseJson.empty()) {
					logWithLevel(OF_LOG_WARNING, "Vision response: " + result.responseJson);
				}
			}
		} catch (const std::exception & e) {
			setPending(std::string("[Error] Vision inference failed: ") + e.what());
		} catch (...) {
			setPending("[Error] Unknown failure during vision inference.");
		}

		{
			std::lock_guard<std::mutex> lock(streamMutex);
			streamingOutput.clear();
		}
		generating.store(false);
	});
}

void ofApp::runVideoInference() {
	if (generating.load()) return;

	if (visionProfiles.empty()) {
		visionProfiles = ofxGgmlVisionInference::defaultProfiles();
	}
	if (visionProfiles.empty()) {
		visionOutput = "[Error] No vision profiles are available.";
		return;
	}

	selectedVisionProfileIndex = std::clamp(
		selectedVisionProfileIndex,
		0,
		std::max(0, static_cast<int>(visionProfiles.size()) - 1));

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::Vision;
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = "Sampling video frames...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	const ofxGgmlVisionModelProfile profileBase =
		visionProfiles[static_cast<size_t>(selectedVisionProfileIndex)];
	const std::string prompt = trim(visionPrompt);
	const std::string videoPath = trim(visionVideoPath);
	const std::string modelPath = trim(visionModelPath);
	const std::string serverUrl = trim(visionServerUrl);
	const std::string systemPrompt = trim(visionSystemPrompt);
	const int taskIndex = std::clamp(visionTaskIndex, 0, 2);
	const int requestedMaxTokens = std::clamp(maxTokens, 96, 4096);
	const float requestedTemperature = std::isfinite(temperature)
		? std::clamp(temperature, 0.0f, 2.0f)
		: 0.2f;
	const int sampledFrames = std::clamp(visionVideoMaxFrames, 1, 12);

	workerThread = std::thread([this, profileBase, prompt, videoPath, modelPath, serverUrl, systemPrompt, taskIndex, requestedMaxTokens, requestedTemperature, sampledFrames]() {
		auto setPending = [this](const std::string & text) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingOutput = text;
			pendingRole = "assistant";
			pendingMode = AiMode::Vision;
		};

		try {
			if (videoPath.empty()) {
				setPending("[Error] Select a video first.");
				generating.store(false);
				return;
			}

			ofxGgmlVisionModelProfile profile = profileBase;
			if (!serverUrl.empty()) {
				profile.serverUrl = serverUrl;
			}
			if (!modelPath.empty()) {
				profile.modelPath = modelPath;
			}

			ofxGgmlVideoRequest request;
			request.task = static_cast<ofxGgmlVideoTask>(taskIndex);
			request.videoPath = videoPath;
			request.prompt = prompt;
			request.systemPrompt = systemPrompt;
			request.maxTokens = requestedMaxTokens;
			request.temperature = requestedTemperature;
			const int effectiveFrames = profile.supportsMultipleImages
				? sampledFrames
				: 1;
			request.maxFrames = effectiveFrames;
			if (chatLanguageIndex > 0 &&
				chatLanguageIndex < static_cast<int>(chatLanguages.size())) {
				request.responseLanguage =
					chatLanguages[static_cast<size_t>(chatLanguageIndex)].name;
			}

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput = "Sampling frames and contacting " +
					ofxGgmlVisionInference::normalizeServerUrl(profile.serverUrl);
			}

			const ofxGgmlVideoResult result = videoInference.runServerRequest(profile, request);
			if (cancelRequested.load()) {
				setPending("[Cancelled] Video request cancelled.");
			} else if (result.success) {
				std::ostringstream output;
				output << "Video analysis";
				if (!result.backendName.empty()) {
					output << " (" << result.backendName << ")";
				}
				output << "\n";
				output << "Sampled frames: " << result.sampledFrames.size() << "\n";
				if (!profile.supportsMultipleImages && sampledFrames > 1) {
					output << "Note: selected profile is single-image oriented, so video analysis used one representative frame.\n";
				}
				output << "\n" << result.text;
				setPending(output.str());
				logWithLevel(
					OF_LOG_NOTICE,
					"Video request completed in " +
						ofxGgmlHelpers::formatDurationMs(result.elapsedMs) +
						" using " + result.backendName);
			} else {
				setPending("[Error] " + result.error);
				if (!result.visionResult.responseJson.empty()) {
					logWithLevel(OF_LOG_WARNING, "Video vision response: " + result.visionResult.responseJson);
				}
			}
		} catch (const std::exception & e) {
			setPending(std::string("[Error] Video inference failed: ") + e.what());
		} catch (...) {
			setPending("[Error] Unknown failure during video inference.");
		}

		{
			std::lock_guard<std::mutex> lock(streamMutex);
			streamingOutput.clear();
		}
		generating.store(false);
	});
}

void ofApp::runSpeechInference() {
	if (generating.load()) return;

	if (speechProfiles.empty()) {
		speechProfiles = ofxGgmlSpeechInference::defaultProfiles();
	}
	selectedSpeechProfileIndex = std::clamp(
		selectedSpeechProfileIndex,
		0,
		std::max(0, static_cast<int>(speechProfiles.size()) - 1));

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::Speech;
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = "Preparing speech transcription...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	const ofxGgmlSpeechModelProfile profileBase =
		speechProfiles.empty()
			? ofxGgmlSpeechModelProfile{}
			: speechProfiles[static_cast<size_t>(selectedSpeechProfileIndex)];
	const std::string audioPath = trim(speechAudioPath);
	const std::string executable = trim(speechExecutable);
	const std::string modelPath = trim(speechModelPath);
	const std::string prompt = trim(speechPrompt);
	const std::string languageHint = trim(speechLanguageHint);
	const int taskIndex = std::clamp(speechTaskIndex, 0, 1);
	const bool returnTimestamps = speechReturnTimestamps;

	workerThread = std::thread([this, profileBase, audioPath, executable, modelPath, prompt, languageHint, taskIndex, returnTimestamps]() {
		auto setPending = [this](const std::string & text) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingOutput = text;
			pendingRole = "assistant";
			pendingMode = AiMode::Speech;
		};

		try {
			if (audioPath.empty()) {
				setPending("[Error] Select an audio file first.");
				generating.store(false);
				return;
			}

			std::string effectiveExecutable =
				executable.empty() ? trim(profileBase.executable) : executable;
			if (effectiveExecutable.empty()) {
				effectiveExecutable = "whisper-cli";
			}

			std::string effectiveModelPath = modelPath.empty()
				? trim(profileBase.modelPath)
				: modelPath;
			if (effectiveModelPath.empty() && !trim(profileBase.modelFileHint).empty()) {
				const std::filesystem::path suggested =
					std::filesystem::path(ofToDataPath("models", true)) /
					trim(profileBase.modelFileHint);
				std::error_code ec;
				if (std::filesystem::exists(suggested, ec) && !ec) {
					effectiveModelPath = suggested.string();
				}
			}

			speechInference.setBackend(
				ofxGgmlSpeechInference::createWhisperCliBackend(
					effectiveExecutable));

			ofxGgmlSpeechRequest request;
			request.task = static_cast<ofxGgmlSpeechTask>(taskIndex);
			request.audioPath = audioPath;
			request.modelPath = effectiveModelPath;
			request.languageHint = languageHint;
			request.prompt = prompt;
			request.returnTimestamps = returnTimestamps;

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput = "Running " + effectiveExecutable + "...";
			}

			const ofxGgmlSpeechResult result = speechInference.transcribe(request);
			if (cancelRequested.load()) {
				setPending("[Cancelled] Speech request cancelled.");
			} else if (result.success) {
				std::string displayText = result.text;
				if (!result.segments.empty()) {
					const std::string segmentText = formatSpeechSegments(result.segments);
					if (!segmentText.empty()) {
						displayText += "\n\nTimestamp segments:\n" + segmentText;
					}
				}
				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingSpeechDetectedLanguage = result.detectedLanguage;
					pendingSpeechTranscriptPath = result.transcriptPath;
					pendingSpeechSrtPath = result.srtPath;
					pendingSpeechSegmentCount = static_cast<int>(result.segments.size());
				}
				setPending(displayText);
				logWithLevel(
					OF_LOG_NOTICE,
					"Speech request completed in " +
						ofxGgmlHelpers::formatDurationMs(result.elapsedMs) +
						" via " + result.backendName);
			} else {
				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingSpeechDetectedLanguage.clear();
					pendingSpeechTranscriptPath.clear();
					pendingSpeechSrtPath.clear();
					pendingSpeechSegmentCount = 0;
				}
				setPending("[Error] " + result.error);
				if (!result.rawOutput.empty()) {
					logWithLevel(OF_LOG_WARNING, "Speech raw output: " + result.rawOutput);
				}
			}
		} catch (const std::exception & e) {
			{
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingSpeechDetectedLanguage.clear();
				pendingSpeechTranscriptPath.clear();
				pendingSpeechSrtPath.clear();
				pendingSpeechSegmentCount = 0;
			}
			setPending(std::string("[Error] Speech inference failed: ") + e.what());
		} catch (...) {
			{
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingSpeechDetectedLanguage.clear();
				pendingSpeechTranscriptPath.clear();
				pendingSpeechSrtPath.clear();
				pendingSpeechSegmentCount = 0;
			}
			setPending("[Error] Unknown failure during speech inference.");
		}

		{
			std::lock_guard<std::mutex> lock(streamMutex);
			streamingOutput.clear();
		}
		generating.store(false);
	});
}


bool ofApp::runRealInference(AiMode mode, const std::string & prompt, std::string & output, std::string & error,
	std::function<void(const std::string &)> onStreamData,
	bool preserveLlamaInstructions,
	bool suppressFallbackWarning) {
	output.clear();
	error.clear();

	const bool preferredServerBackend =
		(textInferenceBackend == TextInferenceBackend::LlamaServer);
	const std::string modelPath = getSelectedModelPath();
	bool useServerBackend = preferredServerBackend;
	auto prepareCliBackend = [&](std::string * cliError = nullptr) -> bool {
		std::string localError;
		if (modelPath.empty()) {
			localError = "No model preset selected.";
		} else if (!std::filesystem::exists(modelPath)) {
			localError = "Model file not found: " + modelPath;
		}
		if (!localError.empty()) {
			if (cliError) {
				*cliError = localError;
			} else {
				error = localError;
			}
			return false;
		}
		if (llamaCliState.load(std::memory_order_relaxed) != 1) {
			probeLlamaCli();
			if (llamaCliState.load(std::memory_order_relaxed) != 1) {
				localError = "Optional CLI fallback is not installed. Build it with scripts/build-llama-cli.sh if you want a local non-server fallback.";
				if (cliError) {
					*cliError = localError;
				} else {
					error = localError;
				}
				return false;
			}
		}
		llmInference.setCompletionExecutable(llamaCliCommand);
		llmInference.probeCompletionCapabilities(true);
		return true;
	};
	if (useServerBackend && !ensureTextServerReady(false, true)) {
		const std::string serverError = !textServerStatusMessage.empty()
			? textServerStatusMessage
			: "Server-backed inference is not ready.";
		std::string cliError;
		if (prepareCliBackend(&cliError)) {
			useServerBackend = false;
			if (shouldLog(OF_LOG_NOTICE)) {
				logWithLevel(
					OF_LOG_NOTICE,
					"Server-backed inference is unavailable; falling back to local llama-completion for this request.");
			}
		} else {
			error = serverError;
			if (!cliError.empty()) {
				error += " CLI fallback unavailable: " + cliError;
			}
			return false;
		}
	}
	if (!useServerBackend && !prepareCliBackend()) {
		return false;
	}

	ofxGgmlInferenceSettings inferenceSettings;
	inferenceSettings.maxTokens = std::clamp(maxTokens, 1, 8192);
	inferenceSettings.temperature = std::isfinite(temperature)
		? std::clamp(temperature, 0.0f, 2.0f)
		: kDefaultTemp;
	inferenceSettings.topP = std::isfinite(topP)
		? std::clamp(topP, 0.0f, 1.0f)
		: kDefaultTopP;
	inferenceSettings.topK = std::clamp(topK, 0, 200);
	inferenceSettings.minP = std::isfinite(minP)
		? std::clamp(minP, 0.0f, 1.0f)
		: 0.0f;
	inferenceSettings.repeatPenalty = std::isfinite(repeatPenalty)
		? std::clamp(repeatPenalty, 1.0f, 2.0f)
		: kDefaultRepeatPenalty;
	inferenceSettings.contextSize = std::clamp(contextSize, 256, 16384);
	inferenceSettings.batchSize = std::clamp(batchSize, 32, 4096);
	inferenceSettings.threads = std::clamp(numThreads, 1, 128);
	inferenceSettings.gpuLayers = std::clamp(gpuLayers, 0, detectedModelLayers > 0 ? detectedModelLayers : 128);
	inferenceSettings.seed = seed;
	inferenceSettings.simpleIo = true;
	inferenceSettings.singleTurn = true;
	inferenceSettings.autoProbeCliCapabilities = true;
	inferenceSettings.trimPromptToContext = true;
	inferenceSettings.allowBatchFallback = true;
	inferenceSettings.autoContinueCutoff = (mode == AiMode::Script) && autoContinueCutoff;
	inferenceSettings.stopAtNaturalBoundary = stopAtNaturalBoundary;
	inferenceSettings.autoPromptCache = usePromptCache;
	inferenceSettings.promptCachePath = usePromptCache ? promptCachePathFor(modelPath, mode) : std::string();
	inferenceSettings.mirostat = mirostatMode;
	inferenceSettings.mirostatTau = mirostatTau;
	inferenceSettings.mirostatEta = mirostatEta;
	inferenceSettings.useServerBackend = useServerBackend;
	if (useServerBackend) {
		inferenceSettings.serverUrl = effectiveTextServerUrl(textServerUrl);
		inferenceSettings.serverModel = trim(textServerModel);
	}

	if (!useServerBackend &&
		!backendNames.empty() &&
		selectedBackendIndex >= 0 &&
		selectedBackendIndex < static_cast<int>(backendNames.size())) {
		const std::string & selected = backendNames[static_cast<size_t>(selectedBackendIndex)];
		if (selected != "CPU") {
			inferenceSettings.device = selected;
		}
	}
	if (!useServerBackend && inferenceSettings.device.empty()) {
		const std::string backend = ggml.getBackendName();
		if (!backend.empty() && backend != "CPU" && backend != "none") {
			inferenceSettings.device = backend;
		}
	}
	if (!useServerBackend &&
		inferenceSettings.gpuLayers == 0 &&
		inferenceSettings.device != "CPU") {
		inferenceSettings.gpuLayers = detectedModelLayers > 0 ? detectedModelLayers : 999;
	}

	std::string streamedText;
	auto chunkBridge = [&](const std::string & chunk) -> bool {
		if (cancelRequested.load()) {
			return false;
		}
		streamedText += chunk;
		if (onStreamData) {
			onStreamData(streamedText);
		}
		return true;
	};

	const ofxGgmlInferenceResult inferenceResult = llmInference.generate(
		modelPath,
		prompt,
		inferenceSettings,
		chunkBridge);
	bool useLegacyFallback = false;
	if (!inferenceResult.success) {
		if (!streamedText.empty() && !preserveLlamaInstructions) {
			output = ofxGgmlInference::sanitizeGeneratedText(
				streamedText,
				prompt);
			if (!output.empty()) {
				logWithLevel(OF_LOG_WARNING,
					"Generation ended with an error, but streamed output is available; using streamed text.");
				return true;
			}
		}
		error = inferenceResult.error.empty()
			? "Inference failed."
			: inferenceResult.error;
		useLegacyFallback = true;
	}
	if (!useLegacyFallback) {
		if (preserveLlamaInstructions) {
			output = trim(stripAnsi(streamedText.empty() ? inferenceResult.text : streamedText));
		} else if (!streamedText.empty()) {
			output = ofxGgmlInference::sanitizeGeneratedText(streamedText, prompt);
		} else {
			output = trim(inferenceResult.text);
		}
		if (!output.empty()) {
			return true;
		}
		if (error.empty()) {
			error = useServerBackend
				? "llama-server returned empty output."
				: "llama-completion returned empty output.";
		}
		useLegacyFallback = true;
		if (!useLegacyFallback) {
			return false;
		}
	}
	if (!suppressFallbackWarning && shouldLog(OF_LOG_WARNING)) {
		logWithLevel(OF_LOG_WARNING,
			useServerBackend
				? "Server-backed inference produced no usable output; falling back to local llama-completion."
				: "Modern inference path produced no usable output; falling back to legacy CLI execution.");
	}

	// Probe for llama-completion/llama-cli/llama if not already found.
	// Unlike earlier revisions this no longer permanently caches a
	// "not-found" result so the user can install the tools without
	// restarting the app.

	if (!prepareCliBackend()) {
		return false;
	}
	probeCliCapabilities();

	std::error_code tempEc;
	std::string dataDir = std::filesystem::temp_directory_path(tempEc).string();
	if (tempEc || dataDir.empty()) {
		dataDir = ofToDataPath("", true);
	}
	std::random_device rd;
	const uint64_t nonceHi = static_cast<uint64_t>(rd());
	const uint64_t nonceLo = static_cast<uint64_t>(rd());
	const uint64_t nonce = (nonceHi << 32) | nonceLo;
	const std::string id = ofToString(ofGetSystemTimeMillis()) + "_" + ofToString(nonce);
	const std::string promptPath = ofFilePath::join(dataDir, "llama_prompt_" + id + ".txt");

	{
		std::ofstream promptFile(promptPath);
		if (!promptFile.is_open()) {
			error = "Failed to create prompt file.";
			return false;
		}
		promptFile << prompt;
	}

	const int safeMaxTokens = std::clamp(maxTokens, 1, 8192);
	const float safeTemp = (std::isfinite(temperature) ? std::clamp(temperature, 0.0f, 2.0f) : kDefaultTemp);
	const float safeTopP = (std::isfinite(topP) ? std::clamp(topP, 0.0f, 1.0f) : kDefaultTopP);
const int safeTopK = std::clamp(topK, 0, 200);
const float safeMinP = (std::isfinite(minP) ? std::clamp(minP, 0.0f, 1.0f) : 0.0f);
	const float safeRepeatPenalty = (std::isfinite(repeatPenalty) ? std::clamp(repeatPenalty, 1.0f, 2.0f) : kDefaultRepeatPenalty);
	const int safeThreads = std::clamp(numThreads, 1, 128);
	const int safeContext = std::clamp(contextSize, 256, 16384);
	int effectiveBatch = std::clamp(batchSize, 32, 4096);
	const int maxGpuLayers = detectedModelLayers > 0 ? detectedModelLayers : 128;
	const int safeGpuLayers = std::clamp(gpuLayers, 0, maxGpuLayers);
	std::string cliDevice;
	if (!backendNames.empty() &&
		selectedBackendIndex >= 0 &&
		selectedBackendIndex < static_cast<int>(backendNames.size())) {
		const std::string & selected = backendNames[static_cast<size_t>(selectedBackendIndex)];
		if (selected != "CPU") {
			cliDevice = selected;
		}
	}
	if (cliDevice.empty()) {
		const std::string backend = ggml.getBackendName();
		if (!backend.empty() && backend != "CPU" && backend != "none") {
			cliDevice = backend;
		}
	}
	if (!cliDevice.empty() && effectiveBatch > 256) {
		if (shouldLog(OF_LOG_NOTICE)) {
			logWithLevel(OF_LOG_NOTICE,
				"Reducing batch size from " + ofToString(effectiveBatch) +
				" to 256 for CUDA/Vulkan stability.");
		}
		effectiveBatch = 256;
	}
	// GPU layers control the llama-completion CLI process, which has
	// its own GPU support independent of the addon's ggml engine.
	int effectiveGpuLayers = safeGpuLayers;
	if (effectiveGpuLayers == 0) {
		bool cpuBackendSelected = false;
		if (!backendNames.empty() &&
			selectedBackendIndex >= 0 &&
			selectedBackendIndex < static_cast<int>(backendNames.size())) {
			cpuBackendSelected = (backendNames[static_cast<size_t>(selectedBackendIndex)] == "CPU");
		}
		if (!cpuBackendSelected) {
			effectiveGpuLayers = (detectedModelLayers > 0) ? detectedModelLayers : 999;
			if (shouldLog(OF_LOG_NOTICE)) {
				logWithLevel(OF_LOG_NOTICE,
					(detectedModelLayers > 0)
						? ("GPU layers was 0; using detected model layer count (" +
							ofToString(detectedModelLayers) + ") for llama CLI offload.")
						: "GPU layers was 0 and model layer metadata is unavailable; using -ngl 999 for llama CLI offload.");
			}
		}
	}

	std::ostringstream tempStr, topPStr, repeatPenaltyStr;
	tempStr << std::fixed << std::setprecision(3) << safeTemp;
	topPStr << std::fixed << std::setprecision(3) << safeTopP;
	repeatPenaltyStr << std::fixed << std::setprecision(3) << safeRepeatPenalty;

	auto makeArgs = [&](bool shortFlags) {
		std::vector<std::string> out;
		out.reserve(40);
		out.emplace_back(llamaCliCommand);
		out.emplace_back("-m");
		out.emplace_back(modelPath);
		out.emplace_back(shortFlags ? "-f" : "--file");
		out.emplace_back(promptPath);
		out.emplace_back("-n");
		out.emplace_back(ofToString(safeMaxTokens));
		out.emplace_back("-c");
		out.emplace_back(ofToString(safeContext));
		out.emplace_back("-b");
		out.emplace_back(ofToString(effectiveBatch));
		out.emplace_back("-ngl");
		out.emplace_back(ofToString(effectiveGpuLayers));
		if (!cliDevice.empty()) {
			out.emplace_back("--device");
			out.emplace_back(cliDevice);
			out.emplace_back("--split-mode");
			out.emplace_back("none");
		}
		out.emplace_back("--temp");
		out.emplace_back(tempStr.str());
		out.emplace_back("--top-p");
		out.emplace_back(topPStr.str());
		if (safeTopK > 0 && cliSupportsTopK) {
			out.emplace_back("--top-k");
			out.emplace_back(ofToString(safeTopK));
		}
		if (safeMinP > 0.0f && cliSupportsMinP) {
			std::ostringstream minPStr;
			minPStr << std::fixed << std::setprecision(3) << safeMinP;
			out.emplace_back("--min-p");
			out.emplace_back(minPStr.str());
		}
		out.emplace_back("--repeat-penalty");
		out.emplace_back(repeatPenaltyStr.str());
		out.emplace_back(shortFlags ? "-t" : "--threads");
		out.emplace_back(ofToString(safeThreads));
		(void)usePromptCache;
		out.emplace_back("--no-display-prompt");
		out.emplace_back("--simple-io");
		if (cliSupportsSingleTurn) {
			out.emplace_back("--single-turn");
		}
		if (seed >= 0) {
			out.emplace_back("--seed");
			out.emplace_back(ofToString(seed));
		}
		if ((mirostatMode == 1 || mirostatMode == 2) && cliSupportsMirostat) {
			out.emplace_back("--mirostat");
			out.emplace_back(ofToString(mirostatMode));
			std::ostringstream tauStr, etaStr;
			tauStr << std::fixed << std::setprecision(3) << std::clamp(mirostatTau, 0.0f, 20.0f);
			etaStr << std::fixed << std::setprecision(3) << std::clamp(mirostatEta, 0.0f, 1.0f);
			out.emplace_back("--mirostat-lr");
			out.emplace_back(etaStr.str());
			out.emplace_back("--mirostat-ent");
			out.emplace_back(tauStr.str());
		}
		return out;
	};
	std::vector<std::string> args = makeArgs(false);

	// Print the command line to console for debugging.
	if (shouldLog(OF_LOG_VERBOSE)) {
		std::string cmdLine;
		for (size_t i = 0; i < args.size(); i++) {
			if (i > 0) cmdLine += " ";
			cmdLine += args[i];
		}
		logWithLevel(OF_LOG_VERBOSE, "Running: " + cmdLine);
	}

	std::string raw;
	int ret = -1;
	const auto tCliStart = std::chrono::steady_clock::now();
	const bool started = runProcessCapture(args, raw, ret, true, onStreamData, false);
	const auto tCliEnd = std::chrono::steady_clock::now();
	const float cliElapsedMs = std::chrono::duration<float, std::milli>(tCliEnd - tCliStart).count();
	if (shouldLog(OF_LOG_VERBOSE)) {
		logWithLevel(OF_LOG_VERBOSE, std::string("Process ") +
			(started ? "started" : "failed to start") + ", exit code: " + ofToString(ret));
	}
	if (started && ret != 0) {
		const bool crashLikeExit =
			ret == -1073740791 || // Windows STATUS_STACK_BUFFER_OVERRUN
			ret == -1073741819;   // Windows STATUS_ACCESS_VIOLATION
		if (crashLikeExit) {
			if (shouldLog(OF_LOG_WARNING)) {
				logWithLevel(OF_LOG_WARNING,
					"llama-completion crashed; retrying once with lower batch size.");
			}
			effectiveBatch = std::min(effectiveBatch, 128);
			raw.clear();
			ret = -1;
			runProcessCapture(makeArgs(false), raw, ret, true, onStreamData, false);
		}

		// With stderr separated, error messages about unknown flags
		// are no longer in `raw`.  If the process failed quickly and
		// produced no usable stdout, retry with short-style flags as
		// a fallback in case the installed CLI version uses a
		// different option syntax.
		if (trim(raw).empty()) {
			args = makeArgs(true);
			runProcessCapture(args, raw, ret, true, onStreamData, false);
		}
		// If still no stdout output after the short-flag retry, do a
		// diagnostic run with stderr captured so the user sees the
		// actual error message from the CLI tool.
		if (ret != 0 && trim(raw).empty()) {
			std::string diagOut;
			int diagRet = -1;
			runProcessCapture(makeArgs(false), diagOut, diagRet, false, nullptr, true);
			if (!trim(diagOut).empty()) {
				raw = diagOut;
			}
		}
	}

	// Exit code 130 (128 + SIGINT) is expected when the interactive-
	// mode CLI receives EOF on stdin and shuts down.  Treat it as
	// success when we captured valid generated output.
	// Also tolerate specific crash-on-exit codes that can occur
	// during cleanup after generation already produced output.
	if (started && ret != 0 && !trim(stripAnsi(raw)).empty()) {
		const bool benignExit =
			ret == 130                   // SIGINT (EOF on stdin)
			|| ret == 1                  // generic error (may occur during cleanup)
			|| ret == -1                 // signal-killed on POSIX
			|| (ret >= 128 && ret < 160) // POSIX signal exits (128+signal)
			;
		if (benignExit) {
			ret = 0;
		}
	}

	std::error_code ec;
	std::filesystem::remove(promptPath, ec);
	if (ec) {
		logWithLevel(OF_LOG_WARNING, "failed to remove temp prompt file: " + promptPath);
	}

	if (!started || ret == kExecNotFound) {
		// Binary truly missing — invalidate cache so next call re-probes.
		llamaCliState.store(-1, std::memory_order_relaxed);
		error = "llama-completion/llama-cli/llama not found in PATH.";
		return false;
	}

	if (ret != 0) {
		const std::string trimmedRaw = trim(stripAnsi(raw));
		const std::string codeDesc = describeExitCode(ret);
		if (!codeDesc.empty()) {
			error = llamaCliCommand + " crashed: " + codeDesc + ".";
			if (!trimmedRaw.empty()) {
				error += "\nOutput:\n" + trimmedRaw;
			}
			error += "\nTry reducing context size, setting GPU layers to 0 (CPU-only), or updating your GPU drivers.";
		} else {
			error = llamaCliCommand + " exited with code " + ofToString(ret) + ".";
			if (!trimmedRaw.empty()) {
				error += " Output:\n" + trimmedRaw;
			}
		}
		return false;
	}

	if (shouldLog(OF_LOG_NOTICE)) {
		logWithLevel(OF_LOG_NOTICE,
			"llama-completion run: " + ofToString(cliElapsedMs, 1) +
				" ms, output " + ofToString(trim(stripAnsi(raw)).size()) + " chars");
	}

	output = trim(stripAnsi(raw));
	if (preserveLlamaInstructions) {
		if (output.empty()) {
			error = llamaCliCommand + " returned empty output.";
			return false;
		}
		return true;
	}
	output = ofxGgmlInference::sanitizeGeneratedText(output, prompt);

	if (output.empty()) {
		error = llamaCliCommand + " returned empty output.";
		return false;
	}

	return true;
}

// ---------------------------------------------------------------------------
// Backend reinitialization — called when the user changes backend or device.
// ---------------------------------------------------------------------------

void ofApp::reinitBackend() {
if (generating.load()) return;

ggml.close();

ofxGgmlSettings settings;
settings.threads = numThreads;
if (selectedBackendIndex >= 0 &&
	selectedBackendIndex < static_cast<int>(backendNames.size())) {
	settings.preferredBackendName = backendNames[selectedBackendIndex];
}
setVulkanRuntimeDisabled(
	shouldDisableVulkanForCurrentSelection(backendNames, selectedBackendIndex));
settings.graphSize = static_cast<size_t>(contextSize);

engineReady = ggml.setup(settings);
if (engineReady) {
	engineStatus = "Ready (" + ggml.getBackendName() + ")";
	devices = ggml.listDevices();
	lastBackendUsed = ggml.getBackendName();
	// Refresh backend names from discovered devices.
	backendNames.clear();
	for (const auto & d : devices) {
		backendNames.push_back(d.name);
	}
	// Update the dropdown to match the *actual* running backend.
	// The engine may have fallen back to CPU if the requested GPU
	// backend could not be initialised.
	syncSelectedBackendIndex();
} else {
	engineStatus = "Failed to initialize ggml engine";
	devices.clear();
}

	logWithLevel(OF_LOG_NOTICE, "Backend reinitialized: " + engineStatus);
}

void ofApp::syncSelectedBackendIndex() {
	if (backendNames.empty()) {
		selectedBackendIndex = 0;
		return;
	}
	std::string actualName = ggml.getBackendName();
	int matchIdx = -1;
	for (int i = 0; i < static_cast<int>(backendNames.size()); i++) {
		if (backendNames[i] == actualName) {
			matchIdx = i;
			break;
		}
	}
	selectedBackendIndex = (matchIdx >= 0) ? matchIdx : 0;
}

ofxGgmlRealtimeInfoSettings ofApp::buildLiveContextSettings(
	const std::string & rawUrls,
	const std::string & heading,
	bool enableAutoLiveContext) const {
	ofxGgmlRealtimeInfoSettings settings;
	settings.heading = heading;
	settings.explicitUrls = extractHttpUrls(rawUrls);
	settings.allowPromptUrlFetch = liveContextAllowPromptUrls;
	settings.allowDomainProviders = liveContextAllowDomainProviders;
	settings.allowGenericSearch = liveContextAllowGenericSearch;

	switch (liveContextMode) {
	case LiveContextMode::Offline:
		settings.enabled = false;
		settings.explicitUrls.clear();
		settings.requestCitations = false;
		settings.allowPromptUrlFetch = false;
		settings.allowDomainProviders = false;
		settings.allowGenericSearch = false;
		break;
	case LiveContextMode::LoadedSourcesOnly:
		settings.enabled = false;
		settings.requestCitations = true;
		settings.allowPromptUrlFetch = false;
		settings.allowDomainProviders = false;
		settings.allowGenericSearch = false;
		break;
	case LiveContextMode::LiveContext:
		settings.enabled = enableAutoLiveContext;
		settings.requestCitations = false;
		break;
	case LiveContextMode::LiveContextStrictCitations:
		settings.enabled = enableAutoLiveContext;
		settings.requestCitations = true;
		break;
	}

	return settings;
}

void ofApp::runInference(AiMode mode, const std::string & userText,
	const std::string & systemPrompt,
	const std::string & overridePrompt,
	const ofxGgmlRealtimeInfoSettings & realtimeSettings) {
if (generating.load() || !engineReady) return;
if (mode == AiMode::Script) {
lastScriptRequest = userText;
}

const int chatLanguageIndexSnapshot = chatLanguageIndex;
const int selectedLanguageIndexSnapshot = selectedLanguageIndex;
const int translateSourceLangSnapshot = translateSourceLang;
const int translateTargetLangSnapshot = translateTargetLang;
const int selectedScriptFileIndexSnapshot = selectedScriptFileIndex;
const bool scriptIncludeRepoContextSnapshot = scriptIncludeRepoContext;
const auto chatLanguagesSnapshot = chatLanguages;
const auto scriptLanguagesSnapshot = scriptLanguages;
const auto translateLanguagesSnapshot = translateLanguages;

generating.store(true);
cancelRequested.store(false);
activeGenerationMode = mode;
generationStartTime = ofGetElapsedTimef();

{
std::lock_guard<std::mutex> lock(streamMutex);
streamingOutput.clear();
}

// Detach previous thread if any.
if (workerThread.joinable()) {
workerThread.join();
}

	workerThread = std::thread([this, mode, userText, systemPrompt, overridePrompt, realtimeSettings,
		chatLanguageIndexSnapshot, selectedLanguageIndexSnapshot,
		translateSourceLangSnapshot, translateTargetLangSnapshot,
		selectedScriptFileIndexSnapshot, scriptIncludeRepoContextSnapshot,
		chatLanguagesSnapshot, scriptLanguagesSnapshot, translateLanguagesSnapshot]() {
 try {
 const bool preserveLlamaInstructions = (mode == AiMode::Script);
 ofxGgmlRealtimeInfoSettings effectiveRealtimeSettings = realtimeSettings;
 if (liveContextMode == LiveContextMode::Offline) {
	effectiveRealtimeSettings.enabled = false;
	effectiveRealtimeSettings.explicitUrls.clear();
 } else if (mode == AiMode::Script &&
	scriptSource.getSourceType() == ofxGgmlScriptSourceType::Internet) {
	effectiveRealtimeSettings.heading = "Context fetched from loaded sources";
	effectiveRealtimeSettings.explicitUrls = scriptSource.getInternetUrls();
	effectiveRealtimeSettings.allowPromptUrlFetch = false;
	effectiveRealtimeSettings.allowDomainProviders = false;
	effectiveRealtimeSettings.allowGenericSearch = false;
	effectiveRealtimeSettings.enabled =
		(liveContextMode == LiveContextMode::LiveContext ||
		 liveContextMode == LiveContextMode::LiveContextStrictCitations);
	effectiveRealtimeSettings.requestCitations =
		(liveContextMode == LiveContextMode::LoadedSourcesOnly ||
		 liveContextMode == LiveContextMode::LiveContextStrictCitations);
 }

 auto buildPromptForCurrentMode = [&](const std::string & text) {
	switch (mode) {
	case AiMode::Chat: {
		ofxGgmlChatAssistantRequest request;
		request.userText = text;
		request.systemPrompt = systemPrompt;
		if (chatLanguageIndexSnapshot > 0 &&
			chatLanguageIndexSnapshot < static_cast<int>(chatLanguagesSnapshot.size())) {
			request.responseLanguage =
				chatLanguagesSnapshot[static_cast<size_t>(chatLanguageIndexSnapshot)].name;
		}
		return chatAssistant.preparePrompt(request).prompt;
	}
	case AiMode::Script: {
		ofxGgmlCodeAssistantRequest request;
		request.action = ofxGgmlCodeAssistantAction::Ask;
		request.userInput = text;
		request.lastTask = lastScriptRequest;
		request.lastOutput = scriptOutput;
		if (selectedLanguageIndexSnapshot >= 0 &&
			selectedLanguageIndexSnapshot < static_cast<int>(scriptLanguagesSnapshot.size())) {
			request.language =
				scriptLanguagesSnapshot[static_cast<size_t>(selectedLanguageIndexSnapshot)];
		}

		ofxGgmlCodeAssistantContext context;
		context.scriptSource = &scriptSource;
		context.projectMemory = &scriptProjectMemory;
		context.focusedFileIndex = selectedScriptFileIndexSnapshot;
		context.includeRepoContext = scriptIncludeRepoContextSnapshot;
		context.maxRepoFiles = kMaxScriptContextFiles;
		context.maxFocusedFileChars = kMaxFocusedFileSnippetChars;
		return scriptAssistant.preparePrompt(request, context).prompt;
	}
	case AiMode::Summarize: {
		ofxGgmlTextAssistantRequest request;
		request.task = ofxGgmlTextTask::Summarize;
		request.inputText = text;
		return textAssistant.preparePrompt(request).prompt;
	}
	case AiMode::Write: {
		ofxGgmlTextAssistantRequest request;
		request.task = ofxGgmlTextTask::Rewrite;
		request.inputText = text;
		return textAssistant.preparePrompt(request).prompt;
	}
	case AiMode::Translate: {
		ofxGgmlTextAssistantRequest request;
		request.task = ofxGgmlTextTask::Translate;
		request.inputText = text;
		if (translateSourceLangSnapshot >= 0 &&
			translateSourceLangSnapshot < static_cast<int>(translateLanguagesSnapshot.size())) {
			request.sourceLanguage =
				translateLanguagesSnapshot[static_cast<size_t>(translateSourceLangSnapshot)].name;
		}
		if (translateTargetLangSnapshot >= 0 &&
			translateTargetLangSnapshot < static_cast<int>(translateLanguagesSnapshot.size())) {
			request.targetLanguage =
				translateLanguagesSnapshot[static_cast<size_t>(translateTargetLangSnapshot)].name;
		}
		return textAssistant.preparePrompt(request).prompt;
	}
	case AiMode::Custom: {
		ofxGgmlTextAssistantRequest request;
		request.task = ofxGgmlTextTask::Custom;
		request.inputText = text;
		request.systemPrompt = systemPrompt;
		return textAssistant.preparePrompt(request).prompt;
	}
	}
	return text;
 };

 std::string prompt = overridePrompt.empty()
	? buildPromptForCurrentMode(userText)
	: overridePrompt;
 if (effectiveRealtimeSettings.enabled || !effectiveRealtimeSettings.explicitUrls.empty()) {
	prompt = ofxGgmlInference::buildPromptWithRealtimeInfo(
		prompt,
		userText,
		effectiveRealtimeSettings);
 }
 std::string result;
 std::string error;

 if (shouldLog(OF_LOG_VERBOSE)) {
 logWithLevel(OF_LOG_VERBOSE, "=== Generation started ===");
 logWithLevel(OF_LOG_VERBOSE, std::string("Mode: ") + modeLabels[static_cast<int>(mode)]);
 logWithLevel(OF_LOG_VERBOSE, "Prompt (" + ofToString(prompt.size()) + " chars):\n" + prompt);
 }

 bool promptTrimmed = false;
 const size_t estimatedTokens = prompt.size() / 3;
 const size_t maxCtxTokens = static_cast<size_t>(contextSize);
 if (estimatedTokens > maxCtxTokens) {
	prompt = clampPromptToContext(prompt, maxCtxTokens, promptTrimmed);
	if (promptTrimmed) {
		logWithLevel(OF_LOG_WARNING,
			"Prompt exceeded context budget (~" + std::to_string(estimatedTokens) +
			" tokens > " + std::to_string(maxCtxTokens) +
			"); trimmed automatically to fit.");
	}
 }

 const std::string trimmedPrompt = trim(prompt);
 std::string latestRawPartial;

 auto cleanPartialForDisplay = [&](const std::string & rawPartial) {
	if (preserveLlamaInstructions) {
		return trim(stripAnsi(rawPartial));
	}
	std::string cleaned = ofxGgmlInference::sanitizeGeneratedText(
		stripAnsi(rawPartial),
		trimmedPrompt);
	if (cleaned.empty() && stripAnsi(rawPartial).size() < trimmedPrompt.size()) {
		cleaned.clear();
	}
	return cleaned;
 };

 auto streamCb = [this, trimmedPrompt,
	lastUiUpdate = std::chrono::steady_clock::now(),
	lastOutputSize = static_cast<size_t>(0),
	&latestRawPartial, &cleanPartialForDisplay](const std::string & partial) mutable {
	latestRawPartial = partial;
	const auto now = std::chrono::steady_clock::now();
	const bool sizeAdvanced = partial.size() > lastOutputSize;
	if (!sizeAdvanced) {
		return;
	}
	if ((partial.size() - lastOutputSize) < kStreamUiMinGrowth &&
		(now - lastUiUpdate) < kStreamUiUpdateInterval) {
		return;
	}
	lastUiUpdate = now;
	lastOutputSize = partial.size();
	std::string cleaned = cleanPartialForDisplay(partial);
 std::lock_guard<std::mutex> lock(streamMutex);
 streamingOutput = cleaned;
 };

 if (!runRealInference(mode, prompt, result, error, streamCb, preserveLlamaInstructions)) {
 // If inference failed but streaming already delivered output
 // (e.g. llama-completion crashed during cleanup after producing
 // text), use the streamed data as the result instead of showing
 // an error to the user.
 std::string streamed;
 {
 std::lock_guard<std::mutex> lock(streamMutex);
 streamed = streamingOutput;
 }
 if (streamed.empty() && !latestRawPartial.empty()) {
	streamed = cleanPartialForDisplay(latestRawPartial);
 }
	const bool hardCrash = error.find("crashed:") != std::string::npos;
	if (!streamed.empty() && !hardCrash) {
		logWithLevel(
			OF_LOG_WARNING,
			"Process failed but streamed output available (" +
				ofToString(streamed.size()) + " chars), using it.");
		result = streamed;
	} else {
		logWithLevel(OF_LOG_ERROR, "Inference error: " + error);
		result = "[Error] " + error;
	}
 } else {
	if (shouldLog(OF_LOG_VERBOSE)) {
		logWithLevel(
			OF_LOG_VERBOSE,
			"Output (" + ofToString(result.size()) + " chars):\n" + result);
	}
 }

 if (shouldLog(OF_LOG_VERBOSE)) {
 logWithLevel(OF_LOG_VERBOSE, "=== Generation finished ===");
 }

	bool likelyCutoff = isLikelyCutoffOutput(result, mode);

	if (stopAtNaturalBoundary && result.rfind("[Error]", 0) != 0) {
		if (mode == AiMode::Script) {
			if (!result.empty() && result.back() != '\n') {
				size_t cut = result.find_last_of('\n');
				if (cut != std::string::npos && cut > result.size() / 2) {
					result = trim(result.substr(0, cut));
				}
			}
		} else {
			size_t best = std::string::npos;
			for (size_t i = 0; i < result.size(); i++) {
				const char c = result[i];
				if (c == '.' || c == '!' || c == '?') {
					if (i + 1 == result.size() || std::isspace(static_cast<unsigned char>(result[i + 1])) ||
						result[i + 1] == '"' || result[i + 1] == '\'') {
						best = i + 1;
					}
				}
			}
			if (best != std::string::npos && best > result.size() / 2) {
				result = trim(result.substr(0, best));
			}
		}
	}

	if (mode == AiMode::Script && autoContinueCutoff && likelyCutoff &&
		result.rfind("[Error]", 0) != 0 && !cancelRequested.load()) {
		const size_t tailChars = std::min<size_t>(result.size(), 600);
		const std::string tail = result.substr(result.size() - tailChars);
		ofxGgmlCodeAssistantRequest continuationRequest;
		continuationRequest.action = ofxGgmlCodeAssistantAction::ContinueCutoff;
		continuationRequest.userInput = tail;
		continuationRequest.lastOutput = tail;
		if (selectedLanguageIndexSnapshot >= 0 &&
			selectedLanguageIndexSnapshot < static_cast<int>(scriptLanguagesSnapshot.size())) {
			continuationRequest.language =
				scriptLanguagesSnapshot[static_cast<size_t>(selectedLanguageIndexSnapshot)];
		}
		std::string continuationPrompt =
			scriptAssistant.preparePrompt(continuationRequest, {}).prompt;
		bool contTrimmed = false;
		const size_t contEstimatedTokens = continuationPrompt.size() / 3;
		const size_t contMaxCtxTokens = static_cast<size_t>(contextSize);
		if (contEstimatedTokens > contMaxCtxTokens) {
			continuationPrompt = clampPromptToContext(continuationPrompt, contMaxCtxTokens, contTrimmed);
		}

		std::string continuationOut;
		std::string continuationErr;
		if (runRealInference(mode, continuationPrompt, continuationOut, continuationErr, nullptr, preserveLlamaInstructions) &&
			!continuationOut.empty()) {
			if (stopAtNaturalBoundary && continuationOut.back() != '\n') {
				size_t cut = continuationOut.find_last_of('\n');
				if (cut != std::string::npos && cut > continuationOut.size() / 2) {
					continuationOut = trim(continuationOut.substr(0, cut));
				}
			}
			result += "\n" + continuationOut;
			likelyCutoff = isLikelyCutoffOutput(continuationOut, mode);
			logWithLevel(OF_LOG_NOTICE, "Auto-continued Script output after cutoff detection.");
		} else if (!continuationErr.empty()) {
			logWithLevel(OF_LOG_WARNING, "Auto-continue failed: " + continuationErr);
		}
	}

{
std::lock_guard<std::mutex> lock(outputMutex);
if (!cancelRequested.load()) {
pendingOutput = result;
pendingRole = "assistant";
pendingMode = mode;
	if (mode == AiMode::Script) {
		lastScriptOutputLikelyCutoff = likelyCutoff;
		const size_t tailChars = std::min<size_t>(result.size(), 600);
		lastScriptOutputTail = result.substr(result.size() - tailChars);
	}
}
}

{
std::lock_guard<std::mutex> lock(streamMutex);
 streamingOutput.clear();
}

 } catch (const std::exception & e) {
 logWithLevel(OF_LOG_ERROR, std::string("Exception in worker thread: ") + e.what());
 std::lock_guard<std::mutex> lock(outputMutex);
 pendingOutput = std::string("[Error] Internal exception: ") + e.what();
 pendingRole = "assistant";
 pendingMode = mode;
 } catch (...) {
 logWithLevel(OF_LOG_ERROR, "Unknown exception in worker thread");
 std::lock_guard<std::mutex> lock(outputMutex);
 pendingOutput = "[Error] Unknown internal exception occurred.";
 pendingRole = "assistant";
 pendingMode = mode;
 }

generating.store(false);
});
}

void ofApp::stopGeneration() {
if (generating.load()) {
cancelRequested.store(true);
killInferenceProcess();
}
if (workerThread.joinable()) {
workerThread.join();
}
{
std::lock_guard<std::mutex> lock(streamMutex);
streamingOutput.clear();
}
generating.store(false);
}

void ofApp::applyPendingOutput() {
std::lock_guard<std::mutex> lock(outputMutex);
if (pendingOutput.empty()) return;

switch (pendingMode) {
case AiMode::Chat:
chatMessages.push_back({"assistant", pendingOutput, ofGetElapsedTimef()});
fprintf(stderr, "[ChatWindow] AI: %s\n", formatConsoleLogText(pendingOutput, true).c_str());
break;
case AiMode::Script:
scriptOutput = pendingOutput;
scriptMessages.push_back({"assistant", pendingOutput, ofGetElapsedTimef()});
if (pendingOutput.rfind("[Error]", 0) != 0) {
scriptProjectMemory.addInteraction(lastScriptRequest, pendingOutput);
}
fprintf(stderr, "[ChatWindow] Script: %s\n", formatConsoleLogText(pendingOutput).c_str());
break;
case AiMode::Summarize:
summarizeOutput = pendingOutput;
fprintf(stderr, "[ChatWindow] Summarize: %s\n", formatConsoleLogText(pendingOutput, true).c_str());
break;
case AiMode::Write:
writeOutput = pendingOutput;
fprintf(stderr, "[ChatWindow] Write: %s\n", formatConsoleLogText(pendingOutput, true).c_str());
break;
case AiMode::Translate:
translateOutput = pendingOutput;
fprintf(stderr, "[ChatWindow] Translate: %s\n", formatConsoleLogText(pendingOutput, true).c_str());
break;
case AiMode::Custom:
customOutput = pendingOutput;
fprintf(stderr, "[ChatWindow] Custom: %s\n", formatConsoleLogText(pendingOutput, true).c_str());
break;
case AiMode::Vision:
visionOutput = pendingOutput;
fprintf(stderr, "[ChatWindow] Vision: %s\n", formatConsoleLogText(pendingOutput, true).c_str());
break;
case AiMode::Speech:
speechOutput = pendingOutput;
speechDetectedLanguage = pendingSpeechDetectedLanguage;
speechTranscriptPath = pendingSpeechTranscriptPath;
speechSrtPath = pendingSpeechSrtPath;
speechSegmentCount = pendingSpeechSegmentCount;
fprintf(stderr, "[ChatWindow] Speech: %s\n", formatConsoleLogText(pendingOutput, true).c_str());
break;
}
pendingOutput.clear();
pendingSpeechDetectedLanguage.clear();
pendingSpeechTranscriptPath.clear();
pendingSpeechSrtPath.clear();
pendingSpeechSegmentCount = 0;
}

// ---------------------------------------------------------------------------
// Performance metrics window
// ---------------------------------------------------------------------------

void ofApp::drawPerformanceWindow() {
ImGui::SetNextWindowSize(ImVec2(380, 220), ImGuiCond_FirstUseEver);
if (ImGui::Begin("Performance Metrics", &showPerformance)) {
ImGui::Text("Last Computation:");
ImGui::Separator();
ImGui::Text("  Elapsed:    %.2f ms", lastComputeMs);
ImGui::Text("  Nodes:      %d", lastNodeCount);
ImGui::Text("  Backend:    %s", lastBackendUsed.empty() ? "(none)" : lastBackendUsed.c_str());
ImGui::Spacing();

ImGui::Text("Configuration:");
ImGui::Separator();
{
std::string prefLabel = "(none)";
if (selectedBackendIndex >= 0 &&
	selectedBackendIndex < static_cast<int>(backendNames.size())) {
	prefLabel = backendNames[selectedBackendIndex];
}
ImGui::Text("  Preference: %s", prefLabel.c_str());
}
ImGui::Text("  Threads:    %d", numThreads);
ImGui::Text("  Context:    %d", contextSize);
ImGui::Text("  Batch:      %d", batchSize);
ImGui::Text("  Text path:  %s",
	textInferenceBackend == TextInferenceBackend::LlamaServer
		? "llama-server"
		: "CLI");
if (detectedModelLayers > 0) {
ImGui::Text("  GPU Layers: %d / %d", gpuLayers, detectedModelLayers);
} else {
ImGui::Text("  GPU Layers: %d", gpuLayers);
}
ImGui::Text("  Seed:       %s", seed < 0 ? "random" : ofToString(seed).c_str());
ImGui::Spacing();

ImGui::Text("Sampling:");
ImGui::Separator();
ImGui::Text("  Tokens:     %d", maxTokens);
ImGui::Text("  Temp:       %.2f", temperature);
ImGui::Text("  Top-P:      %.2f", topP);
ImGui::Text("  Top-K:      %d", topK);
ImGui::Text("  Min-P:      %.2f", minP);
ImGui::Text("  Repeat Pen: %.2f", repeatPenalty);
ImGui::Spacing();

// Device memory summary.
if (!devices.empty()) {
ImGui::Text("Devices:");
ImGui::Separator();
for (const auto & d : devices) {
ImGui::Text("  %s (%s)", d.name.c_str(),
ofxGgmlHelpers::backendTypeName(d.type).c_str());
if (d.memoryTotal > 0) {
float usedPct = 1.0f - static_cast<float>(d.memoryFree) /
static_cast<float>(d.memoryTotal);
ImGui::SameLine();
ImGui::ProgressBar(usedPct, ImVec2(100, 14),
ofxGgmlHelpers::formatBytes(d.memoryTotal).c_str());
}
}
}

if (ImGui::Button("Refresh Devices")) {
devices = ggml.listDevices();
}
}
ImGui::End();
}

// ---------------------------------------------------------------------------
// Theme application
// ---------------------------------------------------------------------------

void ofApp::applyTheme(int index) {
switch (index) {
case 0:  // Dark
ImGui::StyleColorsDark();
break;
case 1:  // Light
ImGui::StyleColorsLight();
break;
case 2:  // Classic
ImGui::StyleColorsClassic();
break;
default:
ImGui::StyleColorsDark();
break;
}
}

// ---------------------------------------------------------------------------
// Clipboard helper
// ---------------------------------------------------------------------------

void ofApp::copyToClipboard(const std::string & text) {
ImGui::SetClipboardText(text.c_str());
}

// ---------------------------------------------------------------------------
// Export chat history to a Markdown file
// ---------------------------------------------------------------------------

void ofApp::exportChatHistory(const std::string & path) {
std::ofstream out(path);
if (!out.is_open()) return;

out << "# Chat Export\n\n";
for (const auto & msg : chatMessages) {
	if (msg.role == "user") {
		out << "**User:** " << msg.text << "\n\n";
	} else if (msg.role == "assistant") {
		out << "**Assistant:** " << msg.text << "\n\n";
	} else {
		out << "**" << msg.role << ":** " << msg.text << "\n\n";
	}
}
}
