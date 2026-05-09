#include "ofxGgmlLlamaCliTextBackend.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <sstream>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

void appendOption(std::vector<std::string> & args, const std::string & name, int value) {
	args.push_back(name);
	args.push_back(std::to_string(value));
}

void appendOption(std::vector<std::string> & args, const std::string & name, float value) {
	std::ostringstream stream;
	stream << value;
	args.push_back(name);
	args.push_back(stream.str());
}

std::string roleLabel(ofxGgmlTextRole role) {
	switch (role) {
	case ofxGgmlTextRole::System: return "System";
	case ofxGgmlTextRole::User: return "User";
	case ofxGgmlTextRole::Assistant: return "Assistant";
	}
	return "User";
}

#if defined(_WIN32)

std::wstring utf8ToWide(const std::string & text) {
	if (text.empty()) {
		return {};
	}
	const int count = MultiByteToWideChar(
		CP_UTF8,
		0,
		text.data(),
		static_cast<int>(text.size()),
		nullptr,
		0);
	if (count <= 0) {
		return {};
	}
	std::wstring wide(static_cast<std::size_t>(count), L'\0');
	MultiByteToWideChar(
		CP_UTF8,
		0,
		text.data(),
		static_cast<int>(text.size()),
		wide.data(),
		count);
	return wide;
}

std::string lastWindowsError(const char * label) {
	const DWORD errorCode = GetLastError();
	LPSTR message = nullptr;
	const DWORD length = FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		errorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPSTR>(&message),
		0,
		nullptr);
	std::ostringstream stream;
	stream << label << " failed";
	if (errorCode != 0) {
		stream << " with Windows error " << errorCode;
	}
	if (length > 0 && message) {
		stream << ": " << message;
	}
	if (message) {
		LocalFree(message);
	}
	return stream.str();
}

std::wstring quoteWindowsArg(const std::string & arg) {
	const std::wstring wide = utf8ToWide(arg);
	if (wide.empty()) {
		return L"\"\"";
	}
	const bool needsQuotes =
		wide.find_first_of(L" \t\n\v\"") != std::wstring::npos;
	if (!needsQuotes) {
		return wide;
	}

	std::wstring quoted = L"\"";
	std::size_t backslashes = 0;
	for (wchar_t c : wide) {
		if (c == L'\\') {
			++backslashes;
			continue;
		}
		if (c == L'"') {
			quoted.append(backslashes * 2 + 1, L'\\');
			quoted.push_back(c);
			backslashes = 0;
			continue;
		}
		quoted.append(backslashes, L'\\');
		backslashes = 0;
		quoted.push_back(c);
	}
	quoted.append(backslashes * 2, L'\\');
	quoted.push_back(L'"');
	return quoted;
}

std::wstring buildWindowsCommandLine(const ofxGgmlTextCommand & command) {
	std::wstring line = quoteWindowsArg(command.executablePath);
	for (const auto & arg : command.arguments) {
		line.push_back(L' ');
		line += quoteWindowsArg(arg);
	}
	return line;
}

ofxGgmlTextCommandResult runCommandWindows(
	const ofxGgmlTextCommand & command,
	const ofxGgmlTextChunkCallback & onChunk) {
	ofxGgmlTextCommandResult result;

	SECURITY_ATTRIBUTES security {};
	security.nLength = sizeof(security);
	security.bInheritHandle = TRUE;

	HANDLE readPipe = nullptr;
	HANDLE writePipe = nullptr;
	if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
		result.error = lastWindowsError("CreatePipe");
		return result;
	}
	if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0)) {
		result.error = lastWindowsError("SetHandleInformation");
		CloseHandle(readPipe);
		CloseHandle(writePipe);
		return result;
	}

	STARTUPINFOW startup {};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	startup.hStdOutput = writePipe;
	startup.hStdError = writePipe;

	PROCESS_INFORMATION process {};
	std::wstring commandLine = buildWindowsCommandLine(command);
	std::vector<wchar_t> mutableCommandLine(
		commandLine.begin(),
		commandLine.end());
	mutableCommandLine.push_back(L'\0');

	const BOOL created = CreateProcessW(
		nullptr,
		mutableCommandLine.data(),
		nullptr,
		nullptr,
		TRUE,
		CREATE_NO_WINDOW,
		nullptr,
		nullptr,
		&startup,
		&process);
	CloseHandle(writePipe);

	if (!created) {
		result.error = lastWindowsError("CreateProcessW");
		CloseHandle(readPipe);
		return result;
	}
	result.started = true;

	std::string output;
	bool cancelled = false;
	char buffer[4096];
	DWORD bytesRead = 0;
	while (ReadFile(
		readPipe,
		buffer,
		static_cast<DWORD>(sizeof(buffer)),
		&bytesRead,
		nullptr) &&
		bytesRead > 0) {
		const std::string chunk(buffer, buffer + bytesRead);
		output += chunk;
		if (onChunk && !onChunk(chunk)) {
			cancelled = true;
			TerminateProcess(process.hProcess, 130);
			break;
		}
	}
	CloseHandle(readPipe);
	WaitForSingleObject(process.hProcess, INFINITE);

	DWORD exitCode = 1;
	GetExitCodeProcess(process.hProcess, &exitCode);
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);

	result.exitCode = static_cast<int>(exitCode);
	result.output = std::move(output);
	if (cancelled) {
		result.error = "llama.cpp CLI command was cancelled by callback";
	}
	return result;
}

#else

ofxGgmlTextCommandResult runCommandPosix(
	const ofxGgmlTextCommand & command,
	const ofxGgmlTextChunkCallback & onChunk) {
	ofxGgmlTextCommandResult result;
	int pipeFds[2] = { -1, -1 };
	if (pipe(pipeFds) != 0) {
		result.error = std::string("pipe failed: ") + std::strerror(errno);
		return result;
	}

	const pid_t pid = fork();
	if (pid < 0) {
		result.error = std::string("fork failed: ") + std::strerror(errno);
		close(pipeFds[0]);
		close(pipeFds[1]);
		return result;
	}
	if (pid == 0) {
		dup2(pipeFds[1], STDOUT_FILENO);
		dup2(pipeFds[1], STDERR_FILENO);
		close(pipeFds[0]);
		close(pipeFds[1]);

		std::vector<char *> argv;
		argv.reserve(command.arguments.size() + 2);
		argv.push_back(const_cast<char *>(command.executablePath.c_str()));
		for (const auto & arg : command.arguments) {
			argv.push_back(const_cast<char *>(arg.c_str()));
		}
		argv.push_back(nullptr);
		execvp(command.executablePath.c_str(), argv.data());
		_exit(127);
	}

	close(pipeFds[1]);
	result.started = true;

	std::string output;
	bool cancelled = false;
	char buffer[4096];
	for (;;) {
		const ssize_t bytesRead = read(pipeFds[0], buffer, sizeof(buffer));
		if (bytesRead > 0) {
			const std::string chunk(buffer, buffer + bytesRead);
			output += chunk;
			if (onChunk && !onChunk(chunk)) {
				cancelled = true;
				kill(pid, SIGTERM);
				break;
			}
			continue;
		}
		if (bytesRead == 0) {
			break;
		}
		if (errno == EINTR) {
			continue;
		}
		result.error = std::string("read failed: ") + std::strerror(errno);
		break;
	}
	close(pipeFds[0]);

	int status = 0;
	waitpid(pid, &status, 0);
	if (WIFEXITED(status)) {
		result.exitCode = WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		result.exitCode = 128 + WTERMSIG(status);
	} else {
		result.exitCode = 1;
	}
	result.output = std::move(output);
	if (cancelled) {
		result.error = "llama.cpp CLI command was cancelled by callback";
	}
	return result;
}

#endif

} // namespace

ofxGgmlLlamaCliTextBackend::ofxGgmlLlamaCliTextBackend(
	ofxGgmlTextCommandRunner runner,
	std::string displayName)
	: m_runner(runner ? std::move(runner) : ofxGgmlLlamaCliTextBackend::runCommand)
	, m_displayName(std::move(displayName)) {
}

void ofxGgmlLlamaCliTextBackend::setCommandRunner(
	ofxGgmlTextCommandRunner runner) {
	m_runner = runner ? std::move(runner) : ofxGgmlLlamaCliTextBackend::runCommand;
}

bool ofxGgmlLlamaCliTextBackend::hasCommandRunner() const {
	return static_cast<bool>(m_runner);
}

std::string ofxGgmlLlamaCliTextBackend::backendName() const {
	return m_displayName.empty() ? "llama.cpp CLI" : m_displayName;
}

ofxGgmlTextResult ofxGgmlLlamaCliTextBackend::generate(
	const ofxGgmlTextRequest & request,
	ofxGgmlTextChunkCallback onChunk) const {
	ofxGgmlTextResult result;
	result.backendName = backendName();

	const std::string prompt = composePrompt(request);
	if (request.settings.executablePath.empty()) {
		result.error = "llama.cpp CLI executable path is empty";
		return result;
	}
	if (request.modelPath.empty()) {
		result.error = "model path is empty";
		return result;
	}
	if (prompt.empty()) {
		result.error = "prompt is empty";
		return result;
	}
	const auto started = std::chrono::steady_clock::now();
	const ofxGgmlTextCommand command = buildCommand(request, prompt);
	const ofxGgmlTextCommandResult commandResult = m_runner(command, onChunk);
	result.elapsedMs = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - started).count();
	result.rawOutput = commandResult.output;
	result.metadata.push_back({ "executable", command.executablePath });
	result.metadata.push_back({ "model", request.modelPath });

	if (!commandResult.started) {
		result.error = commandResult.error.empty()
			? "llama.cpp CLI process did not start"
			: commandResult.error;
		return result;
	}
	if (commandResult.exitCode != 0) {
		result.error = commandResult.error.empty()
			? "llama.cpp CLI exited with code " + std::to_string(commandResult.exitCode)
			: commandResult.error;
		return result;
	}
	if (commandResult.output.empty()) {
		result.error = "llama.cpp CLI returned empty output";
		return result;
	}

	result.success = true;
	result.text = commandResult.output;
	result.finishReason = "stop";
	return result;
}

std::string ofxGgmlLlamaCliTextBackend::composePrompt(
	const ofxGgmlTextRequest & request) {
	if (!request.prompt.empty()) {
		return request.prompt;
	}

	std::ostringstream prompt;
	if (!request.systemPrompt.empty()) {
		prompt << "System: " << request.systemPrompt << "\n";
	}
	for (const auto & message : request.messages) {
		if (message.content.empty()) {
			continue;
		}
		prompt << roleLabel(message.role) << ": " << message.content << "\n";
	}
	return prompt.str();
}

ofxGgmlTextCommand ofxGgmlLlamaCliTextBackend::buildCommand(
	const ofxGgmlTextRequest & request,
	const std::string & prompt) {
	ofxGgmlTextCommand command;
	command.executablePath = request.settings.executablePath;
	command.inputText = prompt;

	auto & args = command.arguments;
	args.reserve(36 + request.settings.stopSequences.size() * 2);
	args.push_back("-m");
	args.push_back(request.modelPath);
	args.push_back("-p");
	args.push_back(prompt);
	appendOption(args, "-n", request.settings.maxTokens);
	appendOption(args, "--temp", request.settings.temperature);
	appendOption(args, "--top-p", request.settings.topP);
	appendOption(args, "--top-k", request.settings.topK);
	appendOption(args, "--repeat-penalty", request.settings.repeatPenalty);
	appendOption(args, "-c", request.settings.contextSize);
	appendOption(args, "-b", request.settings.batchSize);
	if (request.settings.gpuLayers >= 0) {
		appendOption(args, "-ngl", request.settings.gpuLayers);
	}
	if (request.settings.threads > 0) {
		appendOption(args, "-t", request.settings.threads);
	}
	if (request.settings.seed >= 0) {
		appendOption(args, "--seed", request.settings.seed);
	}
	for (const auto & stop : request.settings.stopSequences) {
		if (!stop.empty()) {
			args.push_back("--reverse-prompt");
			args.push_back(stop);
		}
	}
	args.push_back("--no-display-prompt");
	return command;
}

ofxGgmlTextCommandResult ofxGgmlLlamaCliTextBackend::runCommand(
	const ofxGgmlTextCommand & command,
	const ofxGgmlTextChunkCallback & onChunk) {
	if (command.executablePath.empty()) {
		ofxGgmlTextCommandResult result;
		result.error = "executable path is empty";
		return result;
	}
#if defined(_WIN32)
	return runCommandWindows(command, onChunk);
#else
	return runCommandPosix(command, onChunk);
#endif
}
