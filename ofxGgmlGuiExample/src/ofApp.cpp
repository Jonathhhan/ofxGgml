#include "ofApp.h"

#include "ofJson.h"

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
"Chat", "Script", "Summarize", "Write", "Translate", "Custom"
};

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

static const std::array<LogLevelOption, 5> kLogLevelOptions = {{
	{"Silent",   OF_LOG_SILENT},
	{"Errors",   OF_LOG_ERROR},
	{"Warnings", OF_LOG_WARNING},
	{"Notices",  OF_LOG_NOTICE},
	{"Verbose",  OF_LOG_VERBOSE}
}};

static constexpr TokenLiteral kPreambleMarkers[] = {
	{"Running in interactive mode", 27},
	{"Press Ctrl+C to interject", 24},
	{"Press Return to return control to the AI", 39},
	{"To return control without starting a new line", 43},
	{"If you want to submit another line", 34},
	{"Not using system message", 24},
	{"Using system message", 20},
	{"Reverse prompt", 14},
};

static constexpr TokenLiteral kRoleLabels[] = {
	{"user", 4},
	{"assistant", 9},
	{"system", 6},
	{"User", 4},
	{"Assistant", 9},
	{"System", 6},
	{"A:", 2},
	{"> ", 2},
};

static constexpr TokenLiteral kTrailingArtifacts[] = {
	{"> EOF by user", 13},
	{"> EOF", 5},
	{"EOF", 3},
	{"Interrupted by user", 19},
};

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

std::string fetchUrlContentLimited(const std::string & url, size_t maxChars) {
	if (url.empty()) return "";
	ofHttpResponse resp = ofLoadURL(url);
	if (resp.status < 200 || resp.status >= 300) return "";
	std::string body = resp.data.getText();
	if (body.size() > maxChars) {
		body = body.substr(0, maxChars) + "\n...[truncated]";
	}
	return body;
}

std::string urlEncode(const std::string & value) {
	std::ostringstream escaped;
	escaped.fill('0');
	escaped << std::hex;
	for (unsigned char c : value) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			escaped << c;
		} else {
			escaped << '%' << std::setw(2) << std::uppercase << int(c);
		}
	}
	return escaped.str();
}

std::string fetchWeatherContext(const std::string & userText, size_t maxChars) {
	static const std::regex weatherLocRegex(
		R"(weather\s+(in|for)\s+([A-Za-z0-9 ,._-]+))",
		std::regex::icase);
	std::string location = "home";
	std::smatch m;
	if (std::regex_search(userText, m, weatherLocRegex) && m.size() >= 3) {
		location = trim(m[2].str());
		if (location.size() > 64) location = location.substr(0, 64);
	}
	const std::string url = "https://wttr.in/" + urlEncode(location) + "?format=3";
	return fetchUrlContentLimited(url, maxChars);
}

std::string stripHtmlTags(const std::string & html) {
	std::string out;
	out.reserve(html.size());
	bool inTag = false;
	for (char c : html) {
		if (c == '<') {
			inTag = true;
		} else if (c == '>') {
			inTag = false;
			out.push_back(' ');
		} else if (!inTag) {
			out.push_back(c);
		}
	}
	return out;
}

std::string fetchSearchSnippet(const std::string & query, size_t maxChars) {
	if (query.empty()) return "";
	const std::string url = "https://lite.duckduckgo.com/lite/?q=" + urlEncode(query);
	ofHttpResponse resp = ofLoadURL(url);
	if (resp.status < 200 || resp.status >= 300) return "";
	const std::string body = resp.data.getText();
	std::regex snippetRe(R"(<td[^>]*class=\"result-snippet\"[^>]*>([\s\S]*?)</td>)",
		std::regex::icase);
	std::smatch m;
	if (std::regex_search(body, m, snippetRe) && m.size() >= 2) {
		std::string snippet = stripHtmlTags(m[1].str());
		if (snippet.size() > maxChars) {
			snippet = snippet.substr(0, maxChars) + "\n...[truncated]";
		}
		return trim(snippet);
	}
	return "";
}

std::string buildInternetContextFromUrls(
	const std::vector<std::string> & urls,
	size_t maxUrls,
	size_t maxCharsPerUrl,
	size_t maxTotalChars,
	const std::string & heading) {
	if (urls.empty() || maxUrls == 0 || maxTotalChars == 0) return {};

	std::ostringstream ctx;
	size_t used = 0;
	size_t fetched = 0;

	for (size_t i = 0; i < urls.size() && fetched < maxUrls; i++) {
		const std::string content = fetchUrlContentLimited(urls[i], maxCharsPerUrl);
		if (content.empty()) continue;
		std::string clipped = content;
		if (used + clipped.size() > maxTotalChars) {
			clipped = clipped.substr(0, maxTotalChars - used) + "\n...[truncated]";
		}
		ctx << "\nURL: " << urls[i] << "\n" << clipped << "\n";
		used += clipped.size();
		fetched++;
		if (used >= maxTotalChars) break;
	}

	if (used == 0) return {};
	return "\n\n" + heading + ":" + ctx.str();
}

std::string buildInternetContextFromText(const std::string & userText, bool allowSearchFallback) {
	static constexpr size_t kMaxUrls = 3;
	static constexpr size_t kMaxCharsPerUrl = 2000;
	static constexpr size_t kMaxTotalChars = 6000;

	const auto urls = extractHttpUrls(userText);
	std::string context = buildInternetContextFromUrls(
		urls,
		kMaxUrls,
		kMaxCharsPerUrl,
		kMaxTotalChars,
		"Context fetched from URLs in your message");
	if (!context.empty() || !allowSearchFallback) {
		return context;
	}

	std::string lowered = userText;
	std::transform(lowered.begin(), lowered.end(), lowered.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	std::string autoContext;
	if (lowered.find("weather") != std::string::npos) {
		autoContext = fetchWeatherContext(userText, 512);
	}
	if (autoContext.empty()) {
		autoContext = fetchSearchSnippet(userText, 1200);
	}
	if (autoContext.empty()) return {};
	return "\n\nInternet context:\n" + autoContext + "\n";
}

// Remove the llama.cpp interactive banner/instruction block that can
// precede the actual model response when the CLI runs in chat mode.
std::string stripInteractivePreamble(const std::string & text) {
	bool skipping = true;
	size_t pos = 0;
	while (pos <= text.size()) {
		const size_t end = text.find('\n', pos);
		const std::string line = (end == std::string::npos)
			? text.substr(pos)
			: text.substr(pos, end - pos);
		const std::string trimmed = trim(line);

		if (skipping) {
			if (!trimmed.empty()) {
				bool isPreamble = false;
				for (const auto & marker : kPreambleMarkers) {
					if (trimmed.find(marker.text) != std::string::npos) {
						isPreamble = true;
						break;
					}
				}
				if (!isPreamble) {
					return trim(text.substr(pos));
				}
			}
		}

		if (end == std::string::npos) break;
		pos = end + 1;
	}

	return "";
}

// Strip common chat-template role markers and prompt artefacts that
// llama-completion may emit around the actual generated text.
// Examples of markers removed: "user", "assistant", "system",
// "<|...|>" ChatML tokens, and leading/trailing ">" prompt chars.
std::string cleanChatOutput(const std::string & text) {
	std::string out = text;

	// Drop llama-cli interactive banners before further cleanup.
	out = stripInteractivePreamble(out);

	// Strip <|...|> ChatML-style tokens (e.g. <|assistant|>, <|end|>).
	{
		std::string cleaned;
		cleaned.reserve(out.size());
		size_t i = 0;
		while (i < out.size()) {
			if (i + 1 < out.size() && out[i] == '<' && out[i + 1] == '|') {
				size_t end = out.find("|>", i + 2);
				if (end != std::string::npos) {
					i = end + 2;
					continue;
				}
			}
			cleaned += out[i];
			i++;
		}
		out = cleaned;
	}

	// Strip leading lines that are just a role label or prompt marker.
	// The llama chat template may prepend "user\n", "assistant\n", etc.
	auto startsWithWord = [](const std::string & s, const std::string & word) {
		if (s.size() < word.size()) return false;
		if (s.compare(0, word.size(), word) != 0) return false;
		if (s.size() == word.size()) return true;
		const char after = s[word.size()];
		return after == '\n' || after == '\r' || after == ':' || after == ' ';
	};

	bool changed = true;
	while (changed) {
		changed = false;
		out = trim(out);
		for (const auto & label : kRoleLabels) {
			if (startsWithWord(out, label.text)) {
				out = out.substr(label.len);
				// Also strip an optional ':' delimiter after the role label.
				out = trim(out);
				if (!out.empty() && out[0] == ':') {
					out = out.substr(1);
				}
				out = trim(out);
				changed = true;
				break;
			}
		}
		// Strip a bare leading ">" (without trailing space — the "> "
		// entry in roleLabels handles the space case).
		if (!out.empty() && out[0] == '>') {
			out = trim(out.substr(1));
			changed = true;
		}
	}

	// Strip trailing ">" prompt markers.
	while (!out.empty() && out.back() == '>') {
		out.pop_back();
	}
	out = trim(out);

	// Strip trailing CLI artifacts: "> EOF", "EOF", "Interrupted by user".
	{
		bool stripped = true;
		while (stripped) {
			stripped = false;
			for (const auto & art : kTrailingArtifacts) {
				if (out.size() >= art.len &&
					out.compare(out.size() - art.len, art.len, art.text) == 0) {
					out = trim(out.substr(0, out.size() - art.len));
					stripped = true;
				}
			}
		}
	}

	// Strip stray literal ANSI codes or terminal markers that bypassed parser
	const char* const strayArtifacts[] = {"[1m", "[32m", "[0m", "> "};
	for (const char* art : strayArtifacts) {
		size_t pos = 0;
		while ((pos = out.find(art, pos)) != std::string::npos) {
			out.erase(pos, std::strlen(art));
		}
	}
	out = trim(out);

	return out;
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

std::string buildCutoffContinuationRequest(const std::string & tailText) {
	return
		"Continue exactly from where the previous answer stopped. "
		"Do not restart. Finish the incomplete thought/code naturally.\n\n"
		"Tail of previous output:\n" + tailText;
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
constexpr size_t kMaxReviewTocFiles = 50;
constexpr size_t kMaxEmbeddingSnippetChars = 4000;
constexpr size_t kMaxEmbeddingParallelTasks = 4;
constexpr size_t kMaxSummaryParallelTasks = 3;
constexpr size_t kMaxInternetSourceUrls = 3;
constexpr size_t kMaxInternetCharsPerSourceUrl = 1500;
constexpr size_t kMaxInternetCharsFromSourceUrls = 4500;
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

	// 2. Try bare names via PATH (execvp search).
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

	// 3. Try common installation directories.
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
		const char * progFiles = std::getenv("ProgramFiles");
		if (progFiles) {
			searchDirs.push_back(std::string(progFiles) + "\\llama.cpp");
			searchDirs.push_back(std::string(progFiles) + "\\LlamaCpp");
		}
		const char * localAppData = std::getenv("LOCALAPPDATA");
		if (localAppData) {
			searchDirs.push_back(std::string(localAppData) + "\\llama.cpp");
		}
#else
		// Common POSIX install directories that may not be in PATH
		// when launched from a GUI / IDE.
		searchDirs.push_back("/usr/local/bin");
		searchDirs.push_back("/usr/bin");
#ifdef __APPLE__
		searchDirs.push_back("/opt/homebrew/bin");
#endif
		const char * home = std::getenv("HOME");
		if (home) {
			searchDirs.push_back(std::string(home) + "/.local/bin");
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
			if (logger) logger(OF_LOG_NOTICE, "llama-completion/llama-cli/llama not found in PATH or common directories.");
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

	cliSupportsTopK = hasTopK;
	cliSupportsMinP = hasMinP;
	cliSupportsMirostat = hasMirostat;

	if (!cliSupportsTopK) {
		logWithLevel(OF_LOG_WARNING, "Detected CLI does not support --top-k; Top-K will be ignored.");
	}
	if (!cliSupportsMinP) {
		logWithLevel(OF_LOG_WARNING, "Detected CLI does not support --min-p; Min-P will be ignored.");
	}
	if (!cliSupportsMirostat) {
		logWithLevel(OF_LOG_WARNING, "Detected CLI does not support Mirostat flags; Mirostat settings will be ignored.");
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
			}
		};
		// Match the CLI defaults: preset 1 for most modes, preset 2 for Script.
		taskDefaultModelIndices = {0, 1, 0, 0, 0, 0};
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
scriptLanguages = {
{"C++",        ".cpp",  "You are a C++ expert. Generate modern C++17 code."},
{"Python",     ".py",   "You are a Python expert. Generate clean, idiomatic Python 3 code."},
{"JavaScript", ".js",   "You are a JavaScript expert. Generate modern ES6+ code."},
{"Rust",       ".rs",   "You are a Rust expert. Generate safe, idiomatic Rust code."},
{"GLSL",       ".glsl", "You are a GLSL shader expert. Generate efficient GPU shader code."},
{"Go",         ".go",   "You are a Go expert. Generate idiomatic Go code."},
{"Bash",       ".sh",   "You are a Bash scripting expert. Generate portable shell scripts."},
{"TypeScript", ".ts",   "You are a TypeScript expert. Generate type-safe TypeScript code."}
};
}

// ---------------------------------------------------------------------------
// Presets — prompt templates for Custom panel
// ---------------------------------------------------------------------------

void ofApp::initPromptTemplates() {
promptTemplates = {
{"Code Reviewer",
"You are an expert code reviewer. Analyze the provided code for bugs, "
"security issues, performance problems, and style improvements. "
"Provide specific, actionable feedback with line references."},
{"Technical Writer",
"You are a technical documentation writer. Generate clear, well-structured "
"documentation with examples, parameters, return values, and usage notes."},
{"Data Analyst",
"You are a data analysis expert. Help interpret data, suggest statistical "
"methods, write queries, and explain results in plain language."},
{"System Architect",
"You are a software architect. Design systems with clear component "
"boundaries, data flows, and technology choices. Consider scalability, "
"reliability, and maintainability."},
{"Debugger",
"You are an expert debugger. Analyze error messages, stack traces, and "
"code to identify root causes. Suggest specific fixes and explain why "
"the bug occurs."},
{"Test Engineer",
"You are a test engineering expert. Generate comprehensive test cases "
"including unit tests, edge cases, error paths, and integration scenarios. "
"Use the appropriate testing framework for the language."},
{"Translator",
"You are a code translator. Convert code between programming languages "
"while preserving logic, idioms, and best practices of the target language."},
{"Optimizer",
"You are a performance optimization expert. Analyze code for bottlenecks, "
"memory issues, and algorithmic improvements. Suggest concrete optimizations "
"with expected impact."},
};
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
		scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExt);
}

// Default branch for GitHub.
std::strncpy(scriptSourceBranch, "main", sizeof(scriptSourceBranch) - 1);
scriptSourceBranch[sizeof(scriptSourceBranch) - 1] = '\0';

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

// Pre-fill example system prompt only if not restored from session.
if (customSystemPrompt[0] == '\0') {
std::strncpy(customSystemPrompt,
"You are a helpful assistant. Respond concisely.", sizeof(customSystemPrompt) - 1);
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
stopGeneration();
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
}
}

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();

// Model preset selector.
ImGui::Text("Model:");
ImGui::SetNextItemWidth(-1);
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

	const std::string modelPath = getSelectedModelPath();
	if (!modelPath.empty()) {
		std::error_code modelEc;
		if (std::filesystem::exists(modelPath, modelEc) && !modelEc) {
			ImGui::TextDisabled("File: %s", modelPath.c_str());
		} else {
			ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f),
				"Model missing: %s", ofFilePath::getFileName(modelPath).c_str());
			ImGui::TextDisabled("Place file at: %s", modelPath.c_str());
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
ImGui::Checkbox("Offline mode", &strictOfflineMode);

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
ImGui::SliderInt("##Threads", &numThreads, 1, 32, "Threads: %d");
ImGui::SetNextItemWidth(-1);
ImGui::SliderInt("##ContextSize", &contextSize, 256, 16384, "Context: %d");
ImGui::SetNextItemWidth(-1);
ImGui::SliderInt("##BatchSize", &batchSize, 32, 4096, "Batch: %d");

if (!backendNames.empty()) {
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
}

ImGui::SetNextItemWidth(-1);
{
// GPU layers control the llama-completion CLI process, which has
// its own GPU support — always allow the user to adjust them.
int sliderMax = detectedModelLayers > 0 ? detectedModelLayers : 128;
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
}
}
ImGui::End();
}

// ---------------------------------------------------------------------------
// Chat panel
// ---------------------------------------------------------------------------

void ofApp::drawChatPanel() {
drawPanelHeader("Chat", "conversation with the ggml engine");

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
ImGui::BeginDisabled(strictOfflineMode);
ImGui::Checkbox("All modes", &internetContextAllModes);
if (ImGui::IsItemHovered()) {
	ImGui::SetTooltip("When enabled, internet grounding can be applied to non-chat modes too. Chat mode is grounded by default unless Offline mode is enabled.");
}
ImGui::EndDisabled();
if (strictOfflineMode) {
	ImGui::SameLine();
	ImGui::TextDisabled("(offline)");
}

if ((submitted || sendClicked) && std::strlen(chatInput) > 0 && !generating.load()) {
std::string userText(chatInput);
chatMessages.push_back({"user", userText, ofGetElapsedTimef()});
fprintf(stderr, "[ChatWindow] You: %s\n", userText.c_str());
std::memset(chatInput, 0, sizeof(chatInput));
runInference(AiMode::Chat, userText);
}

// Copy / Clear / Export row.
if (!chatMessages.empty()) {
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
			scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExt);
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
			scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExt);
	}
	scriptSource.setLocalFolder(result.getPath());
}
}
if (isLocal) ImGui::PopStyleColor();
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

	if (sourceType == ofxGgmlScriptSourceType::GitHubRepo ||
		sourceType == ofxGgmlScriptSourceType::Internet) {
		drawScriptSourcePanel();
	}

	ImGui::Spacing();

	// --- 3. Chat History (dynamic size) ---
	ImGui::Text("Coding Chat:");
	ImGui::BeginChild("##ScriptChatHistory", ImVec2(-1, -120), true);
	for (const auto & msg : scriptMessages) {
		if (msg.role == "user") {
			ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "You:");
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

auto buildScriptPrompt = [this, sourceType, &scriptSourceFiles](const std::string & body) {
std::string prompt;
if (!scriptLanguages.empty()) {
prompt = scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].systemPrompt + "\n";
}

// If a folder or GitHub repo is loaded, provide context about available files
if (scriptIncludeRepoContext && sourceType != ofxGgmlScriptSourceType::None && !scriptSourceFiles.empty()) {
	if (sourceType == ofxGgmlScriptSourceType::LocalFolder) {
		const std::string folderPath = scriptSource.getLocalFolderPath();
		prompt += "\nContext: Loaded folder: " + folderPath + "\n";
		prompt += "Available files in this folder:\n";
	} else if (sourceType == ofxGgmlScriptSourceType::GitHubRepo) {
		const std::string ownerRepo = scriptSource.getGitHubOwnerRepo();
		const std::string branch = scriptSource.getGitHubBranch();
		prompt += "\nContext: Loaded GitHub repository: " + ownerRepo + " (branch: " + branch + ")\n";
		prompt += "Available files in this repository:\n";
	} else {
		prompt += "\nContext: Loaded internet sources (URLs):\n";
	}
	// List up to a bounded number of files to avoid overly long prompts
	const size_t maxFilesToList = kMaxScriptContextFiles;
	size_t fileCount = 0;
	for (const auto & entry : scriptSourceFiles) {
		if (!entry.isDirectory) {
			prompt += "  - " + entry.name + "\n";
			fileCount++;
			if (fileCount >= maxFilesToList) {
				if (scriptSourceFiles.size() > maxFilesToList) {
					prompt += "  ... and " + std::to_string(scriptSourceFiles.size() - maxFilesToList) + " more files\n";
				}
				break;
			}
		}
	}
	prompt += "\n";

	if (selectedScriptFileIndex >= 0 &&
		selectedScriptFileIndex < static_cast<int>(scriptSourceFiles.size())) {
		const auto & selected = scriptSourceFiles[static_cast<size_t>(selectedScriptFileIndex)];
		if (!selected.isDirectory) {
			std::string selectedContent;
			if (scriptSource.loadFileContent(selectedScriptFileIndex, selectedContent)) {
				const size_t maxSelectedSnippet = kMaxFocusedFileSnippetChars;
				if (selectedContent.size() > maxSelectedSnippet) {
					selectedContent = selectedContent.substr(0, maxSelectedSnippet) + "\n...[truncated]";
				}
				prompt += "Focused file: " + selected.name + "\n";
				prompt += "Focused file snippet:\n" + selectedContent + "\n\n";
			}
		}
	}
}

prompt += body;
return prompt;
};

const bool hasSelectedFile =
	selectedScriptFileIndex >= 0 &&
	selectedScriptFileIndex < static_cast<int>(scriptSourceFiles.size()) &&
	!scriptSourceFiles[static_cast<size_t>(selectedScriptFileIndex)].isDirectory;
const bool hasUserInput = std::strlen(scriptInput) > 0;

auto buildScriptActionBody = [&](const std::string & defaultForFile,
	const std::string & prefixForInput) {
	if (hasSelectedFile) {
		if (hasUserInput) {
			return defaultForFile + "\n\nExtra instructions:\n" + std::string(scriptInput);
		}
		return defaultForFile;
	}
	return prefixForInput + std::string(scriptInput);
};

struct ScriptActionSpec {
	const char * label;
	ImVec2 size;
	const char * defaultForFile;
	const char * prefixForInput;
	bool plainInput;
};

const ScriptActionSpec actionSpecs[] = {
	{ "Generate",    ImVec2(100, 0), "Generate improved code for the focused file.", "", true },
	{ "Explain",     ImVec2(100, 0), "Explain the focused file code.", "Explain the following code:\n", false },
	{ "Debug",       ImVec2(100, 0), "Find bugs in the focused file code.", "Find bugs in the following code:\n", false },
	{ "Optimize",    ImVec2(100, 0), "Optimize the focused file code for performance. Show the improved version and explain what changed.", "Optimize the following code for performance. Show the improved version and explain what changed:\n", false },
	{ "Refactor",    ImVec2(100, 0), "Refactor the focused file code to improve readability, maintainability, and structure. Show the refactored version.", "Refactor the following code to improve readability, maintainability, and structure. Show the refactored version:\n", false },
	{ "Review Mode", ImVec2(100, 0), "Review the focused file code for bugs, security issues, and style. Provide specific feedback.", "Review the following code for bugs, security issues, and style. Provide specific feedback:\n", false },
};

bool canSendScriptChat = !generating.load() && hasUserInput;

ImGui::BeginDisabled(generating.load());
if (canSendScriptChat) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.3f, 1.0f));
if ((ImGui::Button("Send to Chat", ImVec2(110, 0)) || scriptChatSubmitted) && canSendScriptChat) {
	const std::string message(scriptInput);
	scriptMessages.push_back({"user", message, ofGetElapsedTimef()});
	runInference(AiMode::Script, buildScriptPrompt(message));
	std::memset(scriptInput, 0, sizeof(scriptInput));
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
	const std::string continuation = buildCutoffContinuationRequest(lastScriptOutputTail);
	scriptMessages.push_back({"user", "Continue from cutoff.", ofGetElapsedTimef()});
	runInference(AiMode::Script, buildScriptPrompt(continuation));
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
		ImGui::SetTooltip("Run embedding-powered, multi-pass review over the loaded folder/repository");
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
ImGui::BeginDisabled(generating.load() || (!hasUserInput && !hasSelectedFile));
for (size_t i = 0; i < std::size(actionSpecs); i++) {
	const auto & spec = actionSpecs[i];
	if (ImGui::Button(spec.label, spec.size)) {
		std::string body;
		if (spec.plainInput) {
			body = hasUserInput ? std::string(scriptInput) : std::string(spec.defaultForFile);
		} else {
			body = buildScriptActionBody(spec.defaultForFile, spec.prefixForInput);
		}
		
		std::string chatActionLabel = std::string(spec.label) + (hasSelectedFile ? " focused file." : "");
		if (hasUserInput) chatActionLabel += " Instructions: " + std::string(scriptInput);
		
		scriptMessages.push_back({"user", chatActionLabel, ofGetElapsedTimef()});
		runInference(AiMode::Script, buildScriptPrompt(body));
		std::memset(scriptInput, 0, sizeof(scriptInput));
	}
	if (i + 1 < std::size(actionSpecs)) {
		ImGui::SameLine();
	}
}
ImGui::EndDisabled();

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
		std::string followup = "Continue the task from the previous response. Keep the same intent and provide next concrete steps.\n\n";
		if (hasScriptOutput) followup += "Previous response:\n" + scriptOutput;
		scriptMessages.push_back({"user", "Continue the previous task.", ofGetElapsedTimef()});
		runInference(AiMode::Script, buildScriptPrompt(followup));
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Continue from the latest coding response without rewriting your full prompt.");
	ImGui::SameLine();
	if (ImGui::Button("Shorter", ImVec2(80, 0))) {
		std::string req = hasLastTask ? lastScriptRequest : std::string("Rewrite the previous response");
		req += "\n\nProvide a shorter answer. Keep only essential code and brief explanation.";
		scriptMessages.push_back({"user", "Provide a shorter answer for the previous task.", ofGetElapsedTimef()});
		runInference(AiMode::Script, buildScriptPrompt(req));
	}
	ImGui::SameLine();
	if (ImGui::Button("More Detail", ImVec2(90, 0))) {
		std::string req = hasLastTask ? lastScriptRequest : std::string("Expand the previous response");
		req += "\n\nProvide a more detailed answer with reasoning, edge cases, and step-by-step implementation notes.";
		scriptMessages.push_back({"user", "Provide more detail for the previous task.", ofGetElapsedTimef()});
		runInference(AiMode::Script, buildScriptPrompt(req));
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
			size_t maxLen = sizeof(scriptInput) - 1;
			std::strncpy(scriptInput, lastScriptRequest.c_str(), maxLen);
			scriptInput[maxLen] = '\0';
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

if (sourceType == ofxGgmlScriptSourceType::GitHubRepo) {
	ImGui::TextColored(ImVec4(0.6f, 0.7f, 1.0f, 1.0f), "GitHub Repository:");
	ImGui::SetNextItemWidth(250);
	ImGui::InputText("##GHRepo", scriptSourceGitHub, sizeof(scriptSourceGitHub));
	ImGui::SameLine();
	ImGui::Text("Branch:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(100);
	ImGui::InputText("##GHBranch", scriptSourceBranch, sizeof(scriptSourceBranch));
	ImGui::SameLine();
	if (ImGui::Button("Fetch", ImVec2(60, 0))) {
		if (std::strlen(scriptSourceGitHub) > 0) {
			std::string branch = std::strlen(scriptSourceBranch) > 0
				? std::string(scriptSourceBranch) : "main";
			selectedScriptFileIndex = -1;
			if (!scriptLanguages.empty()) {
				scriptSource.setPreferredExtension(
					scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExt);
			}
			if (scriptSource.setGitHubRepo(scriptSourceGitHub, branch)) {
				scriptSource.fetchGitHubRepo();
			}
		}
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
ext = scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExt;
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

ImGui::Text("Paste text to summarize:");
ImGui::InputTextMultiline("##SumIn", summarizeInput, sizeof(summarizeInput),
ImVec2(-1, 150));

ImGui::BeginDisabled(generating.load() || std::strlen(summarizeInput) == 0);
if (ImGui::Button("Summarize", ImVec2(140, 0))) {
runInference(AiMode::Summarize, summarizeInput);
}
ImGui::SameLine();
if (ImGui::Button("Key Points", ImVec2(140, 0))) {
std::string prompt = std::string("Extract key points from:\n") + summarizeInput;
runInference(AiMode::Summarize, prompt);
}
ImGui::SameLine();
if (ImGui::Button("TL;DR", ImVec2(140, 0))) {
std::string prompt = std::string("Give a one-sentence TL;DR of:\n") + summarizeInput;
runInference(AiMode::Summarize, prompt);
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

ImGui::Text("Enter your text:");
ImGui::InputTextMultiline("##WriteIn", writeInput, sizeof(writeInput),
ImVec2(-1, 120));

ImGui::BeginDisabled(generating.load() || std::strlen(writeInput) == 0);
if (ImGui::Button("Rewrite", ImVec2(110, 0))) {
std::string prompt = std::string("Rewrite the following more clearly:\n") + writeInput;
runInference(AiMode::Write, prompt);
}
ImGui::SameLine();
if (ImGui::Button("Expand", ImVec2(110, 0))) {
std::string prompt = std::string("Expand on the following:\n") + writeInput;
runInference(AiMode::Write, prompt);
}
ImGui::SameLine();
if (ImGui::Button("Make Formal", ImVec2(110, 0))) {
std::string prompt = std::string("Make this text more formal:\n") + writeInput;
runInference(AiMode::Write, prompt);
}
ImGui::SameLine();
if (ImGui::Button("Make Casual", ImVec2(110, 0))) {
std::string prompt = std::string("Make this text more casual:\n") + writeInput;
runInference(AiMode::Write, prompt);
}
ImGui::SameLine();
if (ImGui::Button("Fix Grammar", ImVec2(110, 0))) {
std::string prompt = std::string("Fix grammar and spelling in:\n") + writeInput;
runInference(AiMode::Write, prompt);
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

// ---------------------------------------------------------------------------
// Translate panel
// ---------------------------------------------------------------------------

static const char * kTranslateLanguages[] = {
"English", "Spanish", "French", "German", "Italian",
"Portuguese", "Chinese", "Japanese", "Korean", "Russian",
"Arabic", "Hindi", "Dutch", "Swedish", "Polish"
};
static constexpr int kTranslateLangCount = static_cast<int>(
sizeof(kTranslateLanguages) / sizeof(kTranslateLanguages[0]));

void ofApp::drawTranslatePanel() {
drawPanelHeader("Translate", "translate text between languages");

ImGui::Text("Source language:");
ImGui::SameLine();
ImGui::SetNextItemWidth(160);
ImGui::Combo("##SrcLang", &translateSourceLang, kTranslateLanguages, kTranslateLangCount);
ImGui::SameLine();
ImGui::Text("  Target language:");
ImGui::SameLine();
ImGui::SetNextItemWidth(160);
ImGui::Combo("##TgtLang", &translateTargetLang, kTranslateLanguages, kTranslateLangCount);
ImGui::SameLine();
if (ImGui::Button("Swap", ImVec2(60, 0))) {
std::swap(translateSourceLang, translateTargetLang);
}

ImGui::Text("Enter text to translate:");
ImGui::InputTextMultiline("##TransIn", translateInput, sizeof(translateInput),
ImVec2(-1, 120));

bool hasInput = std::strlen(translateInput) > 0;
ImGui::BeginDisabled(generating.load() || !hasInput);
if (ImGui::Button("Translate", ImVec2(110, 0))) {
std::string prompt = std::string("Translate the following from ")
+ kTranslateLanguages[translateSourceLang] + " to "
+ kTranslateLanguages[translateTargetLang] + ":\n" + translateInput;
runInference(AiMode::Translate, prompt);
}
ImGui::SameLine();
if (ImGui::Button("Detect Language", ImVec2(140, 0))) {
std::string prompt = std::string("Detect the language of the following text and explain:\n") + translateInput;
runInference(AiMode::Translate, prompt);
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
std::strncpy(customSystemPrompt, sp.c_str(), sizeof(customSystemPrompt) - 1);
customSystemPrompt[sizeof(customSystemPrompt) - 1] = '\0';
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
runInference(AiMode::Custom, customInput, customSystemPrompt);
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
if (activeMode == AiMode::Script && !scriptLanguages.empty()) {
ImGui::SameLine();
ImGui::Text(" | Lang: %s", scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].name.c_str());
}
ImGui::SameLine();
ImGui::Text(" | Tokens: %d  Temp: %.2f  Top-P: %.2f  Top-K: %d  Min-P: %.2f",
	maxTokens, temperature, topP, topK, minP);
if (strictOfflineMode) {
	ImGui::SameLine();
	ImGui::TextDisabled(" | Offline");
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
out << "useModeTokenBudgets=" << (useModeTokenBudgets ? 1 : 0) << "\n";
out << "autoContinueCutoff=" << (autoContinueCutoff ? 1 : 0) << "\n";
for (int i = 0; i < kModeCount; i++) {
	out << "modeTokenBudget" << i << "="
		<< std::clamp(modeMaxTokens[static_cast<size_t>(i)], 32, 4096) << "\n";
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

// Outputs.
out << "scriptOutput=" << escapeSessionText(scriptOutput) << "\n";
out << "summarizeOutput=" << escapeSessionText(summarizeOutput) << "\n";
out << "writeOutput=" << escapeSessionText(writeOutput) << "\n";
out << "translateOutput=" << escapeSessionText(translateOutput) << "\n";
out << "customOutput=" << escapeSessionText(customOutput) << "\n";
out << "internetContextAllModes=" << (internetContextAllModes ? 1 : 0) << "\n";
out << "strictOfflineMode=" << (strictOfflineMode ? 1 : 0) << "\n";
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

auto copyToBuf = [this](char * buf, size_t bufSize, const std::string & value) {
std::string text = unescapeSessionText(value);
std::strncpy(buf, text.c_str(), bufSize - 1);
buf[bufSize - 1] = '\0';
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
else if (key == "useModeTokenBudgets") useModeTokenBudgets = (safeStoi(value, 1) != 0);
else if (key == "autoContinueCutoff") autoContinueCutoff = (safeStoi(value, 0) != 0);
else if (key.rfind("modeTokenBudget", 0) == 0) {
	try {
		int idx = std::stoi(key.substr(std::strlen("modeTokenBudget")));
		if (idx >= 0 && idx < kModeCount) {
			modeMaxTokens[static_cast<size_t>(idx)] = std::clamp(safeStoi(value, 512), 32, 4096);
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
else if (key == "translateSourceLang") translateSourceLang = std::clamp(safeStoi(value), 0, kTranslateLangCount - 1);
else if (key == "translateTargetLang") translateTargetLang = std::clamp(safeStoi(value, 1), 0, kTranslateLangCount - 1);
else if (key == "customInput") copyToBuf(customInput, sizeof(customInput), value);
else if (key == "customSystemPrompt") copyToBuf(customSystemPrompt, sizeof(customSystemPrompt), value);
else if (key == "scriptOutput") scriptOutput = unescapeSessionText(value);
else if (key == "summarizeOutput") summarizeOutput = unescapeSessionText(value);
else if (key == "writeOutput") writeOutput = unescapeSessionText(value);
else if (key == "translateOutput") translateOutput = unescapeSessionText(value);
else if (key == "customOutput") customOutput = unescapeSessionText(value);
else if (key == "internetContextAllModes") internetContextAllModes = (safeStoi(value, 0) != 0);
else if (key == "strictOfflineMode") strictOfflineMode = (safeStoi(value, 0) != 0);
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
		scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExt);
}
if (loadedScriptSourceType == static_cast<int>(ofxGgmlScriptSourceType::LocalFolder) &&
	!loadedScriptSourcePath.empty()) {
	scriptSource.setLocalFolder(loadedScriptSourcePath);
	selectedScriptFileIndex = -1;
} else if (loadedScriptSourceType == static_cast<int>(ofxGgmlScriptSourceType::GitHubRepo)) {
	scriptSource.setGitHubMode();
	std::string ownerRepo = scriptSourceGitHub;
	std::string branch = std::strlen(scriptSourceBranch) > 0
		? std::string(scriptSourceBranch) : "main";
	if (!ownerRepo.empty()) {
		scriptSource.setGitHubRepo(ownerRepo, branch);
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

if (useModeTokenBudgets) {
	maxTokens = std::clamp(modeMaxTokens[static_cast<size_t>(activeMode)], 32, 4096);
} else {
	modeMaxTokens[static_cast<size_t>(activeMode)] = std::clamp(maxTokens, 32, 4096);
}

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

// Hierarchical review helpers (shared across passes)
namespace {

size_t countLines(const std::string & text) {
	if (text.empty()) return 0;
	return static_cast<size_t>(std::count(text.begin(), text.end(), '\n') + 1);
}

size_t estimateCyclomaticComplexity(const std::string & text) {
	static constexpr const char * tokens[] = {
		" if ", " for ", " while ", " case ", "&&", "||", "?", " catch ", " else if ",
		" switch ", " foreach ", " guard ", " when ", " except ", " elif ", " goto "
	};
	size_t score = 1; // baseline path
	std::string lower = text;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	for (const auto & tok : tokens) {
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
		std::string trimmed = trim(line);
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
		// import / require / from statements
		auto lower = trimmed;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		static constexpr const char * prefixes[] = {"import ", "from ", "require("};
		for (const auto & p : prefixes) {
			const std::string prefix(p);
			if (lower.rfind(prefix, 0) == 0) {
				size_t quote = trimmed.find('"');
				if (quote == std::string::npos) quote = trimmed.find('\'');
				if (quote != std::string::npos) {
					size_t end = trimmed.find(trimmed[quote], quote + 1);
					if (end != std::string::npos && end > quote + 1) {
						addDep(trimmed.substr(quote + 1, end - quote - 1));
					}
				} else {
					// fallback: grab next token
					std::istringstream ls(trimmed.substr(prefix.size()));
					std::string dep;
					if (ls >> dep) addDep(dep);
				}
				break;
			}
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
	static constexpr const char * testExt[] = {
		".spec", ".test", ".tests", ".stories", ".snap"
	};
	static constexpr const char * docExt[] = {
		".md", ".rst", ".txt"
	};
	static constexpr const char * configExt[] = {
		".json", ".yaml", ".yml", ".toml", ".ini"
	};

	for (const auto & ext : testExt) {
		if (lower.rfind(ext) != std::string::npos) return 0.7f;
	}
	for (const auto & ext : docExt) {
		const size_t len = std::char_traits<char>::length(ext);
		if (lower.size() >= len &&
			lower.compare(lower.size() - len, len, ext) == 0) {
			return 0.3f;
		}
	}
	for (const auto & ext : configExt) {
		const size_t len = std::char_traits<char>::length(ext);
		if (lower.size() >= len &&
			lower.compare(lower.size() - len, len, ext) == 0) {
			return 0.5f;
		}
	}
	for (const auto & ext : coreExt) {
		const size_t len = std::char_traits<char>::length(ext);
		if (lower.size() >= len &&
			lower.compare(lower.size() - len, len, ext) == 0) {
			return 1.2f;
		}
	}
	return 0.6f; // default importance for miscellaneous files
}

std::string slidingWindowText(const std::string & content, size_t maxChars) {
	if (content.size() <= maxChars) return content;
	const size_t half = maxChars / 2;
	return content.substr(0, half) + "\n...\n" + content.substr(content.size() - half);
}

} // namespace


int ofApp::countTokensAccurate(const std::string & text, int fallback) {
	const std::string modelPath = getSelectedModelPath();
	if (!modelPath.empty()) {
		int tokens = scriptReviewInference.countPromptTokens(modelPath, text);
		if (tokens >= 0) return tokens;
	}
	if (fallback >= 0) return fallback;
	if (text.empty()) return 0;
	return static_cast<int>(text.size() / 4 + 1); // conservative char->token fallback
}

std::vector<ScriptFileReviewInfo> ofApp::collectScriptFilesForReview(std::string & status) {
	std::vector<ScriptFileReviewInfo> files;
	const auto entries = scriptSource.getFiles();
	if (entries.empty()) {
		status = "No files loaded";
		return files;
	}
	files.reserve(entries.size());
	for (int i = 0; i < static_cast<int>(entries.size()); i++) {
		if (cancelRequested.load()) break;
		const auto & entry = entries[static_cast<size_t>(i)];
		if (entry.isDirectory) continue;

		std::string content;
		if (!scriptSource.loadFileContent(i, content)) {
			continue;
		}

		ScriptFileReviewInfo info;
		info.name = entry.name;
		info.fullPath = entry.fullPath;
		info.content = std::move(content);
		info.truncatedContent = info.content;
		files.push_back(std::move(info));
	}
	status = "Loaded " + ofToString(files.size()) + " files";
	return files;
}

void ofApp::computeFileHeuristics(std::vector<ScriptFileReviewInfo> & files,
	const std::string & baseFolder) {
	const bool hasGit = !baseFolder.empty();
	const auto now = std::chrono::system_clock::now();

	for (auto & f : files) {
		f.loc = countLines(f.content);
		f.complexity = estimateCyclomaticComplexity(f.content);
		f.dependencies = extractDependencies(f.content);
		f.dependencyFanOut = f.dependencies.size();
		f.importanceScore = importanceFromExtension(f.name);

		if (hasGit) {
			std::string rel = f.name;
			std::string out;
			int exitCode = -1;
			if (runProcessCapture({"git", "-C", baseFolder, "log", "-1", "--format=%ct", "--", rel},
				out, exitCode, false, nullptr, true) && exitCode == 0) {
				try {
					long ts = std::stol(trim(out));
					auto fileTime = std::chrono::system_clock::from_time_t(static_cast<time_t>(ts));
					auto age = now - fileTime;
					float days = std::chrono::duration_cast<std::chrono::hours>(age).count() / 24.0f;
					f.recencyScore = 1.0f / (1.0f + (days / 30.0f));
				} catch (...) {
					f.recencyScore = 0.2f;
				}
			} else {
				f.recencyScore = 0.2f; // unknown recency still modestly relevant
			}
		} else {
			f.recencyScore = 0.2f;
		}

		f.tokenCount = countTokensAccurate(f.truncatedContent,
			static_cast<int>(f.truncatedContent.size() / 4 + 1));
	}

	computeDependencyFanIn(files);
}

void ofApp::computeDependencyFanIn(std::vector<ScriptFileReviewInfo> & files) {
	std::unordered_map<std::string, size_t> fanIn;
	for (const auto & f : files) {
		for (const auto & dep : f.dependencies) {
			fanIn[dep]++;
			std::filesystem::path p(dep);
			fanIn[p.filename().string()]++;
		}
	}
	for (auto & f : files) {
		auto it = fanIn.find(f.name);
		if (it != fanIn.end()) f.dependencyFanIn = std::max(f.dependencyFanIn, it->second);
		const std::string base = std::filesystem::path(f.name).filename().string();
		auto itBase = fanIn.find(base);
		if (itBase != fanIn.end()) f.dependencyFanIn = std::max(f.dependencyFanIn, itBase->second);
	}
}

std::string ofApp::buildRepoTableOfContents(const std::vector<ScriptFileReviewInfo> & files,
	size_t maxFiles) const {
	if (files.empty()) return {};
	std::vector<std::string> names;
	names.reserve(files.size());
	for (const auto & f : files) names.push_back(f.name);
	std::sort(names.begin(), names.end());
	std::string toc = "Repository files (table of contents):\n";
	size_t listed = 0;
	for (const auto & n : names) {
		toc += "  - " + n + "\n";
		listed++;
		if (listed >= maxFiles) break;
	}
	if (names.size() > maxFiles) {
		toc += "  ... and " + ofToString(names.size() - maxFiles) + " more\n";
	}
	return toc;
}

std::string ofApp::buildRepoTree(const std::vector<ScriptFileReviewInfo> & files) const {
	if (files.empty()) return {};
	std::vector<std::string> names;
	names.reserve(files.size());
	for (const auto & f : files) names.push_back(f.name);
	std::sort(names.begin(), names.end());

	std::string tree = "Repository tree:\n";
	for (const auto & n : names) {
		size_t depth = std::count(n.begin(), n.end(), '/');
		tree += std::string(depth * 2, ' ');
		tree += "- ";
		tree += n;
		tree += "\n";
	}
	return tree;
}

std::vector<ScriptFileReviewInfo *> ofApp::selectFilesForReview(
	std::vector<ScriptFileReviewInfo> & files,
	const std::string & reviewQuery,
	int availableTokens,
	int responseReserveTokens) {
	(void)reviewQuery;
	std::vector<ScriptFileReviewInfo *> ordered;
	if (files.empty()) return ordered;

	size_t maxLoc = 0;
	size_t maxComplexity = 0;
	size_t maxFanIn = 0;
	size_t maxFanOut = 0;
	float maxSimilarity = 0.0f;
	for (const auto & f : files) {
		maxLoc = std::max(maxLoc, f.loc);
		maxComplexity = std::max(maxComplexity, f.complexity);
		maxFanIn = std::max(maxFanIn, f.dependencyFanIn);
		maxFanOut = std::max(maxFanOut, f.dependencyFanOut);
		maxSimilarity = std::max(maxSimilarity, f.similarityScore);
	}

	for (auto & f : files) {
		const float normComplexity = maxComplexity > 0
			? static_cast<float>(f.complexity) / static_cast<float>(maxComplexity) : 0.0f;
		const float normLoc = maxLoc > 0
			? static_cast<float>(f.loc) / static_cast<float>(maxLoc) : 0.0f;
		const float normFan = (maxFanIn + maxFanOut) > 0
			? static_cast<float>(f.dependencyFanIn + f.dependencyFanOut) /
				static_cast<float>(maxFanIn + maxFanOut) : 0.0f;
		const float normSim = maxSimilarity > 0.0f
			? f.similarityScore / maxSimilarity : f.similarityScore;

		f.priorityScore =
			0.30f * f.importanceScore +
			0.20f * normComplexity +
			0.15f * normLoc +
			0.15f * normFan +
			0.20f * std::clamp(normSim, 0.0f, 1.0f) +
			0.10f * std::clamp(f.recencyScore, 0.0f, 1.5f);
		ordered.push_back(&f);
	}

	std::sort(ordered.begin(), ordered.end(),
		[](const ScriptFileReviewInfo * a, const ScriptFileReviewInfo * b) {
			return a->priorityScore > b->priorityScore;
		});

	const int safetyReserve = std::max(responseReserveTokens, contextSize / 4);
	int remaining = std::max(128, availableTokens - safetyReserve);
	std::vector<ScriptFileReviewInfo *> chosen;

	for (auto * f : ordered) {
		if (remaining <= 0) break;
		int tokens = f->tokenCount > 0 ? f->tokenCount
			: countTokensAccurate(f->truncatedContent,
				static_cast<int>(f->truncatedContent.size() / 4 + 1));
		const int maxPerFile = std::max(96, contextSize / 6);
		if (tokens > maxPerFile) {
			f->truncatedContent = slidingWindowText(f->content,
				static_cast<size_t>(maxPerFile * 4));
			f->truncated = true;
			tokens = countTokensAccurate(f->truncatedContent,
				static_cast<int>(f->truncatedContent.size() / 4 + 1));
		}
		if (tokens <= remaining) {
			f->selected = true;
			f->tokenCount = tokens;
			chosen.push_back(f);
			remaining -= tokens;
		}
	}

	if (chosen.empty() && !ordered.empty()) {
		// Always pick at least one file even if it exceeds budget.
		auto * f = ordered.front();
		f->selected = true;
		f->truncatedContent = slidingWindowText(f->content, static_cast<size_t>(std::max(256, responseReserveTokens * 2)));
		f->truncated = true;
		f->tokenCount = countTokensAccurate(f->truncatedContent,
			static_cast<int>(f->truncatedContent.size() / 4 + 1));
		chosen.push_back(f);
	}
	return chosen;
}

ofxGgmlInferenceSettings ofApp::makeReviewInferenceSettings(int tokenBudget) const {
	ofxGgmlInferenceSettings settings;
	settings.maxTokens = std::clamp(tokenBudget, 96, maxTokens);
	settings.temperature = temperature;
	settings.topP = topP;
	settings.repeatPenalty = repeatPenalty;
	settings.contextSize = contextSize;
	settings.batchSize = batchSize;
	settings.gpuLayers = gpuLayers;
	settings.threads = numThreads;
	settings.simpleIo = true;
	return settings;
}

std::string ofApp::runFileSummary(const ScriptFileReviewInfo & info,
	const std::string & reviewQuery,
	int perFileBudget,
	const std::string & modelPath) {
	auto settings = makeReviewInferenceSettings(perFileBudget);

	std::ostringstream prompt;
	prompt << "First pass: Review this single file in isolation. "
		"Return a concise summary plus concrete issues (bugs, security, tests, readability)."
		"\nRequested focus: " << reviewQuery << "\n";
	prompt << "\nFile: " << info.name << "\n";
	prompt << "Metrics: LOC=" << info.loc
		<< "  complexity~" << info.complexity
		<< "  fan-in=" << info.dependencyFanIn
		<< "  fan-out=" << info.dependencyFanOut
		<< "  recencyScore=" << ofToString(info.recencyScore, 2)
		<< "  importance=" << ofToString(info.importanceScore, 2) << "\n";
	if (info.truncated) {
		prompt << "Note: content truncated with a sliding window to fit context.\n";
	}
	prompt << "\nContent:\n" << info.truncatedContent << "\n";
	prompt << "\nFormat:\n- Summary\n- Risks\n- Tests to add\n";

	auto result = scriptReviewInference.generate(modelPath, prompt.str(), settings);
	if (!result.success) {
		return "[error] " + result.error;
	}
	return trim(result.text);
}

std::string ofApp::runArchitectureReview(
	const std::vector<ScriptFileReviewInfo *> & files,
	const std::string & repoTree,
	const std::string & reviewQuery,
	const std::string & modelPath) {
	auto settings = makeReviewInferenceSettings(maxTokens);

	std::string prompt;
	prompt.reserve(repoTree.size() + reviewQuery.size() + 8192);
	prompt += "Second pass: Architectural review using only the summaries below.\n";
	prompt += "Request: " + reviewQuery + "\n\n";
	prompt += repoTree + "\n";
	prompt += "File summaries:\n";
	int listed = 0;
	for (const auto & f : files) {
		if (f->summary.empty()) continue;
		prompt += "- " + f->name + ": " + f->summary + "\n";
		if (++listed >= 24) break; // guard context
	}
	prompt += "\nIdentify architecture, layering, and dependency issues. "
		"Highlight risky boundaries, missing invariants, and testing gaps. "
		"Keep output concise and actionable.\n";

	auto result = scriptReviewInference.generate(modelPath, prompt, settings);
	if (!result.success) {
		return "[error] " + result.error;
	}
	return trim(result.text);
}

std::string ofApp::runIntegrationReview(
	const std::vector<ScriptFileReviewInfo *> & files,
	const std::string & repoTree,
	const std::string & reviewQuery,
	const std::string & modelPath) {
	auto settings = makeReviewInferenceSettings(maxTokens);

	std::string prompt;
	prompt.reserve(repoTree.size() + reviewQuery.size() + 8192);
	prompt += "Third pass: Cross-file dependency and integration analysis.\n";
	prompt += "Request: " + reviewQuery + "\n\n";
	prompt += repoTree + "\n";
	prompt += "Per-file findings:\n";
	int listed = 0;
	for (const auto & f : files) {
		if (f->summary.empty()) continue;
		prompt += "- " + f->name + " (fan-in " + std::to_string(f->dependencyFanIn)
			+ ", fan-out " + std::to_string(f->dependencyFanOut) + "): "
			+ f->summary + "\n";
		if (++listed >= 24) break;
	}
	prompt += "\nFocus on contract mismatches, API misuse, inconsistent assumptions, "
		"shared state, and missing integration tests. "
		"Propose cross-file actions and dependency trims.\n";

	auto result = scriptReviewInference.generate(modelPath, prompt, settings);
	if (!result.success) {
		return "[error] " + result.error;
	}
	return trim(result.text);
}

void ofApp::runHierarchicalReview() {
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

	workerThread = std::thread([this]() {
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

			const std::string modelPath = getSelectedModelPath();
			if (modelPath.empty()) {
				setError("[Error] No model selected for review.");
				generating.store(false);
				return;
			}

			// Keep inference helpers aligned with detected CLI paths.
			scriptReviewInference.setCompletionExecutable(llamaCliCommand);
			scriptReviewInference.setEmbeddingExecutable("llama-embedding");

			std::string reviewQuery = std::strlen(scriptInput) > 0
				? std::string(scriptInput)
				: std::string("Comprehensive repository code review. Focus on bugs, security, architecture, and missing tests.");

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput = "Loading files...";
			}
			std::string status;
			auto files = collectScriptFilesForReview(status);
			if (files.empty()) {
				setError("[Error] No source files available for review.");
				generating.store(false);
				return;
			}
			if (!status.empty()) {
				logWithLevel(OF_LOG_NOTICE, status);
			}

			const std::string baseFolder =
				(scriptSource.getSourceType() == ofxGgmlScriptSourceType::LocalFolder)
					? scriptSource.getLocalFolderPath() : "";
			computeFileHeuristics(files, baseFolder);
			if (cancelRequested.load()) {
				setError("[Cancelled] Review cancelled.");
				generating.store(false);
				return;
			}

			const std::string toc = buildRepoTableOfContents(files, kMaxReviewTocFiles);
			const std::string repoTree = buildRepoTree(files);

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput = "Computing embeddings...";
			}

			// Query embedding for semantic selection.
			std::vector<float> queryEmbedding;
			auto queryEmbed = scriptReviewInference.embed(modelPath, reviewQuery);
			if (queryEmbed.success) {
				queryEmbedding = queryEmbed.embedding;
			} else {
				logWithLevel(OF_LOG_WARNING, "embedding query failed: " + queryEmbed.error);
			}
			if (cancelRequested.load()) {
				setError("[Cancelled] Review cancelled.");
				generating.store(false);
				return;
			}

			// Build embeddings for all files (parallel, bounded).
			scriptEmbeddingIndex.clear();
			const size_t maxEmbedParallel = std::max<size_t>(1, std::min<size_t>(kMaxEmbeddingParallelTasks, std::thread::hardware_concurrency()));
			std::mutex embedMutex;
			std::vector<std::future<void>> embedTasks;
			for (auto & f : files) {
				auto task = std::async(std::launch::async, [this, &f, &modelPath, &embedMutex]() {
					std::string snippet = f.truncatedContent;
					if (snippet.size() > kMaxEmbeddingSnippetChars) {
						snippet = slidingWindowText(snippet, kMaxEmbeddingSnippetChars);
					}
					auto er = scriptReviewInference.embed(modelPath, snippet);
					if (er.success) {
						f.embedding = er.embedding;
						// Avoid data races when multiple threads update the index.
						std::lock_guard<std::mutex> lock(embedMutex);
						scriptEmbeddingIndex.add(f.name, snippet, er.embedding);
					}
				});
				embedTasks.push_back(std::move(task));
				if (embedTasks.size() >= maxEmbedParallel) {
					embedTasks.front().get();
					embedTasks.erase(embedTasks.begin());
				}
				if (cancelRequested.load()) break;
			}
			for (auto & t : embedTasks) t.get();
			if (cancelRequested.load()) {
				setError("[Cancelled] Review cancelled.");
				generating.store(false);
				return;
			}

			if (!queryEmbedding.empty()) {
				for (auto & f : files) {
					if (!f.embedding.empty()) {
						f.similarityScore = ofxGgmlEmbeddingIndex::cosineSimilarity(queryEmbedding, f.embedding);
					}
				}
			}

			const int responseReserve = std::max(maxTokens, contextSize / 3);
			auto selected = selectFilesForReview(files, reviewQuery, contextSize, responseReserve);
			if (selected.empty()) {
				setError("[Error] Unable to select files for review within token budget.");
				generating.store(false);
				return;
			}

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput = "Running first-pass summaries...";
			}

			const size_t maxSummaryParallel = std::max<size_t>(1, std::min<size_t>(kMaxSummaryParallelTasks, std::thread::hardware_concurrency()));
			std::vector<std::future<void>> summaryTasks;
			for (auto * f : selected) {
				auto task = std::async(std::launch::async, [this, f, &reviewQuery, &modelPath, responseReserve]() {
					const int perFileBudget = std::max(96, std::min(responseReserve, maxTokens));
					f->summary = runFileSummary(*f, reviewQuery, perFileBudget, modelPath);
				});
				summaryTasks.push_back(std::move(task));
				if (summaryTasks.size() >= maxSummaryParallel) {
					summaryTasks.front().get();
					summaryTasks.erase(summaryTasks.begin());
				}
				if (cancelRequested.load()) break;
			}
			for (auto & t : summaryTasks) t.get();
			if (cancelRequested.load()) {
				setError("[Cancelled] Review cancelled.");
				generating.store(false);
				return;
			}

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput = "Aggregating architecture review...";
			}
			const std::string archReview = runArchitectureReview(selected, repoTree, reviewQuery, modelPath);
			if (cancelRequested.load()) {
				setError("[Cancelled] Review cancelled.");
				generating.store(false);
				return;
			}

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput = "Running integration analysis...";
			}
			const std::string integrationReview = runIntegrationReview(selected, repoTree, reviewQuery, modelPath);
			if (cancelRequested.load()) {
				setError("[Cancelled] Review cancelled.");
				generating.store(false);
				return;
			}

			std::ostringstream summaries;
			for (auto * f : selected) {
				summaries << "### " << f->name << "\n";
				summaries << f->summary << "\n\n";
			}

			std::ostringstream final;
			final << "Hierarchical code review (embeddings + multi-pass)\n\n";
			final << toc << "\n";
			final << "Selected files (priority + similarity):\n";
			for (auto * f : selected) {
				final << "- " << f->name
					<< " | priority " << ofToString(f->priorityScore, 2)
					<< " | sim " << ofToString(f->similarityScore, 2)
					<< " | loc " << f->loc
					<< (f->truncated ? " (truncated)" : "")
					<< "\n";
			}
			final << "\nFirst pass - per-file summaries and issues:\n" << summaries.str();
			final << "Second pass - architecture issues:\n" << archReview << "\n\n";
			final << "Third pass - cross-file integration:\n" << integrationReview << "\n\n";
			final << "Context management: reserved " << responseReserve
				<< " tokens for responses; counted via tokenizer when available. "
				<< "Sliding windows applied to oversized files.\n";

			lastScriptReviewFiles = files;
			lastScriptReviewStatus = "reviewed " + ofToString(selected.size()) + " files";
			lastScriptRequest = reviewQuery;

			scriptProjectMemory.addInteraction("First-pass summaries", summaries.str());
			scriptProjectMemory.addInteraction("Architecture review", archReview);
			scriptProjectMemory.addInteraction("Integration review", integrationReview);

			{
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingOutput = final.str();
				pendingRole = "assistant";
				pendingMode = AiMode::Script;
			}

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput.clear();
			}
		} catch (const std::exception & e) {
			setError(std::string("[Error] Hierarchical review failed: ") + e.what());
		} catch (...) {
			setError("[Error] Unknown failure during hierarchical review.");
		}

		generating.store(false);
	});
}


std::string ofApp::buildPromptForMode(AiMode mode, const std::string & userText,
	const std::string & systemPrompt) const {
	std::ostringstream oss;

	if (!systemPrompt.empty()) {
		oss << "System:\n" << systemPrompt << "\n\n";
	}
	if (mode == AiMode::Script) {
		oss << scriptProjectMemory.buildPromptContext();
	}

	switch (mode) {
	case AiMode::Chat:
		oss << "User:\n" << userText << "\n\nAssistant:\n";
		break;
	case AiMode::Script:
		oss << "Generate high-quality code and short explanation for this request:\n"
			<< userText << "\n\nAnswer:\n";
		break;
	case AiMode::Summarize:
		oss << "Summarize this text concisely with key points:\n"
			<< userText << "\n\nSummary:\n";
		break;
	case AiMode::Write:
		oss << "Rewrite and improve this text:\n"
			<< userText << "\n\nImproved text:\n";
		break;
	case AiMode::Translate:
		oss << userText << "\n\nTranslation:\n";
		break;
	case AiMode::Custom:
		oss << "User:\n" << userText << "\n\nAssistant:\n";
		break;
	}

	return oss.str();
}

bool ofApp::runRealInference(const std::string & prompt, std::string & output, std::string & error,
	std::function<void(const std::string &)> onStreamData,
	bool preserveLlamaInstructions) {
	output.clear();
	error.clear();

	const std::string modelPath = getSelectedModelPath();
	if (modelPath.empty()) {
		error = "No model preset selected.";
		return false;
	}

	if (!std::filesystem::exists(modelPath)) {
		error = "Model file not found: " + modelPath;
		return false;
	}

	// Probe for llama-completion/llama-cli/llama if not already found.
	// Unlike earlier revisions this no longer permanently caches a
	// "not-found" result so the user can install the tools without
	// restarting the app.

	if (llamaCliState.load(std::memory_order_relaxed) != 1) {
		probeLlamaCli();
		if (llamaCliState.load(std::memory_order_relaxed) != 1) {
			error = "llama-completion/llama-cli/llama not found. Build with scripts/build-llama-cli.sh.";
			return false;
		}
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
	const int safeBatch = std::clamp(batchSize, 32, 4096);
	const int safeGpuLayers = std::clamp(gpuLayers, 0, 128);
	// GPU layers control the llama-completion CLI process, which has
	// its own GPU support independent of the addon's ggml engine.
	// Do not force layers to zero based on the engine backend.
	int effectiveGpuLayers = safeGpuLayers;

	std::ostringstream tempStr, topPStr, repeatPenaltyStr;
	tempStr << std::fixed << std::setprecision(3) << safeTemp;
	topPStr << std::fixed << std::setprecision(3) << safeTopP;
	repeatPenaltyStr << std::fixed << std::setprecision(3) << safeRepeatPenalty;

	auto makeArgs = [&](bool shortFlags) {
		std::vector<std::string> out;
		out.reserve(32);
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
		out.emplace_back(ofToString(safeBatch));
		out.emplace_back("-ngl");
		out.emplace_back(ofToString(effectiveGpuLayers));
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
		out.emplace_back("--no-display-prompt");
		out.emplace_back("--simple-io");
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
	const bool started = runProcessCapture(args, raw, ret, true, onStreamData, false);
	if (shouldLog(OF_LOG_VERBOSE)) {
		logWithLevel(OF_LOG_VERBOSE, std::string("Process ") +
			(started ? "started" : "failed to start") + ", exit code: " + ofToString(ret));
	}
	if (started && ret != 0) {
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
			|| ret == -1073740791        // Windows STATUS_STACK_BUFFER_OVERRUN (0xC0000409)
			|| ret == -1073741819        // Windows STATUS_ACCESS_VIOLATION (0xC0000005)
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

	output = trim(stripAnsi(raw));
	if (preserveLlamaInstructions) {
		if (output.empty()) {
			error = llamaCliCommand + " returned empty output.";
			return false;
		}
		return true;
	}

	// Strip the prompt echo from the output — llama-cli may echo the
	// prompt before the generated text.  Return only the generated part.
	{
		const std::string trimmedPrompt = trim(prompt);
		if (!trimmedPrompt.empty() && output.size() >= trimmedPrompt.size()) {
			if (output.compare(0, trimmedPrompt.size(), trimmedPrompt) == 0) {
				output = trim(output.substr(trimmedPrompt.size()));
			} else {
				const size_t pos = output.find(trimmedPrompt);
				if (pos != std::string::npos &&
					(pos + trimmedPrompt.size()) < output.size()) {
					output = trim(output.substr(pos + trimmedPrompt.size()));
				}
			}
		}
	}

	// Clean chat-template role markers and prompt artefacts.
	output = cleanChatOutput(output);

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

void ofApp::runInference(AiMode mode, const std::string & userText,
const std::string & systemPrompt) {
if (generating.load() || !engineReady) return;
if (mode == AiMode::Script) {
lastScriptRequest = userText;
}

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

 workerThread = std::thread([this, mode, userText, systemPrompt]() {
 try {
 std::string userTextWithInternet = userText;
 const bool preserveLlamaInstructions = (mode == AiMode::Script);
 const bool allowInternet = !strictOfflineMode;
 const bool internetFromChat = (mode == AiMode::Chat);
 const bool internetFromAllModes = (internetContextAllModes && mode != AiMode::Translate);
 const bool internetFromScriptSource =
	(mode == AiMode::Script && scriptSource.getSourceType() == ofxGgmlScriptSourceType::Internet);
 if (allowInternet && (internetFromChat || internetFromAllModes || internetFromScriptSource)) {
	std::string gatheredContext;
	gatheredContext += buildInternetContextFromText(userText, true);

	if (internetFromScriptSource) {
		const auto sourceUrls = scriptSource.getInternetUrls();
		gatheredContext += buildInternetContextFromUrls(
			sourceUrls,
			kMaxInternetSourceUrls,
			kMaxInternetCharsPerSourceUrl,
			kMaxInternetCharsFromSourceUrls,
			"Context fetched from loaded internet sources");
	}

	if (!gatheredContext.empty()) {
		userTextWithInternet += gatheredContext;
	}
 }

 std::string prompt = buildPromptForMode(mode, userTextWithInternet, systemPrompt);
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
	std::string cleaned = stripAnsi(rawPartial);
	if (!trimmedPrompt.empty()) {
		const size_t pos = cleaned.find(trimmedPrompt);
		if (pos != std::string::npos) {
			cleaned = cleaned.substr(pos + trimmedPrompt.size());
		} else if (cleaned.size() < trimmedPrompt.size()) {
			cleaned.clear();
		}
	}
	return cleanChatOutput(cleaned);
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

 if (!runRealInference(prompt, result, error, streamCb, preserveLlamaInstructions)) {
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
 if (!streamed.empty()) {
 logWithLevel(OF_LOG_WARNING,
 	"Process failed but streamed output available (" + ofToString(streamed.size()) + " chars), using it.");
 result = streamed;
 } else {
 logWithLevel(OF_LOG_ERROR, "Inference error: " + error);
 result = "[Error] " + error;
 }
 } else {
 if (shouldLog(OF_LOG_VERBOSE)) {
 	logWithLevel(OF_LOG_VERBOSE, "Output (" + ofToString(result.size()) + " chars):\n" + result);
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
		const std::string continuationRequest = buildCutoffContinuationRequest(tail);
		std::string continuationPrompt = buildPromptForMode(mode, continuationRequest, systemPrompt);
		bool contTrimmed = false;
		const size_t contEstimatedTokens = continuationPrompt.size() / 3;
		const size_t contMaxCtxTokens = static_cast<size_t>(contextSize);
		if (contEstimatedTokens > contMaxCtxTokens) {
			continuationPrompt = clampPromptToContext(continuationPrompt, contMaxCtxTokens, contTrimmed);
		}

		std::string continuationOut;
		std::string continuationErr;
		if (runRealInference(continuationPrompt, continuationOut, continuationErr, nullptr, preserveLlamaInstructions) &&
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
fprintf(stderr, "[ChatWindow] AI: %s\n", pendingOutput.c_str());
break;
case AiMode::Script:
scriptOutput = pendingOutput;
scriptMessages.push_back({"assistant", pendingOutput, ofGetElapsedTimef()});
if (pendingOutput.rfind("[Error]", 0) != 0) {
scriptProjectMemory.addInteraction(lastScriptRequest, pendingOutput);
}
fprintf(stderr, "[ChatWindow] Script: %s\n", pendingOutput.c_str());
break;
case AiMode::Summarize:
summarizeOutput = pendingOutput;
fprintf(stderr, "[ChatWindow] Summarize: %s\n", pendingOutput.c_str());
break;
case AiMode::Write:
writeOutput = pendingOutput;
fprintf(stderr, "[ChatWindow] Write: %s\n", pendingOutput.c_str());
break;
case AiMode::Translate:
translateOutput = pendingOutput;
fprintf(stderr, "[ChatWindow] Translate: %s\n", pendingOutput.c_str());
break;
case AiMode::Custom:
customOutput = pendingOutput;
fprintf(stderr, "[ChatWindow] Custom: %s\n", pendingOutput.c_str());
break;
}
pendingOutput.clear();
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
