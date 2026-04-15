#include "ofxGgmlWorkspaceAssistant.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_set>
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

namespace {

std::string trimCopy(const std::string & s) {
	size_t start = 0;
	while (start < s.size() &&
		std::isspace(static_cast<unsigned char>(s[start]))) {
		++start;
	}
	size_t end = s.size();
	while (end > start &&
		std::isspace(static_cast<unsigned char>(s[end - 1]))) {
		--end;
	}
	return s.substr(start, end - start);
}

std::string toLowerCopy(const std::string & s) {
	std::string out = s;
	std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return out;
}

std::string getEnvVarString(const char * name) {
	if (!name || *name == '\0') return {};
#ifdef _WIN32
	char * value = nullptr;
	size_t len = 0;
	const errno_t err = _dupenv_s(&value, &len, name);
	if (err != 0 || value == nullptr || len == 0) {
		if (value != nullptr) {
			free(value);
		}
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
	std::string ext = std::filesystem::path(path).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return ext == ".bat" || ext == ".cmd";
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
#endif

bool isValidExecutablePath(const std::string & path) {
	if (path.empty()) return false;
	if (path.find('\0') != std::string::npos) return false;

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
		if (!ec && isRegularExecutableFile(canonical)) return true;
		return isRegularExecutableFile(fsPath);
	}

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

bool runCommandCapture(
	const std::vector<std::string> & args,
	const std::string & workingDirectory,
	std::string & output,
	int & exitCode) {
	output.clear();
	exitCode = -1;
	if (args.empty() || args.front().empty()) {
		return false;
	}

#ifdef _WIN32
	SECURITY_ATTRIBUTES sa {};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	HANDLE readPipe = nullptr;
	HANDLE writePipe = nullptr;
	if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
		return false;
	}
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
	si.hStdError = writePipe;

	PROCESS_INFORMATION pi {};
	std::string cmdLine;
	const std::string resolvedExecutable = resolveWindowsLaunchPath(args.front());
	const bool useCmdWrapper = isWindowsBatchScript(resolvedExecutable);
	const std::string comspec = [&]() {
		const std::string envComspec = getEnvVarString("COMSPEC");
		return envComspec.empty()
			? std::string("C:\\Windows\\System32\\cmd.exe")
			: envComspec;
	}();
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
			if (i > 0) {
				cmdLine.push_back(' ');
			}
			cmdLine += quoteWindowsArg(i == 0 ? resolvedExecutable : args[i]);
		}
	}

	std::vector<char> mutableCmd(cmdLine.begin(), cmdLine.end());
	mutableCmd.push_back('\0');
	const char * workDirPtr =
		workingDirectory.empty() ? nullptr : workingDirectory.c_str();

	BOOL ok = CreateProcessA(
		nullptr,
		mutableCmd.data(),
		nullptr,
		nullptr,
		TRUE,
		CREATE_NO_WINDOW,
		nullptr,
		workDirPtr,
		&si,
		&pi);
	CloseHandle(writePipe);
	if (nullInput != INVALID_HANDLE_VALUE) {
		CloseHandle(nullInput);
	}
	if (!ok) {
		CloseHandle(readPipe);
		return false;
	}

	std::array<char, 4096> buf {};
	DWORD bytesRead = 0;
	while (ReadFile(readPipe, buf.data(), static_cast<DWORD>(buf.size()),
		&bytesRead, nullptr) && bytesRead > 0) {
		output.append(buf.data(), bytesRead);
	}
	CloseHandle(readPipe);

	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD code = 1;
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	exitCode = static_cast<int>(code);
	return true;
#else
	int pipeFds[2] = {-1, -1};
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
		if (!workingDirectory.empty()) {
			(void)chdir(workingDirectory.c_str());
		}
		dup2(pipeFds[1], STDOUT_FILENO);
		dup2(pipeFds[1], STDERR_FILENO);
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
	return true;
#endif
}

std::string summarizeVerification(
	const std::vector<ofxGgmlWorkspaceCommandResult> & commandResults) {
	if (commandResults.empty()) {
		return "No verification commands were run.";
	}

	std::ostringstream summary;
	for (const auto & result : commandResults) {
		summary << "- " << result.command.label
			<< " (" << result.command.executable << ") => "
			<< (result.success ? "ok" : "failed")
			<< " exit=" << result.exitCode << "\n";
		if (!trimCopy(result.output).empty()) {
			summary << "  Output: " << trimCopy(result.output) << "\n";
		}
	}
	return summary.str();
}

bool hasStructuredContent(
	const ofxGgmlCodeAssistantStructuredResult & structured) {
	return structured.detectedStructuredOutput ||
		!structured.patchOperations.empty() ||
		!structured.verificationCommands.empty() ||
		!structured.filesToTouch.empty() ||
		!structured.steps.empty() ||
		!trimCopy(structured.goalSummary).empty() ||
		!trimCopy(structured.approachSummary).empty();
}

std::string patchKindToString(ofxGgmlCodeAssistantPatchKind kind) {
	switch (kind) {
	case ofxGgmlCodeAssistantPatchKind::WriteFile:
		return "write";
	case ofxGgmlCodeAssistantPatchKind::ReplaceTextOp:
		return "replace";
	case ofxGgmlCodeAssistantPatchKind::AppendText:
		return "append";
	case ofxGgmlCodeAssistantPatchKind::DeleteFileOp:
		return "delete";
	}
	return "write";
}

std::filesystem::path resolveTargetPath(
	const std::filesystem::path & root,
	const std::string & relativePath,
	std::string * error) {
	const std::filesystem::path relative(relativePath);
	if (relative.empty() || relative.is_absolute()) {
		if (error != nullptr) {
			*error = "Patch path must be a relative path inside the workspace.";
		}
		return {};
	}

	const std::filesystem::path normalized = (root / relative).lexically_normal();
	const std::string rootString = root.generic_string();
	const std::string candidateString = normalized.generic_string();
	const bool withinRoot =
		candidateString == rootString ||
		(candidateString.size() > rootString.size() &&
			candidateString.compare(0, rootString.size(), rootString) == 0 &&
			candidateString[rootString.size()] == '/');
	if (!withinRoot) {
		if (error != nullptr) {
			*error = "Patch path escapes the workspace root: " + relativePath;
		}
		return {};
	}

	return normalized;
}

std::set<std::string> normalizeAllowedFiles(
	const std::vector<std::string> & allowedFiles) {
	std::set<std::string> normalized;
	for (const auto & file : allowedFiles) {
		const std::string clean = std::filesystem::path(file).generic_string();
		if (!clean.empty()) {
			normalized.insert(clean);
		}
	}
	return normalized;
}

std::vector<std::string> normalizeChangedFiles(
	const std::vector<std::string> & changedFiles) {
	std::vector<std::string> normalized;
	normalized.reserve(changedFiles.size());
	for (const auto & file : changedFiles) {
		normalized.push_back(std::filesystem::path(file).generic_string());
	}
	return normalized;
}

bool fileExistsWithinWorkspace(
	const std::string & workspaceRoot,
	const std::string & relativePath) {
	if (trimCopy(workspaceRoot).empty() || trimCopy(relativePath).empty()) {
		return false;
	}
	std::error_code ec;
	return std::filesystem::exists(
		std::filesystem::path(workspaceRoot) / std::filesystem::path(relativePath),
		ec) && !ec;
}

std::string inferTestTagFromChangedFile(const std::string & filePath) {
	const std::string normalized = toLowerCopy(filePath);
	if (normalized.find("codeassistant") != std::string::npos) {
		return "[code_assistant],[eval]";
	}
	if (normalized.find("workspaceassistant") != std::string::npos) {
		return "[workspace_assistant],[eval]";
	}
	if (normalized.find("chatassistant") != std::string::npos) {
		return "[chat_assistant]";
	}
	if (normalized.find("textassistant") != std::string::npos) {
		return "[text_assistant]";
	}
	if (normalized.find("codereview") != std::string::npos) {
		return "[code_review]";
	}
	if (normalized.find("inference") != std::string::npos) {
		return "[inference]";
	}
	return {};
}

std::vector<std::string> splitContentLines(const std::string & content) {
	std::vector<std::string> lines;
	std::istringstream stream(content);
	std::string line;
	while (std::getline(stream, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		lines.push_back(line);
	}
	if (!content.empty() && (content.back() == '\n' || content.back() == '\r')) {
		return lines;
	}
	if (lines.empty() && !content.empty()) {
		lines.push_back(content);
	}
	return lines;
}

std::string joinContentLines(const std::vector<std::string> & lines) {
	std::ostringstream stream;
	for (size_t i = 0; i < lines.size(); ++i) {
		if (i > 0) {
			stream << '\n';
		}
		stream << lines[i];
	}
	return stream.str();
}

std::string stripDiffPathPrefix(const std::string & text) {
	const std::string trimmed = trimCopy(text);
	if (trimmed == "/dev/null") {
		return trimmed;
	}
	if (trimmed.size() > 2 && trimmed[1] == '/' &&
		(trimmed[0] == 'a' || trimmed[0] == 'b')) {
		return trimmed.substr(2);
	}
	return trimmed;
}

std::vector<ofxGgmlWorkspaceUnifiedDiffFile> parseUnifiedDiffFiles(
	const std::string & unifiedDiff) {
	std::vector<ofxGgmlWorkspaceUnifiedDiffFile> files;
	if (trimCopy(unifiedDiff).empty()) {
		return files;
	}

	static const std::regex hunkPattern(
		R"(^@@\s*-(\d+)(?:,(\d+))?\s+\+(\d+)(?:,(\d+))?\s*@@)");
	const auto lines = splitContentLines(unifiedDiff);
	ofxGgmlWorkspaceUnifiedDiffFile * currentFile = nullptr;
	ofxGgmlWorkspaceUnifiedDiffHunk * currentHunk = nullptr;

	for (const auto & line : lines) {
		if (line.rfind("--- ", 0) == 0) {
			files.emplace_back();
			currentFile = &files.back();
			currentHunk = nullptr;
			currentFile->oldPath = stripDiffPathPrefix(line.substr(4));
			continue;
		}
		if (line.rfind("+++ ", 0) == 0 && currentFile != nullptr) {
			currentFile->newPath = stripDiffPathPrefix(line.substr(4));
			currentFile->isNewFile = currentFile->oldPath == "/dev/null";
			currentFile->isDeleteFile = currentFile->newPath == "/dev/null";
			currentFile->normalizedPath = currentFile->isDeleteFile
				? currentFile->oldPath
				: currentFile->newPath;
			continue;
		}
		if (currentFile == nullptr) {
			continue;
		}
		std::smatch match;
		if (std::regex_match(line, match, hunkPattern) && match.size() >= 5) {
			currentFile->hunks.emplace_back();
			currentHunk = &currentFile->hunks.back();
			currentHunk->oldStart = std::stoi(match[1].str());
			currentHunk->oldCount = match[2].matched && !match[2].str().empty()
				? std::stoi(match[2].str())
				: 1;
			currentHunk->newStart = std::stoi(match[3].str());
			currentHunk->newCount = match[4].matched && !match[4].str().empty()
				? std::stoi(match[4].str())
				: 1;
			continue;
		}
		if (line == "\\ No newline at end of file" || currentHunk == nullptr) {
			continue;
		}
		if (!line.empty() &&
			(line[0] == ' ' || line[0] == '+' || line[0] == '-')) {
			currentHunk->lines.push_back({line[0], line.substr(1)});
		}
	}

	for (auto & file : files) {
		file.oldPath = stripDiffPathPrefix(file.oldPath);
		file.newPath = stripDiffPathPrefix(file.newPath);
		file.normalizedPath = std::filesystem::path(file.normalizedPath).generic_string();
	}
	return files;
}

bool matchHunkAt(
	const std::vector<std::string> & currentLines,
	const ofxGgmlWorkspaceUnifiedDiffHunk & hunk,
	size_t startIndex) {
	size_t cursor = startIndex;
	for (const auto & line : hunk.lines) {
		if (line.kind == '+' ) {
			continue;
		}
		if (cursor >= currentLines.size() || currentLines[cursor] != line.text) {
			return false;
		}
		++cursor;
	}
	return true;
}

std::optional<size_t> findHunkStart(
	const std::vector<std::string> & currentLines,
	const ofxGgmlWorkspaceUnifiedDiffHunk & hunk,
	size_t preferredStart) {
	if (matchHunkAt(currentLines, hunk, preferredStart)) {
		return preferredStart;
	}

	const size_t maxStart = currentLines.size();
	for (size_t radius = 1; radius <= 24; ++radius) {
		if (preferredStart >= radius &&
			matchHunkAt(currentLines, hunk, preferredStart - radius)) {
			return preferredStart - radius;
		}
		if (preferredStart + radius <= maxStart &&
			matchHunkAt(currentLines, hunk, preferredStart + radius)) {
			return preferredStart + radius;
		}
	}
	return std::nullopt;
}

std::optional<std::vector<std::string>> applyUnifiedDiffToLines(
	const std::vector<std::string> & originalLines,
	const ofxGgmlWorkspaceUnifiedDiffFile & filePatch,
	std::vector<std::string> * messages) {
	std::vector<std::string> currentLines = originalLines;
	int runningOffset = 0;

	for (const auto & hunk : filePatch.hunks) {
		size_t preferredStart = hunk.oldStart > 0
			? static_cast<size_t>((std::max)(0, hunk.oldStart - 1 + runningOffset))
			: 0u;
		const auto matchStart = findHunkStart(currentLines, hunk, preferredStart);
		if (!matchStart) {
			if (messages != nullptr) {
				messages->push_back(
					"Unified diff hunk did not match current file content for " +
					filePatch.normalizedPath);
			}
			return std::nullopt;
		}
		if (*matchStart != preferredStart && messages != nullptr) {
			messages->push_back(
				"Applied hunk with drift offset in " + filePatch.normalizedPath);
		}

		std::vector<std::string> updated;
		updated.reserve(currentLines.size() + static_cast<size_t>((std::max)(0, hunk.newCount - hunk.oldCount)));
		updated.insert(
			updated.end(),
			currentLines.begin(),
			currentLines.begin() + static_cast<std::ptrdiff_t>(*matchStart));
		size_t cursor = *matchStart;
		for (const auto & line : hunk.lines) {
			switch (line.kind) {
			case ' ':
				updated.push_back(currentLines[cursor]);
				++cursor;
				break;
			case '-':
				++cursor;
				break;
			case '+':
				updated.push_back(line.text);
				break;
			default:
				break;
			}
		}
		updated.insert(updated.end(), currentLines.begin() + static_cast<std::ptrdiff_t>(cursor), currentLines.end());
		currentLines.swap(updated);
		runningOffset += hunk.newCount - hunk.oldCount;
	}

	return currentLines;
}

std::string readWorkspaceFile(const std::filesystem::path & path) {
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) {
		return {};
	}
	return std::string(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
}

bool writeWorkspaceFile(const std::filesystem::path & path, const std::string & content) {
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		return false;
	}
	out << content;
	return true;
}

std::vector<std::string> collectUnifiedDiffChangedFiles(
	const std::vector<ofxGgmlWorkspaceUnifiedDiffFile> & files) {
	std::vector<std::string> changedFiles;
	changedFiles.reserve(files.size());
	for (const auto & file : files) {
		if (!trimCopy(file.normalizedPath).empty() &&
			file.normalizedPath != "/dev/null") {
			changedFiles.push_back(file.normalizedPath);
		}
	}
	return changedFiles;
}

ofxGgmlWorkspaceCommandResult runDefaultCommand(
	const ofxGgmlCodeAssistantCommandSuggestion & command) {
	ofxGgmlWorkspaceCommandResult result;
	result.command = command;
	const auto start = std::chrono::steady_clock::now();

	if (!isValidExecutablePath(command.executable)) {
		result.output = "Invalid executable: " + command.executable;
		result.exitCode = -1;
		result.elapsedMs = std::chrono::duration<float, std::milli>(
			std::chrono::steady_clock::now() - start).count();
		return result;
	}

	std::vector<std::string> args;
	args.push_back(command.executable);
	args.insert(args.end(),
		command.arguments.begin(),
		command.arguments.end());

	std::string output;
	int exitCode = -1;
	const bool started = runCommandCapture(
		args,
		command.workingDirectory,
		output,
		exitCode);

	result.output = output;
	result.exitCode = exitCode;
	result.success = started && exitCode == 0;
	result.elapsedMs = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - start).count();
	if (!started && result.output.empty()) {
		result.output = "Failed to launch command.";
	}
	return result;
}

} // namespace

void ofxGgmlWorkspaceAssistant::setCompletionExecutable(const std::string & path) {
	m_codeAssistant.setCompletionExecutable(path);
}

void ofxGgmlWorkspaceAssistant::setEmbeddingExecutable(const std::string & path) {
	m_codeAssistant.setEmbeddingExecutable(path);
}

ofxGgmlCodeAssistant & ofxGgmlWorkspaceAssistant::getCodeAssistant() {
	return m_codeAssistant;
}

const ofxGgmlCodeAssistant & ofxGgmlWorkspaceAssistant::getCodeAssistant() const {
	return m_codeAssistant;
}

std::string ofxGgmlWorkspaceAssistant::resolveWorkspaceRoot(
	const ofxGgmlCodeAssistantContext & context,
	const ofxGgmlWorkspaceSettings & settings) const {
	if (!trimCopy(settings.workspaceRoot).empty()) {
		return settings.workspaceRoot;
	}
	if (context.scriptSource == nullptr) {
		return {};
	}
	if (context.scriptSource->getSourceType() ==
		ofxGgmlScriptSourceType::LocalFolder) {
		return context.scriptSource->getLocalFolderPath();
	}
	return {};
}

ofxGgmlWorkspacePatchValidationResult
ofxGgmlWorkspaceAssistant::validatePatchOperations(
	const std::vector<ofxGgmlCodeAssistantPatchOperation> & operations,
	const std::string & workspaceRoot,
	const std::vector<std::string> & allowedFiles) const {
	ofxGgmlWorkspacePatchValidationResult result;
	if (operations.empty()) {
		result.messages.push_back("No patch operations to apply.");
		return result;
	}

	if (trimCopy(workspaceRoot).empty()) {
		result.success = false;
		result.messages.push_back("Workspace root is empty.");
		return result;
	}

	std::error_code ec;
	const std::filesystem::path root =
		std::filesystem::weakly_canonical(workspaceRoot, ec);
	if (ec || root.empty()) {
		result.success = false;
		result.messages.push_back(
			"Workspace root is invalid: " + workspaceRoot);
		return result;
	}

	const auto allowedFileSet = normalizeAllowedFiles(allowedFiles);
	for (const auto & operation : operations) {
		std::string error;
		const std::filesystem::path target = resolveTargetPath(
			root,
			operation.filePath,
			&error);
		if (!error.empty()) {
			result.success = false;
			result.messages.push_back(error);
			continue;
		}
		const std::string relativeTarget =
			std::filesystem::relative(target, root).generic_string();
		if (!allowedFileSet.empty() &&
			allowedFileSet.find(relativeTarget) == allowedFileSet.end()) {
			result.success = false;
			result.messages.push_back(
				"Patch path is not in the allowed file list: " + relativeTarget);
			continue;
		}
		result.validatedFiles.push_back(relativeTarget);
		switch (operation.kind) {
		case ofxGgmlCodeAssistantPatchKind::WriteFile:
		case ofxGgmlCodeAssistantPatchKind::AppendText:
		case ofxGgmlCodeAssistantPatchKind::DeleteFileOp:
			break;
		case ofxGgmlCodeAssistantPatchKind::ReplaceTextOp: {
			std::ifstream in(target, std::ios::binary);
			if (!in.is_open()) {
				result.success = false;
				result.messages.push_back("Failed to open file for validation: " +
					target.string());
				break;
			}
			std::string content(
				(std::istreambuf_iterator<char>(in)),
				std::istreambuf_iterator<char>());
			const size_t pos = content.find(operation.searchText);
			if (operation.searchText.empty() || pos == std::string::npos) {
				result.success = false;
				result.messages.push_back("Search text not found in " +
					target.string());
				break;
			}
			content.replace(
				pos,
				operation.searchText.size(),
				operation.replacementText);
			break;
		}
		}
	}

	return result;
}

ofxGgmlWorkspacePatchValidationResult
ofxGgmlWorkspaceAssistant::validateUnifiedDiff(
	const std::string & unifiedDiff,
	const std::string & workspaceRoot,
	const std::vector<std::string> & allowedFiles) const {
	ofxGgmlWorkspacePatchValidationResult result;
	if (trimCopy(unifiedDiff).empty()) {
		result.success = false;
		result.messages.push_back("Unified diff is empty.");
		return result;
	}

	if (trimCopy(workspaceRoot).empty()) {
		result.success = false;
		result.messages.push_back("Workspace root is empty.");
		return result;
	}

	std::error_code ec;
	const std::filesystem::path root =
		std::filesystem::weakly_canonical(workspaceRoot, ec);
	if (ec || root.empty()) {
		result.success = false;
		result.messages.push_back("Workspace root is invalid: " + workspaceRoot);
		return result;
	}

	const auto allowedFileSet = normalizeAllowedFiles(allowedFiles);
	const auto parsedFiles = parseUnifiedDiffFiles(unifiedDiff);
	if (parsedFiles.empty()) {
		result.success = false;
		result.messages.push_back("Unified diff could not be parsed.");
		return result;
	}

	for (const auto & filePatch : parsedFiles) {
		std::string error;
		const std::filesystem::path target = resolveTargetPath(
			root,
			filePatch.normalizedPath,
			&error);
		if (!error.empty()) {
			result.success = false;
			result.messages.push_back(error);
			continue;
		}
		const std::string relativeTarget =
			std::filesystem::relative(target, root).generic_string();
		if (!allowedFileSet.empty() &&
			allowedFileSet.find(relativeTarget) == allowedFileSet.end()) {
			result.success = false;
			result.messages.push_back(
				"Unified diff path is not in the allowed file list: " + relativeTarget);
			continue;
		}

		const bool exists = std::filesystem::exists(target, ec) && !ec;
		std::vector<std::string> currentLines;
		if (filePatch.isNewFile) {
			if (exists) {
				result.success = false;
				result.messages.push_back("Unified diff expects a new file but it already exists: " + relativeTarget);
				continue;
			}
		} else {
			if (!exists) {
				result.success = false;
				result.messages.push_back("Unified diff target does not exist: " + relativeTarget);
				continue;
			}
			currentLines = splitContentLines(readWorkspaceFile(target));
		}

		if (!filePatch.hunks.empty()) {
			std::vector<std::string> validationMessages;
			const auto maybePatched = applyUnifiedDiffToLines(
				currentLines,
				filePatch,
				&validationMessages);
			if (!maybePatched) {
				result.success = false;
				result.messages.insert(
					result.messages.end(),
					validationMessages.begin(),
					validationMessages.end());
				continue;
			}
			if (!validationMessages.empty()) {
				result.messages.insert(
					result.messages.end(),
					validationMessages.begin(),
					validationMessages.end());
			}
		}
		result.validatedFiles.push_back(relativeTarget);
	}

	return result;
}

ofxGgmlWorkspaceTransaction ofxGgmlWorkspaceAssistant::beginTransaction(
	const std::vector<ofxGgmlCodeAssistantPatchOperation> & operations,
	const std::string & workspaceRoot,
	const std::vector<std::string> & allowedFiles) const {
	ofxGgmlWorkspaceTransaction transaction;
	transaction.workspaceRoot = workspaceRoot;
	transaction.operations = operations;
	transaction.validationResult = validatePatchOperations(
		operations,
		workspaceRoot,
		allowedFiles);
	ofxGgmlCodeAssistantStructuredResult diffStructured;
	diffStructured.patchOperations = operations;
	transaction.unifiedDiffPreview =
		ofxGgmlCodeAssistant::buildUnifiedDiffFromStructuredResult(diffStructured);
	if (!transaction.validationResult.success) {
		return transaction;
	}

	std::error_code ec;
	const std::filesystem::path root =
		std::filesystem::weakly_canonical(workspaceRoot, ec);
	if (ec || root.empty()) {
		transaction.validationResult.success = false;
		transaction.validationResult.messages.push_back("Workspace root is invalid.");
		return transaction;
	}

	std::set<std::string> seenFiles;
	for (const auto & operation : operations) {
		if (!seenFiles.insert(operation.filePath).second) {
			continue;
		}

		std::string error;
		const std::filesystem::path target = resolveTargetPath(
			root,
			operation.filePath,
			&error);
		if (!error.empty()) {
			transaction.validationResult.success = false;
			transaction.validationResult.messages.push_back(error);
			continue;
		}
		ofxGgmlWorkspaceBackupEntry backup;
		backup.filePath = std::filesystem::relative(target, root).generic_string();
		backup.existedBefore = std::filesystem::exists(target, ec) && !ec;
		if (backup.existedBefore) {
			std::ifstream in(target, std::ios::binary);
			backup.originalContent.assign(
				(std::istreambuf_iterator<char>(in)),
				std::istreambuf_iterator<char>());
		}
		transaction.backups.push_back(std::move(backup));
	}

	return transaction;
}

ofxGgmlWorkspaceTransaction ofxGgmlWorkspaceAssistant::beginUnifiedDiffTransaction(
	const std::string & unifiedDiff,
	const std::string & workspaceRoot,
	const std::vector<std::string> & allowedFiles) const {
	ofxGgmlWorkspaceTransaction transaction;
	transaction.workspaceRoot = workspaceRoot;
	transaction.unifiedDiff = unifiedDiff;
	transaction.usesUnifiedDiff = true;
	transaction.parsedDiffFiles = parseUnifiedDiffFiles(unifiedDiff);
	transaction.validationResult = validateUnifiedDiff(
		unifiedDiff,
		workspaceRoot,
		allowedFiles);
	transaction.unifiedDiffPreview = unifiedDiff;
	if (!transaction.validationResult.success) {
		return transaction;
	}

	std::error_code ec;
	const std::filesystem::path root =
		std::filesystem::weakly_canonical(workspaceRoot, ec);
	if (ec || root.empty()) {
		transaction.validationResult.success = false;
		transaction.validationResult.messages.push_back("Workspace root is invalid.");
		return transaction;
	}

	for (const auto & filePatch : transaction.parsedDiffFiles) {
		std::string error;
		const std::filesystem::path target = resolveTargetPath(
			root,
			filePatch.normalizedPath,
			&error);
		if (!error.empty()) {
			transaction.validationResult.success = false;
			transaction.validationResult.messages.push_back(error);
			continue;
		}

		ofxGgmlWorkspaceBackupEntry backup;
		backup.filePath = std::filesystem::relative(target, root).generic_string();
		backup.existedBefore = std::filesystem::exists(target, ec) && !ec;
		if (backup.existedBefore) {
			backup.originalContent = readWorkspaceFile(target);
		}
		transaction.backups.push_back(std::move(backup));
	}

	return transaction;
}

ofxGgmlWorkspaceApplyResult ofxGgmlWorkspaceAssistant::applyTransaction(
	ofxGgmlWorkspaceTransaction & transaction,
	bool dryRun) const {
	ofxGgmlWorkspaceApplyResult result;
	result.unifiedDiffPreview = transaction.unifiedDiffPreview;
	if (!transaction.validationResult.success) {
		result.success = false;
		result.messages = transaction.validationResult.messages;
		return result;
	}

	if (trimCopy(transaction.workspaceRoot).empty()) {
		result.success = false;
		result.messages.push_back("Workspace root is empty.");
		return result;
	}

	std::error_code ec;
	const std::filesystem::path root =
		std::filesystem::weakly_canonical(transaction.workspaceRoot, ec);
	if (ec || root.empty()) {
		result.success = false;
		result.messages.push_back("Workspace root is invalid.");
		return result;
	}

	if (transaction.usesUnifiedDiff) {
		std::set<std::string> touched;
		for (const auto & filePatch : transaction.parsedDiffFiles) {
			std::string error;
			const std::filesystem::path target = resolveTargetPath(
				root,
				filePatch.normalizedPath,
				&error);
			if (!error.empty()) {
				result.success = false;
				result.messages.push_back(error);
				continue;
			}
			touched.insert(target.string());
			if (dryRun) {
				result.messages.push_back(
					"[dry-run] diff " + filePatch.normalizedPath);
				continue;
			}

			const bool exists = std::filesystem::exists(target, ec) && !ec;
			const std::vector<std::string> currentLines = exists
				? splitContentLines(readWorkspaceFile(target))
				: std::vector<std::string>{};
			std::vector<std::string> applyMessages;
			const auto maybePatched = applyUnifiedDiffToLines(
				currentLines,
				filePatch,
				&applyMessages);
			result.messages.insert(
				result.messages.end(),
				applyMessages.begin(),
				applyMessages.end());
			if (!maybePatched) {
				result.success = false;
				continue;
			}

			if (filePatch.isDeleteFile) {
				std::filesystem::remove(target, ec);
				if (ec) {
					result.success = false;
					result.messages.push_back("Failed to delete file: " + target.string());
				} else {
					result.messages.push_back("Deleted file via diff: " + target.string());
				}
				continue;
			}

			if (!writeWorkspaceFile(target, joinContentLines(*maybePatched))) {
				result.success = false;
				result.messages.push_back("Failed to write file from diff: " + target.string());
				continue;
			}
			result.messages.push_back("Updated file via diff: " + target.string());
		}

		transaction.applied = result.success && !dryRun;
		result.touchedFiles.assign(touched.begin(), touched.end());
		return result;
	}

	std::set<std::string> touched;
	for (const auto & operation : transaction.operations) {
		std::string error;
		const std::filesystem::path target = resolveTargetPath(
			root,
			operation.filePath,
			&error);
		if (!error.empty()) {
			result.success = false;
			result.messages.push_back(error);
			continue;
		}
		touched.insert(target.string());
		if (dryRun) {
			result.messages.push_back(
				"[dry-run] " + patchKindToString(operation.kind) + " " +
				operation.filePath + " - " + operation.summary);
			continue;
		}

		switch (operation.kind) {
		case ofxGgmlCodeAssistantPatchKind::WriteFile: {
			std::filesystem::create_directories(target.parent_path(), ec);
			std::ofstream out(target, std::ios::binary);
			if (!out.is_open()) {
				result.success = false;
				result.messages.push_back("Failed to write file: " + target.string());
				break;
			}
			out << operation.content;
			result.messages.push_back("Wrote file: " + target.string());
			break;
		}
		case ofxGgmlCodeAssistantPatchKind::AppendText: {
			std::filesystem::create_directories(target.parent_path(), ec);
			std::ofstream out(target, std::ios::binary | std::ios::app);
			if (!out.is_open()) {
				result.success = false;
				result.messages.push_back("Failed to append file: " + target.string());
				break;
			}
			out << operation.content;
			result.messages.push_back("Appended file: " + target.string());
			break;
		}
		case ofxGgmlCodeAssistantPatchKind::ReplaceTextOp: {
			std::ifstream in(target, std::ios::binary);
			if (!in.is_open()) {
				result.success = false;
				result.messages.push_back("Failed to open file for replace: " + target.string());
				break;
			}
			std::string content(
				(std::istreambuf_iterator<char>(in)),
				std::istreambuf_iterator<char>());
			const size_t pos = content.find(operation.searchText);
			if (operation.searchText.empty() || pos == std::string::npos) {
				result.success = false;
				result.messages.push_back("Search text not found in " + target.string());
				break;
			}
			content.replace(
				pos,
				operation.searchText.size(),
				operation.replacementText);
			std::ofstream out(target, std::ios::binary | std::ios::trunc);
			if (!out.is_open()) {
				result.success = false;
				result.messages.push_back("Failed to rewrite file: " + target.string());
				break;
			}
			out << content;
			result.messages.push_back("Updated file: " + target.string());
			break;
		}
		case ofxGgmlCodeAssistantPatchKind::DeleteFileOp: {
			std::filesystem::remove(target, ec);
			if (ec) {
				result.success = false;
				result.messages.push_back("Failed to delete file: " + target.string());
				break;
			}
			result.messages.push_back("Deleted file: " + target.string());
			break;
		}
		}
	}

	transaction.applied = result.success && !dryRun;
	result.touchedFiles.assign(touched.begin(), touched.end());
	return result;
}

bool ofxGgmlWorkspaceAssistant::rollbackTransaction(
	ofxGgmlWorkspaceTransaction & transaction,
	std::vector<std::string> * messages) const {
	if (!transaction.applied) {
		return true;
	}

	std::error_code ec;
	const std::filesystem::path root =
		std::filesystem::weakly_canonical(transaction.workspaceRoot, ec);
	if (ec || root.empty()) {
		if (messages != nullptr) {
			messages->push_back("Rollback failed: invalid workspace root.");
		}
		return false;
	}

	bool success = true;
	for (const auto & backup : transaction.backups) {
		const std::filesystem::path target = root / backup.filePath;
		if (backup.existedBefore) {
			std::filesystem::create_directories(target.parent_path(), ec);
			std::ofstream out(target, std::ios::binary | std::ios::trunc);
			if (!out.is_open()) {
				success = false;
				if (messages != nullptr) {
					messages->push_back("Rollback failed for " + backup.filePath);
				}
				continue;
			}
			out << backup.originalContent;
			if (messages != nullptr) {
				messages->push_back("Rolled back file: " + backup.filePath);
			}
		} else {
			std::filesystem::remove(target, ec);
			if (messages != nullptr) {
				messages->push_back("Removed newly created file: " + backup.filePath);
			}
		}
	}

	transaction.rolledBack = success;
	transaction.applied = false;
	return success;
}

ofxGgmlWorkspaceApplyResult ofxGgmlWorkspaceAssistant::applyPatchOperations(
	const std::vector<ofxGgmlCodeAssistantPatchOperation> & operations,
	const std::string & workspaceRoot,
	const std::vector<std::string> & allowedFiles,
	bool dryRun) const {
	auto transaction = beginTransaction(operations, workspaceRoot, allowedFiles);
	return applyTransaction(transaction, dryRun);
}

ofxGgmlWorkspaceApplyResult ofxGgmlWorkspaceAssistant::applyUnifiedDiff(
	const std::string & unifiedDiff,
	const std::string & workspaceRoot,
	const std::vector<std::string> & allowedFiles,
	bool dryRun) const {
	auto transaction = beginUnifiedDiffTransaction(
		unifiedDiff,
		workspaceRoot,
		allowedFiles);
	return applyTransaction(transaction, dryRun);
}

std::vector<ofxGgmlCodeAssistantCommandSuggestion>
ofxGgmlWorkspaceAssistant::suggestVerificationCommands(
	const std::vector<std::string> & changedFiles,
	const std::string & workspaceRoot) const {
	std::vector<ofxGgmlCodeAssistantCommandSuggestion> commands;
	const auto normalized = normalizeChangedFiles(changedFiles);
	if (normalized.empty() || trimCopy(workspaceRoot).empty()) {
		return commands;
	}

	const bool testsBuildExists =
		fileExistsWithinWorkspace(workspaceRoot, "tests/build");
	const bool testExeExists =
#ifdef _WIN32
		fileExistsWithinWorkspace(workspaceRoot, "tests/build/Release/ofxGgml-tests.exe");
#else
		fileExistsWithinWorkspace(workspaceRoot, "tests/build/ofxGgml-tests");
#endif

	const bool touchesCode = std::any_of(
		normalized.begin(), normalized.end(),
		[](const std::string & file) {
			return file.rfind("src/", 0) == 0 || file.rfind("tests/", 0) == 0;
		});
	if (testsBuildExists && touchesCode) {
		ofxGgmlCodeAssistantCommandSuggestion build;
		build.label = "build-tests";
		build.workingDirectory = workspaceRoot;
		build.executable = "cmake";
		build.arguments = {"--build", "tests/build", "--config", "Release", "--target", "ofxGgml-tests"};
		build.expectedOutcome = "test executable builds successfully";
		commands.push_back(std::move(build));
	}

	if (testExeExists) {
		std::set<std::string> tags;
		for (const auto & file : normalized) {
			const std::string tag = inferTestTagFromChangedFile(file);
			if (!tag.empty()) {
				tags.insert(tag);
			}
		}

		ofxGgmlCodeAssistantCommandSuggestion test;
		test.label = "run-targeted-tests";
		test.workingDirectory = workspaceRoot;
#ifdef _WIN32
		test.executable = "tests/build/Release/ofxGgml-tests.exe";
#else
		test.executable = "./tests/build/ofxGgml-tests";
#endif
		if (!tags.empty()) {
			for (const auto & tag : tags) {
				test.arguments.push_back(tag);
			}
			test.expectedOutcome = "targeted assistant tests pass";
		} else {
			test.expectedOutcome = "full addon test suite passes";
		}
		commands.push_back(std::move(test));
	}

	return commands;
}

ofxGgmlWorkspaceVerificationResult ofxGgmlWorkspaceAssistant::runVerification(
	const std::vector<ofxGgmlCodeAssistantCommandSuggestion> & commands,
	const ofxGgmlWorkspaceSettings & settings,
	ofxGgmlWorkspaceCommandRunner commandRunner) const {
	ofxGgmlWorkspaceVerificationResult result;
	result.attempts = 1;
	if (commands.empty()) {
		result.summary = "No verification commands were provided.";
		return result;
	}

	const ofxGgmlWorkspaceCommandRunner effectiveRunner =
		commandRunner ? commandRunner : runDefaultCommand;

	for (const auto & command : commands) {
		auto commandResult = effectiveRunner(command);
		result.commandResults.push_back(commandResult);
		if (!commandResult.success) {
			result.success = false;
			if (settings.stopOnFirstFailedCommand) {
				break;
			}
		}
	}

	result.summary = summarizeVerification(result.commandResults);
	return result;
}

ofxGgmlWorkspaceResult ofxGgmlWorkspaceAssistant::runTask(
	const std::string & modelPath,
	const ofxGgmlCodeAssistantRequest & request,
	const ofxGgmlCodeAssistantContext & context,
	const ofxGgmlWorkspaceSettings & workspaceSettings,
	const ofxGgmlInferenceSettings & inferenceSettings,
	const ofxGgmlPromptSourceSettings & sourceSettings,
	ofxGgmlWorkspaceCommandRunner commandRunner,
	ofxGgmlWorkspaceRetryProvider retryProvider) const {
	ofxGgmlWorkspaceResult result;
	const std::string workspaceRoot = resolveWorkspaceRoot(
		context,
		workspaceSettings);

	ofxGgmlCodeAssistantRequest initialRequest = request;
	if (initialRequest.action == ofxGgmlCodeAssistantAction::FixBuild &&
		initialRequest.allowedFiles.empty()) {
		const auto buildErrors =
			ofxGgmlCodeAssistant::parseBuildErrors(initialRequest.buildErrors);
		for (const auto & error : buildErrors) {
			if (!trimCopy(error.filePath).empty()) {
				initialRequest.allowedFiles.push_back(
					std::filesystem::path(error.filePath).generic_string());
			}
		}
	}
	initialRequest.requestStructuredResult = true;
	result.assistantAttempts.push_back(m_codeAssistant.run(
		modelPath,
		initialRequest,
		context,
		inferenceSettings,
		sourceSettings,
		nullptr));

	if (result.assistantAttempts.back().inference.success == false) {
		result.success = false;
		return result;
	}

	ofxGgmlCodeAssistantStructuredResult currentStructured =
		result.assistantAttempts.back().structured;
	int attempt = 0;
	while (true) {
		++attempt;
		ofxGgmlWorkspaceTransaction transaction;
		bool hadTransaction = false;
		const bool hasUnifiedDiff = !trimCopy(currentStructured.unifiedDiff).empty();

		if (workspaceSettings.applyPatchOperations &&
			(hasUnifiedDiff || !currentStructured.patchOperations.empty())) {
			const auto effectiveAllowedFiles =
				workspaceSettings.allowedFiles.empty()
					? request.allowedFiles
					: workspaceSettings.allowedFiles;
			transaction = hasUnifiedDiff
				? beginUnifiedDiffTransaction(
					currentStructured.unifiedDiff,
					workspaceRoot,
					effectiveAllowedFiles)
				: beginTransaction(
					currentStructured.patchOperations,
					workspaceRoot,
					effectiveAllowedFiles);
			hadTransaction = true;
			if (!transaction.validationResult.success &&
				workspaceSettings.stopOnApplyError) {
				result.applyResult.success = false;
				result.applyResult.messages.insert(
					result.applyResult.messages.end(),
					transaction.validationResult.messages.begin(),
					transaction.validationResult.messages.end());
				result.verificationResult.success = false;
				result.verificationResult.summary =
					"Patch validation failed before apply.";
				result.success = false;
				return result;
			}
			const auto applyResult = applyTransaction(
				transaction,
				workspaceSettings.dryRun);
			result.applyResult.messages.insert(
				result.applyResult.messages.end(),
				applyResult.messages.begin(),
				applyResult.messages.end());
			result.applyResult.touchedFiles.insert(
				result.applyResult.touchedFiles.end(),
				applyResult.touchedFiles.begin(),
				applyResult.touchedFiles.end());
			if (result.applyResult.unifiedDiffPreview.empty()) {
				result.applyResult.unifiedDiffPreview =
					applyResult.unifiedDiffPreview;
			}
			result.applyResult.success =
				result.applyResult.success && applyResult.success;
			if (!applyResult.success && workspaceSettings.stopOnApplyError) {
				result.verificationResult.success = false;
				result.verificationResult.summary =
					"Patch application failed before verification.";
				result.success = false;
				return result;
			}
		}

		auto verificationCommands = currentStructured.verificationCommands;
		if (workspaceSettings.runVerification &&
			verificationCommands.empty() &&
			workspaceSettings.autoSelectVerificationCommands) {
			std::vector<std::string> changedFiles = hasUnifiedDiff
				? collectUnifiedDiffChangedFiles(transaction.parsedDiffFiles)
				: std::vector<std::string>{};
			if (changedFiles.empty()) {
				for (const auto & operation : currentStructured.patchOperations) {
					changedFiles.push_back(operation.filePath);
				}
			}
			verificationCommands = suggestVerificationCommands(
				changedFiles,
				workspaceRoot);
		}

		if (!workspaceSettings.runVerification ||
			verificationCommands.empty()) {
			result.verificationResult.success = result.applyResult.success;
			result.verificationResult.attempts = attempt;
			result.verificationResult.summary =
				verificationCommands.empty()
				? "No verification commands were provided."
				: "Verification skipped by settings.";
			result.success = result.applyResult.success;
			return result;
		}

		result.verificationResult = runVerification(
			verificationCommands,
			workspaceSettings,
			commandRunner);
		result.verificationResult.attempts = attempt;
		if (result.verificationResult.success) {
			result.success = result.applyResult.success &&
				result.verificationResult.success;
			return result;
		}
		if (workspaceSettings.rollbackOnVerificationFailure && hadTransaction) {
			std::vector<std::string> rollbackMessages;
			if (!rollbackTransaction(transaction, &rollbackMessages)) {
				result.applyResult.success = false;
			}
			result.applyResult.messages.insert(
				result.applyResult.messages.end(),
				rollbackMessages.begin(),
				rollbackMessages.end());
		}

		if (attempt >= (std::max)(1, workspaceSettings.maxVerificationAttempts)) {
			break;
		}

		ofxGgmlCodeAssistantStructuredResult nextStructured;
		if (retryProvider) {
			nextStructured = retryProvider(result.verificationResult, attempt);
			if (hasStructuredContent(nextStructured)) {
				ofxGgmlCodeAssistantResult syntheticAttempt;
				syntheticAttempt.inference.success = true;
				syntheticAttempt.structured = nextStructured;
				result.assistantAttempts.push_back(std::move(syntheticAttempt));
			}
		} else if (workspaceSettings.autoRetryWithAssistant) {
			ofxGgmlCodeAssistantRequest retryRequest = request;
			retryRequest.action =
				request.action == ofxGgmlCodeAssistantAction::FixBuild
				? ofxGgmlCodeAssistantAction::FixBuild
				: ofxGgmlCodeAssistantAction::Debug;
			retryRequest.requestStructuredResult = true;
			retryRequest.lastTask =
				result.assistantAttempts.back().prepared.body;
			retryRequest.lastOutput =
				result.assistantAttempts.back().inference.text;
			retryRequest.bodyOverride =
				"Verification failed after applying the planned changes.\n\n"
				"Original task:\n" + request.userInput + "\n\n"
				"Verification summary:\n" + result.verificationResult.summary +
				"\nReturn an updated structured remediation plan.";
			result.assistantAttempts.push_back(m_codeAssistant.run(
				modelPath,
				retryRequest,
				context,
				inferenceSettings,
				sourceSettings,
				nullptr));
			nextStructured = result.assistantAttempts.back().structured;
		}

		if (!hasStructuredContent(nextStructured)) {
			break;
		}
		currentStructured = nextStructured;
	}

	result.success = false;
	return result;
}
