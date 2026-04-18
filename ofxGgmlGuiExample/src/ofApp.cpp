#include "ofApp.h"

#include "ImHelpers.h"
#include "utils/ImGuiHelpers.h"
#include "utils/TextPromptHelpers.h"
#include "utils/ModelHelpers.h"
#include "utils/SpeechHelpers.h"
#include "utils/AudioHelpers.h"
#include "utils/ServerHelpers.h"
#include "utils/VisionHelpers.h"
#include "utils/ProcessHelpers.h"
#include "utils/ConsoleHelpers.h"
#include "utils/PathHelpers.h"
#include "utils/BackendHelpers.h"
#include "utils/ScriptCommandHelpers.h"
#include "config/ModelPresets.h"
#include "ofJson.h"
#include "core/ofxGgmlWindowsUtf8.h"
#include "support/ofxGgmlSimpleSrtSubtitleParser.h"
#include "gguf.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
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
#include <tlhelp32.h>
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
	"Chat", "Script", "Summarize", "Write", "Translate", "Custom", "Vision", "Speech", "TTS", "Diffusion", "CLIP"
};

const char * const kTextBackendLabels[] = {
	"CLI fallback",
	"llama-server"
};

const char * const kDefaultTextServerUrl = "http://127.0.0.1:8080";
const char * const kDefaultSpeechServerUrl = "http://127.0.0.1:8081";

namespace {
constexpr std::array<int, 8> kSupportedDiffusionImageSizes = {128, 256, 384, 512, 640, 768, 896, 1024};

struct TokenLiteral {
	const char * text;
	size_t len;
};







#ifdef _WIN32





#else

#endif






























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

struct WorkspaceDiffSnapshot {
	bool success = false;
	bool hasChanges = false;
	std::string workspaceRoot;
	std::string repoRoot;
	std::string statusText;
	std::string diffText;
	std::string error;
};

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
// Lifecycle
// ---------------------------------------------------------------------------

void ofApp::setup() {
	gConsoleAnsiEnabled = enableConsoleAnsiFormatting();
	ofDisableArbTex();
	ofSetWindowTitle("ofxGgml AI Studio");
	ofSetFrameRate(60);
	ofSetBackgroundColor(ofColor(30, 30, 34));

gui.setup(nullptr, true, ImGuiConfigFlags_None, true);
ImGui::GetIO().IniFilename = "imgui_ggml_studio.ini";
applyLogLevel(logLevel);

// Initialize presets.
loadModelPresets(modelPresets, taskDefaultModelIndices);
selectedModelIndex = std::clamp(selectedModelIndex, 0,
	std::max(0, static_cast<int>(modelPresets.size()) - 1));
scriptLanguages = ofxGgmlCodeAssistant::defaultLanguagePresets();
chatLanguages = ofxGgmlChatAssistant::defaultResponseLanguages();
translateLanguages = ofxGgmlTextAssistant::defaultTranslateLanguages();
loadPromptTemplates(promptTemplates);
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

// Log callback.
ggml.setLogCallback([this](int level, const std::string & msg) {
	const ofLogLevel mapped = mapGgmlLogLevel(level);
	if (mapped != OF_LOG_SILENT) {
		logWithLevel(mapped, msg);
	}
});
engineStatus = "Initializing ggml engine...";

// Auto-load last session if available.
deferredAutoLoadSessionPending = true;

{
	const std::string configuredSpeechExecutable = trim(speechExecutable);
	const std::string localSpeechCliExecutable = findLocalSpeechCliExecutable(true);
	if (!localSpeechCliExecutable.empty() &&
		isDefaultWhisperCliExecutableHint(configuredSpeechExecutable)) {
		copyStringToBuffer(
			speechExecutable,
			sizeof(speechExecutable),
			localSpeechCliExecutable);
	}
}

if (useModeTokenBudgets) {
	maxTokens = std::clamp(modeMaxTokens[static_cast<size_t>(activeMode)], 32, 4096);
}

if (trim(diffusionOutputDir).empty()) {
	copyStringToBuffer(
		diffusionOutputDir,
		sizeof(diffusionOutputDir),
		ofToDataPath("generated", true));
}
if (shouldManageLocalSpeechServer(effectiveSpeechServerUrl(speechServerUrl)) &&
	!findLocalSpeechServerExecutable().empty()) {
	startLocalSpeechServer();
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
  if (!visionPreviewVideoLoadedPath.empty()) {
    visionPreviewVideo.update();
  }
#if OFXGGML_HAS_OFXVLC4
  if (montageVlcPreviewInitialized) {
		montageVlcPreviewPlayer.update();
  }
#endif
  if (montagePreviewTimelinePlaying) {
		const ofxGgmlMontagePreviewTrack * previewTrack = getSelectedMontagePreviewTrack();
		if (previewTrack != nullptr &&
			getSelectedMontagePreviewTimingMode() == ofxGgmlMontagePreviewTimingMode::Montage) {
			const double durationSeconds = ofxGgmlMontagePreviewBridge::getTrackDuration(*previewTrack);
			const float now = ofGetElapsedTimef();
			if (montagePreviewTimelineLastTickTime > 0.0f && durationSeconds > 0.0) {
				montagePreviewTimelineSeconds = std::min(
					durationSeconds,
					montagePreviewTimelineSeconds + static_cast<double>(now - montagePreviewTimelineLastTickTime));
			}
			if (montagePreviewTimelineSeconds >= durationSeconds) {
				montagePreviewTimelineSeconds = durationSeconds;
				montagePreviewTimelinePlaying = false;
			}
			montagePreviewTimelineLastTickTime = now;
		} else {
			montagePreviewTimelinePlaying = false;
		}
  }
  applyLiveSpeechTranscriberSettings();
  speechLiveTranscriber.update();
  if (deferredEngineInitPending) {
    deferredEngineInitPending = false;
    initializeBackendEngine(false);
    deferredPostInitPending = true;
  }

if (deferredPostInitPending) {
	deferredPostInitPending = false;
	probeLlamaCli();
	detectModelLayers();
	if (gpuLayers == 0 && detectedModelLayers > 0) {
		gpuLayers = detectedModelLayers;
	}
	textInferenceBackend = preferredTextBackendForMode(activeMode);
	if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
		if (trim(textServerUrl).empty()) {
			copyStringToBuffer(textServerUrl, sizeof(textServerUrl), kDefaultTextServerUrl);
		}
		applyServerFriendlyDefaultsForMode(activeMode);
	} else {
		textServerStatus = ServerStatusState::Unknown;
		textServerStatusMessage.clear();
		textServerCapabilityHint.clear();
	}
}

updateDeferredTextServerWarmup();
if (deferredAutoLoadSessionPending && deferredAutoLoadSessionArmed) {
	deferredAutoLoadSessionPending = false;
	autoLoadSession();
	const std::string configuredSpeechExecutable = trim(speechExecutable);
	const std::string localSpeechCliExecutable = findLocalSpeechCliExecutable(true);
	if (!localSpeechCliExecutable.empty() &&
		isDefaultWhisperCliExecutableHint(configuredSpeechExecutable)) {
		copyStringToBuffer(
			speechExecutable,
			sizeof(speechExecutable),
			localSpeechCliExecutable);
	}
}
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

if (deferredAutoLoadSessionPending) {
	deferredAutoLoadSessionArmed = true;
}
if (showPerformance) drawPerformanceWindow();

gui.end();
}

void ofApp::exit() {
  autoSaveSession();
  if (!visionPreviewVideoLoadedPath.empty()) {
    visionPreviewVideo.stop();
    visionPreviewVideo.close();
  }
#if OFXGGML_HAS_OFXVLC4
  closeMontageVlcPreview();
#endif
	stopSpeechRecording(false);
	if (speechInputStream.getSoundStream() != nullptr) {
		speechInputStream.stop();
		speechInputStream.close();
	}
	speechInputStreamConfigured = false;
	stopLocalTextServer(false);
	stopLocalSpeechServer(false);
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
	citationOutput.clear();
  visionOutput.clear();
visionSampledVideoFrames.clear();
	speechOutput.clear();
	ttsOutput.clear();
  diffusionOutput.clear();
  clipOutput.clear();
	citationResults.clear();
  speechDetectedLanguage.clear();
	speechTranscriptPath.clear();
	speechSrtPath.clear();
	speechSegmentCount = 0;
	ttsBackendName.clear();
	ttsElapsedMs = 0.0f;
	ttsResolvedSpeakerPath.clear();
	ttsAudioFiles.clear();
	ttsMetadata.clear();
	diffusionBackendName.clear();
diffusionElapsedMs = 0.0f;
diffusionGeneratedImages.clear();
diffusionMetadata.clear();
clipBackendName.clear();
clipElapsedMs = 0.0f;
clipEmbeddingDimension = 0;
clipHits.clear();
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
const float compactSidebarFieldWidth = std::min(260.0f, ImGui::GetContentRegionAvail().x);
ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "AI Studio");
ImGui::Separator();
ImGui::Spacing();
ImGui::Text("Mode:");
ImGui::Spacing();
const float modeMenuRowHeight = 24.0f;

for (int i = 0; i < kModeCount; i++) {
bool selected = (static_cast<int>(activeMode) == i);
if (ImGui::Selectable(modeLabels[i], selected, ImGuiSelectableFlags_None, ImVec2(0, modeMenuRowHeight))) {
activeMode = static_cast<AiMode>(i);
	if (useModeTokenBudgets) {
		maxTokens = std::clamp(modeMaxTokens[static_cast<size_t>(i)], 32, 4096);
	}
	syncTextBackendForActiveMode(false, false);
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
ImGui::SetNextItemWidth(compactSidebarFieldWidth);
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
showWrappedTooltipf("%s\nBest for: %s\nFile: %s",
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
		showWrappedTooltip("Switch to the catalog default for this mode.");
	}

	const auto & selectedPreset = modelPresets[static_cast<size_t>(selectedModelIndex)];
	const std::string modelPath = getSelectedModelPath();
	if (!modelPath.empty()) {
		const std::string modelFileName = ofFilePath::getFileName(modelPath);
		std::error_code modelEc;
		if (std::filesystem::exists(modelPath, modelEc) && !modelEc) {
			ImGui::TextDisabled("Local GGUF: %s", modelFileName.c_str());
			if (ImGui::IsItemHovered()) {
				showWrappedTooltip(modelPath);
			}
			if (useServerBackend) {
				ImGui::TextDisabled("Used for review / embeddings");
				drawHelpMarker("When llama-server is active, the local GGUF can still be useful for review and embedding-related flows.");
			}
		} else if (useServerBackend) {
			ImGui::TextDisabled("Local GGUF optional");
			drawHelpMarker("Normal text generation can run through llama-server without a local GGUF. This local file remains optional.");
			ImGui::TextDisabled("Suggested file: %s", modelFileName.c_str());
			if (ImGui::IsItemHovered()) {
				showWrappedTooltip(modelPath);
			}
			ImGui::BeginDisabled(selectedPreset.url.empty());
			if (ImGui::SmallButton("Download in browser")) {
				ofLaunchBrowser(selectedPreset.url);
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered()) {
				showWrappedTooltip("Opens the preset download URL in your browser.");
			}
		} else {
			ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f),
				"Model missing: %s", modelFileName.c_str());
			if (ImGui::IsItemHovered()) {
				showWrappedTooltip(modelPath);
			}
			ImGui::TextDisabled("Target path");
			drawHelpMarker(modelPath.c_str());
			ImGui::BeginDisabled(selectedPreset.url.empty());
			if (ImGui::SmallButton("Download in browser")) {
				ofLaunchBrowser(selectedPreset.url);
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered()) {
				showWrappedTooltip("Opens the preset download URL in your browser.");
			}
			ImGui::SameLine();
			const int presetNumber = selectedModelIndex + 1;
			std::string downloadCmd = "./scripts/download-model.sh --preset " + ofToString(presetNumber);
			if (ImGui::SmallButton("Copy download command")) {
				copyToClipboard(downloadCmd);
			}
			if (ImGui::IsItemHovered()) {
				showWrappedTooltipf("Copies a shell command to fetch preset %d into bin/data/models/", presetNumber);
			}
		}
	}
}
ImGui::Spacing();
const bool modeSupportsTextBackend = aiModeSupportsTextBackend(activeMode);
ImGui::Text(modeSupportsTextBackend ? "Text Backend (this mode):" : "Text Backend:");
ImGui::SetNextItemWidth(compactSidebarFieldWidth);
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
drawHelpMarker(modeSupportsTextBackend
	? "Stored separately per text mode. Switching tabs restores that mode's backend."
	: "Vision, Speech, and Diffusion use their own pipelines. Switch to a text mode to change its backend.");
if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
	ImGui::SetNextItemWidth(compactSidebarFieldWidth);
	const bool serverUrlChanged = ImGui::InputText("Server URL", textServerUrl, sizeof(textServerUrl));
	ImGui::SetNextItemWidth(compactSidebarFieldWidth);
	ImGui::InputText("Server model", textServerModel, sizeof(textServerModel));
	if (serverUrlChanged) {
		textServerStatus = ServerStatusState::Unknown;
		textServerStatusMessage.clear();
		textServerCapabilityHint.clear();
		ensureTextServerReady(
			false,
			shouldManageLocalTextServer(effectiveTextServerUrl(textServerUrl)));
	}
	ImGui::TextDisabled("Persistent server backend");
	drawHelpMarker("Uses a warm OpenAI-compatible llama-server for Chat, Script, and text modes. Leave Server model empty to use the model already loaded by the server.");
	ImGui::TextDisabled("Auto-tuned on setup");
	drawHelpMarker("Server-friendly defaults are applied automatically, and the app starts the local server during setup when the configured URL is local.");
	const std::string localServerExe = findLocalTextServerExecutable();
	if (!localServerExe.empty()) {
		ImGui::TextDisabled("Local server exe: %s", ofFilePath::getFileName(localServerExe).c_str());
		if (ImGui::IsItemHovered()) {
			showWrappedTooltip(localServerExe);
		}
	} else {
		ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.35f, 1.0f), "Local server executable not found.");
		drawHelpMarker("Build it with scripts/build-llama-server.ps1 on Windows or scripts/build-llama-cli.sh on Linux/macOS when you want a managed local server.");
	}
	if (textServerStatus != ServerStatusState::Unknown && !textServerStatusMessage.empty()) {
		const ImVec4 statusColor =
			(textServerStatus == ServerStatusState::Reachable)
				? ImVec4(0.35f, 0.8f, 0.45f, 1.0f)
				: ImVec4(0.9f, 0.45f, 0.35f, 1.0f);
		ImGui::TextColored(statusColor, "%s", textServerStatusMessage.c_str());
		if (!textServerCapabilityHint.empty()) {
			drawHelpMarker(textServerCapabilityHint.c_str());
		}
	}
} else {
	if (llamaCliState.load(std::memory_order_relaxed) == 1) {
		ImGui::TextDisabled("CLI fallback available");
		drawHelpMarker(llamaCliCommand.c_str());
	} else {
		ImGui::TextDisabled("CLI fallback not installed");
		drawHelpMarker("Build it only if you want a local non-server fallback.");
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
	showWrappedTooltip("Remember and auto-apply a separate Max Tokens value per mode.");
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
	showWrappedTooltip("Trims cut-off output to sentence or line boundaries when generation ends abruptly.");
}
ImGui::Checkbox("Auto-continue cutoffs (Script)", &autoContinueCutoff);
if (ImGui::IsItemHovered()) {
	showWrappedTooltip("When Script output appears cut off, run one automatic continuation pass.");
}
ImGui::BeginDisabled(useServerBackend);
ImGui::Checkbox("Use prompt cache", &usePromptCache);
ImGui::EndDisabled();
if (ImGui::IsItemHovered()) {
	showWrappedTooltip(useServerBackend
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
	showWrappedTooltip(
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
	showWrappedTooltip("Thread count is only used by the local CLI path.");
}
ImGui::SetNextItemWidth(-1);
ImGui::SliderInt(
	"##ContextSize",
	&contextSize,
	256,
	16384,
	useServerBackend ? "Prompt budget: %d" : "Context: %d");
if (ImGui::IsItemHovered()) {
	showWrappedTooltip(useServerBackend
		? "Used as a local prompt-trimming heuristic before sending text to llama-server."
		: "Maximum local context window requested for llama-completion.");
}
ImGui::SetNextItemWidth(-1);
ImGui::BeginDisabled(useServerBackend);
ImGui::SliderInt("##BatchSize", &batchSize, 32, 4096, "Batch: %d");
ImGui::EndDisabled();
if (useServerBackend && ImGui::IsItemHovered()) {
	showWrappedTooltip("Batch size is only used by the local CLI path.");
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
showWrappedTooltipf("Model has %d layers\nGPU offloading via llama-completion", detectedModelLayers);
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
	case AiMode::Tts:       drawTtsPanel();       break;
	case AiMode::Diffusion: drawDiffusionPanel(); break;
	case AiMode::Clip:      drawClipPanel();      break;
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
	showWrappedTooltip("Auto lets the model decide. Choose a language to force chat replies.");
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
		showWrappedTooltip("Optional source URLs for grounded answers in Chat, Summarize, and Custom.");
	}

	std::string citationSuggestedTopic = trim(chatInput);
	if (citationSuggestedTopic.empty()) {
		for (auto it = chatMessages.rbegin(); it != chatMessages.rend(); ++it) {
			if (it->role == "user" && !trim(it->text).empty()) {
				citationSuggestedTopic = it->text;
				break;
			}
		}
	}
	drawCitationSearchSection("Use Chat Input", citationSuggestedTopic);

	if ((submitted || sendClicked || sendWithSourcesClicked) && std::strlen(chatInput) > 0 && !generating.load()) {
std::string userText(chatInput);
chatMessages.push_back({"user", userText, ofGetElapsedTimef()});
fprintf(stderr, "%s\n", formatConsoleLogLine("ChatWindow", "You", userText, true).c_str());
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
if (sourceType == ofxGgmlScriptSourceType::None && deferredScriptSourceRestorePending) {
	sourceType = deferredScriptSourceType;
}
bool isNone = (sourceType == ofxGgmlScriptSourceType::None);
bool isLocal = (sourceType == ofxGgmlScriptSourceType::LocalFolder);
bool isGitHub = (sourceType == ofxGgmlScriptSourceType::GitHubRepo);
bool isInternet = (sourceType == ofxGgmlScriptSourceType::Internet);

if (isNone) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
if (ImGui::SmallButton("None")) {
	clearDeferredScriptSourceRestore();
	scriptSource.clear();
	selectedScriptFileIndex = -1;
}
if (isNone) ImGui::PopStyleColor();
ImGui::SameLine();

if (isLocal) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
if (ImGui::SmallButton("Local Folder")) {
ofFileDialogResult result = ofSystemLoadDialog("Select Script Folder", true);
if (result.bSuccess) {
	clearDeferredScriptSourceRestore();
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
	(scriptSource.getSourceType() == ofxGgmlScriptSourceType::LocalFolder) &&
	(localWorkspaceInfo.hasVisualStudioSolution ||
	 !localWorkspaceInfo.visualStudioProjectPaths.empty());
if (isVisualStudioWorkspace) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.35f, 0.7f, 1.0f));
if (ImGui::SmallButton("Visual Studio")) {
	ofFileDialogResult result = ofSystemLoadDialog("Select Visual Studio .sln or .vcxproj", false);
	if (result.bSuccess) {
		clearDeferredScriptSourceRestore();
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
	clearDeferredScriptSourceRestore();
	selectedScriptFileIndex = -1;
	scriptSource.setGitHubMode();
}
if (isGitHub) ImGui::PopStyleColor();
ImGui::SameLine();

if (isInternet) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.5f, 0.7f, 1.0f));
if (ImGui::SmallButton("Internet")) {
	clearDeferredScriptSourceRestore();
	selectedScriptFileIndex = -1;
	scriptSource.setInternetMode();
}
if (isInternet) ImGui::PopStyleColor();

if (deferredScriptSourceRestorePending &&
	scriptSource.getSourceType() == ofxGgmlScriptSourceType::None) {
	ImGui::Separator();
	if (deferredScriptSourceType == ofxGgmlScriptSourceType::LocalFolder) {
		ImGui::TextDisabled("Saved local workspace is available but not loaded yet.");
		if (!deferredScriptSourcePath.empty()) {
			ImGui::TextWrapped("Path: %s", deferredScriptSourcePath.c_str());
		}
	} else if (deferredScriptSourceType == ofxGgmlScriptSourceType::GitHubRepo) {
		ImGui::TextDisabled("Saved GitHub source is available but not loaded yet.");
		if (std::strlen(scriptSourceGitHub) > 0) {
			ImGui::TextWrapped(
				"Repo: %s%s%s",
				scriptSourceGitHub,
				std::strlen(scriptSourceBranch) > 0 ? " @ " : "",
				std::strlen(scriptSourceBranch) > 0 ? scriptSourceBranch : "");
		}
	} else if (deferredScriptSourceType == ofxGgmlScriptSourceType::Internet) {
		const auto pendingUrls = splitStoredScriptSourceUrls(deferredScriptSourceInternetUrls);
		ImGui::TextDisabled("Saved internet sources are available but not loaded yet.");
		ImGui::TextWrapped("URLs: %d", static_cast<int>(pendingUrls.size()));
	}
	if (ImGui::Button("Load Saved Source", ImVec2(160, 0))) {
		restoreDeferredScriptSourceIfNeeded();
	}
	if (sourceType != ofxGgmlScriptSourceType::None) {
		ImGui::SameLine();
	}
}

// Script source file browser (inline when active).
const auto scriptSourceFiles = scriptSource.getFiles();
if (scriptSource.getSourceType() != ofxGgmlScriptSourceType::None && !scriptSourceFiles.empty()) {
ImGui::BeginChild("##ScriptFiles", ImVec2(-1, 80), true);
if (scriptSource.getSourceType() == ofxGgmlScriptSourceType::LocalFolder) {
const std::string localPath = scriptSource.getLocalFolderPath();
ImGui::TextDisabled("Folder: %s", localPath.c_str());
} else if (scriptSource.getSourceType() == ofxGgmlScriptSourceType::GitHubRepo) {
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

	if (scriptSource.getSourceType() != ofxGgmlScriptSourceType::None) {
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
		context.activeMode = "Script";
		if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
			context.selectedBackend = "llama-server";
			const std::string serverUrl = trim(textServerUrl);
			if (!serverUrl.empty()) {
				context.selectedBackend += " @ " + serverUrl;
			}
		} else {
			context.selectedBackend = trim(llamaCliCommand).empty()
				? "llama-completion"
				: trim(llamaCliCommand);
			const std::string ggmlBackend = trim(ggml.getBackendName());
			if (!ggmlBackend.empty()) {
				context.selectedBackend += " via " + ggmlBackend;
			}
		}
		context.recentTouchedFiles = recentScriptTouchedFiles;
		context.lastFailureReason = lastScriptFailureReason;
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
	if (!changedFiles.empty()) {
		recentScriptTouchedFiles = changedFiles;
	}
	if (!verificationCommands.empty()) {
		cachedScriptVerificationCommands = verificationCommands;
	}
	if (!validation.success) {
		lastScriptFailureReason = validation.messages.empty()
			? std::string("Workspace dry run validation failed.")
			: validation.messages.front();
	} else {
		lastScriptFailureReason.clear();
	}

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
		lastScriptFailureReason.clear();
		recentScriptTouchedFiles.clear();
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
		showWrappedTooltip("Run embedding-powered, multi-pass review over the loaded folder/repository.\nRecommended: use the Script-mode recommended model plus Review Preset.");
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
		showWrappedTooltip("Review the loaded workspace and return an actionable fix plan with structured edits.");
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
		showWrappedTooltip("Switch to the recommended Script model and review-tuned generation settings.");
	}
}

	const bool hasScriptOutput = !scriptOutput.empty();
	const bool hasLastTask = !lastScriptRequest.empty();
	const bool hasBuildErrors = trim(scriptBuildErrors).size() > 0;
	const bool hasWorkspaceSource =
		sourceType == ofxGgmlScriptSourceType::LocalFolder &&
		!scriptSource.getLocalFolderPath().empty();
	const std::string focusedScriptFileLabel =
		(hasSelectedFile && selectedScriptFileIndex >= 0 &&
		 selectedScriptFileIndex < static_cast<int>(scriptSourceFiles.size()))
			? scriptSourceFiles[static_cast<size_t>(selectedScriptFileIndex)].name
			: std::string();
	const std::string scriptBackendLabel = [&]() {
		if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
			const std::string serverUrl = trim(textServerUrl);
			return serverUrl.empty()
				? std::string("llama-server")
				: std::string("llama-server @ ") + serverUrl;
		}
		const std::string cliPath = trim(llamaCliCommand);
		return cliPath.empty() ? std::string("llama-completion") : cliPath;
	}();
	const std::string scriptSourceLabel = [&]() {
		switch (sourceType) {
		case ofxGgmlScriptSourceType::LocalFolder: return std::string("local workspace");
		case ofxGgmlScriptSourceType::GitHubRepo: return std::string("GitHub repo");
		case ofxGgmlScriptSourceType::Internet: return std::string("internet sources");
		case ofxGgmlScriptSourceType::None:
		default:
			return std::string("none");
		}
	}();

	ImGui::Separator();
	ImGui::TextDisabled("Assistant context");
	ImGui::TextWrapped(
		"Backend: %s | Source: %s | Focused file: %s",
		scriptBackendLabel.c_str(),
		scriptSourceLabel.c_str(),
		focusedScriptFileLabel.empty() ? "none" : focusedScriptFileLabel.c_str());
	ImGui::TextDisabled(
		"Repo context: %s | Recent touched files: %d",
		scriptIncludeRepoContext ? "on" : "off",
		static_cast<int>(recentScriptTouchedFiles.size()));
	if (!lastScriptFailureReason.empty()) {
		ImGui::TextColored(
			ImVec4(0.95f, 0.68f, 0.35f, 1.0f),
			"Last failure: %s",
			lastScriptFailureReason.c_str());
	}

	ImGui::Spacing();
	ImGui::TextDisabled("Suggested next step:");
	if (!hasWorkspaceSource) {
		if (ImGui::Button("Load local workspace...", ImVec2(170, 0))) {
			ofFileDialogResult result = ofSystemLoadDialog("Select Script Folder", true);
			if (result.bSuccess) {
				clearDeferredScriptSourceRestore();
				selectedScriptFileIndex = -1;
				if (!scriptLanguages.empty()) {
					scriptSource.setPreferredExtension(
						scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExtension);
				}
				scriptSource.setLocalFolder(result.getPath());
			}
		}
		ImGui::SameLine();
		ImGui::TextDisabled("Load a local project to unlock grounded edit plans and dry runs.");
	} else if (hasBuildErrors) {
		ImGui::BeginDisabled(generating.load());
		if (ImGui::Button("Fix Build Plan", ImVec2(150, 0))) {
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
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextDisabled("You already have compiler output loaded, so start with a grounded build fix.");
	} else if (hasScriptOutput) {
		const auto lastStructuredOutput = ofxGgmlCodeAssistant::parseStructuredResult(scriptOutput);
		if (!lastStructuredOutput.unifiedDiff.empty() || !lastStructuredOutput.patchOperations.empty()) {
			ImGui::BeginDisabled(generating.load());
			if (ImGui::Button("Workspace Dry Run", ImVec2(150, 0))) {
				previewWorkspacePlan("Workspace dry run");
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::TextDisabled("Preview the latest structured edit plan before applying anything manually.");
		} else if (hasSelectedFile && !hasUserInput) {
			if (ImGui::Button("Explain focused file", ImVec2(150, 0))) {
				copyStringToBuffer(
					scriptInput,
					sizeof(scriptInput),
					("Explain how " + focusedScriptFileLabel + " works, including key functions, data flow, and risks.").c_str());
			}
			ImGui::SameLine();
			ImGui::TextDisabled("Seed the input from the selected file instead of starting from a blank prompt.");
		}
	} else if (hasWorkspaceSource && hasUserInput) {
		ImGui::BeginDisabled(generating.load());
		if (ImGui::Button("Grounded Edit Plan", ImVec2(150, 0))) {
			submitScriptRequest(
				ofxGgmlCodeAssistantAction::Edit,
				scriptInput,
				"",
				"",
				false,
				true,
				true,
				"",
				buildWorkspaceAllowedFiles());
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextDisabled("You have a workspace and a request loaded, so the strongest next step is a structured edit plan.");
	} else if (hasWorkspaceSource) {
		ImGui::BeginDisabled(generating.load());
		if (ImGui::Button("Review workspace", ImVec2(150, 0))) {
			runHierarchicalReview();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextDisabled("No prompt yet. Start with a workspace review to surface the next useful change.");
	}

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
	showWrappedTooltip(buildScriptCommandHelpText());
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
	showWrappedTooltip("Summarize local Git changes professionally for reviewers.");
}
ImGui::EndDisabled();

if (ImGui::CollapsingHeader("Semantic & Workspace")) {
	ImGui::Checkbox("Restrict workspace previews to focused file", &restrictWorkspaceToFocusedFile);
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip("When enabled, structured edit previews stay inside the currently selected file.");
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
	if (ImGui::IsItemHovered()) showWrappedTooltip("Include loaded file list and selected file snippet in script prompts");

	ImGui::BeginDisabled(generating.load() || (!hasScriptOutput && !hasLastTask));
	if (ImGui::Button("Continue Task", ImVec2(120, 0))) {
		submitScriptRequest(ofxGgmlCodeAssistantAction::ContinueTask);
	}
	if (ImGui::IsItemHovered()) showWrappedTooltip("Continue from the latest coding response without rewriting your full prompt.");
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
		showWrappedTooltip("Remove all added URLs.");
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

	drawCitationSearchSection("Use Summarize Text", summarizeInput);
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
	if (hasDeferredTranslateInput) {
		copyStringToBuffer(
			translateInput,
			sizeof(translateInput),
			deferredTranslateInput);
		hasDeferredTranslateInput = false;
		deferredTranslateInput.clear();
	}
	translateSourceLang = std::clamp(
		translateSourceLang,
		0,
		std::max(0, static_cast<int>(translateLanguages.size()) - 1));
	translateTargetLang = std::clamp(
		translateTargetLang,
		0,
		std::max(0, static_cast<int>(translateLanguages.size()) - 1));

	const auto currentSourceLanguage = [&]() -> std::string {
		if (translateSourceLang < 0 ||
			translateSourceLang >= static_cast<int>(translateLanguages.size())) {
			return "Auto detect";
		}
		return translateLanguages[static_cast<size_t>(translateSourceLang)].name;
	};
	const auto currentTargetLanguage = [&]() -> std::string {
		if (translateTargetLang < 0 ||
			translateTargetLang >= static_cast<int>(translateLanguages.size())) {
			return "English";
		}
		return translateLanguages[static_cast<size_t>(translateTargetLang)].name;
	};
	const auto sourceIsAutoDetect = [&]() -> bool {
		return currentSourceLanguage() == "Auto detect";
	};

	ImGui::TextWrapped(
		"Translate with cleaner output and quicker handoff from other panels. "
		"Use Auto detect for unknown source text, then reuse the result in Write or as a fresh input.");

	ImGui::Text("Source language:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(170);
	if (ImGui::BeginCombo("##SrcLang", currentSourceLanguage().c_str())) {
		for (int i = 0; i < static_cast<int>(translateLanguages.size()); i++) {
			const bool selected = (translateSourceLang == i);
			if (ImGui::Selectable(translateLanguages[static_cast<size_t>(i)].name.c_str(), selected)) {
				translateSourceLang = i;
				autoSaveSession();
			}
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::Text("Target language:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(170);
	if (ImGui::BeginCombo("##TgtLang", currentTargetLanguage().c_str())) {
		for (int i = 0; i < static_cast<int>(translateLanguages.size()); i++) {
			if (translateLanguages[static_cast<size_t>(i)].name == "Auto detect") {
				continue;
			}
			const bool selected = (translateTargetLang == i);
			if (ImGui::Selectable(translateLanguages[static_cast<size_t>(i)].name.c_str(), selected)) {
				translateTargetLang = i;
				autoSaveSession();
			}
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::Button("Swap", ImVec2(60, 0))) {
		if (!sourceIsAutoDetect()) {
			std::swap(translateSourceLang, translateTargetLang);
		} else {
			translateSourceLang = translateTargetLang;
			for (int i = 0; i < static_cast<int>(translateLanguages.size()); ++i) {
				if (translateLanguages[static_cast<size_t>(i)].name == "English") {
					translateTargetLang = i;
					break;
				}
			}
		}
		autoSaveSession();
	}

	const bool sameLanguages =
		!sourceIsAutoDetect() &&
		currentSourceLanguage() == currentTargetLanguage();
	if (sameLanguages) {
		ImGui::TextColored(
			ImVec4(0.95f, 0.7f, 0.3f, 1.0f),
			"Source and target are currently the same language.");
	}

	ImGui::Separator();
	ImGui::TextDisabled("Quick sources");
	ImGui::BeginDisabled(trim(writeOutput).empty());
	if (ImGui::SmallButton("Use Write output")) {
		deferredTranslateInput = writeOutput;
		hasDeferredTranslateInput = true;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(customOutput).empty());
	if (ImGui::SmallButton("Use Custom output")) {
		deferredTranslateInput = customOutput;
		hasDeferredTranslateInput = true;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(speechOutput).empty());
	if (ImGui::SmallButton("Use Speech output")) {
		deferredTranslateInput = speechOutput;
		hasDeferredTranslateInput = true;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(translateOutput).empty());
	if (ImGui::SmallButton("Use current translation")) {
		deferredTranslateInput = translateOutput;
		hasDeferredTranslateInput = true;
	}
	ImGui::EndDisabled();

	ImGui::Text("Enter text to translate:");
	ImGui::InputTextMultiline("##TransIn", translateInput, sizeof(translateInput),
	ImVec2(-1, 120));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}

	bool hasInput = std::strlen(translateInput) > 0;
	auto submitTranslatePrompt = [&](const std::string & instruction, const std::string & outputHeading) {
		const std::string sourceLanguage = currentSourceLanguage();
		const std::string targetLanguage = currentTargetLanguage();
		std::string languageLine = "Target language: " + targetLanguage + ".";
		if (!sourceIsAutoDetect()) {
			languageLine = "Source language: " + sourceLanguage + ". " + languageLine;
		}
		runInference(
			AiMode::Translate,
			translateInput,
			"",
			buildStructuredTextPrompt(
				"You are a careful translator.",
				instruction + " " + languageLine + " Preserve paragraph breaks and return only the requested result.",
				"Text",
				translateInput,
				outputHeading));
	};
	ImGui::BeginDisabled(generating.load() || !hasInput || sameLanguages);
	if (ImGui::Button("Translate", ImVec2(110, 0))) {
		ofxGgmlTextAssistantRequest request;
		request.task = ofxGgmlTextTask::Translate;
		request.inputText = translateInput;
		request.sourceLanguage = currentSourceLanguage();
		request.targetLanguage = currentTargetLanguage();
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
	ImGui::BeginDisabled(generating.load() || !hasInput || sameLanguages);
	if (ImGui::Button("Natural", ImVec2(110, 0))) {
		submitTranslatePrompt(
			"Translate naturally and fluently for a native speaker while preserving intent, tone, and formatting.",
			"Natural translation");
	}
	ImGui::SameLine();
	if (ImGui::Button("Literal", ImVec2(110, 0))) {
		submitTranslatePrompt(
			"Translate as literally as possible while remaining grammatical and structurally faithful.",
			"Literal translation");
	}
	ImGui::SameLine();
	if (ImGui::Button("Detect + Translate", ImVec2(140, 0))) {
		submitTranslatePrompt(
			"Detect the source language first, then translate to the target language. Start with one short line naming the detected language, then provide the translation only.",
			"Detected translation");
	}
	ImGui::EndDisabled();

	ImGui::Separator();
	ImGui::Text("Translation:");
	if (!translateOutput.empty()) {
		ImGui::SameLine();
		if (ImGui::SmallButton("Copy##TransCopy")) copyToClipboard(translateOutput);
		ImGui::SameLine();
		if (ImGui::SmallButton("Use as input##TransReuse")) {
			deferredTranslateInput = translateOutput;
			hasDeferredTranslateInput = true;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Send to Write##TransToWrite")) {
			copyStringToBuffer(writeInput, sizeof(writeInput), translateOutput);
			activeMode = AiMode::Write;
		}
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
		if (translateOutput.empty()) {
			ImGui::TextDisabled("Translation results will appear here.");
		} else {
			ImGui::TextWrapped("%s", translateOutput.c_str());
		}
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

	drawCitationSearchSection("Use Custom Input", customInput);

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

void ofApp::ensureVisionPreviewResources() {
	ensureLocalImagePreview(
		trim(visionImagePath),
		visionPreviewImage,
		visionPreviewImageLoadedPath,
		visionPreviewImageError);

	const std::string videoPath = trim(visionVideoPath);
	if (videoPath != visionPreviewVideoLoadedPath) {
		if (!visionPreviewVideoLoadedPath.empty()) {
			visionPreviewVideo.stop();
			visionPreviewVideo.close();
		}
		visionPreviewVideoLoadedPath.clear();
		visionPreviewVideoError.clear();
		visionPreviewVideoReady = false;
		if (!videoPath.empty()) {
			if (visionPreviewVideo.load(videoPath)) {
				visionPreviewVideo.setLoopState(OF_LOOP_NONE);
				visionPreviewVideo.play();
				visionPreviewVideo.setPaused(true);
				visionPreviewVideo.setPosition(0.0f);
				visionPreviewVideo.update();
				visionPreviewVideoLoadedPath = videoPath;
				visionPreviewVideoReady = visionPreviewVideo.isLoaded();
			} else {
			visionPreviewVideoError = "Unable to load video preview.";
		}
	}
	}

	std::string outputPreviewPath;
	if (!visionSampledVideoFrames.empty()) {
		outputPreviewPath = trim(visionSampledVideoFrames.front().imagePath);
	}
	ensureLocalImagePreview(
		outputPreviewPath,
		visionOutputPreviewImage,
		visionOutputPreviewLoadedPath,
		visionOutputPreviewError);
}

int ofApp::clampSupportedDiffusionImageSize(int value) {
	int bestValue = kSupportedDiffusionImageSizes.front();
	int bestDistance = std::abs(value - bestValue);
	for (int candidate : kSupportedDiffusionImageSizes) {
		const int distance = std::abs(value - candidate);
		if (distance < bestDistance) {
			bestValue = candidate;
			bestDistance = distance;
		}
	}
	return bestValue;
}

ofxGgmlInferenceSettings ofApp::buildCurrentTextInferenceSettings(AiMode mode) const {
	constexpr float kDefaultTemp = 0.7f;
	constexpr float kDefaultTopP = 0.9f;
	constexpr float kDefaultRepeatPenalty = 1.1f;

	ofxGgmlInferenceSettings settings;
	settings.maxTokens = std::clamp(maxTokens, 1, 8192);
	settings.temperature = std::isfinite(temperature)
		? std::clamp(temperature, 0.0f, 2.0f)
		: kDefaultTemp;
	settings.topP = std::isfinite(topP)
		? std::clamp(topP, 0.0f, 1.0f)
		: kDefaultTopP;
	settings.topK = std::clamp(topK, 0, 200);
	settings.minP = std::isfinite(minP)
		? std::clamp(minP, 0.0f, 1.0f)
		: 0.0f;
	settings.repeatPenalty = std::isfinite(repeatPenalty)
		? std::clamp(repeatPenalty, 1.0f, 2.0f)
		: kDefaultRepeatPenalty;
	settings.contextSize = std::clamp(contextSize, 256, 16384);
	settings.batchSize = std::clamp(batchSize, 32, 4096);
	settings.threads = std::clamp(numThreads, 1, 128);
	settings.gpuLayers = std::clamp(gpuLayers, 0, detectedModelLayers > 0 ? detectedModelLayers : 128);
	settings.seed = seed;
	settings.simpleIo = true;
	settings.singleTurn = true;
	settings.autoProbeCliCapabilities = true;
	settings.trimPromptToContext = true;
	settings.allowBatchFallback = true;
	settings.autoContinueCutoff = (mode == AiMode::Script) && autoContinueCutoff;
	settings.stopAtNaturalBoundary = stopAtNaturalBoundary;
	const std::string modelPath = getSelectedModelPath();
	settings.autoPromptCache = usePromptCache;
	settings.promptCachePath = usePromptCache ? promptCachePathFor(modelPath, mode) : std::string();
	settings.mirostat = mirostatMode;
	settings.mirostatTau = mirostatTau;
	settings.mirostatEta = mirostatEta;
	settings.useServerBackend = (textInferenceBackend == TextInferenceBackend::LlamaServer);
	if (settings.useServerBackend) {
		settings.serverUrl = effectiveTextServerUrl(textServerUrl);
		settings.serverModel = trim(textServerModel);
	} else if (!backendNames.empty() &&
		selectedBackendIndex >= 0 &&
		selectedBackendIndex < static_cast<int>(backendNames.size())) {
		const std::string & selected = backendNames[static_cast<size_t>(selectedBackendIndex)];
		if (selected != "CPU") {
			settings.device = selected;
		}
	}
	if (!settings.useServerBackend && settings.device.empty()) {
		const std::string backend = ggml.getBackendName();
		if (!backend.empty() && backend != "CPU" && backend != "none") {
			settings.device = backend;
		}
	}
	return settings;
}

void ofApp::ensureLocalImagePreview(
	const std::string & imagePath,
	ofImage & previewImage,
	std::string & loadedPath,
	std::string & errorMessage) {
	if (imagePath != loadedPath) {
		previewImage.clear();
		loadedPath.clear();
		errorMessage.clear();
		if (!imagePath.empty()) {
			if (previewImage.load(imagePath)) {
				loadedPath = imagePath;
			} else {
				loadedPath = imagePath;
				errorMessage = "Unable to load image preview.";
			}
		}
	}
}

void ofApp::drawMediaTexturePreview(const ofBaseHasTexture & previewTexture, const char * childId) {
	const float availWidth = std::max(160.0f, ImGui::GetContentRegionAvail().x);
	const float maxWidth = std::min(availWidth, 420.0f);
	const float texWidth = std::max(1.0f, static_cast<float>(previewTexture.getTexture().getWidth()));
	const float texHeight = std::max(1.0f, static_cast<float>(previewTexture.getTexture().getHeight()));
	const float scale = std::min(maxWidth / texWidth, 240.0f / texHeight);
	const ImVec2 drawSize(
		std::max(1.0f, texWidth * scale),
		std::max(1.0f, texHeight * scale));

	ImGui::BeginChild(childId, ImVec2(0, drawSize.y + 12.0f), true);
	ofxImGui::AddImage(previewTexture, glm::vec2(drawSize.x, drawSize.y));
	ImGui::EndChild();
}

void ofApp::drawLocalImagePreview(
	const char * label,
	const std::string & imagePath,
	ofImage & previewImage,
	const std::string & errorMessage,
	const char * childId) {
	if (imagePath.empty()) {
		return;
	}
	if (!errorMessage.empty()) {
		ImGui::TextDisabled("%s", errorMessage.c_str());
		return;
	}
	if (!previewImage.isAllocated() || !previewImage.getTexture().isAllocated()) {
		ImGui::TextDisabled("Image preview will appear here.");
		return;
	}
	ImGui::TextDisabled(
		"%s: %d x %d",
		label,
		previewImage.getWidth(),
		previewImage.getHeight());
	drawMediaTexturePreview(previewImage, childId);
}

void ofApp::drawVisionImagePreview(const std::string & imagePath) {
	drawLocalImagePreview(
		"Image preview",
		imagePath,
		visionPreviewImage,
		visionPreviewImageError,
		"##VisionImagePreview");
}

int ofApp::findActiveMontageSourceCueIndex() const {
	if (!montageSubtitlePlaybackEnabled ||
		montageSourceSubtitleTrack.cues.empty() ||
		!visionPreviewVideoReady ||
		!visionPreviewVideo.isLoaded()) {
		return -1;
	}

	const double durationSeconds = std::max(0.0, static_cast<double>(visionPreviewVideo.getDuration()));
	const double currentSeconds = std::max(
		0.0,
		std::min(
			durationSeconds,
			static_cast<double>(visionPreviewVideo.getPosition()) * durationSeconds));
	for (size_t i = 0; i < montageSourceSubtitleTrack.cues.size(); ++i) {
		const auto & cue = montageSourceSubtitleTrack.cues[i];
		if (currentSeconds >= cue.startSeconds && currentSeconds <= cue.endSeconds) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

ofxGgmlMontagePreviewTimingMode ofApp::getSelectedMontagePreviewTimingMode() const {
	return montagePreviewTimingModeIndex == 1
		? ofxGgmlMontagePreviewTimingMode::Montage
		: ofxGgmlMontagePreviewTimingMode::Source;
}

const ofxGgmlMontagePreviewTrack * ofApp::getSelectedMontagePreviewTrack() const {
	if (montagePreviewBundle.montageTrack.cues.empty() &&
		montagePreviewBundle.sourceTrack.cues.empty()) {
		return nullptr;
	}
	return &ofxGgmlMontagePreviewBridge::selectTrack(
		montagePreviewBundle,
		getSelectedMontagePreviewTimingMode());
}

double ofApp::getSelectedMontagePreviewTimeSeconds() const {
	if (getSelectedMontagePreviewTimingMode() == ofxGgmlMontagePreviewTimingMode::Source) {
		if (!visionPreviewVideoReady || !visionPreviewVideo.isLoaded()) {
			return 0.0;
		}
		const double durationSeconds = std::max(0.0, static_cast<double>(visionPreviewVideo.getDuration()));
		return std::max(
			0.0,
			std::min(
				durationSeconds,
				static_cast<double>(visionPreviewVideo.getPosition()) * durationSeconds));
	}
	return std::max(0.0, montagePreviewTimelineSeconds);
}

int ofApp::findActiveMontagePreviewCueIndex() const {
	const ofxGgmlMontagePreviewTrack * track = getSelectedMontagePreviewTrack();
	if (!montageSubtitlePlaybackEnabled || track == nullptr) {
		return -1;
	}
	if (getSelectedMontagePreviewTimingMode() == ofxGgmlMontagePreviewTimingMode::Source &&
		(!visionPreviewVideoReady || !visionPreviewVideo.isLoaded())) {
		return -1;
	}
	return ofxGgmlMontagePreviewBridge::findCueAtTime(
		*track,
		getSelectedMontagePreviewTimeSeconds());
}

std::string ofApp::exportSelectedMontagePreviewTrack(
	ofxGgmlMontagePreviewTextFormat format,
	std::string * errorOut) const {
	const ofxGgmlMontagePreviewTrack * track = getSelectedMontagePreviewTrack();
	if (track == nullptr) {
		if (errorOut) {
			*errorOut = "No montage preview track is available yet.";
		}
		return {};
	}
	const std::filesystem::path exportDir =
		std::filesystem::path(ofToDataPath("cache/montage_preview", true));
	const std::filesystem::path outputPath =
		exportDir / ofxGgmlMontagePreviewBridge::suggestSubtitleFileName(*track, format);
	std::string error;
	if (!ofxGgmlMontagePreviewBridge::exportTrack(*track, outputPath.string(), format, &error)) {
		if (errorOut) {
			*errorOut = error;
		}
		return {};
	}
	return outputPath.string();
}

#if OFXGGML_HAS_OFXVLC4
bool ofApp::ensureMontageVlcPreviewInitialized(std::string * errorOut) {
	if (montageVlcPreviewInitialized) {
		return true;
	}

	try {
		montageVlcPreviewPlayer.init(0, nullptr);
		montageVlcPreviewPlayer.setVolume(0);
		montageVlcPreviewPlayer.setSubtitleDelayMs(0);
		montageVlcPreviewPlayer.setSubtitleTextScale(1.0f);
		montageVlcPreviewInitialized = true;
		montageVlcPreviewError.clear();
		return true;
	} catch (const std::exception & e) {
		montageVlcPreviewError = std::string("Failed to initialize ofxVlc4 preview: ") + e.what();
		if (errorOut) {
			*errorOut = montageVlcPreviewError;
		}
		return false;
	}
}

bool ofApp::loadMontageVlcPreview(std::string * errorOut) {
	if (!ensureMontageVlcPreviewInitialized(errorOut)) {
		return false;
	}

	const std::string videoPath = trim(visionVideoPath);
	if (videoPath.empty()) {
		montageVlcPreviewError = "Select a source video before loading the ofxVlc4 preview.";
		if (errorOut) {
			*errorOut = montageVlcPreviewError;
		}
		return false;
	}

	std::string subtitleError;
	const std::string subtitlePath =
		exportSelectedMontagePreviewTrack(ofxGgmlMontagePreviewTextFormat::Srt, &subtitleError);
	if (subtitlePath.empty()) {
		montageVlcPreviewError =
			subtitleError.empty() ? std::string("Failed to prepare the active subtitle track for ofxVlc4.")
								  : subtitleError;
		if (errorOut) {
			*errorOut = montageVlcPreviewError;
		}
		return false;
	}

	montagePreviewSubtitleSlavePath = subtitlePath;

	const bool reloadVideo = montageVlcPreviewLoadedVideoPath != videoPath;
	if (reloadVideo) {
		montageVlcPreviewPlayer.stop();
		montageVlcPreviewPlayer.clearMediaSlaves();
		montageVlcPreviewPlayer.clearPlaylist();
		if (montageVlcPreviewPlayer.addPathToPlaylist(videoPath) <= 0) {
			montageVlcPreviewError = "ofxVlc4 could not load the selected source video.";
			if (errorOut) {
				*errorOut = montageVlcPreviewError;
			}
			return false;
		}
		montageVlcPreviewPlayer.playIndex(0);
		montageVlcPreviewPlayer.setVolume(0);
		montageVlcPreviewLoadedVideoPath = videoPath;
		montageVlcPreviewLoadedSubtitlePath.clear();
	}

	if (reloadVideo || montageVlcPreviewLoadedSubtitlePath != subtitlePath) {
		montageVlcPreviewPlayer.clearMediaSlaves();
		if (!montageVlcPreviewPlayer.addSubtitleSlave(subtitlePath)) {
			montageVlcPreviewError = "ofxVlc4 could not attach the exported subtitle slave.";
			if (errorOut) {
				*errorOut = montageVlcPreviewError;
			}
			return false;
		}
		const auto subtitleTracks = montageVlcPreviewPlayer.getSubtitleTracks();
		if (!subtitleTracks.empty()) {
			montageVlcPreviewPlayer.selectSubtitleTrackById(subtitleTracks.back().id);
		}
		montageVlcPreviewLoadedSubtitlePath = subtitlePath;
	}

	montageVlcPreviewError.clear();
	if (errorOut) {
		errorOut->clear();
	}
	return true;
}

void ofApp::closeMontageVlcPreview() {
	if (!montageVlcPreviewInitialized) {
		return;
	}
	montageVlcPreviewPlayer.close();
	montageVlcPreviewInitialized = false;
	montageVlcPreviewLoadedVideoPath.clear();
	montageVlcPreviewLoadedSubtitlePath.clear();
}

void ofApp::drawMontageVlcPreview() {
	if (!montageVlcPreviewInitialized) {
		return;
	}

	if (!montageVlcPreviewError.empty()) {
		ImGui::TextDisabled("%s", montageVlcPreviewError.c_str());
	}

	const ofTexture & previewTexture = montageVlcPreviewPlayer.getTexture();
	if (previewTexture.isAllocated()) {
		const float availWidth = std::max(160.0f, ImGui::GetContentRegionAvail().x);
		const float maxWidth = std::min(availWidth, 420.0f);
		const float texWidth = std::max(1.0f, static_cast<float>(previewTexture.getWidth()));
		const float texHeight = std::max(1.0f, static_cast<float>(previewTexture.getHeight()));
		const float scale = std::min(maxWidth / texWidth, 240.0f / texHeight);
		const glm::vec2 drawSize(
			std::max(1.0f, texWidth * scale),
			std::max(1.0f, texHeight * scale));
		ImGui::BeginChild("##MontageVlcPreview", ImVec2(0, drawSize.y + 12.0f), true);
		ofxImGui::AddImage(previewTexture, drawSize);
		ImGui::EndChild();
	} else {
		ImGui::TextDisabled("ofxVlc4 preview will appear here after the media attaches.");
	}

	const float durationSeconds = std::max(0.0f, montageVlcPreviewPlayer.getLength() / 1000.0f);
	float previewPosition = std::clamp(montageVlcPreviewPlayer.getPosition(), 0.0f, 1.0f);
	const float currentSeconds = previewPosition * durationSeconds;
	if (ImGui::Button(montageVlcPreviewPlayer.isPlaying() ? "Pause VLC preview" : "Play VLC preview", ImVec2(150, 0))) {
		montageVlcPreviewPlayer.togglePlayPause();
	}
	ImGui::SameLine();
	if (ImGui::Button("Restart VLC preview", ImVec2(150, 0))) {
		montageVlcPreviewPlayer.play();
		montageVlcPreviewPlayer.setPosition(0.0f);
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%.2fs / %.2fs", currentSeconds, durationSeconds);

	ImGui::SetNextItemWidth(-1);
	if (ImGui::SliderFloat("VLC preview position", &previewPosition, 0.0f, 1.0f, "%.3f")) {
		montageVlcPreviewPlayer.setPosition(previewPosition);
	}

	int subtitleDelayMs = montageVlcPreviewPlayer.getSubtitleDelayMs();
	ImGui::SetNextItemWidth(180);
	if (ImGui::SliderInt("VLC subtitle delay", &subtitleDelayMs, -5000, 5000, "%d ms")) {
		montageVlcPreviewPlayer.setSubtitleDelayMs(subtitleDelayMs);
	}
	ImGui::SameLine();
	float subtitleTextScale = montageVlcPreviewPlayer.getSubtitleTextScale();
	ImGui::SetNextItemWidth(180);
	if (ImGui::SliderFloat("VLC subtitle scale", &subtitleTextScale, 0.5f, 3.0f, "%.2fx")) {
		montageVlcPreviewPlayer.setSubtitleTextScale(subtitleTextScale);
	}

	const auto subtitleState = montageVlcPreviewPlayer.getSubtitleStateInfo();
	if (subtitleState.trackSelected && !subtitleState.selectedTrackLabel.empty()) {
		ImGui::TextDisabled("Active VLC subtitle track: %s", subtitleState.selectedTrackLabel.c_str());
	} else {
		ImGui::TextDisabled("No VLC subtitle track selected yet.");
	}
	if (!montageVlcPreviewLoadedSubtitlePath.empty()) {
		ImGui::TextDisabled("%s", montageVlcPreviewLoadedSubtitlePath.c_str());
	}
}
#endif

void ofApp::rebuildMontageSubtitleTrackFromText() {
	montageSubtitleTrack = {};
	if (trim(montageSrtText).empty()) {
		montagePreviewBundle.montageTrack = {};
		selectedMontageCueIndex = -1;
		return;
	}

	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;
	if (!ofxGgmlSimpleSrtSubtitleParser::parseText(montageSrtText, cues, error)) {
		montagePreviewBundle.montageTrack = {};
		selectedMontageCueIndex = -1;
		return;
	}

	montageSubtitleTrack.title = trim(montageEdlTitle).empty() ? "MONTAGE" : trim(montageEdlTitle);
	montageSubtitleTrack.cues.reserve(cues.size());
	for (size_t i = 0; i < cues.size(); ++i) {
		const auto & cue = cues[i];
		ofxGgmlMontageSubtitleCue montageCue;
		montageCue.index = static_cast<int>(i + 1);
		montageCue.startSeconds = std::max(0.0, static_cast<double>(cue.startMs) / 1000.0);
		montageCue.endSeconds = std::max(montageCue.startSeconds, static_cast<double>(cue.endMs) / 1000.0);
		montageCue.text = cue.text;
		montageSubtitleTrack.cues.push_back(std::move(montageCue));
	}

	selectedMontageCueIndex = montageSubtitleTrack.cues.empty()
		? -1
		: std::clamp(selectedMontageCueIndex, 0, static_cast<int>(montageSubtitleTrack.cues.size()) - 1);
	montagePreviewBundle.sourceVideoPath = trim(visionVideoPath);
	montagePreviewBundle.montageTrack.title = montageSubtitleTrack.title;
	montagePreviewBundle.montageTrack.timingMode = ofxGgmlMontagePreviewTimingMode::Montage;
	montagePreviewBundle.montageTrack.cues = montageSubtitleTrack.cues;
}

void ofApp::drawVisionVideoPreview(const std::string & videoPath) {
	if (videoPath.empty()) {
		return;
	}
	if (!visionPreviewVideoError.empty()) {
		ImGui::TextDisabled("%s", visionPreviewVideoError.c_str());
		return;
	}
	if (!visionPreviewVideoReady || !visionPreviewVideo.isLoaded() ||
		!visionPreviewVideo.getTexture().isAllocated()) {
		ImGui::TextDisabled("Video preview will appear here after the file loads.");
		return;
	}
	ImGui::TextDisabled(
		"Video preview: %d x %d",
		visionPreviewVideo.getWidth(),
		visionPreviewVideo.getHeight());
	drawMediaTexturePreview(visionPreviewVideo, "##VisionVideoPreview");

	const float durationSeconds = std::max(0.0f, visionPreviewVideo.getDuration());
	float previewPosition = std::clamp(visionPreviewVideo.getPosition(), 0.0f, 1.0f);
	const float currentSeconds = previewPosition * durationSeconds;
	if (ImGui::Button(visionPreviewVideo.isPaused() ? "Play preview" : "Pause preview", ImVec2(120, 0))) {
		visionPreviewVideo.setPaused(!visionPreviewVideo.isPaused());
		if (!visionPreviewVideo.isPaused()) {
			visionPreviewVideo.play();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Restart preview", ImVec2(120, 0))) {
		visionPreviewVideo.play();
		visionPreviewVideo.setPosition(0.0f);
		if (visionPreviewVideo.isPaused()) {
			visionPreviewVideo.update();
		}
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%.2fs / %.2fs", currentSeconds, durationSeconds);

	ImGui::SetNextItemWidth(-1);
	if (ImGui::SliderFloat("Preview position", &previewPosition, 0.0f, 1.0f, "%.3f")) {
		visionPreviewVideo.setPosition(previewPosition);
		visionPreviewVideo.update();
	}

	if (montageSubtitlePlaybackEnabled &&
		getSelectedMontagePreviewTimingMode() == ofxGgmlMontagePreviewTimingMode::Source &&
		!montageSourceSubtitleTrack.cues.empty()) {
		const int activeCueIndex = findActiveMontagePreviewCueIndex();
		if (activeCueIndex >= 0 &&
			activeCueIndex < static_cast<int>(montageSourceSubtitleTrack.cues.size())) {
			const auto & cue = montageSourceSubtitleTrack.cues[static_cast<size_t>(activeCueIndex)];
			ImGui::Separator();
			ImGui::TextDisabled("Live source-timed subtitle");
			ImGui::TextWrapped("%s", cue.text.c_str());
		}
	}
}

void ofApp::ensureDiffusionPreviewResources() {
	ensureLocalImagePreview(
		trim(diffusionInitImagePath),
		diffusionInitPreviewImage,
		diffusionInitPreviewLoadedPath,
		diffusionInitPreviewError);
	ensureLocalImagePreview(
		trim(diffusionMaskImagePath),
		diffusionMaskPreviewImage,
		diffusionMaskPreviewLoadedPath,
		diffusionMaskPreviewError);

	std::string outputPreviewPath;
	for (const auto & image : diffusionGeneratedImages) {
		if (image.selected && !trim(image.path).empty()) {
			outputPreviewPath = trim(image.path);
			break;
		}
	}
	if (outputPreviewPath.empty() && !diffusionGeneratedImages.empty()) {
		outputPreviewPath = trim(diffusionGeneratedImages.front().path);
	}
	ensureLocalImagePreview(
		outputPreviewPath,
		diffusionOutputPreviewImage,
		diffusionOutputPreviewLoadedPath,
		diffusionOutputPreviewError);
}

void ofApp::drawDiffusionImagePreview(
	const char * label,
	const std::string & imagePath,
	ofImage & previewImage,
	const std::string & errorMessage,
	const char * childId) {
	drawLocalImagePreview(label, imagePath, previewImage, errorMessage, childId);
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
	const std::string serverUrl = trim(speechServerUrl);
	const std::string serverModel = trim(speechServerModel);
	const std::string prompt = trim(speechPrompt);
	const std::string languageHint = trim(speechLanguageHint);
	const int taskIndex = std::clamp(speechTaskIndex, 0, 1);
	const bool returnTimestamps = speechReturnTimestamps;

	workerThread = std::thread([this, profileBase, audioPath, executable, modelPath, serverUrl, serverModel, prompt, languageHint, taskIndex, returnTimestamps]() {
		auto setPending = [this](const std::string & text) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingOutput = text;
			pendingRole = "assistant";
			pendingMode = AiMode::Speech;
		};

		try {
			SpeechExecutionPlan plan;
			std::string planError;
			if (!buildSpeechExecutionPlan(
					profileBase,
					audioPath,
					executable,
					modelPath,
					serverUrl,
					serverModel,
					prompt,
					languageHint,
					taskIndex,
					returnTimestamps,
					plan,
					planError)) {
				setPending("[Error] " + planError);
				generating.store(false);
				return;
			}

			const ofxGgmlSpeechResult result = executeSpeechExecutionPlan(
				plan,
				[this](const std::string & status) {
					std::lock_guard<std::mutex> lock(streamMutex);
					streamingOutput = status;
				},
				[this](ofLogLevel level, const std::string & message) {
					logWithLevel(level, message);
				});
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

void ofApp::runTtsInference() {
	if (generating.load()) return;

	ensureTtsProfilesLoaded();

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::Tts;
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = "Preparing TTS synthesis request...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	const ofxGgmlTtsModelProfile profileBase = getSelectedTtsProfile();
	const std::string text = trim(ttsInput);
	const std::string executablePath = trim(ttsExecutablePath);
	const std::string modelPath = trim(ttsModelPath);
	const std::string speakerPath = trim(ttsSpeakerPath);
	const std::string speakerReferencePath = trim(ttsSpeakerReferencePath);
	const std::string outputPath = trim(ttsOutputPath);
	const std::string promptAudioPath = trim(ttsPromptAudioPath);
	const std::string language = trim(ttsLanguage);
	const int taskIndex = std::clamp(ttsTaskIndex, 0, 2);
	const int requestSeed = ttsSeed;
	const int requestMaxTokens = std::max(0, ttsMaxTokens);
	const float requestTemperature = std::isfinite(ttsTemperature)
		? std::clamp(ttsTemperature, 0.0f, 2.0f)
		: 0.4f;
	const float requestPenalty = std::isfinite(ttsRepetitionPenalty)
		? std::clamp(ttsRepetitionPenalty, 1.0f, 3.0f)
		: 1.1f;
	const int requestRange = std::clamp(ttsRepetitionRange, 0, 512);
	const int requestTopK = std::clamp(ttsTopK, 0, 200);
	const float requestTopP = std::isfinite(ttsTopP)
		? std::clamp(ttsTopP, 0.0f, 1.0f)
		: 0.9f;
	const float requestMinP = std::isfinite(ttsMinP)
		? std::clamp(ttsMinP, 0.0f, 1.0f)
		: 0.05f;
	const bool requestStreamAudio = ttsStreamAudio;
	const bool requestNormalizeText = ttsNormalizeText;
	const auto backend = createConfiguredTtsBackend(executablePath);
	const std::string backendLabel =
		backend ? backend->backendName() : std::string("TTS backend");

	workerThread = std::thread([this, backend, backendLabel, profileBase, text, modelPath, speakerPath, speakerReferencePath, outputPath, promptAudioPath, language, taskIndex, requestSeed, requestMaxTokens, requestTemperature, requestPenalty, requestRange, requestTopK, requestTopP, requestMinP, requestStreamAudio, requestNormalizeText]() {
		auto setPending = [this](const std::string & textValue) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingOutput = textValue;
			pendingRole = "assistant";
			pendingMode = AiMode::Tts;
		};

		auto clearPendingTtsArtifacts = [this]() {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingTtsBackendName.clear();
			pendingTtsElapsedMs = 0.0f;
			pendingTtsResolvedSpeakerPath.clear();
			pendingTtsAudioFiles.clear();
			pendingTtsMetadata.clear();
		};

		try {
			const auto task = static_cast<ofxGgmlTtsTask>(taskIndex);
			if (text.empty()) {
				clearPendingTtsArtifacts();
				setPending("[Error] Enter text to synthesize first.");
				generating.store(false);
				return;
			}
			if (task == ofxGgmlTtsTask::CloneVoice && speakerReferencePath.empty()) {
				clearPendingTtsArtifacts();
				setPending("[Error] Select a reference audio file for Clone Voice.");
				generating.store(false);
				return;
			}
			if (task == ofxGgmlTtsTask::ContinueSpeech && promptAudioPath.empty()) {
				clearPendingTtsArtifacts();
				setPending("[Error] Select prompt audio for Continue Speech.");
				generating.store(false);
				return;
			}

			const std::string effectiveModelPath = modelPath.empty()
				? suggestedModelPath(profileBase.modelPath, profileBase.modelFileHint)
				: modelPath;
			const std::string effectiveSpeakerPath = speakerPath.empty()
				? suggestedModelPath(profileBase.speakerPath, profileBase.speakerFileHint)
				: speakerPath;
			std::string effectiveOutputPath = outputPath;
			if (effectiveOutputPath.empty()) {
				effectiveOutputPath = ofToDataPath("generated/tts_output.wav", true);
			}

			const std::filesystem::path outputDir =
				std::filesystem::path(effectiveOutputPath).parent_path();
			if (!outputDir.empty()) {
				std::error_code dirEc;
				std::filesystem::create_directories(outputDir, dirEc);
				if (dirEc) {
					clearPendingTtsArtifacts();
					setPending("[Error] Failed to create TTS output directory: " + outputDir.string());
					generating.store(false);
					return;
				}
			}

			ofxGgmlTtsRequest request;
			request.task = task;
			request.text = text;
			request.modelPath = effectiveModelPath;
			request.speakerPath = effectiveSpeakerPath;
			request.speakerReferencePath = speakerReferencePath;
			request.language = language;
			request.outputPath = effectiveOutputPath;
			request.promptAudioPath = promptAudioPath;
			request.seed = requestSeed;
			request.maxTokens = requestMaxTokens;
			request.temperature = requestTemperature;
			request.repetitionPenalty = requestPenalty;
			request.repetitionRange = requestRange;
			request.topK = requestTopK;
			request.topP = requestTopP;
			request.minP = requestMinP;
			request.streamAudio = requestStreamAudio;
			request.normalizeText = requestNormalizeText;

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput = "Calling " + backendLabel + "...";
			}

			if (!backend) {
				clearPendingTtsArtifacts();
				setPending("[Error] TTS backend is not available.");
				generating.store(false);
				return;
			}

			const ofxGgmlTtsResult result = backend->synthesize(request);
			if (cancelRequested.load()) {
				clearPendingTtsArtifacts();
				setPending("[Cancelled] TTS synthesis cancelled.");
			} else if (result.success) {
				std::ostringstream summary;
				summary << "Synthesized audio";
				if (!result.backendName.empty()) {
					summary << " via " << result.backendName;
				}
				if (result.elapsedMs > 0.0f) {
					summary << " in " << ofxGgmlHelpers::formatDurationMs(result.elapsedMs);
				}
				summary << ".";
				if (!result.audioFiles.empty()) {
					summary << "\n\nGenerated audio:";
					for (const auto & artifact : result.audioFiles) {
						summary << "\n- " << artifact.path;
					}
				} else if (!effectiveOutputPath.empty()) {
					summary << "\n\nRequested output: " << effectiveOutputPath;
				}
				if (!result.rawOutput.empty()) {
					summary << "\n\n" << result.rawOutput;
				}

				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingTtsBackendName = result.backendName;
					pendingTtsElapsedMs = result.elapsedMs;
					pendingTtsResolvedSpeakerPath = result.speakerPath;
					pendingTtsAudioFiles = result.audioFiles;
					pendingTtsMetadata = result.metadata;
				}
				setPending(summary.str());
			} else {
				clearPendingTtsArtifacts();
				const std::string message = result.error.empty()
					? "TTS synthesis failed."
					: result.error;
				setPending("[Error] " + message);
			}
		} catch (const std::exception & e) {
			clearPendingTtsArtifacts();
			setPending(std::string("[Error] TTS synthesis failed: ") + e.what());
		} catch (...) {
			clearPendingTtsArtifacts();
			setPending("[Error] TTS synthesis failed.");
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

void ofApp::initializeBackendEngine(bool announceReinit) {
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

	auto result = ggml.setup(settings);
	engineReady = result.isOk();
	if (engineReady) {
		engineStatus = "Ready (" + ggml.getBackendName() + ")";
		devices = ggml.listDevices();
		lastBackendUsed = ggml.getBackendName();
		backendNames.clear();
		for (const auto & d : devices) {
			backendNames.push_back(d.name);
		}
		syncSelectedBackendIndex();
	} else {
		engineStatus = "Failed to initialize ggml engine";
		devices.clear();
	}

	if (announceReinit) {
		logWithLevel(OF_LOG_NOTICE, "Backend reinitialized: " + engineStatus);
	}
}

void ofApp::reinitBackend() {
 if (generating.load()) return;
 initializeBackendEngine(true);
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
const auto recentScriptTouchedFilesSnapshot = recentScriptTouchedFiles;
const std::string lastScriptFailureReasonSnapshot = lastScriptFailureReason;
const std::string scriptBackendLabelSnapshot = [&]() {
	if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
		const std::string serverUrl = trim(textServerUrl);
		return serverUrl.empty()
			? std::string("llama-server")
			: std::string("llama-server @ ") + serverUrl;
	}
	const std::string cliPath = trim(llamaCliCommand);
	return cliPath.empty() ? std::string("llama-completion") : cliPath;
}();
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
		recentScriptTouchedFilesSnapshot, lastScriptFailureReasonSnapshot,
		scriptBackendLabelSnapshot,
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
		context.activeMode = "Script";
		context.selectedBackend = scriptBackendLabelSnapshot;
		context.recentTouchedFiles = recentScriptTouchedFilesSnapshot;
		context.lastFailureReason = lastScriptFailureReasonSnapshot;
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

	bool likelyCutoff = isLikelyCutoffOutput(result, static_cast<int>(mode));

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
			likelyCutoff = isLikelyCutoffOutput(continuationOut, static_cast<int>(mode));
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
	const bool hasPendingTextOutput = !pendingOutput.empty();
	if (!hasPendingTextOutput && !pendingImageSearchDirty) {
		return;
	}

	if (hasPendingTextOutput) {
		switch (pendingMode) {
		case AiMode::Chat:
			chatMessages.push_back({"assistant", pendingOutput, ofGetElapsedTimef()});
			fprintf(stderr, "%s\n", formatConsoleLogLine("ChatWindow", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::Script:
			scriptOutput = pendingOutput;
			scriptMessages.push_back({"assistant", pendingOutput, ofGetElapsedTimef()});
			if (pendingOutput.rfind("[Error]", 0) != 0) {
				scriptProjectMemory.addInteraction(lastScriptRequest, pendingOutput);
				lastScriptFailureReason.clear();
				const auto structured = ofxGgmlCodeAssistant::parseStructuredResult(pendingOutput);
				std::vector<std::string> touchedFiles;
				touchedFiles.reserve(
					structured.filesToTouch.size() +
					structured.patchOperations.size());
				for (const auto & fileIntent : structured.filesToTouch) {
					if (!fileIntent.filePath.empty()) {
						touchedFiles.push_back(fileIntent.filePath);
					}
				}
				for (const auto & patchOperation : structured.patchOperations) {
					if (!patchOperation.filePath.empty()) {
						touchedFiles.push_back(patchOperation.filePath);
					}
				}
				std::sort(touchedFiles.begin(), touchedFiles.end());
				touchedFiles.erase(
					std::unique(touchedFiles.begin(), touchedFiles.end()),
					touchedFiles.end());
				recentScriptTouchedFiles = touchedFiles;
				if (!structured.verificationCommands.empty()) {
					cachedScriptVerificationCommands = structured.verificationCommands;
				}
			} else {
				lastScriptFailureReason = pendingOutput;
			}
			fprintf(stderr, "%s\n", formatConsoleLogLine("Script", "AI", pendingOutput).c_str());
			break;
		case AiMode::Summarize:
			summarizeOutput = pendingOutput;
			fprintf(stderr, "%s\n", formatConsoleLogLine("Summarize", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::Write:
			writeOutput = pendingOutput;
			fprintf(stderr, "%s\n", formatConsoleLogLine("Write", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::Translate:
			translateOutput = pendingOutput;
			fprintf(stderr, "%s\n", formatConsoleLogLine("Translate", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::Custom:
			customOutput = pendingOutput;
			fprintf(stderr, "%s\n", formatConsoleLogLine("Custom", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::Vision:
			visionOutput = pendingOutput;
			visionSampledVideoFrames = pendingVisionSampledVideoFrames;
			montageSummary = pendingMontageSummary;
			montageEditorBrief = pendingMontageEditorBrief;
			montageEdlText = pendingMontageEdlText;
			montageSrtText = pendingMontageSrtText;
			montageVttText = pendingMontageVttText;
			montagePreviewBundle = pendingMontagePreviewBundle;
			montageSubtitleTrack = pendingMontageSubtitleTrack;
			montageSourceSubtitleTrack = pendingMontageSourceSubtitleTrack;
			montagePreviewTimelineSeconds = 0.0;
			montagePreviewTimelinePlaying = false;
			montagePreviewTimelineLastTickTime = 0.0f;
			montagePreviewSubtitleSlavePath.clear();
#if OFXGGML_HAS_OFXVLC4
			montageVlcPreviewLoadedSubtitlePath.clear();
			montageVlcPreviewError.clear();
#endif
			montagePreviewStatusMessage =
				montagePreviewBundle.montageTrack.cues.empty() &&
				montagePreviewBundle.sourceTrack.cues.empty()
					? std::string()
					: ofxGgmlMontagePreviewBridge::summarizeBundle(montagePreviewBundle);
			selectedMontageCueIndex = montageSubtitleTrack.cues.empty()
				? -1
				: std::clamp(selectedMontageCueIndex, 0, static_cast<int>(montageSubtitleTrack.cues.size()) - 1);
			videoPlanSummary = pendingVideoPlanSummary;
			videoEditPlanSummary = pendingVideoEditPlanSummary;
			if (!pendingVideoPlanJson.empty()) {
				copyStringToBuffer(videoPlanJson, sizeof(videoPlanJson), pendingVideoPlanJson);
			}
			if (!pendingVideoEditPlanJson.empty()) {
				copyStringToBuffer(videoEditPlanJson, sizeof(videoEditPlanJson), pendingVideoEditPlanJson);
			}
			fprintf(stderr, "%s\n", formatConsoleLogLine("Vision", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::Speech:
			speechOutput = pendingOutput;
			speechDetectedLanguage = pendingSpeechDetectedLanguage;
			speechTranscriptPath = pendingSpeechTranscriptPath;
			speechSrtPath = pendingSpeechSrtPath;
			speechSegmentCount = pendingSpeechSegmentCount;
			fprintf(stderr, "%s\n", formatConsoleLogLine("Speech", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::Tts:
			ttsOutput = pendingOutput;
			ttsBackendName = pendingTtsBackendName;
			ttsElapsedMs = pendingTtsElapsedMs;
			ttsResolvedSpeakerPath = pendingTtsResolvedSpeakerPath;
			ttsAudioFiles = pendingTtsAudioFiles;
			ttsMetadata = pendingTtsMetadata;
			fprintf(stderr, "%s\n", formatConsoleLogLine("TTS", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::Diffusion:
			diffusionOutput = pendingOutput;
			diffusionBackendName = pendingDiffusionBackendName;
			diffusionElapsedMs = pendingDiffusionElapsedMs;
			diffusionGeneratedImages = pendingDiffusionImages;
			diffusionMetadata = pendingDiffusionMetadata;
			fprintf(stderr, "%s\n", formatConsoleLogLine("Diffusion", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::Clip:
			clipOutput = pendingOutput;
			clipBackendName = pendingClipBackendName;
			clipElapsedMs = pendingClipElapsedMs;
			clipEmbeddingDimension = pendingClipEmbeddingDimension;
			clipHits = pendingClipHits;
			fprintf(stderr, "%s\n", formatConsoleLogLine("CLIP", "AI", pendingOutput, true).c_str());
			break;
		}
	}

	if (pendingImageSearchDirty) {
		imageSearchOutput = pendingImageSearchOutput;
		imageSearchBackendName = pendingImageSearchBackendName;
		imageSearchElapsedMs = pendingImageSearchElapsedMs;
		imageSearchResults = pendingImageSearchResults;
		if (imageSearchResults.empty()) {
			selectedImageSearchResultIndex = -1;
			imageSearchPreviewImage.clear();
			imageSearchPreviewLoadedPath.clear();
			imageSearchPreviewError.clear();
			imageSearchPreviewSourceUrl.clear();
		} else {
			selectedImageSearchResultIndex = std::clamp(
				selectedImageSearchResultIndex,
				0,
				static_cast<int>(imageSearchResults.size()) - 1);
		}
		fprintf(stderr, "%s\n", formatConsoleLogLine("Image Search", "AI", imageSearchOutput, true).c_str());
	}
	if (pendingCitationDirty) {
		citationOutput = pendingCitationOutput;
		citationBackendName = pendingCitationBackendName;
		citationElapsedMs = pendingCitationElapsedMs;
		citationResults = pendingCitationResults;
		fprintf(stderr, "%s\n", formatConsoleLogLine("Citations", "AI", citationOutput, true).c_str());
	}

	pendingOutput.clear();
	pendingSpeechDetectedLanguage.clear();
	pendingSpeechTranscriptPath.clear();
	pendingSpeechSrtPath.clear();
	pendingSpeechSegmentCount = 0;
	pendingTtsBackendName.clear();
	pendingTtsElapsedMs = 0.0f;
	pendingTtsResolvedSpeakerPath.clear();
	pendingTtsAudioFiles.clear();
	pendingTtsMetadata.clear();
	pendingMontageSummary.clear();
	pendingMontageEditorBrief.clear();
	pendingMontageEdlText.clear();
	pendingMontageSrtText.clear();
	pendingMontageVttText.clear();
	pendingMontagePreviewBundle = {};
	pendingMontageSubtitleTrack = {};
	pendingMontageSourceSubtitleTrack = {};
	pendingVideoPlanJson.clear();
	pendingVideoPlanSummary.clear();
	pendingVideoEditPlanJson.clear();
	pendingVideoEditPlanSummary.clear();
	pendingVisionSampledVideoFrames.clear();
	pendingDiffusionBackendName.clear();
	pendingDiffusionElapsedMs = 0.0f;
  pendingDiffusionImages.clear();
  pendingDiffusionMetadata.clear();
    pendingImageSearchOutput.clear();
    pendingImageSearchBackendName.clear();
    pendingImageSearchElapsedMs = 0.0f;
    pendingImageSearchResults.clear();
  pendingImageSearchDirty = false;
	pendingCitationOutput.clear();
	pendingCitationBackendName.clear();
	pendingCitationElapsedMs = 0.0f;
	pendingCitationResults.clear();
	pendingCitationDirty = false;
    pendingClipBackendName.clear();
  pendingClipElapsedMs = 0.0f;
  pendingClipEmbeddingDimension = 0;
  pendingClipHits.clear();
  }

// ---------------------------------------------------------------------------
// Performance metrics window
// ---------------------------------------------------------------------------

void ofApp::drawPerformanceWindow() {
	performancePanel.draw(
		showPerformance,
		ggml,
		devices,
		lastComputeMs,
		lastNodeCount,
		lastBackendUsed,
		selectedBackendIndex,
		backendNames,
		numThreads,
		contextSize,
		batchSize,
		textInferenceBackend,
		detectedModelLayers,
		gpuLayers,
		seed,
		maxTokens,
		temperature,
		topP,
		topK,
		minP,
		repeatPenalty,
		devices);  // Pass devices as out parameter for refresh
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

void ofApp::drawStatusBar() {
	statusBar.draw(
		engineStatus,
		modelPresets,
		selectedModelIndex,
		activeMode,
		modeLabels,
		chatLanguageIndex,
		chatLanguages,
		selectedLanguageIndex,
		scriptLanguages,
		maxTokens,
		temperature,
		topP,
		topK,
		minP,
		liveContextMode,
		gpuLayers,
		detectedModelLayers,
		generating,
		generationStartTime,
		streamingOutput,
		streamMutex,
		lastComputeMs);
}

void ofApp::drawDeviceInfoWindow() {
	deviceInfoPanel.draw(showDeviceInfo, ggml, devices);
}

void ofApp::drawLogWindow() {
	logPanel.draw(showLog, logMessages, logMutex);
}

ofxGgmlLiveSpeechSettings ofApp::makeLiveSpeechSettings() const {
	ofxGgmlLiveSpeechSettings settings;
	if (!speechProfiles.empty()) {
		const int clampedIndex = std::clamp(
			selectedSpeechProfileIndex,
			0,
			std::max(0, static_cast<int>(speechProfiles.size()) - 1));
		settings.profile = speechProfiles[static_cast<size_t>(clampedIndex)];
	}
	settings.executable = trim(speechExecutable);
	settings.modelPath = trim(speechModelPath);
	settings.serverUrl = trim(speechServerUrl);
	settings.serverModel = trim(speechServerModel);
	settings.prompt = trim(speechPrompt);
	settings.languageHint = trim(speechLanguageHint);
	settings.task = static_cast<ofxGgmlSpeechTask>(std::clamp(speechTaskIndex, 0, 1));
	settings.sampleRate = speechInputSampleRate;
	settings.intervalSeconds = speechLiveIntervalSeconds;
	settings.windowSeconds = speechLiveWindowSeconds;
	settings.overlapSeconds = speechLiveOverlapSeconds;
	settings.enabled = speechLiveTranscriptionEnabled;
	settings.returnTimestamps = false;
	return settings;
}

void ofApp::applyLiveSpeechTranscriberSettings() {
	speechLiveTranscriber.setSettings(makeLiveSpeechSettings());
	speechLiveTranscriber.setLogCallback(
		[this](ofLogLevel level, const std::string & message) {
			logWithLevel(level, message);
		});
}

void ofApp::audioIn(ofSoundBuffer & input) {
	if (!speechRecording) {
		return;
	}

	const size_t channels = std::max<size_t>(1, input.getNumChannels());
	const size_t frames = input.getNumFrames();
	std::vector<float> monoSamples;
	monoSamples.reserve(frames);
	{
		std::lock_guard<std::mutex> lock(speechRecordMutex);
		speechRecordedSamples.reserve(speechRecordedSamples.size() + frames);
		for (size_t frame = 0; frame < frames; ++frame) {
			float mono = 0.0f;
			for (size_t channel = 0; channel < channels; ++channel) {
				mono += input[frame * channels + channel];
			}
			mono /= static_cast<float>(channels);
			speechRecordedSamples.push_back(mono);
			monoSamples.push_back(mono);
		}
	}
	if (!monoSamples.empty()) {
		speechLiveTranscriber.appendMonoSamples(monoSamples);
	}
}

bool ofApp::ensureSpeechInputStreamReady() {
	const bool configMatches =
		speechInputStreamConfigured &&
		speechInputStreamConfigSampleRate == speechInputSampleRate &&
		speechInputStreamConfigChannels == speechInputChannels &&
		speechInputStreamConfigBufferSize == speechInputBufferSize;
	if (configMatches && speechInputStream.getSoundStream() != nullptr) {
		return true;
	}

	if (speechInputStream.getSoundStream() != nullptr) {
		speechInputStream.stop();
		speechInputStream.close();
	}

	ofSoundStreamSettings settings;
	settings.setInListener(this);
	settings.sampleRate = speechInputSampleRate;
	settings.numInputChannels = speechInputChannels;
	settings.numOutputChannels = 0;
	settings.bufferSize = speechInputBufferSize;
	settings.numBuffers = 4;
	if (!speechInputStream.setup(settings)) {
		speechInputStreamConfigured = false;
		return false;
	}

	speechInputStreamConfigured = true;
	speechInputStreamConfigSampleRate = speechInputSampleRate;
	speechInputStreamConfigChannels = speechInputChannels;
	speechInputStreamConfigBufferSize = speechInputBufferSize;
	return true;
}

bool ofApp::startSpeechRecording() {
	if (speechRecording) {
		return true;
	}

	try {
		applyLiveSpeechTranscriberSettings();
		{
			std::lock_guard<std::mutex> lock(speechRecordMutex);
			speechRecordedSamples.clear();
			speechRecordedTempPath.clear();
		}
		if (!ensureSpeechInputStreamReady()) {
			logWithLevel(OF_LOG_ERROR, "Failed to open microphone input stream for speech mode.");
			return false;
		}
		speechRecording = true;
		speechRecordingStartTime = ofGetElapsedTimef();
		if (speechLiveTranscriptionEnabled) {
			speechLiveTranscriber.beginCapture(true);
		}
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
	if (speechRecording) {
		speechRecording = false;
		speechRecordingStartTime = 0.0f;
	}
	if (!keepBufferedAudio) {
		std::lock_guard<std::mutex> lock(speechRecordMutex);
		speechRecordedSamples.clear();
		speechRecordedTempPath.clear();
	}
	applyLiveSpeechTranscriberSettings();
	speechLiveTranscriber.stopCapture(keepBufferedAudio);
	if (!speechLiveTranscriptionEnabled) {
		speechLiveTranscriber.reset(!keepBufferedAudio);
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

bool ofApp::ensureTtsProfilesLoaded() {
	if (ttsProfiles.empty()) {
		ttsProfiles = ofxGgmlTtsInference::defaultProfiles();
		selectedTtsProfileIndex = 0;
	}
	if (ttsProfiles.empty()) {
		selectedTtsProfileIndex = 0;
		return false;
	}
	selectedTtsProfileIndex = std::clamp(
		selectedTtsProfileIndex,
		0,
		std::max(0, static_cast<int>(ttsProfiles.size()) - 1));
	return true;
}

ofxGgmlTtsModelProfile ofApp::getSelectedTtsProfile() const {
	if (ttsProfiles.empty()) {
		return {};
	}
	const int clampedIndex = std::clamp(
		selectedTtsProfileIndex,
		0,
		std::max(0, static_cast<int>(ttsProfiles.size()) - 1));
	return ttsProfiles[static_cast<size_t>(clampedIndex)];
}

void ofApp::applyTtsProfileDefaults(
	const ofxGgmlTtsModelProfile & profile,
	bool onlyWhenEmpty) {
	const std::string suggestedPath =
		suggestedModelPath(profile.modelPath, profile.modelFileHint);
	if (!suggestedPath.empty() &&
		(!onlyWhenEmpty || trim(ttsModelPath).empty())) {
		copyStringToBuffer(ttsModelPath, sizeof(ttsModelPath), suggestedPath);
	}
	const std::string suggestedSpeakerPath =
		suggestedModelPath(profile.speakerPath, profile.speakerFileHint);
	if (!suggestedSpeakerPath.empty() &&
		(!onlyWhenEmpty || trim(ttsSpeakerPath).empty())) {
		copyStringToBuffer(ttsSpeakerPath, sizeof(ttsSpeakerPath), suggestedSpeakerPath);
	}
	if (trim(ttsOutputPath).empty()) {
		copyStringToBuffer(
			ttsOutputPath,
			sizeof(ttsOutputPath),
			ofToDataPath("generated/tts_output.wav", true));
	}
}

std::string ofApp::resolveConfiguredTtsExecutable() const {
	return ofxGgmlChatLlmTtsAdapters::resolveChatLlmExecutable(trim(ttsExecutablePath));
}

std::shared_ptr<ofxGgmlTtsBackend> ofApp::createConfiguredTtsBackend(
	const std::string & executableHint) const {
	ofxGgmlChatLlmTtsAdapters::RuntimeOptions runtimeOptions;
	runtimeOptions.executablePath =
		executableHint.empty() ? trim(ttsExecutablePath) : executableHint;
	return ofxGgmlChatLlmTtsAdapters::createBackend(runtimeOptions, "ChatLLM TTS");
}

bool ofApp::ensureClipBackendConfigured(
	const std::string & modelPath,
	int verbosity,
	bool normalizeEmbeddings) {
#if OFXGGML_HAS_CLIPCPP
	const std::string trimmedModelPath = trim(modelPath);
	if (trimmedModelPath.empty()) {
		return false;
	}
	const int clampedVerbosity = std::clamp(verbosity, 0, 2);
	const bool needsReload =
		!clipInference.getBackend() ||
		clipInference.getBackend()->backendName() != "clip.cpp" ||
		configuredClipBackendModelPath != trimmedModelPath ||
		configuredClipBackendVerbosity != clampedVerbosity ||
		configuredClipBackendNormalize != normalizeEmbeddings;
	if (needsReload) {
		ofxGgmlClipCppAdapters::RuntimeOptions options;
		options.verbosity = clampedVerbosity;
		options.normalizeByDefault = normalizeEmbeddings;
		ofxGgmlClipCppAdapters::attachBackend(
			clipInference,
			trimmedModelPath,
			options,
			"clip.cpp");
		configuredClipBackendModelPath = trimmedModelPath;
		configuredClipBackendVerbosity = clampedVerbosity;
		configuredClipBackendNormalize = normalizeEmbeddings;
	}
	return clipInference.getBackend() != nullptr;
#else
	(void)modelPath;
	(void)verbosity;
	(void)normalizeEmbeddings;
	return clipInference.getBackend() != nullptr;
#endif
}

bool ofApp::ensureDiffusionBackendConfigured() {
#if OFXGGML_HAS_OFXSTABLEDIFFUSION
	if (!stableDiffusionEngine) {
		stableDiffusionEngine = std::make_shared<ofxStableDiffusion>();
	}
	const auto existingBackend = diffusionInference.getBackend();
	const auto existingBridge =
		std::dynamic_pointer_cast<ofxGgmlStableDiffusionBridgeBackend>(existingBackend);
	const bool backendNeedsAttach =
		!existingBackend ||
		!existingBridge ||
		!existingBridge->isConfigured() ||
		existingBackend->backendName() != "ofxStableDiffusion";
	if (backendNeedsAttach) {
		ofxGgmlStableDiffusionAdapters::RuntimeOptions runtimeOptions;
		runtimeOptions.clipInference =
			std::shared_ptr<ofxGgmlClipInference>(&clipInference, [](ofxGgmlClipInference *) {});
		diffusionInference.setBackend(
			ofxGgmlStableDiffusionAdapters::createImageBackend(
				stableDiffusionEngine,
				runtimeOptions));
	}
	return diffusionInference.getBackend() != nullptr;
#else
	return diffusionInference.getBackend() != nullptr;
#endif
}

bool ofApp::ensureDiffusionClipBackendConfigured() {
	return ensureClipBackendConfigured(trim(clipModelPath), clipVerbosity, clipNormalizeEmbeddings);
}

std::string ofApp::getPreferredDiffusionReuseImagePath() const {
	for (const auto & image : diffusionGeneratedImages) {
		if (image.selected && !trim(image.path).empty()) {
			return trim(image.path);
		}
	}
	if (!diffusionGeneratedImages.empty()) {
		return trim(diffusionGeneratedImages.front().path);
	}
	return trim(visionImagePath);
}

void ofApp::setDiffusionInitImagePath(const std::string & path, bool promoteTask) {
	const std::string trimmedPath = trim(path);
	if (trimmedPath.empty()) {
		return;
	}
	copyStringToBuffer(diffusionInitImagePath, sizeof(diffusionInitImagePath), trimmedPath);
	if (promoteTask &&
		static_cast<ofxGgmlImageGenerationTask>(std::clamp(diffusionTaskIndex, 0, 6)) ==
			ofxGgmlImageGenerationTask::TextToImage) {
		diffusionTaskIndex = static_cast<int>(ofxGgmlImageGenerationTask::ImageToImage);
	}
	autoSaveSession();
}

void ofApp::copyDiffusionOutputsToClipPaths() {
	std::ostringstream joined;
	for (size_t i = 0; i < diffusionGeneratedImages.size(); ++i) {
		if (i > 0) {
			joined << "\n";
		}
		joined << diffusionGeneratedImages[i].path;
	}
	copyStringToBuffer(clipImagePaths, sizeof(clipImagePaths), joined.str());
	autoSaveSession();
}

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
	if (modelPath.empty()) {
		return;
	}

	std::error_code ec;
	if (!std::filesystem::exists(modelPath, ec) || ec) {
		return;
	}

	detectedModelLayers = detectGgufLayerCountMetadata(modelPath);
	if (detectedModelLayers > 0 && shouldLog(OF_LOG_VERBOSE)) {
		logWithLevel(
			OF_LOG_VERBOSE,
			"Detected " + ofToString(detectedModelLayers) + " layers from GGUF metadata.");
	}
}

void ofApp::runHierarchicalReview(const std::string & overrideQuery) {
	const std::string effectiveReviewQuery = !trim(overrideQuery).empty()
		? trim(overrideQuery)
		: (std::strlen(scriptInput) > 0
			? std::string(scriptInput)
			: ofxGgmlCodeReview::defaultReviewQuery());
	runInference(AiMode::Script, effectiveReviewQuery);
}

void ofApp::drawSpeechPanel() {
	drawPanelHeader("Speech", "audio transcription and translation via Whisper backends");
	const float compactModeFieldWidth = std::min(320.0f, ImGui::GetContentRegionAvail().x);

	if (speechProfiles.empty()) {
		speechProfiles = ofxGgmlSpeechInference::defaultProfiles();
	}
	selectedSpeechProfileIndex = std::clamp(
		selectedSpeechProfileIndex,
		0,
		std::max(0, static_cast<int>(speechProfiles.size()) - 1));

	if (!speechProfiles.empty()) {
		std::vector<const char *> profileNames;
		profileNames.reserve(speechProfiles.size());
		for (const auto & profile : speechProfiles) {
			profileNames.push_back(profile.name.c_str());
		}
		ImGui::SetNextItemWidth(260);
		if (ImGui::Combo(
			"Speech profile",
			&selectedSpeechProfileIndex,
			profileNames.data(),
			static_cast<int>(profileNames.size()))) {
			const auto & profile = speechProfiles[static_cast<size_t>(selectedSpeechProfileIndex)];
			if (!profile.executable.empty()) {
				copyStringToBuffer(speechExecutable, sizeof(speechExecutable), profile.executable);
			}
			const std::string suggestedPath =
				suggestedModelPath(profile.modelPath, profile.modelFileHint);
			if (!suggestedPath.empty()) {
				copyStringToBuffer(speechModelPath, sizeof(speechModelPath), suggestedPath);
			}
			if (!profile.supportsTranslate &&
				speechTaskIndex == static_cast<int>(ofxGgmlSpeechTask::Translate)) {
				speechTaskIndex = static_cast<int>(ofxGgmlSpeechTask::Transcribe);
			}
			autoSaveSession();
		}
	}

	if (hasDeferredSpeechAudioPath) {
		copyStringToBuffer(
			speechAudioPath,
			sizeof(speechAudioPath),
			deferredSpeechAudioPath);
		hasDeferredSpeechAudioPath = false;
		deferredSpeechAudioPath.clear();
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Audio path", speechAudioPath, sizeof(speechAudioPath));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse audio...", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select audio file", false);
		if (result.bSuccess) {
			copyStringToBuffer(speechAudioPath, sizeof(speechAudioPath), result.getPath());
			autoSaveSession();
		}
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Server URL", speechServerUrl, sizeof(speechServerUrl));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Server model", speechServerModel, sizeof(speechServerModel));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	if (!speechServerStatusMessage.empty()) {
		ImGui::TextDisabled("%s", speechServerStatusMessage.c_str());
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Executable", speechExecutable, sizeof(speechExecutable));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Model path", speechModelPath, sizeof(speechModelPath));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse model...", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select Whisper model", false);
		if (result.bSuccess) {
			copyStringToBuffer(speechModelPath, sizeof(speechModelPath), result.getPath());
			autoSaveSession();
		}
	}

	static const char * speechTaskLabels[] = {"Transcribe", "Translate"};
	ImGui::SetNextItemWidth(180);
	ImGui::Combo("Speech task", &speechTaskIndex, speechTaskLabels, 2);
	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Language hint", speechLanguageHint, sizeof(speechLanguageHint));
	ImGui::Checkbox("Return timestamps", &speechReturnTimestamps);
	ImGui::InputTextMultiline(
		"Speech prompt",
		speechPrompt,
		sizeof(speechPrompt),
		ImVec2(-1, 80));

	const bool liveToggleChanged =
		ImGui::Checkbox("Live mic transcription", &speechLiveTranscriptionEnabled);
	if (liveToggleChanged) {
		applyLiveSpeechTranscriberSettings();
		if (!speechLiveTranscriptionEnabled) {
			speechLiveTranscriber.reset(false);
		} else if (speechRecording) {
			speechLiveTranscriber.beginCapture(false);
		}
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(180);
	ImGui::SliderFloat("Live interval", &speechLiveIntervalSeconds, 0.5f, 5.0f, "%.2f s");
	ImGui::SetNextItemWidth(180);
	ImGui::SliderFloat("Live window", &speechLiveWindowSeconds, 2.0f, 30.0f, "%.1f s");
	ImGui::SetNextItemWidth(180);
	ImGui::SliderFloat("Live overlap", &speechLiveOverlapSeconds, 0.0f, 3.0f, "%.2f s");
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		applyLiveSpeechTranscriberSettings();
		autoSaveSession();
	}

	const bool hasBufferedMicAudio = [&]() {
		std::lock_guard<std::mutex> lock(speechRecordMutex);
		return !speechRecordedSamples.empty();
	}();
	if (!speechRecording) {
		if (ImGui::Button("Start Mic Recording", ImVec2(160, 0))) {
			startSpeechRecording();
		}
	} else {
		if (ImGui::Button("Stop Mic", ImVec2(160, 0))) {
			stopSpeechRecording(true);
		}
		ImGui::SameLine();
		ImGui::TextDisabled(
			"Recording: %.1f s",
			std::max(0.0f, ofGetElapsedTimef() - speechRecordingStartTime));
	}
	if (hasBufferedMicAudio) {
		ImGui::SameLine();
		if (ImGui::Button("Use Recording", ImVec2(140, 0))) {
			const std::string tempPath = flushSpeechRecordingToTempWav();
			if (!tempPath.empty()) {
				deferredSpeechAudioPath = tempPath;
				hasDeferredSpeechAudioPath = true;
				autoSaveSession();
			}
		}
	}

	const bool canRunSpeech =
		!generating.load() &&
		(std::strlen(speechAudioPath) > 0 || hasBufferedMicAudio);
	ImGui::BeginDisabled(!canRunSpeech);
	if (ImGui::Button("Run Speech", ImVec2(140, 0))) {
		if (std::strlen(speechAudioPath) == 0) {
			const std::string tempPath = flushSpeechRecordingToTempWav();
			if (!tempPath.empty()) {
				copyStringToBuffer(speechAudioPath, sizeof(speechAudioPath), tempPath);
			}
		}
		runSpeechInference();
	}
	ImGui::EndDisabled();

	const ofxGgmlLiveSpeechSnapshot liveSnapshot = speechLiveTranscriber.getSnapshot();
	if (speechLiveTranscriptionEnabled) {
		ImGui::Separator();
		ImGui::TextDisabled(
			"Live status: %s | buffered %.1f s%s",
			liveSnapshot.status.empty() ? "idle" : liveSnapshot.status.c_str(),
			static_cast<float>(liveSnapshot.bufferedSeconds),
			liveSnapshot.busy ? " | transcribing" : "");
		if (!liveSnapshot.detectedLanguage.empty()) {
			ImGui::TextDisabled("Detected language: %s", liveSnapshot.detectedLanguage.c_str());
		}
		ImGui::BeginChild("##SpeechLiveTranscript", ImVec2(0, 120), true);
		if (liveSnapshot.transcript.empty()) {
			ImGui::TextDisabled("Live transcript will appear here while recording.");
		} else {
			ImGui::TextWrapped("%s", liveSnapshot.transcript.c_str());
		}
		ImGui::EndChild();
	}

	ImGui::Separator();
	if (!speechDetectedLanguage.empty()) {
		ImGui::TextDisabled("Detected language: %s", speechDetectedLanguage.c_str());
	}
	if (!speechTranscriptPath.empty()) {
		ImGui::TextDisabled("Transcript file: %s", speechTranscriptPath.c_str());
	}
	if (!speechSrtPath.empty()) {
		ImGui::TextDisabled("SRT file: %s", speechSrtPath.c_str());
	}
	if (speechSegmentCount > 0) {
		ImGui::TextDisabled("Segments: %d", speechSegmentCount);
	}
	ImGui::BeginChild("##SpeechOut", ImVec2(0, 0), true);
	if (generating.load() && activeGenerationMode == AiMode::Speech) {
		std::string partial;
		{
			std::lock_guard<std::mutex> lock(streamMutex);
			partial = streamingOutput;
		}
		if (partial.empty()) {
			ImGui::TextDisabled("Transcribing...");
		} else {
			ImGui::TextWrapped("%s", partial.c_str());
		}
	} else if (speechOutput.empty()) {
		ImGui::TextDisabled("Speech transcription appears here.");
	} else {
		ImGui::TextWrapped("%s", speechOutput.c_str());
	}
	ImGui::EndChild();
}

void ofApp::drawTtsPanel() {
	drawPanelHeader("TTS", "local text-to-speech via chatllm.cpp-backed OuteTTS models");
	const float compactModeFieldWidth = std::min(320.0f, ImGui::GetContentRegionAvail().x);

	const bool loadedTtsProfiles = ensureTtsProfilesLoaded();
	if (loadedTtsProfiles && !ttsProfiles.empty()) {
		applyTtsProfileDefaults(getSelectedTtsProfile(), true);
	}
	if (trim(ttsExecutablePath).empty()) {
		copyStringToBuffer(
			ttsExecutablePath,
			sizeof(ttsExecutablePath),
			ofxGgmlChatLlmTtsAdapters::preferredLocalExecutablePath());
	}

	const ofxGgmlTtsModelProfile activeProfile = getSelectedTtsProfile();
	if (!ttsProfiles.empty()) {
		std::vector<const char *> profileNames;
		profileNames.reserve(ttsProfiles.size());
		for (const auto & profile : ttsProfiles) {
			profileNames.push_back(profile.name.c_str());
		}
		ImGui::SetNextItemWidth(280);
		if (ImGui::Combo(
			"TTS profile",
			&selectedTtsProfileIndex,
			profileNames.data(),
			static_cast<int>(profileNames.size()))) {
			applyTtsProfileDefaults(getSelectedTtsProfile(), false);
			autoSaveSession();
		}
		if (!activeProfile.modelRepoHint.empty()) {
			ImGui::TextDisabled("Recommended repo: %s", activeProfile.modelRepoHint.c_str());
		}
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Executable", ttsExecutablePath, sizeof(ttsExecutablePath));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse exe##Tts", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select chatllm executable", false);
		if (result.bSuccess) {
			copyStringToBuffer(ttsExecutablePath, sizeof(ttsExecutablePath), result.getPath());
			autoSaveSession();
		}
	}
	ImGui::TextDisabled("Resolved executable: %s", resolveConfiguredTtsExecutable().c_str());

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Model path", ttsModelPath, sizeof(ttsModelPath));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse model##Tts", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select TTS model", false);
		if (result.bSuccess) {
			copyStringToBuffer(ttsModelPath, sizeof(ttsModelPath), result.getPath());
			autoSaveSession();
		}
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Speaker profile", ttsSpeakerPath, sizeof(ttsSpeakerPath));
	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Reference audio", ttsSpeakerReferencePath, sizeof(ttsSpeakerReferencePath));
	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Prompt audio", ttsPromptAudioPath, sizeof(ttsPromptAudioPath));
	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Output path", ttsOutputPath, sizeof(ttsOutputPath));
	ImGui::SetNextItemWidth(160);
	ImGui::InputText("Language", ttsLanguage, sizeof(ttsLanguage));

	static const char * ttsTaskLabels[] = {"Synthesize", "Clone Voice", "Continue Speech"};
	ImGui::SetNextItemWidth(200);
	ImGui::Combo("TTS task", &ttsTaskIndex, ttsTaskLabels, 3);
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Seed", &ttsSeed, -1, 999999);
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Max tokens", &ttsMaxTokens, 0, 4096);
	ImGui::SetNextItemWidth(220);
	ImGui::SliderFloat("Temperature", &ttsTemperature, 0.0f, 2.0f, "%.2f");
	ImGui::SetNextItemWidth(220);
	ImGui::SliderFloat("Repetition penalty", &ttsRepetitionPenalty, 1.0f, 3.0f, "%.2f");
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Repetition range", &ttsRepetitionRange, 0, 512);
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Top K", &ttsTopK, 0, 200);
	ImGui::SetNextItemWidth(220);
	ImGui::SliderFloat("Top P", &ttsTopP, 0.0f, 1.0f, "%.2f");
	ImGui::SetNextItemWidth(220);
	ImGui::SliderFloat("Min P", &ttsMinP, 0.0f, 1.0f, "%.2f");
	ImGui::Checkbox("Stream audio", &ttsStreamAudio);
	ImGui::SameLine();
	ImGui::Checkbox("Normalize text", &ttsNormalizeText);

	ImGui::InputTextMultiline(
		"TTS text",
		ttsInput,
		sizeof(ttsInput),
		ImVec2(-1, 120));

	const bool canRunTts =
		!generating.load() &&
		std::strlen(ttsInput) > 0 &&
		std::strlen(ttsModelPath) > 0;
	ImGui::BeginDisabled(!canRunTts);
	if (ImGui::Button("Run TTS", ImVec2(140, 0))) {
		runTtsInference();
	}
	ImGui::EndDisabled();

	ImGui::Separator();
	if (!ttsBackendName.empty()) {
		ImGui::TextDisabled(
			"Last backend: %s%s",
			ttsBackendName.c_str(),
			ttsElapsedMs > 0.0f
				? (" in " + ofxGgmlHelpers::formatDurationMs(ttsElapsedMs)).c_str()
				: "");
	}
	if (!ttsResolvedSpeakerPath.empty()) {
		ImGui::TextDisabled("Resolved speaker: %s", ttsResolvedSpeakerPath.c_str());
	}
	if (!ttsAudioFiles.empty()) {
		ImGui::TextDisabled("Generated audio:");
		for (const auto & artifact : ttsAudioFiles) {
			ImGui::BulletText("%s", artifact.path.c_str());
		}
	}
	ImGui::BeginChild("##TtsOut", ImVec2(0, 0), true);
	if (generating.load() && activeGenerationMode == AiMode::Tts) {
		std::string partial;
		{
			std::lock_guard<std::mutex> lock(streamMutex);
			partial = streamingOutput;
		}
		if (partial.empty()) {
			ImGui::TextDisabled("Synthesizing...");
		} else {
			ImGui::TextWrapped("%s", partial.c_str());
		}
	} else if (ttsOutput.empty()) {
		ImGui::TextDisabled("TTS output appears here.");
	} else {
		ImGui::TextWrapped("%s", ttsOutput.c_str());
	}
	ImGui::EndChild();
}

std::string ofApp::escapeSessionText(const std::string & text) const {
	return text;
}

std::string ofApp::unescapeSessionText(const std::string & text) const {
	return text;
}

bool ofApp::saveSession(const std::string & path) {
	ofJson session;
	session["format"] = "ofxGgmlGuiSession";
	session["version"] = 2;

	session["settings"] = {
		{"activeMode", static_cast<int>(activeMode)},
		{"selectedModelIndex", selectedModelIndex},
		{"selectedLanguageIndex", selectedLanguageIndex},
		{"translateSourceLang", translateSourceLang},
		{"translateTargetLang", translateTargetLang},
		{"chatLanguageIndex", chatLanguageIndex},
		{"maxTokens", maxTokens},
		{"temperature", temperature},
		{"topP", topP},
		{"topK", topK},
		{"minP", minP},
		{"repeatPenalty", repeatPenalty},
		{"contextSize", contextSize},
		{"batchSize", batchSize},
		{"gpuLayers", gpuLayers},
		{"seed", seed},
		{"numThreads", numThreads},
		{"selectedBackendIndex", selectedBackendIndex},
		{"themeIndex", themeIndex},
		{"mirostatMode", mirostatMode},
		{"mirostatTau", mirostatTau},
		{"mirostatEta", mirostatEta},
		{"textInferenceBackend", static_cast<int>(textInferenceBackend)},
		{"useModeTokenBudgets", useModeTokenBudgets},
		{"autoContinueCutoff", autoContinueCutoff},
		{"usePromptCache", usePromptCache},
		{"logLevel", static_cast<int>(logLevel)},
		{"liveContextMode", static_cast<int>(liveContextMode)},
		{"liveContextAllowPromptUrls", liveContextAllowPromptUrls},
		{"liveContextAllowDomainProviders", liveContextAllowDomainProviders},
		{"liveContextAllowGenericSearch", liveContextAllowGenericSearch},
		{"scriptIncludeRepoContext", scriptIncludeRepoContext},
		{"selectedVisionProfileIndex", selectedVisionProfileIndex},
		{"selectedSpeechProfileIndex", selectedSpeechProfileIndex},
		{"selectedTtsProfileIndex", selectedTtsProfileIndex},
		{"selectedDiffusionProfileIndex", selectedDiffusionProfileIndex},
		{"citationUseCrawler", citationUseCrawler},
		{"citationMaxResults", citationMaxResults}
	};
	session["settings"]["modeMaxTokens"] = ofJson::array();
	for (int i = 0; i < kModeCount; ++i) {
		session["settings"]["modeMaxTokens"].push_back(modeMaxTokens[static_cast<size_t>(i)]);
	}
	session["settings"]["modeTextBackendIndices"] = ofJson::array();
	for (int i = 0; i < kModeCount; ++i) {
		session["settings"]["modeTextBackendIndices"].push_back(modeTextBackendIndices[static_cast<size_t>(i)]);
	}
	if (selectedBackendIndex >= 0 &&
		selectedBackendIndex < static_cast<int>(backendNames.size())) {
		session["settings"]["selectedBackendName"] =
			backendNames[static_cast<size_t>(selectedBackendIndex)];
	}

	const ofxGgmlScriptSourceType savedScriptSourceType =
		deferredScriptSourceRestorePending ? deferredScriptSourceType : scriptSource.getSourceType();
	const std::string savedScriptSourcePath =
		deferredScriptSourceRestorePending ? deferredScriptSourcePath : scriptSource.getLocalFolderPath();
	std::string savedScriptSourceInternetUrls = deferredScriptSourceRestorePending
		? deferredScriptSourceInternetUrls
		: std::string();
	if (!deferredScriptSourceRestorePending) {
		const auto internetUrls = scriptSource.getInternetUrls();
		for (size_t i = 0; i < internetUrls.size(); ++i) {
			if (i > 0) {
				savedScriptSourceInternetUrls += "\n";
			}
			savedScriptSourceInternetUrls += internetUrls[i];
		}
	}
	session["scriptSource"] = {
		{"type", static_cast<int>(savedScriptSourceType)},
		{"path", savedScriptSourcePath},
		{"github", std::string(scriptSourceGitHub)},
		{"branch", std::string(scriptSourceBranch)},
		{"internetUrls", savedScriptSourceInternetUrls},
		{"projectMemoryEnabled", scriptProjectMemory.isEnabled()},
		{"projectMemoryText", scriptProjectMemory.getMemoryText()}
	};

	session["buffers"] = {
		{"chatInput", std::string(chatInput)},
		{"scriptInput", std::string(scriptInput)},
		{"summarizeInput", std::string(summarizeInput)},
		{"writeInput", std::string(writeInput)},
		{"translateInput", std::string(translateInput)},
		{"customInput", std::string(customInput)},
		{"customSystemPrompt", std::string(customSystemPrompt)},
		{"sourceUrlsInput", std::string(sourceUrlsInput)},
		{"citationTopic", std::string(citationTopic)},
		{"citationSeedUrl", std::string(citationSeedUrl)},
		{"textServerUrl", std::string(textServerUrl)},
		{"textServerModel", std::string(textServerModel)},
		{"visionPrompt", std::string(visionPrompt)},
		{"visionImagePath", std::string(visionImagePath)},
		{"visionVideoPath", std::string(visionVideoPath)},
		{"visionModelPath", std::string(visionModelPath)},
		{"visionServerUrl", std::string(visionServerUrl)},
		{"videoSidecarUrl", std::string(videoSidecarUrl)},
		{"videoSidecarModel", std::string(videoSidecarModel)},
		{"visionSystemPrompt", std::string(visionSystemPrompt)},
		{"videoPlanJson", std::string(videoPlanJson)},
		{"videoEditPlanJson", std::string(videoEditPlanJson)},
		{"montageSubtitlePath", std::string(montageSubtitlePath)},
		{"montageGoal", std::string(montageGoal)},
		{"montageEdlTitle", std::string(montageEdlTitle)},
		{"montageReelName", std::string(montageReelName)},
		{"videoEditGoal", std::string(videoEditGoal)},
		{"speechAudioPath", std::string(speechAudioPath)},
		{"speechExecutable", std::string(speechExecutable)},
		{"speechModelPath", std::string(speechModelPath)},
		{"speechServerUrl", std::string(speechServerUrl)},
		{"speechServerModel", std::string(speechServerModel)},
		{"speechPrompt", std::string(speechPrompt)},
		{"speechLanguageHint", std::string(speechLanguageHint)},
		{"ttsInput", std::string(ttsInput)},
		{"ttsExecutablePath", std::string(ttsExecutablePath)},
		{"ttsModelPath", std::string(ttsModelPath)},
		{"ttsSpeakerPath", std::string(ttsSpeakerPath)},
		{"ttsSpeakerReferencePath", std::string(ttsSpeakerReferencePath)},
		{"ttsOutputPath", std::string(ttsOutputPath)},
		{"ttsPromptAudioPath", std::string(ttsPromptAudioPath)},
		{"ttsLanguage", std::string(ttsLanguage)},
		{"diffusionPrompt", std::string(diffusionPrompt)},
		{"diffusionInstruction", std::string(diffusionInstruction)},
		{"diffusionNegativePrompt", std::string(diffusionNegativePrompt)},
		{"diffusionRankingPrompt", std::string(diffusionRankingPrompt)},
		{"diffusionModelPath", std::string(diffusionModelPath)},
		{"diffusionVaePath", std::string(diffusionVaePath)},
		{"diffusionInitImagePath", std::string(diffusionInitImagePath)},
		{"diffusionMaskImagePath", std::string(diffusionMaskImagePath)},
		{"diffusionOutputDir", std::string(diffusionOutputDir)},
		{"diffusionOutputPrefix", std::string(diffusionOutputPrefix)},
		{"diffusionSampler", std::string(diffusionSampler)},
		{"imageSearchPrompt", std::string(imageSearchPrompt)},
		{"clipPrompt", std::string(clipPrompt)},
		{"clipModelPath", std::string(clipModelPath)},
		{"clipImagePaths", std::string(clipImagePaths)}
	};

	session["indices"] = {
		{"visionTaskIndex", visionTaskIndex},
		{"videoTaskIndex", videoTaskIndex},
		{"visionVideoMaxFrames", visionVideoMaxFrames},
		{"videoPlanBeatCount", videoPlanBeatCount},
		{"videoPlanSceneCount", videoPlanSceneCount},
		{"videoPlanGenerationMode", videoPlanGenerationMode},
		{"videoEditClipCount", videoEditClipCount},
		{"selectedVideoPlanSceneIndex", selectedVideoPlanSceneIndex},
		{"montageMaxClips", montageMaxClips},
		{"montageFps", montageFps},
		{"speechTaskIndex", speechTaskIndex},
		{"ttsTaskIndex", ttsTaskIndex},
		{"ttsSeed", ttsSeed},
		{"ttsMaxTokens", ttsMaxTokens},
		{"diffusionTaskIndex", diffusionTaskIndex},
		{"diffusionSelectionModeIndex", diffusionSelectionModeIndex},
		{"diffusionWidth", diffusionWidth},
		{"diffusionHeight", diffusionHeight},
		{"diffusionSteps", diffusionSteps},
		{"diffusionBatchCount", diffusionBatchCount},
		{"diffusionSeed", diffusionSeed},
		{"imageSearchMaxResults", imageSearchMaxResults},
		{"clipTopK", clipTopK},
		{"clipVerbosity", clipVerbosity}
	};

	session["floats"] = {
		{"videoPlanDurationSeconds", videoPlanDurationSeconds},
		{"videoEditTargetDurationSeconds", videoEditTargetDurationSeconds},
		{"montageMinScore", montageMinScore},
		{"ttsTemperature", ttsTemperature},
		{"ttsRepetitionPenalty", ttsRepetitionPenalty},
		{"ttsTopP", ttsTopP},
		{"ttsMinP", ttsMinP},
		{"diffusionCfgScale", diffusionCfgScale},
		{"diffusionStrength", diffusionStrength}
	};

	session["bools"] = {
		{"videoPlanMultiScene", videoPlanMultiScene},
		{"videoPlanUseForGeneration", videoPlanUseForGeneration},
		{"montagePreserveChronology", montagePreserveChronology},
		{"montageSubtitlePlaybackEnabled", montageSubtitlePlaybackEnabled},
		{"videoEditUseCurrentAnalysis", videoEditUseCurrentAnalysis},
		{"speechReturnTimestamps", speechReturnTimestamps},
		{"speechLiveTranscriptionEnabled", speechLiveTranscriptionEnabled},
		{"ttsStreamAudio", ttsStreamAudio},
		{"ttsNormalizeText", ttsNormalizeText},
		{"diffusionNormalizeClipEmbeddings", diffusionNormalizeClipEmbeddings},
		{"diffusionSaveMetadata", diffusionSaveMetadata},
		{"clipNormalizeEmbeddings", clipNormalizeEmbeddings}
	};

	session["montagePreview"] = {
		{"timingModeIndex", montagePreviewTimingModeIndex},
		{"subtitleSlavePath", montagePreviewSubtitleSlavePath}
	};

	session["liveSpeech"] = {
		{"intervalSeconds", speechLiveIntervalSeconds},
		{"windowSeconds", speechLiveWindowSeconds},
		{"overlapSeconds", speechLiveOverlapSeconds}
	};

	session["outputs"] = {
		{"scriptOutput", scriptOutput},
		{"summarizeOutput", summarizeOutput},
		{"writeOutput", writeOutput},
		{"translateOutput", translateOutput},
		{"customOutput", customOutput},
		{"citationOutput", citationOutput},
		{"visionOutput", visionOutput},
		{"montageSummary", montageSummary},
		{"montageEditorBrief", montageEditorBrief},
		{"montageEdlText", montageEdlText},
		{"montageSrtText", montageSrtText},
		{"montageVttText", montageVttText},
		{"videoPlanSummary", videoPlanSummary},
		{"videoEditPlanSummary", videoEditPlanSummary},
		{"speechOutput", speechOutput},
		{"speechDetectedLanguage", speechDetectedLanguage},
		{"speechTranscriptPath", speechTranscriptPath},
		{"speechSrtPath", speechSrtPath},
		{"speechSegmentCount", speechSegmentCount},
		{"ttsOutput", ttsOutput},
		{"diffusionOutput", diffusionOutput},
		{"imageSearchOutput", imageSearchOutput},
		{"clipOutput", clipOutput}
	};

	session["chatMessages"] = ofJson::array();
	for (const auto & msg : chatMessages) {
		session["chatMessages"].push_back({
			{"role", msg.role},
			{"text", msg.text},
			{"timestamp", msg.timestamp}
		});
	}

	std::ofstream out(path);
	if (!out.is_open()) {
		return false;
	}
	out << session.dump(2);
	return true;
}

bool ofApp::loadSession(const std::string & path) {
	std::ifstream in(path);
	if (!in.is_open()) {
		return false;
	}

	ofJson session;
	try {
		in >> session;
	} catch (...) {
		logWithLevel(OF_LOG_WARNING, "Failed to parse session file: " + path);
		return false;
	}
	if (!session.is_object()) {
		return false;
	}

	auto getString = [](const ofJson & node, const char * key, const std::string & fallback = std::string()) {
		return node.contains(key) && node[key].is_string()
			? node[key].get<std::string>()
			: fallback;
	};
	auto getInt = [](const ofJson & node, const char * key, int fallback = 0) {
		return node.contains(key) && node[key].is_number_integer()
			? node[key].get<int>()
			: fallback;
	};
	auto getFloat = [](const ofJson & node, const char * key, float fallback = 0.0f) {
		return node.contains(key) && node[key].is_number()
			? node[key].get<float>()
			: fallback;
	};
	auto getBool = [](const ofJson & node, const char * key, bool fallback = false) {
		return node.contains(key) && node[key].is_boolean()
			? node[key].get<bool>()
			: fallback;
	};
	auto copyJsonString = [this, &getString](char * buffer, size_t bufferSize, const ofJson & node, const char * key) {
		copyStringToBuffer(buffer, bufferSize, getString(node, key));
	};

	const ofJson settings = session.value("settings", ofJson::object());
	const ofJson scriptSourceJson = session.value("scriptSource", ofJson::object());
	const ofJson buffers = session.value("buffers", ofJson::object());
	const ofJson indices = session.value("indices", ofJson::object());
	const ofJson floats = session.value("floats", ofJson::object());
	const ofJson bools = session.value("bools", ofJson::object());
	const ofJson montagePreview = session.value("montagePreview", ofJson::object());
	const ofJson liveSpeech = session.value("liveSpeech", ofJson::object());
	const ofJson outputs = session.value("outputs", ofJson::object());

	activeMode = static_cast<AiMode>(std::clamp(getInt(settings, "activeMode", static_cast<int>(AiMode::Chat)), 0, kModeCount - 1));
	selectedModelIndex = std::clamp(getInt(settings, "selectedModelIndex", 0), 0, std::max(0, static_cast<int>(modelPresets.size()) - 1));
	selectedLanguageIndex = std::clamp(getInt(settings, "selectedLanguageIndex", 0), 0, std::max(0, static_cast<int>(scriptLanguages.size()) - 1));
	translateSourceLang = std::clamp(getInt(settings, "translateSourceLang", 0), 0, std::max(0, static_cast<int>(translateLanguages.size()) - 1));
	translateTargetLang = std::clamp(getInt(settings, "translateTargetLang", 1), 0, std::max(0, static_cast<int>(translateLanguages.size()) - 1));
	chatLanguageIndex = std::clamp(getInt(settings, "chatLanguageIndex", 0), 0, std::max(0, static_cast<int>(chatLanguages.size()) - 1));
	maxTokens = std::clamp(getInt(settings, "maxTokens", maxTokens), 32, 4096);
	temperature = std::clamp(getFloat(settings, "temperature", temperature), 0.0f, 2.0f);
	topP = std::clamp(getFloat(settings, "topP", topP), 0.0f, 1.0f);
	topK = std::clamp(getInt(settings, "topK", topK), 0, 200);
	minP = std::clamp(getFloat(settings, "minP", minP), 0.0f, 1.0f);
	repeatPenalty = std::clamp(getFloat(settings, "repeatPenalty", repeatPenalty), 1.0f, 2.0f);
	contextSize = std::clamp(getInt(settings, "contextSize", contextSize), 256, 16384);
	batchSize = std::clamp(getInt(settings, "batchSize", batchSize), 32, 4096);
	gpuLayers = std::clamp(getInt(settings, "gpuLayers", gpuLayers), 0, 999);
	seed = getInt(settings, "seed", seed);
	numThreads = std::clamp(getInt(settings, "numThreads", numThreads), 1, 128);
	selectedBackendIndex = std::clamp(getInt(settings, "selectedBackendIndex", selectedBackendIndex), 0, std::max(0, static_cast<int>(backendNames.size()) - 1));
	const std::string selectedBackendName = getString(settings, "selectedBackendName");
	if (!selectedBackendName.empty()) {
		for (int i = 0; i < static_cast<int>(backendNames.size()); ++i) {
			if (backendNames[static_cast<size_t>(i)] == selectedBackendName) {
				selectedBackendIndex = i;
				break;
			}
		}
	}
	themeIndex = std::clamp(getInt(settings, "themeIndex", themeIndex), 0, 2);
	mirostatMode = std::clamp(getInt(settings, "mirostatMode", mirostatMode), 0, 2);
	mirostatTau = std::clamp(getFloat(settings, "mirostatTau", mirostatTau), 0.0f, 10.0f);
	mirostatEta = std::clamp(getFloat(settings, "mirostatEta", mirostatEta), 0.0f, 1.0f);
	textInferenceBackend = clampTextInferenceBackend(getInt(settings, "textInferenceBackend", static_cast<int>(textInferenceBackend)));
	useModeTokenBudgets = getBool(settings, "useModeTokenBudgets", useModeTokenBudgets);
	autoContinueCutoff = getBool(settings, "autoContinueCutoff", autoContinueCutoff);
	usePromptCache = getBool(settings, "usePromptCache", usePromptCache);
	logLevel = static_cast<ofLogLevel>(std::clamp(getInt(settings, "logLevel", static_cast<int>(logLevel)), static_cast<int>(OF_LOG_VERBOSE), static_cast<int>(OF_LOG_SILENT)));
	liveContextMode = static_cast<LiveContextMode>(std::clamp(getInt(settings, "liveContextMode", static_cast<int>(liveContextMode)), 0, 3));
	liveContextAllowPromptUrls = getBool(settings, "liveContextAllowPromptUrls", liveContextAllowPromptUrls);
	liveContextAllowDomainProviders = getBool(settings, "liveContextAllowDomainProviders", liveContextAllowDomainProviders);
	liveContextAllowGenericSearch = getBool(settings, "liveContextAllowGenericSearch", liveContextAllowGenericSearch);
	scriptIncludeRepoContext = getBool(settings, "scriptIncludeRepoContext", scriptIncludeRepoContext);
	selectedVisionProfileIndex = std::max(0, getInt(settings, "selectedVisionProfileIndex", selectedVisionProfileIndex));
	selectedSpeechProfileIndex = std::max(0, getInt(settings, "selectedSpeechProfileIndex", selectedSpeechProfileIndex));
	selectedTtsProfileIndex = std::max(0, getInt(settings, "selectedTtsProfileIndex", selectedTtsProfileIndex));
	selectedDiffusionProfileIndex = std::max(0, getInt(settings, "selectedDiffusionProfileIndex", selectedDiffusionProfileIndex));
	if (settings.contains("modeMaxTokens") && settings["modeMaxTokens"].is_array()) {
		for (size_t i = 0; i < std::min<size_t>(settings["modeMaxTokens"].size(), kModeCount); ++i) {
			modeMaxTokens[i] = std::clamp(settings["modeMaxTokens"][i].get<int>(), 32, 4096);
		}
	}
	if (settings.contains("modeTextBackendIndices") && settings["modeTextBackendIndices"].is_array()) {
		for (size_t i = 0; i < std::min<size_t>(settings["modeTextBackendIndices"].size(), kModeCount); ++i) {
			modeTextBackendIndices[i] = std::clamp(settings["modeTextBackendIndices"][i].get<int>(), 0, 1);
		}
	}

	copyJsonString(chatInput, sizeof(chatInput), buffers, "chatInput");
	copyJsonString(scriptInput, sizeof(scriptInput), buffers, "scriptInput");
	copyJsonString(summarizeInput, sizeof(summarizeInput), buffers, "summarizeInput");
	copyJsonString(writeInput, sizeof(writeInput), buffers, "writeInput");
	copyJsonString(translateInput, sizeof(translateInput), buffers, "translateInput");
	copyJsonString(customInput, sizeof(customInput), buffers, "customInput");
	copyJsonString(customSystemPrompt, sizeof(customSystemPrompt), buffers, "customSystemPrompt");
	copyJsonString(sourceUrlsInput, sizeof(sourceUrlsInput), buffers, "sourceUrlsInput");
	copyJsonString(citationTopic, sizeof(citationTopic), buffers, "citationTopic");
	copyJsonString(citationSeedUrl, sizeof(citationSeedUrl), buffers, "citationSeedUrl");
	copyJsonString(textServerUrl, sizeof(textServerUrl), buffers, "textServerUrl");
	copyJsonString(textServerModel, sizeof(textServerModel), buffers, "textServerModel");
	copyJsonString(visionPrompt, sizeof(visionPrompt), buffers, "visionPrompt");
	copyJsonString(visionImagePath, sizeof(visionImagePath), buffers, "visionImagePath");
	copyJsonString(visionVideoPath, sizeof(visionVideoPath), buffers, "visionVideoPath");
	copyJsonString(visionModelPath, sizeof(visionModelPath), buffers, "visionModelPath");
	copyJsonString(visionServerUrl, sizeof(visionServerUrl), buffers, "visionServerUrl");
	copyJsonString(videoSidecarUrl, sizeof(videoSidecarUrl), buffers, "videoSidecarUrl");
	copyJsonString(videoSidecarModel, sizeof(videoSidecarModel), buffers, "videoSidecarModel");
	copyJsonString(visionSystemPrompt, sizeof(visionSystemPrompt), buffers, "visionSystemPrompt");
	copyJsonString(videoPlanJson, sizeof(videoPlanJson), buffers, "videoPlanJson");
	copyJsonString(videoEditPlanJson, sizeof(videoEditPlanJson), buffers, "videoEditPlanJson");
	copyJsonString(montageSubtitlePath, sizeof(montageSubtitlePath), buffers, "montageSubtitlePath");
	copyJsonString(montageGoal, sizeof(montageGoal), buffers, "montageGoal");
	copyJsonString(montageEdlTitle, sizeof(montageEdlTitle), buffers, "montageEdlTitle");
	copyJsonString(montageReelName, sizeof(montageReelName), buffers, "montageReelName");
	copyJsonString(videoEditGoal, sizeof(videoEditGoal), buffers, "videoEditGoal");
	copyJsonString(speechAudioPath, sizeof(speechAudioPath), buffers, "speechAudioPath");
	copyJsonString(speechExecutable, sizeof(speechExecutable), buffers, "speechExecutable");
	copyJsonString(speechModelPath, sizeof(speechModelPath), buffers, "speechModelPath");
	copyJsonString(speechServerUrl, sizeof(speechServerUrl), buffers, "speechServerUrl");
	copyJsonString(speechServerModel, sizeof(speechServerModel), buffers, "speechServerModel");
	copyJsonString(speechPrompt, sizeof(speechPrompt), buffers, "speechPrompt");
	copyJsonString(speechLanguageHint, sizeof(speechLanguageHint), buffers, "speechLanguageHint");
	copyJsonString(ttsInput, sizeof(ttsInput), buffers, "ttsInput");
	copyJsonString(ttsExecutablePath, sizeof(ttsExecutablePath), buffers, "ttsExecutablePath");
	copyJsonString(ttsModelPath, sizeof(ttsModelPath), buffers, "ttsModelPath");
	copyJsonString(ttsSpeakerPath, sizeof(ttsSpeakerPath), buffers, "ttsSpeakerPath");
	copyJsonString(ttsSpeakerReferencePath, sizeof(ttsSpeakerReferencePath), buffers, "ttsSpeakerReferencePath");
	copyJsonString(ttsOutputPath, sizeof(ttsOutputPath), buffers, "ttsOutputPath");
	copyJsonString(ttsPromptAudioPath, sizeof(ttsPromptAudioPath), buffers, "ttsPromptAudioPath");
	copyJsonString(ttsLanguage, sizeof(ttsLanguage), buffers, "ttsLanguage");
	copyJsonString(diffusionPrompt, sizeof(diffusionPrompt), buffers, "diffusionPrompt");
	copyJsonString(diffusionInstruction, sizeof(diffusionInstruction), buffers, "diffusionInstruction");
	copyJsonString(diffusionNegativePrompt, sizeof(diffusionNegativePrompt), buffers, "diffusionNegativePrompt");
	copyJsonString(diffusionRankingPrompt, sizeof(diffusionRankingPrompt), buffers, "diffusionRankingPrompt");
	copyJsonString(diffusionModelPath, sizeof(diffusionModelPath), buffers, "diffusionModelPath");
	copyJsonString(diffusionVaePath, sizeof(diffusionVaePath), buffers, "diffusionVaePath");
	copyJsonString(diffusionInitImagePath, sizeof(diffusionInitImagePath), buffers, "diffusionInitImagePath");
	copyJsonString(diffusionMaskImagePath, sizeof(diffusionMaskImagePath), buffers, "diffusionMaskImagePath");
	copyJsonString(diffusionOutputDir, sizeof(diffusionOutputDir), buffers, "diffusionOutputDir");
	copyJsonString(diffusionOutputPrefix, sizeof(diffusionOutputPrefix), buffers, "diffusionOutputPrefix");
	copyJsonString(diffusionSampler, sizeof(diffusionSampler), buffers, "diffusionSampler");
	copyJsonString(imageSearchPrompt, sizeof(imageSearchPrompt), buffers, "imageSearchPrompt");
	copyJsonString(clipPrompt, sizeof(clipPrompt), buffers, "clipPrompt");
	copyJsonString(clipModelPath, sizeof(clipModelPath), buffers, "clipModelPath");
	copyJsonString(clipImagePaths, sizeof(clipImagePaths), buffers, "clipImagePaths");

	visionTaskIndex = std::clamp(getInt(indices, "visionTaskIndex", visionTaskIndex), 0, 2);
	videoTaskIndex = std::clamp(getInt(indices, "videoTaskIndex", videoTaskIndex), 0, 4);
	visionVideoMaxFrames = std::clamp(getInt(indices, "visionVideoMaxFrames", visionVideoMaxFrames), 1, 12);
	videoPlanBeatCount = std::clamp(getInt(indices, "videoPlanBeatCount", videoPlanBeatCount), 1, 12);
	videoPlanSceneCount = std::clamp(getInt(indices, "videoPlanSceneCount", videoPlanSceneCount), 1, 8);
	videoPlanGenerationMode = std::clamp(getInt(indices, "videoPlanGenerationMode", videoPlanGenerationMode), 0, 1);
	videoEditClipCount = std::clamp(getInt(indices, "videoEditClipCount", videoEditClipCount), 1, 12);
	selectedVideoPlanSceneIndex = std::max(0, getInt(indices, "selectedVideoPlanSceneIndex", selectedVideoPlanSceneIndex));
	montageMaxClips = std::clamp(getInt(indices, "montageMaxClips", montageMaxClips), 1, 24);
	montageFps = std::clamp(getInt(indices, "montageFps", montageFps), 12, 60);
	speechTaskIndex = std::clamp(getInt(indices, "speechTaskIndex", speechTaskIndex), 0, 1);
	ttsTaskIndex = std::clamp(getInt(indices, "ttsTaskIndex", ttsTaskIndex), 0, 2);
	ttsSeed = getInt(indices, "ttsSeed", ttsSeed);
	ttsMaxTokens = std::max(0, getInt(indices, "ttsMaxTokens", ttsMaxTokens));
	diffusionTaskIndex = std::clamp(getInt(indices, "diffusionTaskIndex", diffusionTaskIndex), 0, 6);
	diffusionSelectionModeIndex = std::clamp(getInt(indices, "diffusionSelectionModeIndex", diffusionSelectionModeIndex), 0, 2);
	diffusionWidth = clampSupportedDiffusionImageSize(getInt(indices, "diffusionWidth", diffusionWidth));
	diffusionHeight = clampSupportedDiffusionImageSize(getInt(indices, "diffusionHeight", diffusionHeight));
	diffusionSteps = std::clamp(getInt(indices, "diffusionSteps", diffusionSteps), 1, 200);
	diffusionBatchCount = std::clamp(getInt(indices, "diffusionBatchCount", diffusionBatchCount), 1, 16);
	diffusionSeed = getInt(indices, "diffusionSeed", diffusionSeed);
	imageSearchMaxResults = std::clamp(getInt(indices, "imageSearchMaxResults", imageSearchMaxResults), 1, 32);
	clipTopK = std::clamp(getInt(indices, "clipTopK", clipTopK), 0, 16);
	clipVerbosity = std::clamp(getInt(indices, "clipVerbosity", clipVerbosity), 0, 2);
	citationMaxResults = std::clamp(getInt(settings, "citationMaxResults", citationMaxResults), 1, 12);

	videoPlanDurationSeconds = std::clamp(getFloat(floats, "videoPlanDurationSeconds", videoPlanDurationSeconds), 1.0f, 30.0f);
	videoEditTargetDurationSeconds = std::clamp(getFloat(floats, "videoEditTargetDurationSeconds", videoEditTargetDurationSeconds), 1.0f, 120.0f);
	montageMinScore = std::clamp(getFloat(floats, "montageMinScore", montageMinScore), 0.0f, 1.0f);
	ttsTemperature = std::clamp(getFloat(floats, "ttsTemperature", ttsTemperature), 0.0f, 2.0f);
	ttsRepetitionPenalty = std::clamp(getFloat(floats, "ttsRepetitionPenalty", ttsRepetitionPenalty), 1.0f, 3.0f);
	ttsTopP = std::clamp(getFloat(floats, "ttsTopP", ttsTopP), 0.0f, 1.0f);
	ttsMinP = std::clamp(getFloat(floats, "ttsMinP", ttsMinP), 0.0f, 1.0f);
	diffusionCfgScale = std::clamp(getFloat(floats, "diffusionCfgScale", diffusionCfgScale), 0.0f, 30.0f);
	diffusionStrength = std::clamp(getFloat(floats, "diffusionStrength", diffusionStrength), 0.0f, 1.0f);

	videoPlanMultiScene = getBool(bools, "videoPlanMultiScene", videoPlanMultiScene);
	videoPlanUseForGeneration = getBool(bools, "videoPlanUseForGeneration", videoPlanUseForGeneration);
	montagePreserveChronology = getBool(bools, "montagePreserveChronology", montagePreserveChronology);
	montageSubtitlePlaybackEnabled = getBool(bools, "montageSubtitlePlaybackEnabled", montageSubtitlePlaybackEnabled);
	montagePreviewTimingModeIndex = std::clamp(
		getInt(montagePreview, "timingModeIndex", montagePreviewTimingModeIndex),
		0,
		1);
	montagePreviewSubtitleSlavePath = getString(montagePreview, "subtitleSlavePath");
	montagePreviewTimelineSeconds = 0.0;
	montagePreviewTimelinePlaying = false;
	montagePreviewTimelineLastTickTime = 0.0f;
	videoEditUseCurrentAnalysis = getBool(bools, "videoEditUseCurrentAnalysis", videoEditUseCurrentAnalysis);
	speechReturnTimestamps = getBool(bools, "speechReturnTimestamps", speechReturnTimestamps);
	speechLiveTranscriptionEnabled = getBool(bools, "speechLiveTranscriptionEnabled", speechLiveTranscriptionEnabled);
	ttsStreamAudio = getBool(bools, "ttsStreamAudio", ttsStreamAudio);
	ttsNormalizeText = getBool(bools, "ttsNormalizeText", ttsNormalizeText);
	diffusionNormalizeClipEmbeddings = getBool(bools, "diffusionNormalizeClipEmbeddings", diffusionNormalizeClipEmbeddings);
	diffusionSaveMetadata = getBool(bools, "diffusionSaveMetadata", diffusionSaveMetadata);
	clipNormalizeEmbeddings = getBool(bools, "clipNormalizeEmbeddings", clipNormalizeEmbeddings);
	citationUseCrawler = getBool(settings, "citationUseCrawler", citationUseCrawler);

	speechLiveIntervalSeconds = std::clamp(getFloat(liveSpeech, "intervalSeconds", speechLiveIntervalSeconds), 0.5f, 5.0f);
	speechLiveWindowSeconds = std::clamp(getFloat(liveSpeech, "windowSeconds", speechLiveWindowSeconds), 2.0f, 30.0f);
	speechLiveOverlapSeconds = std::clamp(getFloat(liveSpeech, "overlapSeconds", speechLiveOverlapSeconds), 0.0f, 3.0f);

	scriptOutput = getString(outputs, "scriptOutput");
	summarizeOutput = getString(outputs, "summarizeOutput");
	writeOutput = getString(outputs, "writeOutput");
	translateOutput = getString(outputs, "translateOutput");
	customOutput = getString(outputs, "customOutput");
	citationOutput = getString(outputs, "citationOutput");
	visionOutput = getString(outputs, "visionOutput");
	montageSummary = getString(outputs, "montageSummary");
	montageEditorBrief = getString(outputs, "montageEditorBrief");
	montageEdlText = getString(outputs, "montageEdlText");
	montageSrtText = getString(outputs, "montageSrtText");
	montageVttText = getString(outputs, "montageVttText");
	videoPlanSummary = getString(outputs, "videoPlanSummary");
	videoEditPlanSummary = getString(outputs, "videoEditPlanSummary");
	speechOutput = getString(outputs, "speechOutput");
	speechDetectedLanguage = getString(outputs, "speechDetectedLanguage");
	speechTranscriptPath = getString(outputs, "speechTranscriptPath");
	speechSrtPath = getString(outputs, "speechSrtPath");
	speechSegmentCount = getInt(outputs, "speechSegmentCount", speechSegmentCount);
	ttsOutput = getString(outputs, "ttsOutput");
	diffusionOutput = getString(outputs, "diffusionOutput");
	imageSearchOutput = getString(outputs, "imageSearchOutput");
	clipOutput = getString(outputs, "clipOutput");

	chatMessages.clear();
	if (session.contains("chatMessages") && session["chatMessages"].is_array()) {
		for (const auto & entry : session["chatMessages"]) {
			Message msg;
			msg.role = getString(entry, "role");
			msg.text = getString(entry, "text");
			msg.timestamp = getFloat(entry, "timestamp", 0.0f);
			chatMessages.push_back(std::move(msg));
		}
	}

	deferredScriptSourceRestorePending = false;
	deferredScriptSourceType =
		static_cast<ofxGgmlScriptSourceType>(std::clamp(getInt(scriptSourceJson, "type", 0), 0, 3));
	deferredScriptSourcePath = getString(scriptSourceJson, "path");
	deferredScriptSourceInternetUrls = getString(scriptSourceJson, "internetUrls");
	copyStringToBuffer(scriptSourceGitHub, sizeof(scriptSourceGitHub), getString(scriptSourceJson, "github"));
	copyStringToBuffer(scriptSourceBranch, sizeof(scriptSourceBranch), getString(scriptSourceJson, "branch", "main"));
	scriptProjectMemory.setEnabled(getBool(scriptSourceJson, "projectMemoryEnabled", scriptProjectMemory.isEnabled()));
	scriptProjectMemory.setMemoryText(getString(scriptSourceJson, "projectMemoryText"));
	if (deferredScriptSourceType != ofxGgmlScriptSourceType::None) {
		deferredScriptSourceRestorePending = true;
	}

	applyTheme(themeIndex);
	applyLogLevel(logLevel);
	applyLiveSpeechTranscriberSettings();
	rebuildMontageSubtitleTrackFromText();
	return true;
}

void ofApp::autoSaveSession() {
	if (!lastSessionPath.empty()) {
		saveSession(lastSessionPath);
	}
}

void ofApp::clearDeferredScriptSourceRestore() {
	deferredScriptSourceRestorePending = false;
	deferredScriptSourceType = ofxGgmlScriptSourceType::None;
	deferredScriptSourcePath.clear();
	deferredScriptSourceInternetUrls.clear();
}

bool ofApp::restoreDeferredScriptSourceIfNeeded() {
	if (!deferredScriptSourceRestorePending) {
		return true;
	}

	bool success = true;
	switch (deferredScriptSourceType) {
	case ofxGgmlScriptSourceType::LocalFolder:
		if (!scriptLanguages.empty() &&
			selectedLanguageIndex >= 0 &&
			selectedLanguageIndex < static_cast<int>(scriptLanguages.size())) {
			scriptSource.setPreferredExtension(
				scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExtension);
		}
		success = !deferredScriptSourcePath.empty() &&
			scriptSource.setLocalFolder(deferredScriptSourcePath);
		break;
	case ofxGgmlScriptSourceType::GitHubRepo: {
		scriptSource.setGitHubMode();
		const std::string ownerRepo = trim(scriptSourceGitHub);
		const std::string branch = trim(scriptSourceBranch);
		success = ownerRepo.empty() ||
			scriptSource.setGitHubRepoFromInput(ownerRepo, branch);
		break;
	}
	case ofxGgmlScriptSourceType::Internet:
		scriptSource.setInternetUrls(
			splitStoredScriptSourceUrls(deferredScriptSourceInternetUrls));
		success = true;
		break;
	default:
		scriptSource.clear();
		success = true;
		break;
	}

	selectedScriptFileIndex = -1;
	if (success) {
		clearDeferredScriptSourceRestore();
	} else {
		logWithLevel(
			OF_LOG_WARNING,
			"Saved script source could not be restored. You can re-select it from the Script panel.");
	}
	return success;
}

void ofApp::autoLoadSession() {
	std::error_code ec;
	if (!lastSessionPath.empty() &&
		std::filesystem::exists(lastSessionPath, ec) &&
		!ec) {
		loadSession(lastSessionPath);
	}
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
