#include "ofxGgmlWorkspaceAssistant.h"
#include "core/ofxGgmlWindowsUtf8.h"
#include "support/ofxGgmlProcessSecurity.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
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
std::string runWindowsProcessCaptureFirstLine(
	const std::string & executable,
	const std::vector<std::string> & arguments) {
	if (trimCopy(executable).empty()) {
		return {};
	}

	SECURITY_ATTRIBUTES sa {};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	HANDLE readPipe = nullptr;
	HANDLE writePipe = nullptr;
	if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
		return {};
	}
	SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOW si {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = writePipe;
	si.hStdError = writePipe;

	std::string commandLine = ofxGgmlProcessSecurity::quoteWindowsArg(executable);
	for (const auto & arg : arguments) {
		commandLine.push_back(' ');
		commandLine += ofxGgmlProcessSecurity::quoteWindowsArg(arg);
	}

	std::wstring wideCommandLine = ofxGgmlWideFromUtf8(commandLine);
	std::vector<wchar_t> mutableCommandLine(wideCommandLine.begin(), wideCommandLine.end());
	mutableCommandLine.push_back(L'\0');

	PROCESS_INFORMATION pi {};
	const BOOL ok = CreateProcessW(
		nullptr,
		mutableCommandLine.data(),
		nullptr,
		nullptr,
		TRUE,
		CREATE_NO_WINDOW,
		nullptr,
		nullptr,
		&si,
		&pi);
	CloseHandle(writePipe);
	if (!ok) {
		CloseHandle(readPipe);
		return {};
	}

	std::string output;
	char buffer[512];
	DWORD bytesRead = 0;
	while (ReadFile(readPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) &&
		bytesRead > 0) {
		buffer[bytesRead] = '\0';
		output += buffer;
	}

	WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	CloseHandle(readPipe);

	std::istringstream stream(output);
	std::string line;
	while (std::getline(stream, line)) {
		line = trimCopy(line);
		if (!line.empty()) {
			return line;
		}
	}
	return {};
}

std::string resolveWindowsMsbuildPath() {
	static std::once_flag once;
	static std::string cached;
	std::call_once(once, []() {
		const std::string programFilesX86 = getEnvVarString("ProgramFiles(x86)");
		if (!programFilesX86.empty()) {
			const std::filesystem::path vswherePath =
				std::filesystem::path(programFilesX86) /
				"Microsoft Visual Studio" / "Installer" / "vswhere.exe";
			std::error_code ec;
			if (std::filesystem::exists(vswherePath, ec) && !ec) {
				cached = runWindowsProcessCaptureFirstLine(
					vswherePath.string(),
					{
						"-latest",
						"-products",
						"*",
						"-requires",
						"Microsoft.Component.MSBuild",
						"-find",
						"MSBuild\\**\\Bin\\MSBuild.exe"
					});
			}
		}

		if (!cached.empty()) {
			return;
		}

		const std::vector<std::filesystem::path> candidates = {
			std::filesystem::path(getEnvVarString("ProgramFiles")) /
				"Microsoft Visual Studio/18/Professional/MSBuild/Current/Bin/MSBuild.exe",
			std::filesystem::path(getEnvVarString("ProgramFiles")) /
				"Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe",
			std::filesystem::path(getEnvVarString("ProgramFiles(x86)")) /
				"Microsoft Visual Studio/2022/Professional/MSBuild/Current/Bin/MSBuild.exe",
			std::filesystem::path(getEnvVarString("ProgramFiles(x86)")) /
				"Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe"
		};
		for (const auto & candidate : candidates) {
			std::error_code ec;
			if (!candidate.empty() && std::filesystem::exists(candidate, ec) && !ec) {
				cached = candidate.string();
				return;
			}
		}
		cached = "MSBuild.exe";
	});
	return cached;
}
#endif

std::string chooseBestVisualStudioProjectPath(
	const std::vector<std::string> & changedFiles,
	const ofxGgmlScriptSourceWorkspaceInfo * workspaceInfo) {
	if (workspaceInfo == nullptr || workspaceInfo->visualStudioProjectPaths.empty()) {
		return {};
	}

	const auto & projects = workspaceInfo->visualStudioProjectPaths;
	const std::string activePath = toLowerCopy(workspaceInfo->activeVisualStudioPath);
	if (!activePath.empty() &&
		std::filesystem::path(activePath).extension().string() == ".vcxproj") {
		for (const auto & project : projects) {
			if (toLowerCopy(project) == activePath) {
				return project;
			}
		}
	}

	if (workspaceInfo->hasExplicitVisualStudioProjectSelection &&
		!trimCopy(workspaceInfo->selectedVisualStudioProjectPath).empty()) {
		return workspaceInfo->selectedVisualStudioProjectPath;
	}

	if (changedFiles.empty()) {
		return projects.front();
	}

	auto normalizePathForMatch = [](std::string value) {
		std::replace(value.begin(), value.end(), '\\', '/');
		return toLowerCopy(value);
	};

	size_t bestScore = 0;
	std::string bestProject = projects.front();
	for (const auto & project : projects) {
		const std::string projectDir = normalizePathForMatch(
			std::filesystem::path(project).parent_path().generic_string());
		size_t score = 0;
		for (const auto & changedFile : changedFiles) {
			const std::string changedLower = normalizePathForMatch(changedFile);
			if (!projectDir.empty() &&
				(changedLower.rfind(projectDir + "/", 0) == 0 ||
				 changedLower.find("/" + projectDir + "/") != std::string::npos ||
				 changedLower == projectDir)) {
				score = (std::max)(score, projectDir.size());
			}
		}
		if (score > bestScore) {
			bestScore = score;
			bestProject = project;
		}
	}
	return bestProject;
}

bool isValidExecutablePath(const std::string & path) {
	return ofxGgmlProcessSecurity::isValidExecutablePath(path);
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

	STARTUPINFOW si {};
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
	const std::string cmdLine =
		ofxGgmlProcessSecurity::buildWindowsCommandLine(args);
	if (cmdLine.empty()) {
		CloseHandle(readPipe);
		CloseHandle(writePipe);
		if (nullInput != INVALID_HANDLE_VALUE) {
			CloseHandle(nullInput);
		}
		return false;
	}

	std::wstring wideCmdLine = ofxGgmlWideFromUtf8(cmdLine);
	std::vector<wchar_t> mutableCmd(wideCmdLine.begin(), wideCmdLine.end());
	mutableCmd.push_back(L'\0');
	const std::wstring wideWorkingDirectory = ofxGgmlWideFromUtf8(workingDirectory);
	const wchar_t * workDirPtr =
		workingDirectory.empty() ? nullptr : wideWorkingDirectory.c_str();

	BOOL ok = CreateProcessW(
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

bool isPathWithinRoot(
	const std::filesystem::path & candidate,
	const std::filesystem::path & root) {
	const std::string rootString = root.lexically_normal().generic_string();
	const std::string candidateString = candidate.lexically_normal().generic_string();
	return candidateString == rootString ||
		(candidateString.size() > rootString.size() &&
			candidateString.compare(0, rootString.size(), rootString) == 0 &&
			candidateString[rootString.size()] == '/');
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
	if (!isPathWithinRoot(normalized, root)) {
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

std::string findFirstWorkspaceFileByExtension(
	const std::string & workspaceRoot,
	const std::string & extension) {
	if (trimCopy(workspaceRoot).empty() || trimCopy(extension).empty()) {
		return {};
	}

	std::error_code ec;
	auto it = std::filesystem::recursive_directory_iterator(
		workspaceRoot,
		std::filesystem::directory_options::skip_permission_denied,
		ec);
	const auto end = std::filesystem::recursive_directory_iterator();
	std::string bestMatch;
	for (; it != end; it.increment(ec)) {
		if (ec) {
			ec.clear();
			continue;
		}

		const auto & entry = *it;
		const std::string filename = entry.path().filename().string();
		if (entry.is_directory(ec)) {
			if (!filename.empty() && filename[0] == '.') {
				it.disable_recursion_pending();
			}
			ec.clear();
			continue;
		}
		if (toLowerCopy(entry.path().extension().string()) != toLowerCopy(extension)) {
			continue;
		}

		const std::string rel =
			std::filesystem::relative(entry.path(), workspaceRoot, ec).generic_string();
		if (ec) {
			ec.clear();
			continue;
		}
		if (bestMatch.empty() || rel.size() < bestMatch.size()) {
			bestMatch = rel;
		}
	}

	return bestMatch;
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

std::filesystem::path makeShadowWorkspacePath(
	const std::filesystem::path & workspaceRoot,
	const std::string & preferredRoot,
	std::string * error = nullptr) {
	std::error_code ec;
	std::filesystem::path baseRoot;
	if (!trimCopy(preferredRoot).empty()) {
		baseRoot = std::filesystem::path(preferredRoot);
		if (std::filesystem::exists(baseRoot, ec) &&
			!std::filesystem::is_directory(baseRoot, ec)) {
			if (error != nullptr) {
				*error = "Shadow workspace root must be a directory: " +
					baseRoot.generic_string();
			}
			return {};
		}
		ec.clear();
		std::filesystem::create_directories(baseRoot, ec);
		if (ec) {
			if (error != nullptr) {
				*error = "Failed to create shadow workspace root: " +
					baseRoot.generic_string();
			}
			return {};
		}
		baseRoot = std::filesystem::weakly_canonical(baseRoot, ec);
		if (ec || baseRoot.empty()) {
			if (error != nullptr) {
				*error = "Failed to resolve shadow workspace root: " +
					std::filesystem::path(preferredRoot).generic_string();
			}
			return {};
		}
	} else {
		baseRoot = std::filesystem::temp_directory_path(ec);
		if (ec || baseRoot.empty()) {
			if (error != nullptr) {
				*error = "Failed to resolve temporary directory for shadow workspace.";
			}
			return {};
		}
	}

	const std::string stem = workspaceRoot.filename().string();
	const auto now = std::chrono::system_clock::now().time_since_epoch();
	const auto stamp =
		std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
	for (int attempt = 0; attempt < 32; ++attempt) {
		const std::string suffix = attempt == 0
			? std::to_string(stamp)
			: std::to_string(stamp) + "-" + std::to_string(attempt);
		const auto candidate = baseRoot /
			("ofxggml-shadow-" + stem + "-" + suffix);
		if (isPathWithinRoot(candidate, workspaceRoot)) {
			if (error != nullptr) {
				*error = "Shadow workspace root must not be inside the source workspace.";
			}
			return {};
		}
		ec.clear();
		if (!std::filesystem::exists(candidate, ec) || ec) {
			return candidate;
		}
	}
	if (error != nullptr) {
		*error = "Failed to allocate a unique shadow workspace path.";
	}
	return {};
}

bool copyWorkspaceTree(
	const std::filesystem::path & sourceRoot,
	const std::filesystem::path & destRoot,
	std::vector<std::string> * messages) {
	std::error_code ec;
	std::filesystem::create_directories(destRoot, ec);
	if (ec) {
		if (messages != nullptr) {
			messages->push_back("Failed to create shadow workspace root: " + destRoot.string());
		}
		return false;
	}

	auto it = std::filesystem::recursive_directory_iterator(
		sourceRoot,
		std::filesystem::directory_options::skip_permission_denied,
		ec);
	const auto end = std::filesystem::recursive_directory_iterator();
	for (; it != end; it.increment(ec)) {
		if (ec) {
			if (messages != nullptr) {
				messages->push_back("Shadow copy skipped entry after filesystem error.");
			}
			ec.clear();
			continue;
		}

		const auto & entry = *it;
		const auto relative = std::filesystem::relative(entry.path(), sourceRoot, ec);
		if (ec) {
			ec.clear();
			continue;
		}
		const auto target = destRoot / relative;
		if (entry.is_directory(ec)) {
			std::filesystem::create_directories(target, ec);
			ec.clear();
			continue;
		}

		std::filesystem::create_directories(target.parent_path(), ec);
		ec.clear();
		std::filesystem::copy_file(
			entry.path(),
			target,
			std::filesystem::copy_options::overwrite_existing,
			ec);
		if (ec && messages != nullptr) {
			messages->push_back("Failed to copy into shadow workspace: " + entry.path().string());
		}
		ec.clear();
	}
	return true;
}

std::string normalizeTouchedPathRelativeToRoot(
	const std::string & filePath,
	const std::filesystem::path & root) {
	std::error_code ec;
	const std::filesystem::path input(filePath);
	if (!input.is_absolute()) {
		return input.generic_string();
	}
	const auto relative = std::filesystem::relative(input, root, ec);
	if (ec) {
		return {};
	}
	return relative.generic_string();
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
	const std::string & workspaceRoot,
	const ofxGgmlScriptSourceWorkspaceInfo * workspaceInfo) const {
	std::vector<ofxGgmlCodeAssistantCommandSuggestion> commands;
	const auto normalized = normalizeChangedFiles(changedFiles);
	if (trimCopy(workspaceRoot).empty()) {
		return commands;
	}

	const std::string selectedProjectPath =
		workspaceInfo != nullptr
			? chooseBestVisualStudioProjectPath(normalized, workspaceInfo)
			: std::string();
	const std::string solutionPath =
		workspaceInfo != nullptr
			? (workspaceInfo->hasVisualStudioSolution
				? workspaceInfo->visualStudioSolutionPath
				: std::string())
			: findFirstWorkspaceFileByExtension(workspaceRoot, ".sln");
	const std::string projectPath =
		workspaceInfo != nullptr
			? selectedProjectPath
			: findFirstWorkspaceFileByExtension(workspaceRoot, ".vcxproj");
	const std::string visualStudioConfiguration =
		workspaceInfo != nullptr && !trimCopy(workspaceInfo->selectedVisualStudioConfiguration).empty()
			? workspaceInfo->selectedVisualStudioConfiguration
			: std::string("Release");
	const std::string visualStudioPlatform =
		workspaceInfo != nullptr && !trimCopy(workspaceInfo->selectedVisualStudioPlatform).empty()
			? workspaceInfo->selectedVisualStudioPlatform
			: std::string("x64");
	const std::string defaultBuildDirectory =
		workspaceInfo != nullptr
			? workspaceInfo->defaultBuildDirectory
			: std::string();

#ifdef _WIN32
	if (!solutionPath.empty() || !projectPath.empty()) {
		ofxGgmlCodeAssistantCommandSuggestion vsBuild;
		const bool preferProject = !projectPath.empty() &&
			(workspaceInfo == nullptr ||
			 !normalized.empty() ||
			 (workspaceInfo != nullptr &&
			  (workspaceInfo->hasExplicitVisualStudioProjectSelection ||
			   (!trimCopy(workspaceInfo->activeVisualStudioPath).empty() &&
				std::filesystem::path(workspaceInfo->activeVisualStudioPath).extension().string() == ".vcxproj"))));
		vsBuild.label = preferProject
			? "build-visual-studio-project"
			: "build-visual-studio-solution";
		vsBuild.workingDirectory = workspaceRoot;
		vsBuild.executable =
			workspaceInfo != nullptr && !trimCopy(workspaceInfo->msbuildPath).empty()
				? workspaceInfo->msbuildPath
				: resolveWindowsMsbuildPath();
		vsBuild.arguments = {
			preferProject ? projectPath : solutionPath,
			"/t:Build",
			"/p:Configuration=" + visualStudioConfiguration,
			"/p:Platform=" + visualStudioPlatform,
			"/m"
		};
		vsBuild.expectedOutcome = !preferProject
			? "Visual Studio solution builds successfully"
			: "Visual Studio project builds successfully";
		commands.push_back(std::move(vsBuild));
	}
#endif

	std::string testsBuildDir = "tests/build";
	if (!trimCopy(defaultBuildDirectory).empty() &&
		fileExistsWithinWorkspace(
			workspaceRoot,
			(std::filesystem::path(defaultBuildDirectory) / "Release").generic_string())) {
		testsBuildDir = defaultBuildDirectory;
	}

	const bool testsBuildExists =
		fileExistsWithinWorkspace(workspaceRoot, testsBuildDir);
	const std::string windowsTestExe =
		(std::filesystem::path(testsBuildDir) / "Release" / "ofxGgml-tests.exe")
		.generic_string();
	const std::string posixTestExe =
		(std::filesystem::path(testsBuildDir) / "ofxGgml-tests")
		.generic_string();
	const bool hasWindowsTestExe = fileExistsWithinWorkspace(workspaceRoot, windowsTestExe);
	const bool hasPosixTestExe = fileExistsWithinWorkspace(workspaceRoot, posixTestExe);
	const bool testExeExists = hasWindowsTestExe || hasPosixTestExe;

	const bool touchesCode = std::any_of(
		normalized.begin(), normalized.end(),
		[](const std::string & file) {
			return file.rfind("src/", 0) == 0 || file.rfind("tests/", 0) == 0;
		});
	if (testsBuildExists && (touchesCode || normalized.empty())) {
		ofxGgmlCodeAssistantCommandSuggestion build;
		build.label = "build-tests";
		build.workingDirectory = workspaceRoot;
		build.executable = "cmake";
		build.arguments = {
			"--build",
			testsBuildDir,
			"--config",
			"Release",
			"--target",
			"ofxGgml-tests"
		};
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
		test.executable = hasWindowsTestExe
			? windowsTestExe
			: (std::filesystem::path(".") / posixTestExe).generic_string();
#else
		test.executable = hasPosixTestExe
			? (std::filesystem::path(".") / posixTestExe).generic_string()
			: windowsTestExe;
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

std::string ofxGgmlWorkspaceAssistant::createShadowWorkspace(
	const std::string & workspaceRoot,
	const std::string & preferredRoot) const {
	if (trimCopy(workspaceRoot).empty()) {
		return {};
	}
	std::error_code ec;
	const auto sourceRoot =
		std::filesystem::weakly_canonical(std::filesystem::path(workspaceRoot), ec);
	if (ec || sourceRoot.empty()) {
		return {};
	}

	std::string error;
	const auto shadowRoot = makeShadowWorkspacePath(
		sourceRoot,
		preferredRoot,
		&error);
	if (!error.empty() || shadowRoot.empty()) {
		return {};
	}
	std::vector<std::string> messages;
	if (!copyWorkspaceTree(sourceRoot, shadowRoot, &messages)) {
		return {};
	}
	return shadowRoot.string();
}

bool ofxGgmlWorkspaceAssistant::synchronizeShadowWorkspace(
	const std::string & shadowWorkspaceRoot,
	const std::string & workspaceRoot,
	const std::vector<std::string> & touchedFiles,
	std::vector<std::string> * messages) const {
	if (trimCopy(shadowWorkspaceRoot).empty() || trimCopy(workspaceRoot).empty()) {
		return false;
	}
	std::error_code ec;
	const auto shadowRoot = std::filesystem::weakly_canonical(
		std::filesystem::path(shadowWorkspaceRoot), ec);
	if (ec || shadowRoot.empty()) {
		return false;
	}
	ec.clear();
	const auto destRoot = std::filesystem::weakly_canonical(
		std::filesystem::path(workspaceRoot), ec);
	if (ec || destRoot.empty()) {
		return false;
	}

	bool success = true;
	std::set<std::string> uniqueFiles;
	for (const auto & touched : touchedFiles) {
		const std::string relative =
			normalizeTouchedPathRelativeToRoot(touched, shadowRoot);
		if (!trimCopy(relative).empty()) {
			uniqueFiles.insert(relative);
		}
	}

	for (const auto & relative : uniqueFiles) {
		const auto source = shadowRoot / std::filesystem::path(relative);
		const auto target = destRoot / std::filesystem::path(relative);
		if (std::filesystem::exists(source, ec) && !ec) {
			std::filesystem::create_directories(target.parent_path(), ec);
			ec.clear();
			std::filesystem::copy_file(
				source,
				target,
				std::filesystem::copy_options::overwrite_existing,
				ec);
			if (ec) {
				success = false;
				if (messages != nullptr) {
					messages->push_back("Failed to sync file from shadow workspace: " + relative);
				}
				ec.clear();
				continue;
			}
			if (messages != nullptr) {
				messages->push_back("Synchronized from shadow workspace: " + relative);
			}
		} else {
			ec.clear();
			std::filesystem::remove(target, ec);
			if (ec) {
				success = false;
				if (messages != nullptr) {
					messages->push_back("Failed to delete original file after shadow verification: " + relative);
				}
				ec.clear();
				continue;
			}
			if (messages != nullptr) {
				messages->push_back("Deleted original file from shadow workspace result: " + relative);
			}
		}
	}
	return success;
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

Result<ofxGgmlWorkspaceVerificationResult>
ofxGgmlWorkspaceAssistant::runVerificationEx(
	const std::vector<ofxGgmlCodeAssistantCommandSuggestion> & commands,
	const ofxGgmlWorkspaceSettings & settings,
	ofxGgmlWorkspaceCommandRunner commandRunner) const {
	const ofxGgmlWorkspaceVerificationResult result = runVerification(
		commands,
		settings,
		std::move(commandRunner));
	if (result.success) {
		return result;
	}
	return ofxGgmlError(
		ofxGgmlErrorCode::ComputeFailed,
		trimCopy(result.summary).empty()
			? "workspace verification failed"
			: result.summary);
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
	result.originalWorkspaceRoot = workspaceRoot;
	result.executionWorkspaceRoot = workspaceRoot;

	if (workspaceSettings.useShadowWorkspace &&
		!trimCopy(workspaceRoot).empty() &&
		!workspaceSettings.dryRun) {
		result.shadowWorkspaceRoot = createShadowWorkspace(
			workspaceRoot,
			workspaceSettings.shadowWorkspaceRoot);
		if (trimCopy(result.shadowWorkspaceRoot).empty()) {
			result.applyResult.success = false;
			result.applyResult.messages.push_back(
				"Failed to create shadow workspace.");
			result.verificationResult.success = false;
			result.verificationResult.summary =
				"Shadow workspace setup failed before apply.";
			result.success = false;
			return result;
		}
		result.usedShadowWorkspace = true;
		result.executionWorkspaceRoot = result.shadowWorkspaceRoot;
	}

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
		if (result.usedShadowWorkspace && !workspaceSettings.keepShadowWorkspace) {
			std::error_code cleanupEc;
			std::filesystem::remove_all(result.shadowWorkspaceRoot, cleanupEc);
		}
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
					? initialRequest.allowedFiles
					: workspaceSettings.allowedFiles;
			transaction = hasUnifiedDiff
				? beginUnifiedDiffTransaction(
					currentStructured.unifiedDiff,
					result.executionWorkspaceRoot,
					effectiveAllowedFiles)
				: beginTransaction(
					currentStructured.patchOperations,
					result.executionWorkspaceRoot,
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
			const ofxGgmlScriptSourceWorkspaceInfo * workspaceInfo = nullptr;
			ofxGgmlScriptSourceWorkspaceInfo workspaceInfoSnapshot;
			if (context.scriptSource != nullptr) {
				const auto sourceType = context.scriptSource->getSourceType();
				if (sourceType == ofxGgmlScriptSourceType::LocalFolder) {
					workspaceInfoSnapshot = context.scriptSource->getWorkspaceInfo();
					workspaceInfo = &workspaceInfoSnapshot;
				}
			}
			verificationCommands = suggestVerificationCommands(
				changedFiles,
				result.executionWorkspaceRoot,
				workspaceInfo);
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
			if (result.success &&
				result.usedShadowWorkspace &&
				workspaceSettings.syncShadowChangesOnSuccess &&
				!workspaceSettings.dryRun) {
				std::vector<std::string> syncMessages;
				if (!synchronizeShadowWorkspace(
						result.shadowWorkspaceRoot,
						workspaceRoot,
						result.applyResult.touchedFiles,
						&syncMessages)) {
					result.success = false;
					result.applyResult.success = false;
				}
				result.synchronizedFiles = result.applyResult.touchedFiles;
				result.applyResult.messages.insert(
					result.applyResult.messages.end(),
					syncMessages.begin(),
					syncMessages.end());
			}
			if (result.usedShadowWorkspace && !workspaceSettings.keepShadowWorkspace) {
				std::error_code cleanupEc;
				std::filesystem::remove_all(result.shadowWorkspaceRoot, cleanupEc);
			}
			return result;
		}

		result.verificationResult = runVerification(
			verificationCommands,
			workspaceSettings,
			commandRunner);
		result.verificationResult.attempts = attempt;
		if (result.verificationResult.success) {
			if (result.usedShadowWorkspace &&
				workspaceSettings.syncShadowChangesOnSuccess &&
				!workspaceSettings.dryRun) {
				std::vector<std::string> syncMessages;
				if (!synchronizeShadowWorkspace(
						result.shadowWorkspaceRoot,
						workspaceRoot,
						result.applyResult.touchedFiles,
						&syncMessages)) {
					result.applyResult.success = false;
					result.verificationResult.success = false;
					result.verificationResult.summary +=
						"\nShadow workspace synchronization failed.";
				}
				result.synchronizedFiles = result.applyResult.touchedFiles;
				result.applyResult.messages.insert(
					result.applyResult.messages.end(),
					syncMessages.begin(),
					syncMessages.end());
			}
			result.success = result.applyResult.success &&
				result.verificationResult.success;
			if (result.usedShadowWorkspace && !workspaceSettings.keepShadowWorkspace) {
				std::error_code cleanupEc;
				std::filesystem::remove_all(result.shadowWorkspaceRoot, cleanupEc);
			}
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
	if (result.usedShadowWorkspace && !workspaceSettings.keepShadowWorkspace) {
		std::error_code cleanupEc;
		std::filesystem::remove_all(result.shadowWorkspaceRoot, cleanupEc);
	}
	return result;
}

Result<ofxGgmlWorkspaceResult> ofxGgmlWorkspaceAssistant::runTaskEx(
	const std::string & modelPath,
	const ofxGgmlCodeAssistantRequest & request,
	const ofxGgmlCodeAssistantContext & context,
	const ofxGgmlWorkspaceSettings & workspaceSettings,
	const ofxGgmlInferenceSettings & inferenceSettings,
	const ofxGgmlPromptSourceSettings & sourceSettings,
	ofxGgmlWorkspaceCommandRunner commandRunner,
	ofxGgmlWorkspaceRetryProvider retryProvider) const {
	const ofxGgmlWorkspaceResult result = runTask(
		modelPath,
		request,
		context,
		workspaceSettings,
		inferenceSettings,
		sourceSettings,
		std::move(commandRunner),
		std::move(retryProvider));
	if (result.success) {
		return result;
	}
	std::string error;
	if (!result.applyResult.messages.empty()) {
		error = result.applyResult.messages.back();
	}
	if (trimCopy(error).empty()) {
		error = result.verificationResult.summary;
	}
	if (trimCopy(error).empty()) {
		error = "workspace task failed";
	}
	return ofxGgmlError(ofxGgmlErrorCode::ComputeFailed, error);
}
