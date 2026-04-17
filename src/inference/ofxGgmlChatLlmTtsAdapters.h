#pragma once

#include "ofFileUtils.h"
#include "ofxGgmlTtsInference.h"
#include "core/ofxGgmlWindowsUtf8.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
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

namespace ofxGgmlChatLlmTtsAdapters {

struct RuntimeOptions {
	std::string executablePath;
	std::string defaultSpeakerName = "EN-FEMALE-1-NEUTRAL";
};

inline std::string trimCopy(const std::string & value) {
	size_t start = 0;
	size_t end = value.size();
	while (start < end &&
		std::isspace(static_cast<unsigned char>(value[start]))) {
		++start;
	}
	while (end > start &&
		std::isspace(static_cast<unsigned char>(value[end - 1]))) {
		--end;
	}
	return value.substr(start, end - start);
}

inline std::string lowerCopy(const std::string & value) {
	std::string lowered = value;
	for (char & c : lowered) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return lowered;
}

inline bool hasPathSeparator(const std::string & value) {
	return value.find('\\') != std::string::npos ||
		value.find('/') != std::string::npos;
}

inline bool isDefaultExecutableHint(
	const std::string & executableHint,
	const std::vector<std::string> & canonicalNames) {
	const std::string trimmed = trimCopy(executableHint);
	if (trimmed.empty()) {
		return true;
	}
	if (hasPathSeparator(trimmed)) {
		return false;
	}
	const std::string fileName = lowerCopy(
		std::filesystem::path(trimmed).filename().string());
	return std::find(
		canonicalNames.begin(),
		canonicalNames.end(),
		fileName) != canonicalNames.end();
}

inline void appendUniquePath(
	std::vector<std::filesystem::path> & paths,
	const std::filesystem::path & candidate) {
	if (candidate.empty()) {
		return;
	}
	std::error_code ec;
	const std::filesystem::path normalized = candidate.lexically_normal();
	for (const auto & existing : paths) {
		if (existing.lexically_normal() == normalized) {
			return;
		}
	}
	paths.push_back(normalized);
}

inline std::vector<std::filesystem::path> executableSearchRoots() {
	std::vector<std::filesystem::path> roots;
	const std::filesystem::path exeDir(ofFilePath::getCurrentExeDir());
	appendUniquePath(roots, exeDir);
	appendUniquePath(roots, exeDir / ".." / ".." / "libs" / "chatllm" / "bin");
	appendUniquePath(roots, exeDir / ".." / ".." / "build" / "chatllm.cpp-build" / "bin");
	appendUniquePath(roots, exeDir / ".." / ".." / "build" / "chatllm.cpp-build" / "bin" / "Release");
	appendUniquePath(roots, exeDir / ".." / ".." / "build" / "chatllm-bld" / "bin");
	appendUniquePath(roots, exeDir / ".." / ".." / "build" / "chatllm-bld" / "bin" / "Release");

	std::error_code ec;
	const std::filesystem::path cwd = std::filesystem::current_path(ec);
	if (!ec) {
		appendUniquePath(roots, cwd);
		appendUniquePath(roots, cwd / "libs" / "chatllm" / "bin");
		appendUniquePath(roots, cwd / "build" / "chatllm.cpp-build" / "bin");
		appendUniquePath(roots, cwd / "build" / "chatllm.cpp-build" / "bin" / "Release");
		appendUniquePath(roots, cwd / "build" / "chatllm-bld" / "bin");
		appendUniquePath(roots, cwd / "build" / "chatllm-bld" / "bin" / "Release");
	}
	return roots;
}

inline std::string preferredLocalExecutablePath() {
#ifdef _WIN32
	const std::string executableName = "chatllm.exe";
#else
	const std::string executableName = "chatllm";
#endif
	const std::filesystem::path exeDir(ofFilePath::getCurrentExeDir());
	return (exeDir / ".." / ".." / "libs" / "chatllm" / "bin" / executableName)
		.lexically_normal()
		.string();
}

inline std::string findFirstExistingExecutable(
	const std::vector<std::filesystem::path> & candidates) {
	for (const auto & candidate : candidates) {
		std::error_code ec;
		if (std::filesystem::exists(candidate, ec) && !ec) {
			return candidate.string();
		}
	}
	return {};
}

#ifdef _WIN32
inline std::string quoteWindowsArg(const std::string & arg) {
	if (arg.empty()) return "\"\"";
	bool needsQuotes = false;
	for (char c : arg) {
		if (std::isspace(static_cast<unsigned char>(c)) || c == '"') {
			needsQuotes = true;
			break;
		}
	}
	if (!needsQuotes) return arg;

	std::string out = "\"";
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
		if (backslashes) {
			out.append(backslashes, '\\');
			backslashes = 0;
		}
		out.push_back(c);
	}
	if (backslashes) {
		out.append(backslashes * 2, '\\');
	}
	out.push_back('"');
	return out;
}

inline std::string getEnvVarString(const char * name) {
	if (!name || !*name) return {};
	const DWORD size = GetEnvironmentVariableA(name, nullptr, 0);
	if (size == 0) return {};
	std::string value(static_cast<size_t>(size), '\0');
	const DWORD written = GetEnvironmentVariableA(name, value.data(), size);
	if (written == 0) return {};
	if (!value.empty() && value.back() == '\0') {
		value.pop_back();
	}
	return value;
}

inline bool isWindowsBatchScript(const std::string & executable) {
	const std::string lowered = lowerCopy(
		std::filesystem::path(executable).extension().string());
	return lowered == ".bat" || lowered == ".cmd";
}

inline std::string resolveWindowsLaunchPath(const std::string & executable) {
	if (executable.empty()) return {};
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
		exts = {".exe", ".bat", ".cmd"};
	}

	const std::string envPath = getEnvVarString("PATH");
	std::istringstream pathStream(envPath);
	std::string dir;
	while (std::getline(pathStream, dir, ';')) {
		if (dir.empty()) continue;
		std::filesystem::path base = trimCopy(dir);
		std::error_code ec;
		if (!std::filesystem::is_directory(base, ec) || ec) continue;

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

inline bool runCommandCapture(
	const std::vector<std::string> & args,
	std::string & output,
	int & exitCode,
	bool mergeStderr = true,
	std::string * launchError = nullptr) {
	output.clear();
	exitCode = -1;
	if (launchError) {
		launchError->clear();
	}
	if (args.empty() || args.front().empty()) {
		if (launchError) {
			*launchError = "no executable was provided";
		}
		return false;
	}

#ifdef _WIN32
	SECURITY_ATTRIBUTES sa {};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	HANDLE readPipe = nullptr;
	HANDLE writePipe = nullptr;
	if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
		if (launchError) {
			*launchError = "CreatePipe failed";
		}
		return false;
	}
	SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOW si {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = writePipe;
	si.hStdError = mergeStderr ? writePipe : GetStdHandle(STD_ERROR_HANDLE);
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

	PROCESS_INFORMATION pi {};
	const std::string resolvedExecutable = resolveWindowsLaunchPath(args.front());
	const bool useCmdWrapper = isWindowsBatchScript(resolvedExecutable);
	const std::string comspec = [&]() {
		const std::string envComspec = getEnvVarString("COMSPEC");
		return envComspec.empty()
			? std::string("C:\\Windows\\System32\\cmd.exe")
			: envComspec;
	}();

	std::string cmdLine;
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

	std::wstring wideCmdLine = ofxGgmlWideFromUtf8(cmdLine);
	std::vector<wchar_t> mutableCmd(wideCmdLine.begin(), wideCmdLine.end());
	mutableCmd.push_back(L'\0');

	const BOOL ok = CreateProcessW(
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
	if (!ok) {
		if (launchError) {
			*launchError = "CreateProcessW failed for \"" + resolvedExecutable +
				"\" (Windows error " +
				std::to_string(static_cast<int>(GetLastError())) + ")";
		}
		CloseHandle(readPipe);
		return false;
	}

	std::array<char, 4096> buf {};
	DWORD bytesRead = 0;
	while (ReadFile(
		readPipe,
		buf.data(),
		static_cast<DWORD>(buf.size()),
		&bytesRead,
		nullptr) && bytesRead > 0) {
		output.append(buf.data(), bytesRead);
	}
	CloseHandle(readPipe);

	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD code = 0;
	GetExitCodeProcess(pi.hProcess, &code);
	exitCode = static_cast<int>(code);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
#else
	int pipeFds[2];
	if (pipe(pipeFds) != 0) {
		if (launchError) {
			*launchError = "pipe() failed";
		}
		return false;
	}

	const pid_t pid = fork();
	if (pid < 0) {
		close(pipeFds[0]);
		close(pipeFds[1]);
		if (launchError) {
			*launchError = "fork() failed";
		}
		return false;
	}

	if (pid == 0) {
		dup2(pipeFds[1], STDOUT_FILENO);
		if (mergeStderr) {
			dup2(pipeFds[1], STDERR_FILENO);
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
	waitpid(pid, &status, 0);
	if (WIFEXITED(status)) {
		exitCode = WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		exitCode = 128 + WTERMSIG(status);
	}
	return true;
#endif
}

inline std::string makeTempOutputPath(const char * extension = ".wav") {
	std::error_code ec;
	std::filesystem::path base = std::filesystem::temp_directory_path(ec);
	if (ec) {
		base = std::filesystem::current_path();
	}
	const auto stamp =
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	return (base / ("ofxggml_tts_" + std::to_string(stamp) + extension)).string();
}

inline std::string resolveChatLlmExecutable(const std::string & executableHint) {
#ifdef _WIN32
	static const std::vector<std::string> kCanonicalNames = {
		"chatllm.exe",
		"chatllm",
		"main.exe",
		"main"
	};
#else
	static const std::vector<std::string> kCanonicalNames = {
		"chatllm",
		"main"
	};
#endif

	const std::string trimmedHint = trimCopy(executableHint);
	const std::string fallback = trimmedHint.empty()
		? kCanonicalNames.front()
		: trimmedHint;
	if (!isDefaultExecutableHint(fallback, kCanonicalNames)) {
		return fallback;
	}

	std::vector<std::filesystem::path> candidates;
	const auto roots = executableSearchRoots();
	for (const auto & root : roots) {
		for (const auto & fileName : kCanonicalNames) {
			candidates.push_back(root / fileName);
		}
	}

	const std::string resolved = findFirstExistingExecutable(candidates);
	return resolved.empty() ? fallback : resolved;
}

inline bool isExplicitExecutablePath(const std::string & executableHint) {
	const std::string trimmed = trimCopy(executableHint);
	if (trimmed.empty()) {
		return false;
	}
	const std::filesystem::path path(trimmed);
	return path.is_absolute() || path.has_parent_path() || hasPathSeparator(trimmed);
}

inline std::shared_ptr<ofxGgmlTtsBackend> createBackend(
	const RuntimeOptions & options = {},
	const std::string & displayName = "ChatLLM TTS") {
	return ofxGgmlTtsInference::createChatLlmTtsBridgeBackend(
		[options, displayName](const ofxGgmlTtsRequest & request) {
			ofxGgmlTtsResult result;
			result.backendName = displayName;

			const std::string modelPath = trimCopy(request.modelPath);
			if (modelPath.empty()) {
				result.error = "chatllm.cpp model path is empty";
				return result;
			}

			if (request.task == ofxGgmlTtsTask::CloneVoice &&
				trimCopy(request.speakerPath).empty()) {
				result.error =
					"chatllm.cpp OuteTTS expects a prepared speaker.json profile for voice cloning. "
					"Creating speaker profiles from reference audio is not wired into this adapter yet.";
				return result;
			}

			if (request.task == ofxGgmlTtsTask::ContinueSpeech) {
				result.error =
					"Continue Speech is not wired into the chatllm.cpp adapter yet.";
				return result;
			}

			std::string outputPath = trimCopy(request.outputPath);
			if (outputPath.empty()) {
				outputPath = makeTempOutputPath(".wav");
			}

			const std::filesystem::path outputDir =
				std::filesystem::path(outputPath).parent_path();
			if (!outputDir.empty()) {
				std::error_code dirEc;
				std::filesystem::create_directories(outputDir, dirEc);
				if (dirEc) {
					result.error =
						"failed to create output directory: " + outputDir.string();
					return result;
				}
			}

			const std::string executable = resolveChatLlmExecutable(
				options.executablePath);
			if (isExplicitExecutablePath(options.executablePath)) {
				std::error_code execEc;
				if (!std::filesystem::exists(std::filesystem::path(executable), execEc) || execEc) {
					result.error =
						"chatllm.cpp executable not found: " + trimCopy(options.executablePath);
					return result;
				}
			}
			const auto started = std::chrono::steady_clock::now();

			std::vector<std::string> args;
			args.reserve(24);
			args.push_back(executable);
			args.push_back("-m");
			args.push_back(modelPath);
			args.push_back("-p");
			args.push_back(request.text);
			args.push_back("--tts_export");
			args.push_back(outputPath);
			args.push_back("--temp");
			args.push_back(std::to_string(request.temperature));
			args.push_back("--repeat_penalty");
			args.push_back(std::to_string(request.repetitionPenalty));
			args.push_back("--penalty_window");
			args.push_back(std::to_string(std::max(0, request.repetitionRange)));
			args.push_back("--top_k");
			args.push_back(std::to_string(std::max(0, request.topK)));
			args.push_back("--top_p");
			args.push_back(std::to_string(request.topP));

			const std::string speakerPath = trimCopy(request.speakerPath);
			if (!speakerPath.empty()) {
				args.push_back("--set");
				args.push_back("speaker");
				args.push_back(speakerPath);
			}

			if (!trimCopy(request.language).empty()) {
				result.metadata.emplace_back(
					"languageHint",
					trimCopy(request.language));
			}
			if (!trimCopy(request.speakerReferencePath).empty()) {
				result.metadata.emplace_back(
					"speakerReferenceIgnored",
					trimCopy(request.speakerReferencePath));
			}
			if (request.streamAudio) {
				result.metadata.emplace_back(
					"streamAudioIgnored",
					"chatllm.cpp adapter exports a completed audio file");
			}
			if (!request.normalizeText) {
				result.metadata.emplace_back(
					"normalizeTextHint",
					"disabled");
			}

			std::string rawOutput;
			std::string launchError;
			int exitCode = -1;
			if (!runCommandCapture(args, rawOutput, exitCode, true, &launchError)) {
				result.error = "failed to start chatllm.cpp TTS process";
				if (!launchError.empty()) {
					result.error += ": " + launchError;
				}
				return result;
			}

			result.rawOutput = rawOutput;
			result.elapsedMs = std::chrono::duration<float, std::milli>(
				std::chrono::steady_clock::now() - started).count();
			result.speakerPath = speakerPath;
			result.metadata.emplace_back("modelPath", modelPath);
			result.metadata.emplace_back("outputPath", outputPath);
			result.metadata.emplace_back("task", ofxGgmlTtsInference::taskLabel(request.task));

			std::error_code existsEc;
			const bool hasOutputFile =
				std::filesystem::exists(std::filesystem::path(outputPath), existsEc) &&
				!existsEc;
			if (exitCode != 0 || !hasOutputFile) {
				result.error = trimCopy(rawOutput);
				if (result.error.empty()) {
					if (exitCode != 0) {
						result.error =
							"chatllm.cpp TTS failed with exit code " +
							std::to_string(exitCode);
					} else {
						result.error =
							"chatllm.cpp TTS did not produce an output file";
					}
				}
				return result;
			}

			result.success = true;
			result.audioFiles.push_back({outputPath, 0, 0, 0.0f});
			return result;
		},
		displayName);
}

inline void attachBackend(
	ofxGgmlTtsInference & inference,
	const RuntimeOptions & options = {},
	const std::string & displayName = "ChatLLM TTS") {
	inference.setBackend(createBackend(options, displayName));
}

} // namespace ofxGgmlChatLlmTtsAdapters
