#pragma once

#include "ofMain.h"
#include "ofxGgmlInference.h"

#include <string>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <signal.h>
#endif

enum class ServerStatusState {
	Unknown = 0,
	Reachable,
	Unreachable
};

class TextServerManager {
public:
	TextServerManager();
	~TextServerManager();

	// Server lifecycle
	void startLocalServer(
		const std::string & configuredUrl,
		const std::string & modelPath,
		int gpuLayers,
		int contextSize,
		bool allowMmproj = false);

	void stopLocalServer(bool logResult = true);

	// Status checking
	ServerStatusState checkStatus(const std::string & configuredUrl, bool logResult = true);
	bool ensureReady(
		const std::string & configuredUrl,
		const std::string & modelPath,
		int gpuLayers,
		int contextSize,
		bool logResult = false,
		bool allowLaunch = true,
		bool allowMmproj = false);

	// Deferred warmup
	void scheduleDeferredWarmup(const std::string & url, float timeoutSeconds = 10.0f);
	void updateDeferredWarmup(const std::string & currentUrl);

	// Query state
	bool isRunning() const;
	bool isManagedByApp() const { return managedByApp_; }
	ServerStatusState getStatus() const { return status_; }
	std::string getStatusMessage() const { return statusMessage_; }
	std::string getCapabilityHint() const { return capabilityHint_; }

	// Executable discovery
	std::string findLocalExecutable(bool refresh = false);

private:
	// Process state
	bool managedByApp_ = false;
#ifdef _WIN32
	HANDLE processHandle_ = nullptr;
	DWORD processId_ = 0;
#else
	pid_t processId_ = 0;
#endif

	// Status state
	ServerStatusState status_ = ServerStatusState::Unknown;
	std::string statusMessage_;
	std::string capabilityHint_;

	// Executable caching
	std::string cachedExecutable_;
	bool executableCached_ = false;

	// Deferred warmup state
	bool deferredWarmupPending_ = false;
	std::string deferredWarmupUrl_;
	float deferredWarmupDeadline_ = 0.0f;
	float deferredWarmupNextProbeTime_ = 0.0f;
};
