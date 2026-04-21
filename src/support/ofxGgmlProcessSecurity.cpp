#include "ofxGgmlProcessSecurity.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <mutex>
#include <sstream>
#include <vector>

#ifdef _WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#else
	#include <fcntl.h>
	#include <signal.h>
	#include <sys/wait.h>
	#include <unistd.h>
#endif

#ifdef _WIN32
	#include "core/ofxGgmlWindowsUtf8.h"
#endif

namespace ofxGgmlProcessSecurity {

namespace {
std::mutex g_execConfigMutex;
bool g_allowPathLookup = false;
std::vector<std::string> g_allowedRoots;

bool isUnderAllowedRoot(const std::filesystem::path & candidate) {
	if (g_allowedRoots.empty()) return true;
	std::error_code ec;
	const auto canon = std::filesystem::weakly_canonical(candidate, ec);
	if (ec) return false;
	for (const auto & root : g_allowedRoots) {
		std::filesystem::path rootPath(root);
		const auto canonRoot = std::filesystem::weakly_canonical(rootPath, ec);
		if (ec) continue;
		auto rel = std::filesystem::relative(canon, canonRoot, ec);
		if (!ec && !rel.empty() && rel.native().front() != '.') {
			return true;
		}
		if (!ec && rel.empty()) {
			return true;
		}
	}
	return false;
}
} // namespace

std::string getEnvVarString(const char * name) {
	if (!name || *name == '\0') return {};
#ifdef _WIN32
	char * value = nullptr;
	size_t len = 0;
	const errno_t err = _dupenv_s(&value, &len, name);
	if (err != 0 || value == nullptr || len == 0) {
		if (value != nullptr) free(value);
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

bool isValidExecutablePath(const std::string & path) {
	if (path.empty()) return false;
	if (path.find('\0') != std::string::npos) return false;

	// Allow PATH lookup only when explicitly enabled for controlled environments.
	bool allowPathLookup = false;
	{
		std::lock_guard<std::mutex> lock(g_execConfigMutex);
		allowPathLookup = g_allowPathLookup;
	}
	if (!allowPathLookup) {
		const std::string envAllow = getEnvVarString("OFXGGML_ALLOW_PATH_EXEC");
		if (!envAllow.empty() && envAllow != "0") {
			allowPathLookup = true;
		}
	}

	auto containsPathSeparator = [](const std::string & value) {
		return value.find('/') != std::string::npos ||
			value.find('\\') != std::string::npos;
	};
	auto isLikelyPath = [&](const std::string & value) {
		std::filesystem::path fsPath(value);
		return fsPath.is_absolute() || fsPath.has_parent_path() ||
			containsPathSeparator(value);
	};
	auto isRegularExecutableFile = [](
		const std::filesystem::path & candidate) {
		std::error_code ec;
		if (!std::filesystem::exists(candidate, ec) || ec) return false;
		if (!std::filesystem::is_regular_file(candidate, ec) || ec) return false;
#ifndef _WIN32
		return access(candidate.c_str(), X_OK) == 0;
#else
		return true;
#endif
	};

	if (isLikelyPath(path)) {
		std::error_code ec;
		const std::filesystem::path fsPath(path);
		const std::filesystem::path canonical =
			std::filesystem::weakly_canonical(fsPath, ec);
		if (!ec && isRegularExecutableFile(canonical) && isUnderAllowedRoot(canonical)) return true;
		return isRegularExecutableFile(fsPath) && isUnderAllowedRoot(fsPath);
	}

	if (!allowPathLookup) return false;

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
		executableExtensions = {".exe", ".bat", ".cmd", ".com"};
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

void setAllowPathLookupForExecutables(bool allow) {
	std::lock_guard<std::mutex> lock(g_execConfigMutex);
	g_allowPathLookup = allow;
}

bool getAllowPathLookupForExecutables() {
	std::lock_guard<std::mutex> lock(g_execConfigMutex);
	return g_allowPathLookup;
}

void setExecutableAllowlistRoots(const std::vector<std::string> & roots) {
	std::lock_guard<std::mutex> lock(g_execConfigMutex);
	g_allowedRoots = roots;
}

std::vector<std::string> getExecutableAllowlistRoots() {
	std::lock_guard<std::mutex> lock(g_execConfigMutex);
	return g_allowedRoots;
}

#ifdef _WIN32
std::string quoteWindowsArg(const std::string & arg) {
	const bool needsQuotes =
		arg.find_first_of(" \t\"%^&|<>") != std::string::npos;
	if (!needsQuotes) {
		return arg;
	}
	std::string out;
	out.push_back('"');
	size_t backslashes = 0;
	for (char c : arg) {
		if (c == '\\') {
			++backslashes;
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

bool isWindowsBatchScript(const std::string & path) {
	const std::string ext = path.empty()
		? std::string()
		: std::filesystem::path(path).extension().string();
	std::string lowered = ext;
	std::transform(lowered.begin(), lowered.end(), lowered.begin(),
		[](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
	return lowered == ".bat" || lowered == ".cmd";
}

std::string resolveWindowsLaunchPath(const std::string & executable) {
	if (executable.empty()) {
		return {};
	}

	auto hasPathSeparator = [](const std::string & value) {
		return value.find('\\') != std::string::npos ||
			value.find('/') != std::string::npos;
	};

	const std::filesystem::path inputPath(executable);
	if (inputPath.is_absolute() || inputPath.has_parent_path() ||
		hasPathSeparator(executable)) {
		return executable;
	}

	std::vector<std::string> exts;
	const std::string pathext = getEnvVarString("PATHEXT");
	if (!pathext.empty()) {
		std::istringstream stream(pathext);
		std::string ext;
		while (std::getline(stream, ext, ';')) {
			if (!ext.empty()) {
				exts.push_back(ext);
			}
		}
	}
	if (exts.empty()) {
		exts = {".exe", ".bat", ".cmd", ".com"};
	}

	const std::string envPath = getEnvVarString("PATH");
	std::istringstream pathStream(envPath);
	std::string dir;
	while (std::getline(pathStream, dir, ';')) {
		if (dir.empty()) {
			continue;
		}

		const std::filesystem::path base(dir);
		std::error_code ec;
		if (!std::filesystem::is_directory(base, ec) || ec) {
			continue;
		}

		const std::filesystem::path direct = base / executable;
		if (std::filesystem::exists(direct, ec) && !ec) {
			return direct.string();
		}
		for (const auto & ext : exts) {
			const std::filesystem::path candidate = base / (executable + ext);
			if (std::filesystem::exists(candidate, ec) && !ec) {
				return candidate.string();
			}
		}
	}

	return executable;
}

std::string buildWindowsCommandLine(const std::vector<std::string> & args) {
	if (args.empty() || args.front().empty()) {
		return {};
	}
	const std::string resolvedExecutable = resolveWindowsLaunchPath(args.front());
	const bool useCmdWrapper = isWindowsBatchScript(resolvedExecutable);
	const std::string comspec = [&]() {
		const std::string envComspec = getEnvVarString("COMSPEC");
		return envComspec.empty()
			? std::string("C:\\Windows\\System32\\cmd.exe")
			: envComspec;
	}();

	std::string cmdLine;
	size_t cmdReserve = 0;
	for (const auto & arg : args) {
		cmdReserve += arg.size() + 3;
	}
	if (useCmdWrapper) {
		cmdReserve += resolvedExecutable.size() + comspec.size() + 32;
	}
	cmdLine.reserve(cmdReserve);
	if (useCmdWrapper) {
		cmdLine += quoteWindowsArg(comspec);
		cmdLine += " /d /s /c \"";
		cmdLine += quoteWindowsArg(resolvedExecutable);
		for (size_t i = 1; i < args.size(); ++i) {
			cmdLine.push_back(' ');
			cmdLine += quoteWindowsArg(args[i]);
		}
		cmdLine += "\"";
	} else {
		for (size_t i = 0; i < args.size(); ++i) {
			if (i > 0) cmdLine.push_back(' ');
			cmdLine += quoteWindowsArg(i == 0 ? resolvedExecutable : args[i]);
		}
	}
	return cmdLine;
}
#endif

bool runCommandCapture(
	const std::vector<std::string> & args,
	std::string & output,
	int & exitCode,
	bool mergeStderr,
	std::function<bool(const std::string &)> onChunk) {
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
	const std::string cmdLine = buildWindowsCommandLine(args);
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
	} else {
		exitCode = -1;
	}
#endif
	return true;
}

} // namespace ofxGgmlProcessSecurity
