#pragma once

#include "ofMain.h"
#include "TextServerManager.h"
#include "utils/BackendHelpers.h"
#include "utils/ProcessHelpers.h"
#include "core/ofxGgmlWindowsUtf8.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#endif

class AceStepServerManager {
public:
	AceStepServerManager();
	~AceStepServerManager();

	void startLocalServer(
		const std::string & configuredUrl,
		const std::string & modelsDir,
		const std::string & adaptersDir = "");

	void stopLocalServer(bool logResult = true);

	bool isRunning() const;
	bool isManagedByApp() const { return managedByApp_; }
	ServerStatusState getStatus() const { return status_; }
	std::string getStatusMessage() const { return statusMessage_; }

	std::string findLocalExecutable(bool refresh = false);
	std::string findLocalModelsDirectory(bool refresh = false);
	std::string findLocalAdaptersDirectory(bool refresh = false);

private:
	bool managedByApp_ = false;
#ifdef _WIN32
	HANDLE processHandle_ = nullptr;
	DWORD processId_ = 0;
#else
	pid_t processId_ = 0;
#endif

	ServerStatusState status_;
	std::string statusMessage_;

	std::string cachedExecutable_;
	bool executableCached_ = false;
	std::string cachedModelsDirectory_;
	bool modelsDirectoryCached_ = false;
	std::string cachedAdaptersDirectory_;
	bool adaptersDirectoryCached_ = false;
};

namespace ofxGgmlAceStepServerManagerInternal {

inline bool directoryContainsFilesWithExtensions(
	const std::filesystem::path & directory,
	const std::vector<std::string> & extensions) {
	std::error_code ec;
	if (!std::filesystem::exists(directory, ec) || ec) {
		return false;
	}
	for (const auto & entry : std::filesystem::recursive_directory_iterator(directory, ec)) {
		if (ec) {
			break;
		}
		if (!entry.is_regular_file()) {
			continue;
		}
		std::string extension = entry.path().extension().string();
		std::transform(
			extension.begin(),
			extension.end(),
			extension.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		for (const auto & expectedExtension : extensions) {
			if (extension == expectedExtension) {
				return true;
			}
		}
	}
	return false;
}

inline bool directoryContainsAceStepModels(const std::filesystem::path & directory) {
	return directoryContainsFilesWithExtensions(directory, {".gguf"});
}

inline bool directoryContainsAceStepAdapters(const std::filesystem::path & directory) {
	return directoryContainsFilesWithExtensions(directory, {".safetensors"});
}

inline bool directoryExists(const std::filesystem::path & directory) {
	std::error_code ec;
	return std::filesystem::exists(directory, ec) &&
		std::filesystem::is_directory(directory, ec) &&
		!ec;
}

} // namespace ofxGgmlAceStepServerManagerInternal

inline AceStepServerManager::AceStepServerManager()
	: status_(ServerStatusState::Unknown) {
}

inline AceStepServerManager::~AceStepServerManager() {
	if (isRunning()) {
		stopLocalServer(false);
	}
}

inline void AceStepServerManager::startLocalServer(
	const std::string & configuredUrl,
	const std::string & modelsDir,
	const std::string & adaptersDir) {

	if (isRunning()) {
		return;
	}

	const std::string serverExe = findLocalExecutable(true);
	if (serverExe.empty()) {
		status_ = ServerStatusState::Unreachable;
		statusMessage_ = "Local AceStep server executable not found.";
		return;
	}

	if (modelsDir.empty() ||
		!ofxGgmlAceStepServerManagerInternal::directoryContainsAceStepModels(modelsDir)) {
		status_ = ServerStatusState::Unreachable;
		statusMessage_ = "AceStep models directory is missing or does not contain GGUF model files.";
		return;
	}

	const auto [host, port] = parseAceStepServerHostPort(configuredUrl);

#ifdef _WIN32
	std::vector<std::string> args = {
		serverExe,
		"--host", host,
		"--port", ofToString(port),
		"--models", modelsDir
	};
	if (!adaptersDir.empty() &&
		ofxGgmlAceStepServerManagerInternal::directoryExists(adaptersDir)) {
		args.emplace_back("--adapters");
		args.emplace_back(adaptersDir);
	}

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
	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	const std::wstring workingDir = ofxGgmlWideFromUtf8(
		std::filesystem::path(serverExe).parent_path().string());
	const BOOL ok = CreateProcessW(
		nullptr,
		mutableCmd.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
		nullptr,
		workingDir.empty() ? nullptr : workingDir.c_str(),
		&si,
		&pi);
	if (!ok) {
		status_ = ServerStatusState::Unreachable;
		statusMessage_ = "Failed to launch local AceStep server.";
		return;
	}
	if (processHandle_) {
		CloseHandle(processHandle_);
	}
	processHandle_ = pi.hProcess;
	processId_ = pi.dwProcessId;
	CloseHandle(pi.hThread);
#else
	std::vector<std::string> args = {
		serverExe,
		"--host", host,
		"--port", ofToString(port),
		"--models", modelsDir
	};
	if (!adaptersDir.empty() &&
		ofxGgmlAceStepServerManagerInternal::directoryExists(adaptersDir)) {
		args.emplace_back("--adapters");
		args.emplace_back(adaptersDir);
	}
	pid_t pid = fork();
	if (pid == 0) {
		chdir(std::filesystem::path(serverExe).parent_path().string().c_str());
		std::vector<char *> argv;
		argv.reserve(args.size() + 1);
		for (auto & arg : args) {
			argv.push_back(arg.data());
		}
		argv.push_back(nullptr);
		execv(serverExe.c_str(), argv.data());
		_exit(127);
	}
	if (pid <= 0) {
		status_ = ServerStatusState::Unreachable;
		statusMessage_ = "Failed to launch local AceStep server.";
		return;
	}
	processId_ = pid;
#endif

	managedByApp_ = true;
	status_ = ServerStatusState::Unknown;
	statusMessage_ = "Local AceStep server started. The app will probe it automatically.";
}

inline void AceStepServerManager::stopLocalServer(bool logResult) {
	(void)logResult;

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
	statusMessage_ = "Local AceStep server stopped.";
}

inline bool AceStepServerManager::isRunning() const {
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

inline std::string AceStepServerManager::findLocalExecutable(bool refresh) {
	if (executableCached_ && !refresh) {
		return cachedExecutable_;
	}
	const std::filesystem::path exeDir(ofFilePath::getCurrentExeDir());
#ifdef _WIN32
	const std::vector<std::filesystem::path> candidates = {
		exeDir / ".." / ".." / "libs" / "acestep" / "bin" / "ace-server.exe",
		exeDir / "ace-server.exe"
	};
#else
	const std::vector<std::filesystem::path> candidates = {
		exeDir / ".." / ".." / "libs" / "acestep" / "bin" / "ace-server",
		exeDir / "ace-server"
	};
#endif
	cachedExecutable_ = probeServerExecutable(candidates);
	executableCached_ = true;
	return cachedExecutable_;
}

inline std::string AceStepServerManager::findLocalModelsDirectory(bool refresh) {
	if (modelsDirectoryCached_ && !refresh && !cachedModelsDirectory_.empty()) {
		return cachedModelsDirectory_;
	}

	std::vector<std::filesystem::path> candidates;
	const std::string serverExe = findLocalExecutable(refresh);
	if (!serverExe.empty()) {
		const std::filesystem::path binDir = std::filesystem::path(serverExe).parent_path();
		candidates.push_back(binDir / "models");
		candidates.push_back(binDir.parent_path() / "source" / "models");
		candidates.push_back(binDir.parent_path() / "models");
	}

	const std::filesystem::path exeDir(ofFilePath::getCurrentExeDir());
	candidates.push_back(exeDir / ".." / ".." / "models" / "acestep");
	candidates.push_back(exeDir / ".." / ".." / "data" / "models" / "acestep");

	cachedModelsDirectory_.clear();
	for (const auto & candidate : candidates) {
		if (ofxGgmlAceStepServerManagerInternal::directoryContainsAceStepModels(candidate)) {
			std::error_code ec;
			const std::filesystem::path normalized =
				std::filesystem::weakly_canonical(candidate, ec);
			cachedModelsDirectory_ = (ec ? candidate : normalized).string();
			break;
		}
	}
	modelsDirectoryCached_ = true;
	return cachedModelsDirectory_;
}

inline std::string AceStepServerManager::findLocalAdaptersDirectory(bool refresh) {
	if (adaptersDirectoryCached_ && !refresh && !cachedAdaptersDirectory_.empty()) {
		return cachedAdaptersDirectory_;
	}

	std::vector<std::filesystem::path> candidates;
	const std::string serverExe = findLocalExecutable(refresh);
	if (!serverExe.empty()) {
		const std::filesystem::path binDir = std::filesystem::path(serverExe).parent_path();
		candidates.push_back(binDir / "adapters");
		candidates.push_back(binDir.parent_path() / "source" / "adapters");
		candidates.push_back(binDir.parent_path() / "adapters");
	}

	cachedAdaptersDirectory_.clear();
	for (const auto & candidate : candidates) {
		if (ofxGgmlAceStepServerManagerInternal::directoryContainsAceStepAdapters(candidate)) {
			std::error_code ec;
			const std::filesystem::path normalized =
				std::filesystem::weakly_canonical(candidate, ec);
			cachedAdaptersDirectory_ = (ec ? candidate : normalized).string();
			break;
		}
	}
	adaptersDirectoryCached_ = true;
	return cachedAdaptersDirectory_;
}
