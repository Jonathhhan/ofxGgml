#include "SpeechServerManager.h"
#include "TextServerManager.h"  // For ServerStatusState
#include "utils/BackendHelpers.h"
#include "utils/PathHelpers.h"
#include "core/ofxGgmlWindowsUtf8.h"

#include <filesystem>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

SpeechServerManager::SpeechServerManager()
	: status_(ServerStatusState::Unknown) {
}

SpeechServerManager::~SpeechServerManager() {
	if (isRunning()) {
		stopLocalServer(false);
	}
}

void SpeechServerManager::startLocalServer(
	const std::string & configuredUrl,
	const std::string & modelPath) {

	if (isRunning()) {
		return;
	}

	const std::string serverExe = findLocalServerExecutable(true);
	if (serverExe.empty()) {
		status_ = ServerStatusState::Unreachable;
		statusMessage_ = "Local whisper-server executable not found.";
		return;
	}

	if (modelPath.empty() || !std::filesystem::exists(modelPath)) {
		status_ = ServerStatusState::Unreachable;
		statusMessage_ = "Local Whisper model not found for whisper-server.";
		return;
	}

	const auto [host, port] = parseSpeechServerHostPort(configuredUrl);

#ifdef _WIN32
	std::vector<std::string> args = {
		serverExe,
		"--host", host,
		"--port", ofToString(port),
		"-m", modelPath
	};
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
			if (c == '"') cmdLine += "\\\"";
			else cmdLine += c;
		}
		cmdLine += "\"";
	}

	std::wstring wideCmd = ofxGgmlWideFromUtf8(cmdLine);
	std::vector<wchar_t> mutableCmd(wideCmd.begin(), wideCmd.end());
	mutableCmd.push_back(L'\0');
	STARTUPINFOW si {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi {};
	const std::wstring workingDir = ofxGgmlWideFromUtf8(
		std::filesystem::path(serverExe).parent_path().string());
	const BOOL ok = CreateProcessW(
		nullptr,
		mutableCmd.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_NO_WINDOW,
		nullptr,
		workingDir.c_str(),
		&si,
		&pi);
	if (!ok) {
		status_ = ServerStatusState::Unreachable;
		statusMessage_ = "Failed to launch local whisper-server.";
		return;
	}
	if (processHandle_) {
		CloseHandle(processHandle_);
	}
	processHandle_ = pi.hProcess;
	processId_ = pi.dwProcessId;
	CloseHandle(pi.hThread);
#else
	pid_t pid = fork();
	if (pid == 0) {
		chdir(std::filesystem::path(serverExe).parent_path().string().c_str());
		execl(
			serverExe.c_str(),
			serverExe.c_str(),
			"--host", host.c_str(),
			"--port", ofToString(port).c_str(),
			"-m", modelPath.c_str(),
			nullptr);
		_exit(127);
	}
	if (pid <= 0) {
		return;
	}
	processId_ = pid;
#endif

	managedByApp_ = true;
	status_ = ServerStatusState::Unknown;
	statusMessage_ = "Local whisper-server started.";
}

void SpeechServerManager::stopLocalServer(bool logResult) {
	(void)logResult; // Suppress unused parameter warning

	if (!isRunning()) {
		managedByApp_ = false;
		status_ = ServerStatusState::Unknown;
		return;
	}

#ifdef _WIN32
	TerminateProcess(processHandle_, 0);
	CloseHandle(processHandle_);
	processHandle_ = nullptr;
	processId_ = 0;
#else
	kill(processId_, SIGTERM);
	processId_ = 0;
#endif
	managedByApp_ = false;
	status_ = ServerStatusState::Unknown;
	statusMessage_ = "Local whisper-server stopped.";
}

bool SpeechServerManager::isRunning() const {
	if (!managedByApp_) {
		return false;
	}
#ifdef _WIN32
	if (!processHandle_) {
		return false;
	}
	const DWORD waitCode = WaitForSingleObject(processHandle_, 0);
	return (waitCode == WAIT_TIMEOUT);
#else
	if (processId_ <= 0) {
		return false;
	}
	return (kill(processId_, 0) == 0);
#endif
}

std::string SpeechServerManager::findLocalServerExecutable(bool refresh) {
	if (serverExecutableCached_ && !refresh) {
		return cachedServerExecutable_;
	}
	std::string resolved =
		ofxGgmlSpeechInference::resolveWhisperServerExecutable();
	if (resolved == "whisper-server") {
		resolved.clear();
	}
	cachedServerExecutable_ = std::move(resolved);
	serverExecutableCached_ = true;
	return cachedServerExecutable_;
}

std::string SpeechServerManager::findLocalCliExecutable(bool refresh) {
	if (cliExecutableCached_ && !refresh) {
		return cachedCliExecutable_;
	}
	std::string resolved = ofxGgmlSpeechInference::resolveWhisperCliExecutable();
	if (resolved == "whisper-cli") {
		resolved.clear();
	}
	cachedCliExecutable_ = std::move(resolved);
	cliExecutableCached_ = true;
	return cachedCliExecutable_;
}
