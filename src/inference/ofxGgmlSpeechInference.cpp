#include "ofxGgmlSpeechInference.h"
#include "core/ofxGgmlWindowsUtf8.h"
#include "support/ofxGgmlSimpleSrtSubtitleParser.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <random>
#include <regex>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
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

std::string readTextFile(const std::string & path) {
	std::ifstream input(path);
	if (!input.is_open()) {
		return {};
	}
	std::ostringstream buffer;
	buffer << input.rdbuf();
	return buffer.str();
}

std::string detectLanguageFromOutput(const std::string & text) {
	static const std::regex langRe(
		R"((?:language|lang)[^a-zA-Z0-9]+([a-z]{2,3}(?:[-_][a-z]{2,3})?))",
		std::regex::icase);
	std::smatch match;
	if (std::regex_search(text, match, langRe)) {
		return trimCopy(match[1].str());
	}
	return {};
}

std::string makeTempOutputBase(const char * prefix) {
	std::error_code ec;
	std::filesystem::path base = std::filesystem::temp_directory_path(ec);
	if (ec) {
		// Prefer system temp directory; fallback to /tmp or current path
#ifdef _WIN32
		base = std::filesystem::current_path();
#else
		base = "/tmp";
		if (!std::filesystem::exists(base, ec) || ec) {
			base = std::filesystem::current_path();
		}
#endif
	}

	// Use cryptographically strong random source for temp file names
	// to prevent prediction attacks
	std::random_device rd;
	// Collect multiple samples to ensure good entropy
	std::array<std::uint32_t, 4> seed_data;
	for (auto & s : seed_data) {
		s = rd();
	}
	std::seed_seq seed(seed_data.begin(), seed_data.end());
	std::mt19937_64 rng(seed);
	std::uniform_int_distribution<unsigned long long> dist;

	// Generate unique filename with timestamp and random component
	const auto now = std::chrono::system_clock::now();
	const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
		now.time_since_epoch()).count();

	std::ostringstream name;
	name << prefix << "_" << timestamp << "_" << std::hex << dist(rng);

	std::filesystem::path tempPath = base / name.str();

	// Verify the generated path is within the temp directory
	// to prevent directory traversal in prefix or generated components
	std::filesystem::path canonicalBase = std::filesystem::weakly_canonical(base, ec);
	std::filesystem::path canonicalTemp = std::filesystem::weakly_canonical(tempPath, ec);
	if (ec || canonicalTemp.string().find(canonicalBase.string()) != 0) {
		// Path validation failed, use simpler fallback
		name.str("");
		name << prefix << "_" << std::hex << dist(rng);
		tempPath = base / name.str();
	}

	return tempPath.string();
}

#ifdef _WIN32
std::string quoteWindowsArg(const std::string & arg) {
	const bool needsQuotes = arg.find_first_of(" \t\"%^&|<>") != std::string::npos;
	if (!needsQuotes) return arg;

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
	const std::string ext = std::filesystem::path(path).extension().string();
	std::string lowered = ext;
	std::transform(lowered.begin(), lowered.end(), lowered.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return lowered == ".bat" || lowered == ".cmd";
}

std::string resolveWindowsLaunchPath(const std::string & executable) {
	if (executable.empty()) return {};

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
		if (dir.empty()) continue;
		const std::filesystem::path base(dir);
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

bool runCommandCapture(
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
	if (nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);
	if (nullErr != INVALID_HANDLE_VALUE) CloseHandle(nullErr);

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
	int pipeFds[2] = {-1, -1};
	if (pipe(pipeFds) != 0) {
		return false;
	}

	const pid_t pid = fork();
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

} // namespace

ofxGgmlWhisperCliSpeechBackend::ofxGgmlWhisperCliSpeechBackend(
	std::string executable)
	: m_executable(std::move(executable)) {
}

void ofxGgmlWhisperCliSpeechBackend::setExecutable(const std::string & executable) {
	m_executable = executable;
}

const std::string & ofxGgmlWhisperCliSpeechBackend::getExecutable() const {
	return m_executable;
}

std::string ofxGgmlWhisperCliSpeechBackend::backendName() const {
	return "WhisperCLI";
}

std::vector<std::string> ofxGgmlWhisperCliSpeechBackend::buildCommandArguments(
	const ofxGgmlSpeechRequest & request,
	const std::string & outputBase) const {
	std::vector<std::string> args;
	args.push_back(m_executable.empty() ? "whisper-cli" : m_executable);
	if (!trimCopy(request.modelPath).empty()) {
		args.push_back("-m");
		args.push_back(trimCopy(request.modelPath));
	}
	args.push_back("-f");
	args.push_back(request.audioPath);
	args.push_back("-otxt");
	if (request.returnTimestamps) {
		args.push_back("-osrt");
		args.push_back("-ovtt");
	}
	args.push_back("-of");
	args.push_back(outputBase);
	if (!trimCopy(request.languageHint).empty() &&
		trimCopy(request.languageHint) != "Auto" &&
		trimCopy(request.languageHint) != "auto") {
		args.push_back("-l");
		args.push_back(trimCopy(request.languageHint));
	}
	if (request.task == ofxGgmlSpeechTask::Translate) {
		args.push_back("--translate");
	}
	if (!trimCopy(request.prompt).empty()) {
		args.push_back("--prompt");
		args.push_back(trimCopy(request.prompt));
	}
	return args;
}

std::string ofxGgmlWhisperCliSpeechBackend::expectedTranscriptPath(
	const std::string & outputBase) const {
	return outputBase + ".txt";
}

std::string ofxGgmlWhisperCliSpeechBackend::expectedSrtPath(
	const std::string & outputBase) const {
	return outputBase + ".srt";
}

std::string ofxGgmlWhisperCliSpeechBackend::expectedVttPath(
	const std::string & outputBase) const {
	return outputBase + ".vtt";
}

std::vector<ofxGgmlSpeechSegment> ofxGgmlWhisperCliSpeechBackend::parseSrtSegments(
	const std::string & srtText) {
	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;
	const bool ok = ofxGgmlSimpleSrtSubtitleParser::parseText(
		srtText,
		cues,
		error);
	if (!ok) {
		return {};
	}

	std::vector<ofxGgmlSpeechSegment> segments;
	segments.reserve(cues.size());
	for (const auto & cue : cues) {
		ofxGgmlSpeechSegment segment;
		segment.startSeconds = static_cast<double>(cue.startMs) / 1000.0;
		segment.endSeconds = static_cast<double>(cue.endMs) / 1000.0;
		segment.text = trimCopy(cue.text);
		if (!segment.text.empty()) {
			segments.push_back(std::move(segment));
		}
	}
	return segments;
}

ofxGgmlSpeechResult ofxGgmlWhisperCliSpeechBackend::transcribe(
	const ofxGgmlSpeechRequest & request) const {
	ofxGgmlSpeechResult result;
	result.backendName = backendName();

	const std::string audioPath = trimCopy(request.audioPath);
	if (audioPath.empty()) {
		result.error = "no audio file was provided";
		return result;
	}

	std::error_code ec;
	if (!std::filesystem::exists(std::filesystem::path(audioPath), ec) || ec) {
		result.error = "audio file not found: " + audioPath;
		return result;
	}

	const std::string outputBase = makeTempOutputBase("ofxggml_whisper");
	result.transcriptPath = expectedTranscriptPath(outputBase);
	result.srtPath = expectedSrtPath(outputBase);
	result.vttPath = expectedVttPath(outputBase);
	const auto args = buildCommandArguments(request, outputBase);

	const auto t0 = std::chrono::steady_clock::now();
	int exitCode = -1;
	if (!runCommandCapture(args, result.rawOutput, exitCode, true)) {
		result.error = "failed to start whisper CLI process";
		return result;
	}

	result.text = trimCopy(readTextFile(result.transcriptPath));
	if (result.text.empty()) {
		result.text = trimCopy(result.rawOutput);
	}
	result.detectedLanguage = detectLanguageFromOutput(result.rawOutput);
	if (request.returnTimestamps) {
		const std::string srtText = readTextFile(result.srtPath);
		if (!srtText.empty()) {
			result.segments = parseSrtSegments(srtText);
		}
	}
	result.elapsedMs = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - t0).count();

	if (exitCode != 0 && result.text.empty()) {
		result.error = "whisper CLI failed with exit code " + std::to_string(exitCode);
		return result;
	}
	if (result.text.empty()) {
		result.error = "speech backend returned empty output";
		return result;
	}

	result.success = true;
	return result;
}

ofxGgmlSpeechInference::ofxGgmlSpeechInference()
	: m_backend(createWhisperCliBackend()) {
}

std::vector<ofxGgmlSpeechModelProfile> ofxGgmlSpeechInference::defaultProfiles() {
	return {
		{
			"Whisper Tiny.en",
			"ggerganov/whisper.cpp",
			"ggml-tiny.en.bin",
			"",
			"whisper-cli",
			false,
			false
		},
		{
			"Whisper Base.en",
			"ggerganov/whisper.cpp",
			"ggml-base.en.bin",
			"",
			"whisper-cli",
			false,
			false
		},
		{
			"Whisper Small",
			"ggerganov/whisper.cpp",
			"ggml-small.bin",
			"",
			"whisper-cli",
			true,
			false
		},
		{
			"Whisper Large-v3 Turbo",
			"ggerganov/whisper.cpp",
			"ggml-large-v3-turbo.bin",
			"",
			"whisper-cli",
			true,
			true
		}
	};
}

const char * ofxGgmlSpeechInference::taskLabel(ofxGgmlSpeechTask task) {
	switch (task) {
	case ofxGgmlSpeechTask::Transcribe: return "Transcribe";
	case ofxGgmlSpeechTask::Translate: return "Translate";
	}
	return "Transcribe";
}

std::shared_ptr<ofxGgmlSpeechBackend> ofxGgmlSpeechInference::createWhisperCliBackend(
	const std::string & executable) {
	return std::make_shared<ofxGgmlWhisperCliSpeechBackend>(executable);
}

void ofxGgmlSpeechInference::setBackend(std::shared_ptr<ofxGgmlSpeechBackend> backend) {
	m_backend = backend ? std::move(backend) : createWhisperCliBackend();
}

std::shared_ptr<ofxGgmlSpeechBackend> ofxGgmlSpeechInference::getBackend() const {
	return m_backend;
}

ofxGgmlSpeechResult ofxGgmlSpeechInference::transcribe(
	const ofxGgmlSpeechRequest & request) const {
	const auto backend = m_backend ? m_backend : createWhisperCliBackend();
	return backend->transcribe(request);
}
