#include "ofApp.h"

#include <algorithm>
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
#include <random>
#include <sstream>
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

// Strip common chat-template role markers and prompt artefacts that
// llama-completion may emit around the actual generated text.
// Examples of markers removed: "user", "assistant", "system",
// "<|...|>" ChatML tokens, and leading/trailing ">" prompt chars.
std::string cleanChatOutput(const std::string & text) {
	std::string out = text;

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
		const std::vector<std::string> roleLabels = {
			"user", "assistant", "system", "User", "Assistant", "System",
			"A:", "> "
		};
		for (const auto & label : roleLabels) {
			if (startsWithWord(out, label)) {
				out = out.substr(label.size());
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
		const std::vector<std::string> trailingArtifacts = {
			"> EOF by user", "> EOF", "EOF", "Interrupted by user"
		};
		bool stripped = true;
		while (stripped) {
			stripped = false;
			for (const auto & art : trailingArtifacts) {
				if (out.size() >= art.size() &&
					out.compare(out.size() - art.size(), art.size(), art) == 0) {
					out = trim(out.substr(0, out.size() - art.size()));
					stripped = true;
				}
			}
		}
	}

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

constexpr size_t kMaxLogMessages = 500;
constexpr size_t kExePathBufSize = 4096; // buffer for resolving the executable path
constexpr float kDefaultTemp = 0.7f;
constexpr float kDefaultTopP = 0.9f;
constexpr float kDefaultRepeatPenalty = 1.1f;
constexpr int kExecNotFound = 127; // POSIX convention when execvp fails
constexpr float kSpinnerInterval = 0.15f;       // seconds per spinner frame
constexpr float kDotsAnimationSpeed = 3.0f;     // dots cycle speed multiplier
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

static void probeLlamaCli(std::mutex & logMutex,
                          std::deque<std::string> & logMessages,
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
			std::lock_guard<std::mutex> lock(logMutex);
			logMessages.push_back("[warn] custom CLI path not found: " + customPath);
			if (logMessages.size() > kMaxLogMessages) logMessages.pop_front();
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
			std::lock_guard<std::mutex> lock(logMutex);
			logMessages.push_back("[info] llama-completion/llama-cli/llama not found in PATH or common directories.");
			if (logMessages.size() > kMaxLogMessages) logMessages.pop_front();
		}
		return;
	}
	{
		std::lock_guard<std::mutex> lock(logMutex);
		logMessages.push_back("[info] detected CLI: " + llamaCliCommand);
		if (logMessages.size() > kMaxLogMessages) logMessages.pop_front();
	}
	llamaCliState.store(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Presets — models
// ---------------------------------------------------------------------------

void ofApp::initModelPresets() {
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
// Presets — code templates (per-language quick-start skeletons)
// ---------------------------------------------------------------------------

void ofApp::initCodeTemplates() {
codeTemplates.resize(scriptLanguages.size());

// C++ templates
codeTemplates[0] = {
{"Hello World", "#include <iostream>\n\nint main() {\n    std::cout << \"Hello, World!\" << std::endl;\n    return 0;\n}\n"},
{"Class Definition", "#pragma once\n\n#include <string>\n\nclass MyClass {\npublic:\n    MyClass() = default;\n    ~MyClass() = default;\n\n    void doSomething();\n    const std::string & getName() const { return m_name; }\n\nprivate:\n    std::string m_name;\n};\n"},
{"Unit Test", "#include <cassert>\n#include <cstdio>\n\nstatic int passed = 0;\n\nvoid testExample() {\n    assert(1 + 1 == 2);\n    passed++;\n}\n\nint main() {\n    testExample();\n    std::printf(\"%d tests passed.\\n\", passed);\n    return 0;\n}\n"},
};

// Python templates
codeTemplates[1] = {
{"Hello World", "def main():\n    print(\"Hello, World!\")\n\nif __name__ == \"__main__\":\n    main()\n"},
{"Class Definition", "class MyClass:\n    def __init__(self, name: str = \"\"):\n        self._name = name\n\n    @property\n    def name(self) -> str:\n        return self._name\n\n    def do_something(self) -> None:\n        pass\n"},
{"FastAPI Server", "from fastapi import FastAPI\n\napp = FastAPI()\n\n@app.get(\"/\")\nasync def root():\n    return {\"message\": \"Hello, World!\"}\n\n@app.get(\"/items/{item_id}\")\nasync def read_item(item_id: int):\n    return {\"item_id\": item_id}\n"},
};

// JavaScript templates
codeTemplates[2] = {
{"Hello World", "console.log('Hello, World!');\n"},
{"Express Server", "const express = require('express');\nconst app = express();\nconst port = 3000;\n\napp.get('/', (req, res) => {\n    res.json({ message: 'Hello, World!' });\n});\n\napp.listen(port, () => {\n    console.log(`Server running on port ${port}`);\n});\n"},
{"Async/Await", "async function fetchData(url) {\n    try {\n        const response = await fetch(url);\n        const data = await response.json();\n        return data;\n    } catch (error) {\n        console.error('Fetch failed:', error);\n        throw error;\n    }\n}\n"},
};

// Rust templates
codeTemplates[3] = {
{"Hello World", "fn main() {\n    println!(\"Hello, World!\");\n}\n"},
{"Struct + Impl", "pub struct MyStruct {\n    name: String,\n    value: i32,\n}\n\nimpl MyStruct {\n    pub fn new(name: &str, value: i32) -> Self {\n        Self {\n            name: name.to_string(),\n            value,\n        }\n    }\n\n    pub fn name(&self) -> &str {\n        &self.name\n    }\n}\n"},
{"Error Handling", "use std::io;\nuse std::fs;\n\nfn read_config(path: &str) -> Result<String, io::Error> {\n    let content = fs::read_to_string(path)?;\n    Ok(content)\n}\n\nfn main() {\n    match read_config(\"config.toml\") {\n        Ok(content) => println!(\"Config: {}\", content),\n        Err(e) => eprintln!(\"Error: {}\", e),\n    }\n}\n"},
};

// GLSL templates
codeTemplates[4] = {
{"Vertex Shader", "#version 330 core\n\nlayout(location = 0) in vec3 aPosition;\nlayout(location = 1) in vec2 aTexCoord;\n\nout vec2 vTexCoord;\n\nuniform mat4 uModelViewProjection;\n\nvoid main() {\n    vTexCoord = aTexCoord;\n    gl_Position = uModelViewProjection * vec4(aPosition, 1.0);\n}\n"},
{"Fragment Shader", "#version 330 core\n\nin vec2 vTexCoord;\nout vec4 fragColor;\n\nuniform sampler2D uTexture;\nuniform float uTime;\n\nvoid main() {\n    vec4 color = texture(uTexture, vTexCoord);\n    fragColor = color;\n}\n"},
};

// Go templates
codeTemplates[5] = {
{"Hello World", "package main\n\nimport \"fmt\"\n\nfunc main() {\n    fmt.Println(\"Hello, World!\")\n}\n"},
{"HTTP Server", "package main\n\nimport (\n    \"fmt\"\n    \"net/http\"\n)\n\nfunc handler(w http.ResponseWriter, r *http.Request) {\n    fmt.Fprintf(w, \"Hello, World!\")\n}\n\nfunc main() {\n    http.HandleFunc(\"/\", handler)\n    fmt.Println(\"Server starting on :8080\")\n    http.ListenAndServe(\":8080\", nil)\n}\n"},
};

// Bash templates
codeTemplates[6] = {
{"Hello World", "#!/usr/bin/env bash\nset -euo pipefail\n\necho \"Hello, World!\"\n"},
{"Script with Args", "#!/usr/bin/env bash\nset -euo pipefail\n\nusage() {\n    echo \"Usage: $0 [-h] [-v] <input>\"\n    exit 1\n}\n\nVERBOSE=false\nwhile getopts \"hv\" opt; do\n    case $opt in\n        h) usage ;;\n        v) VERBOSE=true ;;\n        *) usage ;;\n    esac\ndone\nshift $((OPTIND - 1))\n\n[[ $# -lt 1 ]] && usage\n\nINPUT=\"$1\"\nif $VERBOSE; then\n    echo \"Processing: $INPUT\"\nfi\n"},
};

// TypeScript templates
codeTemplates[7] = {
{"Hello World", "const greeting: string = 'Hello, World!';\nconsole.log(greeting);\n"},
{"Interface + Class", "interface IUser {\n    id: number;\n    name: string;\n    email: string;\n}\n\nclass UserService {\n    private users: IUser[] = [];\n\n    addUser(user: IUser): void {\n        this.users.push(user);\n    }\n\n    findById(id: number): IUser | undefined {\n        return this.users.find(u => u.id === id);\n    }\n\n    getAll(): readonly IUser[] {\n        return this.users;\n    }\n}\n"},
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

// Initialize presets.
initModelPresets();
initScriptLanguages();
initCodeTemplates();
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
std::lock_guard<std::mutex> lock(logMutex);
logMessages.push_back("[" + ofToString(level) + "] " + msg);
if (logMessages.size() > 500) {
logMessages.pop_front();
}
});

// Auto-load last session if available.
autoLoadSession();

// Detect llama-completion / llama-cli / llama at startup.
probeLlamaCli(logMutex, logMessages);

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
ImGui::Separator();
ImGui::MenuItem("Verbose Console Output", nullptr, &verbose);
ImGui::EndMenu();
}
if (ImGui::BeginMenu("Options")) {
ImGui::SeparatorText("Generation");
ImGui::SliderInt("Max Tokens", &maxTokens, 32, 4096);
ImGui::SliderFloat("Temperature", &temperature, 0.0f, 2.0f, "%.2f");
ImGui::SliderFloat("Top-P", &topP, 0.0f, 1.0f, "%.2f");
ImGui::SliderFloat("Repeat Penalty", &repeatPenalty, 1.0f, 2.0f, "%.2f");
ImGui::SliderInt("Seed", &seed, -1, 99999);
if (ImGui::IsItemHovered()) {
ImGui::SetTooltip("-1 = random seed each run");
}

ImGui::SeparatorText("Mirostat Sampling");
const char * mirostatLabels[] = { "Off", "Mirostat", "Mirostat 2.0" };
ImGui::Combo("Mirostat Mode", &mirostatMode, mirostatLabels, 3);
if (ImGui::IsItemHovered()) {
ImGui::SetTooltip("Mirostat controls perplexity during generation\n"
"Off = use standard Top-P sampling");
}
if (mirostatMode > 0) {
ImGui::SliderFloat("Mirostat Tau", &mirostatTau, 0.0f, 10.0f, "%.1f");
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Target entropy (lower = more focused)");
ImGui::SliderFloat("Mirostat Eta", &mirostatEta, 0.0f, 1.0f, "%.2f");
if (ImGui::IsItemHovered()) ImGui::SetTooltip("Learning rate for Mirostat adjustment");
}

ImGui::SeparatorText("Engine");
ImGui::SliderInt("Threads", &numThreads, 1, 32);
ImGui::SliderInt("Context Size", &contextSize, 256, 16384);
ImGui::SliderInt("Batch Size", &batchSize, 32, 4096);
if (!backendNames.empty()) {
	selectedBackendIndex = std::clamp(selectedBackendIndex, 0, static_cast<int>(backendNames.size()) - 1);
	const std::string currentBackendLabel = backendNames[static_cast<size_t>(selectedBackendIndex)];
	if (ImGui::BeginCombo("Preferred Backend", currentBackendLabel.c_str())) {
		for (int i = 0; i < static_cast<int>(backendNames.size()); i++) {
			const bool isSelected = (selectedBackendIndex == i);
			if (ImGui::Selectable(backendNames[static_cast<size_t>(i)].c_str(), isSelected)) {
				if (selectedBackendIndex != i) {
					selectedBackendIndex = i;
					reinitBackend();
				}
			}
			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Choose a discovered backend/device (Vulkan appears when available).");
	}
} else {
	ImGui::TextDisabled("Preferred Backend: (no devices)");
}
{
// GPU layers control the llama-completion CLI process, which has
// its own GPU support — always allow the user to adjust them.
int settingsSliderMax = detectedModelLayers > 0 ? detectedModelLayers : 128;
ImGui::SliderInt("GPU Layers", &gpuLayers, 0, settingsSliderMax);
if (ImGui::IsItemHovered()) {
if (detectedModelLayers > 0) {
ImGui::SetTooltip("Number of model layers to offload to GPU (llama-completion)\n"
	"0 = all on CPU\nModel has %d layers", detectedModelLayers);
} else {
ImGui::SetTooltip("Number of model layers to offload to GPU (llama-completion)\n0 = all on CPU");
}
}
ImGui::SameLine();
if (ImGui::SmallButton("None##settgpu")) gpuLayers = 0;
ImGui::SameLine();
if (ImGui::SmallButton("All##settgpu")) gpuLayers = detectedModelLayers > 0 ? detectedModelLayers : 128;
}

ImGui::SeparatorText("Appearance");
const char * themeLabels[] = { "Dark", "Light", "Classic" };
if (ImGui::Combo("Theme", &themeIndex, themeLabels, 3)) {
applyTheme(themeIndex);
}

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

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();
ImGui::Text("Quick Settings");
ImGui::Spacing();
ImGui::SetNextItemWidth(-1);
ImGui::SliderInt("##MaxTok", &maxTokens, 32, 4096, "Tokens: %d");
ImGui::SetNextItemWidth(-1);
ImGui::SliderFloat("##Temp", &temperature, 0.0f, 2.0f, "Temp: %.2f");
ImGui::SetNextItemWidth(-1);
ImGui::SliderFloat("##TopP", &topP, 0.0f, 1.0f, "Top-P: %.2f");
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
ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Chat");
ImGui::SameLine();
ImGui::TextDisabled("(conversation with the ggml engine)");
ImGui::Separator();

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
ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80.0f);
bool submitted = ImGui::InputText("##ChatIn", chatInput, sizeof(chatInput),
ImGuiInputTextFlags_EnterReturnsTrue);
ImGui::SameLine();
bool sendClicked = ImGui::Button("Send", ImVec2(70, 0));

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
ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Script Generation");
ImGui::SameLine();
ImGui::TextDisabled("(generate or explain code)");
ImGui::Separator();

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
	selectedTemplateIndex = -1;
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

// Script source file browser (inline when active).
const auto scriptSourceFiles = scriptSource.getFiles();
if (sourceType != ofxGgmlScriptSourceType::None && !scriptSourceFiles.empty()) {
ImGui::BeginChild("##ScriptFiles", ImVec2(-1, 80), true);
if (sourceType == ofxGgmlScriptSourceType::LocalFolder) {
const std::string localPath = scriptSource.getLocalFolderPath();
ImGui::TextDisabled("Folder: %s", localPath.c_str());
} else {
const std::string ownerRepo = scriptSource.getGitHubOwnerRepo();
const std::string branch = scriptSource.getGitHubBranch();
ImGui::TextDisabled("GitHub: %s (%s)", ownerRepo.c_str(), branch.c_str());
}
for (int i = 0; i < static_cast<int>(scriptSourceFiles.size()); i++) {
const auto & entry = scriptSourceFiles[static_cast<size_t>(i)];
ImGui::PushID(i);
std::string icon = entry.isDirectory ? "[dir] " : "      ";
bool isSelected = (selectedScriptFileIndex == i);
if (ImGui::Selectable((icon + entry.name).c_str(), isSelected) && !entry.isDirectory) {
selectedScriptFileIndex = i;
	std::string content;
	if (scriptSource.loadFileContent(i, content)) {
		size_t maxLen = sizeof(scriptInput) - 1;
		std::strncpy(scriptInput, content.c_str(), maxLen);
		scriptInput[maxLen] = '\0';
	}
}
ImGui::PopID();
}
ImGui::EndChild();
}

if (sourceType == ofxGgmlScriptSourceType::GitHubRepo) {
drawScriptSourcePanel();
}

ImGui::Text("Describe what you want:");
ImGui::InputTextMultiline("##ScriptIn", scriptInput, sizeof(scriptInput),
ImVec2(-1, 100));

bool useProjectMemory = scriptProjectMemory.isEnabled();
if (ImGui::Checkbox("Use project memory", &useProjectMemory)) {
scriptProjectMemory.setEnabled(useProjectMemory);
}
ImGui::SameLine();
ImGui::TextDisabled("(learn from prior script requests in this session)");
if (ImGui::CollapsingHeader("Project Memory", ImGuiTreeNodeFlags_DefaultOpen)) {
if (ImGui::SmallButton("Clear Memory")) {
scriptProjectMemory.clear();
}
ImGui::SameLine();
ImGui::TextDisabled("(%d chars)", static_cast<int>(scriptProjectMemory.getMemoryText().size()));
ImGui::BeginChild("##ProjectMemory", ImVec2(-1, 100), true);
ImGui::TextWrapped("%s", scriptProjectMemory.getMemoryText().c_str());
ImGui::EndChild();
}

// Code template selector.
if (selectedLanguageIndex >= 0 &&
selectedLanguageIndex < static_cast<int>(codeTemplates.size()) &&
!codeTemplates[static_cast<size_t>(selectedLanguageIndex)].empty()) {
const auto & templates = codeTemplates[static_cast<size_t>(selectedLanguageIndex)];
ImGui::Text("Template:");
ImGui::SameLine();
ImGui::SetNextItemWidth(200);
const char * preview = (selectedTemplateIndex >= 0 &&
selectedTemplateIndex < static_cast<int>(templates.size()))
? templates[static_cast<size_t>(selectedTemplateIndex)].name.c_str()
: "(select template)";
if (ImGui::BeginCombo("##TplSel", preview)) {
for (int i = 0; i < static_cast<int>(templates.size()); i++) {
bool sel = (selectedTemplateIndex == i);
if (ImGui::Selectable(templates[static_cast<size_t>(i)].name.c_str(), sel)) {
selectedTemplateIndex = i;
const auto & code = templates[static_cast<size_t>(i)].code;
size_t maxLen = sizeof(scriptInput) - 1;
std::strncpy(scriptInput, code.c_str(), maxLen);
scriptInput[maxLen] = '\0';
}
if (sel) ImGui::SetItemDefaultFocus();
}
ImGui::EndCombo();
}
}

auto buildScriptPrompt = [this](const std::string & body) {
std::string prompt;
if (!scriptLanguages.empty()) {
prompt = scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].systemPrompt + "\n";
}
prompt += body;
return prompt;
};

ImGui::BeginDisabled(generating.load() || std::strlen(scriptInput) == 0);
if (ImGui::Button("Generate Code", ImVec2(120, 0))) {
runInference(AiMode::Script, buildScriptPrompt(scriptInput));
}
ImGui::SameLine();
if (ImGui::Button("Explain Code", ImVec2(110, 0))) {
runInference(AiMode::Script, buildScriptPrompt(
	std::string("Explain the following code:\n") + scriptInput));
}
ImGui::SameLine();
if (ImGui::Button("Debug Code", ImVec2(100, 0))) {
runInference(AiMode::Script, buildScriptPrompt(
	std::string("Find bugs in the following code:\n") + scriptInput));
}
ImGui::SameLine();
if (ImGui::Button("Optimize", ImVec2(80, 0))) {
runInference(AiMode::Script, buildScriptPrompt(
	std::string("Optimize the following code for performance. Show the improved version and explain what changed:\n") + scriptInput));
}
ImGui::SameLine();
if (ImGui::Button("Refactor", ImVec2(80, 0))) {
runInference(AiMode::Script, buildScriptPrompt(
	std::string("Refactor the following code to improve readability, maintainability, and structure. Show the refactored version:\n") + scriptInput));
}
ImGui::SameLine();
if (ImGui::Button("Review", ImVec2(70, 0))) {
runInference(AiMode::Script, buildScriptPrompt(
	std::string("Review the following code for bugs, security issues, and style. Provide specific feedback:\n") + scriptInput));
}
ImGui::EndDisabled();

// Save output to source.
if (!scriptOutput.empty() && sourceType == ofxGgmlScriptSourceType::LocalFolder) {
ImGui::SameLine();
if (ImGui::Button("Save to Folder", ImVec2(130, 0))) {
std::string filename = buildScriptFilename();
scriptSource.saveToLocalSource(filename, scriptOutput);
}
}

ImGui::Separator();
ImGui::Text("Output:");
if (!scriptOutput.empty()) {
ImGui::SameLine();
if (ImGui::SmallButton("Copy##ScriptCopy")) copyToClipboard(scriptOutput);
ImGui::SameLine();
if (ImGui::SmallButton("Clear##ScriptClear")) scriptOutput.clear();
ImGui::SameLine();
ImGui::TextDisabled("(%d chars)", static_cast<int>(scriptOutput.size()));
}
if (generating.load() && activeGenerationMode == AiMode::Script) {
ImGui::BeginChild("##ScriptOut", ImVec2(0, 0), true);
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
ImGui::BeginChild("##ScriptOut", ImVec2(0, 0), true);
ImGui::TextWrapped("%s", scriptOutput.c_str());
ImGui::EndChild();
}
}

// ---------------------------------------------------------------------------
// Script source panel — GitHub repo connection UI
// ---------------------------------------------------------------------------

void ofApp::drawScriptSourcePanel() {
ImGui::Spacing();
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
ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Summarize");
ImGui::SameLine();
ImGui::TextDisabled("(condense text into key points)");
ImGui::Separator();

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
ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Writing Assistant");
ImGui::SameLine();
ImGui::TextDisabled("(rewrite, expand, polish text)");
ImGui::Separator();

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
ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Translate");
ImGui::SameLine();
ImGui::TextDisabled("(translate text between languages)");
ImGui::Separator();

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
ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Custom Prompt");
ImGui::SameLine();
ImGui::TextDisabled("(configure system prompt + user input)");
ImGui::Separator();

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
ImGui::Text(" | Tokens: %d  Temp: %.2f  Top-P: %.2f", maxTokens, temperature, topP);
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
out << "verbose=" << (verbose ? 1 : 0) << "\n";

// Script source.
out << "scriptSourceType=" << static_cast<int>(scriptSource.getSourceType()) << "\n";
out << "scriptSourcePath=" << escapeSessionText(scriptSource.getLocalFolderPath()) << "\n";
out << "scriptSourceGitHub=" << escapeSessionText(scriptSourceGitHub) << "\n";
out << "scriptSourceBranch=" << escapeSessionText(scriptSourceBranch) << "\n";
out << "useProjectMemory=" << (scriptProjectMemory.isEnabled() ? 1 : 0) << "\n";
out << "projectMemory=" << escapeSessionText(scriptProjectMemory.getMemoryText()) << "\n";

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
else if (key == "verbose") verbose = (safeStoi(value) != 0);
else if (key == "customCliPath") { /* ignored — CLI path option removed */ }
else if (key == "scriptSourceType") {
	loadedScriptSourceType = std::clamp(safeStoi(value), 0, 2);
}
else if (key == "scriptSourcePath") loadedScriptSourcePath = unescapeSessionText(value);
else if (key == "scriptSourceGitHub") copyToBuf(scriptSourceGitHub, sizeof(scriptSourceGitHub), value);
else if (key == "scriptSourceBranch") copyToBuf(scriptSourceBranch, sizeof(scriptSourceBranch), value);
else if (key == "useProjectMemory") scriptProjectMemory.setEnabled(safeStoi(value, 1) != 0);
else if (key == "projectMemory") {
scriptProjectMemory.setMemoryText(unescapeSessionText(value));
}
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
} else {
	scriptSource.clear();
	selectedScriptFileIndex = -1;
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
	const auto & preset = modelPresets[static_cast<size_t>(selectedModelIndex)];
	return ofToDataPath(ofFilePath::join("models", preset.filename), true);
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
			if (verbose) {
				fprintf(stderr, "[ofxGgml] Detected %d layers in model (%s)\n",
					detectedModelLayers, arch.c_str());
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
				if (verbose) {
					fprintf(stderr, "[ofxGgml] Detected %d layers in model (%s)\n",
						detectedModelLayers, name);
				}
				break;
			}
		}
	}

	model.close();
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
	std::function<void(const std::string &)> onStreamData) {
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
		probeLlamaCli(logMutex, logMessages);
		if (llamaCliState.load(std::memory_order_relaxed) != 1) {
			error = "llama-completion/llama-cli/llama not found. Build with scripts/build-llama-cli.sh.";
			return false;
		}
	}

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
		std::vector<std::string> out = {
			llamaCliCommand,
			"-m", modelPath,
			(shortFlags ? "-f" : "--file"), promptPath,
			"-n", ofToString(safeMaxTokens),
			"-c", ofToString(safeContext),
			"-b", ofToString(safeBatch),
			"-ngl", ofToString(effectiveGpuLayers),
			"--temp", tempStr.str(),
			"--top-p", topPStr.str(),
			"--repeat-penalty", repeatPenaltyStr.str(),
			(shortFlags ? "-t" : "--threads"), ofToString(safeThreads),
			"--no-display-prompt",
			"--simple-io"
		};
		if (seed >= 0) {
			out.push_back("--seed");
			out.push_back(ofToString(seed));
		}
		if (mirostatMode == 1 || mirostatMode == 2) {
			out.push_back("--mirostat");
			out.push_back(ofToString(mirostatMode));
			std::ostringstream tauStr, etaStr;
			tauStr << std::fixed << std::setprecision(3) << std::clamp(mirostatTau, 0.0f, 20.0f);
			etaStr << std::fixed << std::setprecision(3) << std::clamp(mirostatEta, 0.0f, 1.0f);
			out.push_back("--mirostat-lr");
			out.push_back(etaStr.str());
			out.push_back("--mirostat-ent");
			out.push_back(tauStr.str());
		}
		return out;
	};
	std::vector<std::string> args = makeArgs(false);

	// Print the command line to console for debugging.
	if (verbose) {
		std::string cmdLine;
		for (size_t i = 0; i < args.size(); i++) {
			if (i > 0) cmdLine += " ";
			cmdLine += args[i];
		}
		fprintf(stderr, "[ofxGgml] Running: %s\n", cmdLine.c_str());
	}

	std::string raw;
	int ret = -1;
	const bool started = runProcessCapture(args, raw, ret, true, onStreamData, false);
	if (verbose) {
		fprintf(stderr, "[ofxGgml] Process %s, exit code: %d\n",
			started ? "started" : "failed to start", ret);
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
		std::lock_guard<std::mutex> lock(logMutex);
		logMessages.push_back("[warn] failed to remove temp prompt file: " + promptPath);
		if (logMessages.size() > kMaxLogMessages) logMessages.pop_front();
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

{
	std::lock_guard<std::mutex> lock(logMutex);
	logMessages.push_back("[info] Backend reinitialized: " + engineStatus);
	if (logMessages.size() > 500) logMessages.pop_front();
}
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
 std::string prompt = buildPromptForMode(mode, userText, systemPrompt);
 std::string result;
 std::string error;

 if (verbose) {
 fprintf(stderr, "\n[ofxGgml] === Generation started ===\n");
 fprintf(stderr, "[ofxGgml] Mode: %s\n", modeLabels[static_cast<int>(mode)]);
 fprintf(stderr, "[ofxGgml] Prompt (%zu chars):\n%s\n", prompt.size(), prompt.c_str());
 }

 const std::string trimmedPrompt = trim(prompt);
 auto streamCb = [this, trimmedPrompt](const std::string & partial) {
 std::string cleaned = stripAnsi(partial);
 // Strip the prompt echo so only generated text is shown during streaming.
 if (!trimmedPrompt.empty()) {
 const size_t pos = cleaned.find(trimmedPrompt);
 if (pos != std::string::npos) {
 cleaned = cleaned.substr(pos + trimmedPrompt.size());
 } else if (cleaned.size() < trimmedPrompt.size()) {
 // Prompt likely still being echoed — suppress display.
 cleaned.clear();
 }
 }
 cleaned = cleanChatOutput(cleaned);
 std::lock_guard<std::mutex> lock(streamMutex);
 streamingOutput = cleaned;
 };

 if (!runRealInference(prompt, result, error, streamCb)) {
 // If inference failed but streaming already delivered output
 // (e.g. llama-completion crashed during cleanup after producing
 // text), use the streamed data as the result instead of showing
 // an error to the user.
 std::string streamed;
 {
 std::lock_guard<std::mutex> lock(streamMutex);
 streamed = streamingOutput;
 }
 if (!streamed.empty()) {
 if (verbose) {
 fprintf(stderr, "[ofxGgml] Process failed but streamed output available (%zu chars), using it.\n", streamed.size());
 }
 result = streamed;
 } else {
 if (verbose) {
 fprintf(stderr, "[ofxGgml] Inference error: %s\n", error.c_str());
 }
 result = "[Error] " + error;
 }
 } else {
 if (verbose) {
 fprintf(stderr, "[ofxGgml] Output (%zu chars):\n%s\n", result.size(), result.c_str());
 }
 }

 if (verbose) {
 fprintf(stderr, "[ofxGgml] === Generation finished ===\n\n");
 }

{
std::lock_guard<std::mutex> lock(outputMutex);
if (!cancelRequested.load()) {
pendingOutput = cleanChatOutput(result);
pendingRole = "assistant";
pendingMode = mode;
}
}

{
std::lock_guard<std::mutex> lock(streamMutex);
streamingOutput.clear();
}

 } catch (const std::exception & e) {
 if (verbose) {
 fprintf(stderr, "[ofxGgml] Exception in worker thread: %s\n", e.what());
 }
 std::lock_guard<std::mutex> lock(outputMutex);
 pendingOutput = std::string("[Error] Internal exception: ") + e.what();
 pendingRole = "assistant";
 pendingMode = mode;
 } catch (...) {
 if (verbose) {
 fprintf(stderr, "[ofxGgml] Unknown exception in worker thread\n");
 }
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
