#include "TextServerManager.h"
#include "utils/BackendHelpers.h"
#include "utils/VisionHelpers.h"
#include "utils/ProcessHelpers.h"
#include "utils/ImGuiHelpers.h"
#include "core/ofxGgmlWindowsUtf8.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace {

std::string lowerAscii(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

bool isJanV1ModelPath(const std::string & modelPath) {
	const std::string filename =
		lowerAscii(std::filesystem::path(modelPath).filename().string());
	return filename.find("jan-v1") != std::string::npos ||
		filename.find("jan_v1") != std::string::npos;
}

void appendJanV1ServerFlagsIfNeeded(
	const std::string & modelPath,
	std::vector<std::string> & args) {
	if (!isJanV1ModelPath(modelPath)) {
		return;
	}
	args.emplace_back("--jinja");
	args.emplace_back("--no-context-shift");
}

} // namespace

TextServerManager::TextServerManager() {
}

TextServerManager::~TextServerManager() {
	if (isRunning()) {
		stopLocalServer(false);
	}
}

void TextServerManager::startLocalServer(
	const std::string & configuredUrl,
	const std::string & modelPath,
	int gpuLayers,
	int contextSize,
	bool allowMmproj) {

	if (isRunning()) {
		return;
	}
	lastMmprojPath_.clear();

	const std::string serverExe = findLocalExecutable(true);
	if (serverExe.empty()) {
		status_ = ServerStatusState::Unreachable;
		statusMessage_ = "Local llama-server executable not found.";
		capabilityHint_.clear();
		return;
	}

	if (modelPath.empty() || !std::filesystem::exists(modelPath)) {
		status_ = ServerStatusState::Unreachable;
		statusMessage_ = "Selected GGUF model is missing; local server was not started.";
		capabilityHint_.clear();
		return;
	}

	const auto [host, port] = parseServerHostPort(configuredUrl);
	const int gpuLayerCount = std::max(0, gpuLayers);
	const std::string mmprojPath = allowMmproj
		? findMatchingMmprojPath(modelPath)
		: std::string{};
	lastMmprojPath_ = mmprojPath;

#ifdef _WIN32
	std::vector<std::string> args = {
		serverExe,
		"-m", modelPath,
		"--host", host,
		"--port", ofToString(port),
		"-ngl", ofToString(gpuLayerCount)
	};
	if (!mmprojPath.empty()) {
		args.emplace_back("--mmproj");
		args.emplace_back(mmprojPath);
	}
	if (contextSize > 0) {
		args.emplace_back("-c");
		args.emplace_back(ofToString(contextSize));
	}
	appendJanV1ServerFlagsIfNeeded(modelPath, args);

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
			if (c == '"') cmdLine += '\\';
			cmdLine += c;
		}
		cmdLine += "\"";
	}

	std::wstring wideCmd = ofxGgmlWideFromUtf8(cmdLine);
	std::wstring wideCwd = ofxGgmlWideFromUtf8(std::filesystem::path(serverExe).parent_path().string());
	if (wideCmd.empty()) {
		return;
	}

	std::vector<wchar_t> mutableCmd(wideCmd.begin(), wideCmd.end());
	mutableCmd.push_back(L'\0');

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	const BOOL ok = CreateProcessW(
		nullptr,
		mutableCmd.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
		nullptr,
		wideCwd.empty() ? nullptr : wideCwd.c_str(),
		&si,
		&pi);
	if (!ok) {
		status_ = ServerStatusState::Unreachable;
		statusMessage_ = "Failed to launch local llama-server.";
		capabilityHint_.clear();
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
		"-m", modelPath,
		"--host", host,
		"--port", ofToString(port),
		"-ngl", ofToString(gpuLayerCount)
	};
	if (!mmprojPath.empty()) {
		args.emplace_back("--mmproj");
		args.emplace_back(mmprojPath);
	}
	if (contextSize > 0) {
		args.emplace_back("-c");
		args.emplace_back(ofToString(contextSize));
	}
	appendJanV1ServerFlagsIfNeeded(modelPath, args);
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
		return;
	}
	processId_ = pid;
#endif

	managedByApp_ = true;
	status_ = ServerStatusState::Unknown;
	statusMessage_ = "Local llama-server started. The app will probe it automatically.";
	capabilityHint_.clear();
}

void TextServerManager::stopLocalServer(bool logResult) {
	(void)logResult; // Suppress unused parameter warning

	if (!isRunning()) {
		managedByApp_ = false;
		status_ = ServerStatusState::Unknown;
		lastMmprojPath_.clear();
		capabilityHint_.clear();
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
	statusMessage_ = "Local llama-server stopped.";
	lastMmprojPath_.clear();
	capabilityHint_.clear();
}

ServerStatusState TextServerManager::checkStatus(const std::string & configuredUrl, bool logResult) {
	(void)logResult; // Suppress unused parameter warning

	const ofxGgmlServerProbeResult probe =
		ofxGgmlInference::probeServer(configuredUrl, true);
	if (probe.reachable) {
		status_ = ServerStatusState::Reachable;
		statusMessage_ = "Server reachable at " + probe.baseUrl + ".";
		if (!probe.activeModel.empty()) {
			statusMessage_ += " Model: " + probe.activeModel + ".";
		}
		capabilityHint_ = probe.capabilitySummary.empty()
			? std::string()
			: "Server profile: " + probe.capabilitySummary + ".";
		return status_;
	}

	status_ = ServerStatusState::Unreachable;
	statusMessage_ = "Server not reachable at " + probe.baseUrl + ".";
	capabilityHint_.clear();
	if (!probe.error.empty()) {
		statusMessage_ += " " + probe.error;
	}
	return status_;
}

bool TextServerManager::ensureReady(
	const std::string & configuredUrl,
	const std::string & modelPath,
	int gpuLayers,
	int contextSize,
	bool logResult,
	bool allowLaunch,
	bool allowMmproj) {

	(void)logResult; // Suppress unused parameter warning

	checkStatus(configuredUrl, false);
	if (status_ == ServerStatusState::Reachable) {
		return true;
	}

	if (!allowLaunch) {
		return false;
	}

	if (shouldManageLocalTextServer(configuredUrl) && !isRunning()) {
		startLocalServer(configuredUrl, modelPath, gpuLayers, contextSize, allowMmproj);
		// Give server a moment to start
		ofSleepMillis(500);
		checkStatus(configuredUrl, false);
		return status_ == ServerStatusState::Reachable;
	}

	return false;
}

TextServerEnsureResult TextServerManager::ensureReadyForModel(
	const std::string & configuredUrl,
	const std::string & modelPath,
	int gpuLayers,
	int contextSize,
	bool allowLaunch,
	bool allowMmproj,
	bool allowExternalTermination) {

	TextServerEnsureResult result;
	const std::string effectiveUrl = serverBaseUrlFromConfiguredUrl(configuredUrl);
	result.requestedModel = ofFilePath::getFileName(modelPath);

	// Probe current state.
	checkStatus(effectiveUrl, false);
	result.status = status_;
	result.statusMessage = statusMessage_;
	result.capabilityHint = capabilityHint_;
	result.managedByApp = managedByApp_;
	result.reachable = (status_ == ServerStatusState::Reachable);

	// If reachable, ensure the active model matches when we manage the server.
	bool needsRestartForModelChange = false;
	if (result.reachable &&
		allowLaunch &&
		shouldManageLocalTextServer(effectiveUrl) &&
		!trim(modelPath).empty()) {

		const ofxGgmlServerProbeResult probe = ofxGgmlInference::probeServer(effectiveUrl, true);
		result.previousModel = probe.activeModel;
		if (probe.reachable &&
			!probe.activeModel.empty() &&
			!result.requestedModel.empty() &&
			probe.activeModel != result.requestedModel) {

			needsRestartForModelChange = true;
			result.restartedForModelChange = true;
			result.reachable = false;

			if (managedByApp_) {
				stopLocalServer(false);
			} else if (allowExternalTermination) {
				const std::string serverExe = findLocalExecutable(true);
				const bool terminated = terminateAddonLlamaServerProcesses(serverExe);
				result.terminatedExternalProcess = terminated;
				if (!terminated) {
					status_ = ServerStatusState::Unreachable;
					statusMessage_ =
						"A local llama-server is already running with model " +
						probe.activeModel +
						". Stop it manually or point Vision at a different server URL.";
					capabilityHint_.clear();
					result.status = status_;
					result.statusMessage = statusMessage_;
					result.capabilityHint = capabilityHint_;
					result.managedByApp = managedByApp_;
					return result;
				}
			} else {
				status_ = ServerStatusState::Unreachable;
				statusMessage_ =
					"A local llama-server is already running with model " +
					probe.activeModel +
					". Stop it manually or point Vision at a different server URL.";
				capabilityHint_.clear();
				result.status = status_;
				result.statusMessage = statusMessage_;
				result.capabilityHint = capabilityHint_;
				result.managedByApp = managedByApp_;
				return result;
			}

			status_ = ServerStatusState::Unknown;
			statusMessage_ =
				"Requested model differs from the running local server; restarting.";
			capabilityHint_.clear();
			result.status = status_;
			result.statusMessage = statusMessage_;
			result.capabilityHint = capabilityHint_;
			result.managedByApp = managedByApp_;
		}
	}

	if (result.reachable && !needsRestartForModelChange) {
		return result;
	}

	if (!allowLaunch) {
		return result;
	}

	const std::string serverExe = findLocalExecutable(true);
	if (serverExe.empty()) {
		status_ = ServerStatusState::Unreachable;
		statusMessage_ = "Local llama-server executable not found.";
		capabilityHint_.clear();
		result.status = status_;
		result.statusMessage = statusMessage_;
		result.capabilityHint = capabilityHint_;
		result.managedByApp = managedByApp_;
		result.missingExecutable = true;
		return result;
	}

	std::error_code modelEc;
	if (modelPath.empty() ||
		!std::filesystem::exists(modelPath, modelEc) ||
		modelEc) {
		status_ = ServerStatusState::Unreachable;
		statusMessage_ = "Selected GGUF model is missing; local server was not started.";
		capabilityHint_.clear();
		result.status = status_;
		result.statusMessage = statusMessage_;
		result.capabilityHint = capabilityHint_;
		result.managedByApp = managedByApp_;
		result.missingModel = true;
		return result;
	}

	if (!isRunning()) {
		startLocalServer(
			effectiveUrl,
			modelPath,
			gpuLayers,
			contextSize,
			allowMmproj);
		result.started = isRunning();
		result.managedByApp = managedByApp_;
		result.mmprojPath = lastMmprojPath_;
	} else {
		result.mmprojPath = lastMmprojPath_;
	}

	for (int attempt = 0; attempt < 20; ++attempt) {
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
		checkStatus(effectiveUrl, false);
		result.status = status_;
		result.statusMessage = statusMessage_;
		result.capabilityHint = capabilityHint_;
		result.managedByApp = managedByApp_;
		if (status_ == ServerStatusState::Reachable) {
			result.reachable = true;
			return result;
		}
	}

	return result;
}

void TextServerManager::scheduleDeferredWarmup(const std::string & url, float timeoutSeconds) {
	deferredWarmupPending_ = true;
	deferredWarmupUrl_ = url;
	const float now = ofGetElapsedTimef();
	deferredWarmupDeadline_ = now + timeoutSeconds;
	deferredWarmupNextProbeTime_ = now + 0.15f;
}

void TextServerManager::updateDeferredWarmup(const std::string & currentUrl) {
	if (!deferredWarmupPending_) {
		return;
	}

	if (currentUrl != deferredWarmupUrl_) {
		deferredWarmupPending_ = false;
		return;
	}

	const float now = ofGetElapsedTimef();
	if (now < deferredWarmupNextProbeTime_) {
		return;
	}
	deferredWarmupNextProbeTime_ = now + 0.15f;

	checkStatus(currentUrl, false);
	if (status_ == ServerStatusState::Reachable) {
		deferredWarmupPending_ = false;
		return;
	}

	if (now >= deferredWarmupDeadline_) {
		deferredWarmupPending_ = false;
	}
}

bool TextServerManager::isRunning() const {
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

std::string TextServerManager::findLocalExecutable(bool refresh) {
	if (executableCached_ && !refresh) {
		return cachedExecutable_;
	}
	std::vector<std::filesystem::path> candidates;
	const std::filesystem::path exeDir(ofFilePath::getCurrentExeDir());
#ifdef _WIN32
	candidates.push_back(exeDir / ".." / ".." / "libs" / "llama" / "bin" / "llama-server.exe");
	candidates.push_back(exeDir / ".." / ".." / "build" / "llama.cpp-build" / "bin" / "Release" / "llama-server.exe");
	candidates.push_back(exeDir / "llama-server.exe");
#else
	candidates.push_back(exeDir / ".." / ".." / "libs" / "llama" / "bin" / "llama-server");
	candidates.push_back(exeDir / ".." / ".." / "build" / "llama.cpp-build" / "bin" / "llama-server");
	candidates.push_back(exeDir / ".." / ".." / "build" / "llama.cpp-build" / "bin" / "Release" / "llama-server");
	candidates.push_back(exeDir / "llama-server");
#endif
	cachedExecutable_ = probeServerExecutable(candidates);
	executableCached_ = true;
	return cachedExecutable_;
}
