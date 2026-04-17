#pragma once

#include "ofMain.h"
#include "ofxGgmlSpeechInference.h"

#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <signal.h>
#endif

enum class ServerStatusState;  // Forward declaration

class SpeechServerManager {
public:
	SpeechServerManager();
	~SpeechServerManager();

	// Server lifecycle
	void startLocalServer(
		const std::string & configuredUrl,
		const std::string & modelPath);

	void stopLocalServer(bool logResult = true);

	// Query state
	bool isRunning() const;
	bool isManagedByApp() const { return managedByApp_; }
	ServerStatusState getStatus() const { return status_; }
	std::string getStatusMessage() const { return statusMessage_; }

	// Executable discovery
	std::string findLocalServerExecutable(bool refresh = false);
	std::string findLocalCliExecutable(bool refresh = false);

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
	ServerStatusState status_;
	std::string statusMessage_;

	// Executable caching
	std::string cachedServerExecutable_;
	bool serverExecutableCached_ = false;
	std::string cachedCliExecutable_;
	bool cliExecutableCached_ = false;
};
