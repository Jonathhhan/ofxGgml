#include "ofApp.h"

#include "ImHelpers.h"
#include "utils/ImGuiHelpers.h"
#include "utils/TextPromptHelpers.h"
#include "utils/ModelHelpers.h"
#include "utils/SpeechHelpers.h"
#include "utils/AudioHelpers.h"
#include "utils/ServerHelpers.h"
#include "utils/VisionHelpers.h"
#include "utils/ProcessHelpers.h"
#include "utils/ConsoleHelpers.h"
#include "utils/PathHelpers.h"
#include "utils/BackendHelpers.h"
#include "utils/ScriptCommandHelpers.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>

namespace {
constexpr size_t kMaxLogMessages = 500;
const char * const kDefaultTextServerUrl = "http://127.0.0.1:8080";
const char * const kDefaultAceStepServerUrl = "http://127.0.0.1:8085";
constexpr long kAceStepQuickHealthTimeoutMs = 900L;
constexpr int kAceStepStartupProbeAttempts = 8;
constexpr int kAceStepStartupProbeSleepMs = 150;
}

void ofApp::applyLogLevel(ofLogLevel level) {
	logLevel = level;
	ofSetLogLevel(level);
}

bool ofApp::shouldLog(ofLogLevel level) const {
	return logLevel != OF_LOG_SILENT && level >= logLevel;
}

void ofApp::logWithLevel(ofLogLevel level, const std::string & message) {
	if (!shouldLog(level) || message.empty()) return;
	const std::string normalizedMessage = trimLogMessage(message);
	if (normalizedMessage.empty()) return;
	ofLog(level, normalizedMessage);
	std::lock_guard<std::mutex> lock(logMutex);
	std::string entry = "[" + std::string(logLevelLabel(level)) + "] " + normalizedMessage;
	logMessages.push_back(entry);
	if (logMessages.size() > kMaxLogMessages) {
		logMessages.pop_front();
	}
}

void ofApp::announceTextBackendChange() {
	const bool useServerBackend =
		(textInferenceBackend == TextInferenceBackend::LlamaServer);
	const std::string modePrefix = aiModeSupportsTextBackend(activeMode)
		? std::string("Text backend for ") + modeLabels[static_cast<int>(activeMode)] + " switched to "
		: std::string("Text backend switched to ");
	std::string message = useServerBackend
		? modePrefix + "llama-server (persistent)."
		: modePrefix + "CLI fallback (optional local llama-completion).";
	if (useServerBackend) {
		const std::string serverUrl = effectiveTextServerUrl(textServerUrl);
		message += " Server: " + serverUrl + ".";
	} else {
		message += " This optional fallback uses the selected local GGUF model when the server path is not preferred.";
	}

	logWithLevel(OF_LOG_NOTICE, message);

	const Message notice{"system", message, ofGetElapsedTimef()};
	if (activeMode == AiMode::Chat) {
		chatMessages.push_back(notice);
	} else if (activeGenerationMode == AiMode::Script || activeMode == AiMode::Script) {
		scriptMessages.push_back(notice);
	}

	if (useServerBackend) {
		applyServerFriendlyDefaultsForMode(activeMode);
		ensureTextServerReady(
			false,
			shouldManageLocalTextServer(effectiveTextServerUrl(textServerUrl)));
	} else {
		textServerStatus = ServerStatusState::Unknown;
		textServerStatusMessage.clear();
		textServerCapabilityHint.clear();
	}
}

TextInferenceBackend ofApp::preferredTextBackendForMode(AiMode mode) const {
	if (!aiModeSupportsTextBackend(mode)) {
		return textInferenceBackend;
	}
	const int index = modeTextBackendIndices[static_cast<size_t>(mode)];
	return clampTextInferenceBackend(index);
}

void ofApp::rememberTextBackendForMode(AiMode mode, TextInferenceBackend backend) {
	if (!aiModeSupportsTextBackend(mode)) {
		return;
	}
	modeTextBackendIndices[static_cast<size_t>(mode)] = static_cast<int>(backend);
}

void ofApp::applyServerFriendlyDefaultsForMode(AiMode mode) {
	int tunedMaxTokens = 512;
	switch (mode) {
	case AiMode::Chat: tunedMaxTokens = 384; break;
	case AiMode::Script: tunedMaxTokens = 768; break;
	case AiMode::Summarize: tunedMaxTokens = 512; break;
	case AiMode::Write: tunedMaxTokens = 640; break;
	case AiMode::Translate: tunedMaxTokens = 384; break;
	case AiMode::Custom: tunedMaxTokens = 512; break;
	case AiMode::VideoEssay: tunedMaxTokens = 896; break;
	case AiMode::Vision:
	case AiMode::Speech:
	case AiMode::Diffusion:
	case AiMode::Clip:
		tunedMaxTokens = std::clamp(maxTokens, 256, 768);
		break;
	}
	maxTokens = tunedMaxTokens;
	modeMaxTokens[static_cast<size_t>(mode)] = maxTokens;
	temperature = (mode == AiMode::Chat || mode == AiMode::Write) ? 0.6f : 0.25f;
	topP = 0.9f;
	topK = 50;
	minP = 0.05f;
	repeatPenalty = 1.03f;
	contextSize = std::clamp(contextSize, 2048, 8192);
	stopAtNaturalBoundary = true;
	autoContinueCutoff = (mode == AiMode::Script);
	usePromptCache = false;
}

void ofApp::syncTextBackendForActiveMode(bool announce, bool allowBlockingEnsure) {
	if (!aiModeSupportsTextBackend(activeMode)) {
		return;
	}
	const TextInferenceBackend preferred = preferredTextBackendForMode(activeMode);
	if (textInferenceBackend == preferred) {
		if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
			if (trim(textServerUrl).empty()) {
				copyStringToBuffer(textServerUrl, sizeof(textServerUrl), kDefaultTextServerUrl);
			}
			applyServerFriendlyDefaultsForMode(activeMode);
			const std::string effectiveUrl = effectiveTextServerUrl(textServerUrl);
			if (allowBlockingEnsure) {
				ensureTextServerReady(
					false,
					shouldManageLocalTextServer(effectiveUrl));
			} else {
				textServerStatus = ServerStatusState::Unknown;
				textServerStatusMessage = "Server-backed mode selected. Local server startup is deferred until first request.";
				textServerCapabilityHint.clear();
			}
		}
		return;
	}
	textInferenceBackend = preferred;
	if (textInferenceBackend == TextInferenceBackend::LlamaServer &&
		trim(textServerUrl).empty()) {
		copyStringToBuffer(textServerUrl, sizeof(textServerUrl), kDefaultTextServerUrl);
	}
	if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
		applyServerFriendlyDefaultsForMode(activeMode);
	}
	if (announce) {
		announceTextBackendChange();
	} else if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
		const std::string effectiveUrl = effectiveTextServerUrl(textServerUrl);
		if (allowBlockingEnsure) {
			ensureTextServerReady(
				false,
				shouldManageLocalTextServer(effectiveUrl));
		} else {
			textServerStatus = ServerStatusState::Unknown;
			textServerStatusMessage = "Server-backed mode selected. Local server startup is deferred until first request.";
			textServerCapabilityHint.clear();
		}
	} else {
		textServerStatus = ServerStatusState::Unknown;
		textServerStatusMessage.clear();
		textServerCapabilityHint.clear();
	}
}

void ofApp::scheduleDeferredTextServerWarmup(const std::string & configuredUrl) {
	const std::string effectiveUrl = trim(configuredUrl).empty()
		? std::string(kDefaultTextServerUrl)
		: trim(configuredUrl);
	textServerManager.scheduleDeferredWarmup(effectiveUrl, 3.5f);

	// Update local UI state
	textServerStatus = ServerStatusState::Unknown;
	textServerStatusMessage = "Local llama-server is starting...";
	textServerCapabilityHint.clear();
}

void ofApp::updateDeferredTextServerWarmup() {
	textServerManager.updateDeferredWarmup(effectiveTextServerUrl(textServerUrl));
}

void ofApp::checkTextServerStatus(bool logResult) {
	const std::string configuredUrl = effectiveTextServerUrl(textServerUrl);
	textServerManager.checkStatus(configuredUrl, false);

	TextServerEnsureResult state;
	state.status = textServerManager.getStatus();
	state.statusMessage = textServerManager.getStatusMessage();
	state.capabilityHint = textServerManager.getCapabilityHint();
	state.managedByApp = textServerManager.isManagedByApp();
	updateTextServerStateFromResult(state);

	if (logResult) {
		if (textServerStatus == ServerStatusState::Reachable) {
			logWithLevel(OF_LOG_NOTICE, textServerStatusMessage);
		} else {
			logWithLevel(OF_LOG_WARNING, textServerStatusMessage);
		}
	}
}

bool ofApp::ensureTextServerReady(bool logResult, bool allowLaunch) {
	if (textInferenceBackend != TextInferenceBackend::LlamaServer) {
		return true;
	}
	return ensureLlamaServerReadyForModel(
		effectiveTextServerUrl(textServerUrl),
		getSelectedModelPath(),
		logResult,
		allowLaunch,
		false);
}

bool ofApp::ensureLlamaServerReadyForModel(
	const std::string & configuredUrl,
	const std::string & modelPath,
	bool logResult,
	bool allowLaunch,
	bool allowMmproj) {
	const TextServerEnsureResult result = textServerManager.ensureReadyForModel(
		configuredUrl,
		modelPath,
		gpuLayers,
		contextSize,
		allowLaunch,
		allowMmproj);
	updateTextServerStateFromResult(result);

	if (result.restartedForModelChange && logResult) {
		if (!result.previousModel.empty() && !result.requestedModel.empty()) {
			logWithLevel(
				OF_LOG_NOTICE,
				"Restarting local llama-server to switch models from " +
					result.previousModel + " to " + result.requestedModel + ".");
		} else {
			logWithLevel(OF_LOG_NOTICE, "Restarting local llama-server to switch models.");
		}
	}

	if (result.started && result.managedByApp) {
		const auto [host, port] = parseServerHostPort(configuredUrl);
		const std::string modelName = result.requestedModel.empty()
			? ofFilePath::getFileName(modelPath)
			: result.requestedModel;
		logWithLevel(
			OF_LOG_NOTICE,
			"Started local llama-server on " + host + ":" + ofToString(port) +
			" using model " + modelName + ".");
		if (!result.mmprojPath.empty()) {
			logWithLevel(
				OF_LOG_NOTICE,
				"Using multimodal projector " + ofFilePath::getFileName(result.mmprojPath) + ".");
		}
	}

	if (logResult && !textServerStatusMessage.empty()) {
		const ofLogLevel level =
			textServerStatus == ServerStatusState::Reachable
				? OF_LOG_NOTICE
				: OF_LOG_WARNING;
		logWithLevel(level, textServerStatusMessage);
	}

	return result.reachable;
}

void ofApp::updateTextServerStateFromResult(const TextServerEnsureResult & result) {
	textServerStatus = result.status;
	textServerStatusMessage = result.statusMessage;
	textServerCapabilityHint = result.capabilityHint;
	textServerManagedByApp = result.managedByApp;
}

std::string ofApp::findLocalTextServerExecutable(bool refresh) {
	return textServerManager.findLocalExecutable(refresh);
}

bool ofApp::isManagedTextServerRunning() {
	const bool running = textServerManager.isRunning();
	textServerManagedByApp = textServerManager.isManagedByApp();
	return running;
}

void ofApp::startLocalTextServer() {
	ensureLlamaServerReadyForModel(
		effectiveTextServerUrl(textServerUrl),
		getSelectedModelPath(),
		true,
		true,
		false);
}

void ofApp::stopLocalTextServer(bool logResult) {
	textServerManager.stopLocalServer(logResult);

	TextServerEnsureResult state;
	state.status = textServerManager.getStatus();
	state.statusMessage = textServerManager.getStatusMessage();
	state.capabilityHint = textServerManager.getCapabilityHint();
	state.managedByApp = textServerManager.isManagedByApp();
	updateTextServerStateFromResult(state);

	if (logResult) {
		logWithLevel(OF_LOG_NOTICE, textServerStatusMessage);
	}
}

std::string ofApp::findLocalSpeechServerExecutable(bool refresh) {
	return speechServerManager.findLocalServerExecutable(refresh);
}

std::string ofApp::findLocalSpeechCliExecutable(bool refresh) {
	return speechServerManager.findLocalCliExecutable(refresh);
}

bool ofApp::isManagedSpeechServerRunning() {
	const bool running = speechServerManager.isRunning();
	speechServerManagedByApp = speechServerManager.isManagedByApp();
	return running;
}

void ofApp::startLocalSpeechServer() {
	if (isManagedSpeechServerRunning()) {
		logWithLevel(OF_LOG_NOTICE, "Local whisper-server is already running.");
		return;
	}

	if (speechProfiles.empty()) {
		speechProfiles = ofxGgmlSpeechInference::defaultProfiles();
	}
	selectedSpeechProfileIndex = std::clamp(
		selectedSpeechProfileIndex,
		0,
		std::max(0, static_cast<int>(speechProfiles.size()) - 1));
	const ofxGgmlSpeechModelProfile activeSpeechProfile =
		speechProfiles.empty()
			? ofxGgmlSpeechModelProfile{}
			: speechProfiles[static_cast<size_t>(selectedSpeechProfileIndex)];

	std::string modelPath = trim(speechModelPath);
	if (modelPath.empty()) {
		modelPath = trim(activeSpeechProfile.modelPath);
	}
	if (modelPath.empty() && !trim(activeSpeechProfile.modelFileHint).empty()) {
		const std::string suggestedPath =
			suggestedModelPath(activeSpeechProfile.modelPath, activeSpeechProfile.modelFileHint);
		if (!suggestedPath.empty()) {
			modelPath = suggestedPath;
		}
	}

	const std::string serverExe = findLocalSpeechServerExecutable(true);
	if (serverExe.empty()) {
		logWithLevel(OF_LOG_ERROR, "No local whisper-server executable was found. Build or copy whisper-server.exe into libs/whisper/bin first.");
		speechServerStatus = ServerStatusState::Unreachable;
		speechServerStatusMessage = "Local whisper-server executable not found.";
		return;
	}
	if (modelPath.empty() || !std::filesystem::exists(modelPath)) {
		logWithLevel(OF_LOG_ERROR, "No local Whisper model was found for whisper-server startup.");
		speechServerStatus = ServerStatusState::Unreachable;
		speechServerStatusMessage = "Local Whisper model not found for whisper-server.";
		return;
	}

	// Delegate to manager
	speechServerManager.startLocalServer(
		effectiveSpeechServerUrl(speechServerUrl),
		modelPath);

	// Update local UI state
	speechServerManagedByApp = speechServerManager.isManagedByApp();
	speechServerStatus = speechServerManager.getStatus();
	speechServerStatusMessage = speechServerManager.getStatusMessage();

	// Log the result
	if (speechServerManagedByApp) {
		const auto [host, port] = parseSpeechServerHostPort(effectiveSpeechServerUrl(speechServerUrl));
		logWithLevel(
			OF_LOG_NOTICE,
			"Started local whisper-server on " + host + ":" + ofToString(port) +
			" using model " + ofFilePath::getFileName(modelPath) + ".");
	} else if (!speechServerStatusMessage.empty()) {
		logWithLevel(OF_LOG_ERROR, speechServerStatusMessage);
	}
}

void ofApp::stopLocalSpeechServer(bool logResult) {
	speechServerManager.stopLocalServer(logResult);

	// Update local UI state
	speechServerManagedByApp = speechServerManager.isManagedByApp();
	speechServerStatus = speechServerManager.getStatus();
	speechServerStatusMessage = speechServerManager.getStatusMessage();

	if (logResult) {
		logWithLevel(OF_LOG_NOTICE, speechServerStatusMessage);
	}
}

std::string ofApp::effectiveAceStepServerUrl(const std::string & configuredUrl) const {
	return aceStepServerBaseUrlFromConfiguredUrl(configuredUrl);
}

std::string ofApp::findLocalAceStepServerExecutable(bool refresh) {
	return aceStepServerManager.findLocalExecutable(refresh);
}

std::string ofApp::findLocalAceStepModelsDirectory(bool refresh) {
	return aceStepServerManager.findLocalModelsDirectory(refresh);
}

bool ofApp::isManagedAceStepServerRunning() {
	const bool running = aceStepServerManager.isRunning();
	aceStepServerManagedByApp = aceStepServerManager.isManagedByApp();
	return running;
}

bool ofApp::ensureAceStepServerReady(bool logResult, bool allowLaunch) {
	const std::string configuredUrl = effectiveAceStepServerUrl(aceStepServerUrl);
	const ofxGgmlAceStepHealthResult health =
		aceStepBridge.healthCheck(configuredUrl, kAceStepQuickHealthTimeoutMs);
	if (health.success) {
		aceStepServerStatus = ServerStatusState::Reachable;
		aceStepServerStatusMessage = "AceStep server reachable at " + configuredUrl + ".";
		aceStepServerManagedByApp = aceStepServerManager.isManagedByApp();
		if (logResult) {
			logWithLevel(OF_LOG_NOTICE, aceStepServerStatusMessage);
		}
		return true;
	}

	aceStepServerStatus = ServerStatusState::Unreachable;
	aceStepServerStatusMessage = health.error.empty()
		? "AceStep server not reachable at " + configuredUrl + "."
		: "AceStep server not reachable at " + configuredUrl + ". " + health.error;
	aceStepServerManagedByApp = aceStepServerManager.isManagedByApp();

	if (!allowLaunch || !shouldManageLocalAceStepServer(configuredUrl)) {
		if (logResult) {
			logWithLevel(OF_LOG_WARNING, aceStepServerStatusMessage);
		}
		return false;
	}

	if (!findLocalAceStepServerExecutable().empty() &&
		findLocalAceStepModelsDirectory().empty()) {
		aceStepServerStatus = ServerStatusState::Unreachable;
		aceStepServerStatusMessage =
			"Local AceStep server executable was found, but no GGUF AceStep models directory is available.";
		if (logResult) {
			logWithLevel(OF_LOG_WARNING, aceStepServerStatusMessage);
		}
		return false;
	}

	if (!isManagedAceStepServerRunning()) {
		startLocalAceStepServer();
	}

	for (int attempt = 0; attempt < kAceStepStartupProbeAttempts; ++attempt) {
		std::this_thread::sleep_for(
			std::chrono::milliseconds(kAceStepStartupProbeSleepMs));
		if (!isManagedAceStepServerRunning()) {
			aceStepServerStatus = ServerStatusState::Unreachable;
			const std::string genericStartedMessage =
				"Local AceStep server started. The app will probe it automatically.";
			if (!aceStepServerStatusMessage.empty() &&
				aceStepServerStatusMessage != genericStartedMessage) {
				if (logResult) {
					logWithLevel(OF_LOG_WARNING, aceStepServerStatusMessage);
				}
				return false;
			}
			aceStepServerStatusMessage =
				"Local AceStep server stopped before it became reachable at " +
				configuredUrl + ".";
			if (logResult) {
				logWithLevel(OF_LOG_WARNING, aceStepServerStatusMessage);
			}
			return false;
		}
		const ofxGgmlAceStepHealthResult probe =
			aceStepBridge.healthCheck(
				configuredUrl,
				kAceStepQuickHealthTimeoutMs);
		if (probe.success) {
			aceStepServerStatus = ServerStatusState::Reachable;
			aceStepServerStatusMessage = "AceStep server reachable at " + configuredUrl + ".";
			aceStepServerManagedByApp = aceStepServerManager.isManagedByApp();
			if (logResult) {
				logWithLevel(OF_LOG_NOTICE, aceStepServerStatusMessage);
			}
			return true;
		}
		if (!probe.error.empty()) {
			aceStepServerStatusMessage =
				"AceStep server not reachable at " + configuredUrl + ". " + probe.error;
		}
	}

	if (isManagedAceStepServerRunning()) {
		aceStepServerStatus = ServerStatusState::Unknown;
		aceStepServerStatusMessage =
			"Local AceStep server is still starting at " + configuredUrl +
			". Try again in a moment.";
	}

	if (logResult) {
		logWithLevel(OF_LOG_WARNING, aceStepServerStatusMessage);
	}
	return false;
}

void ofApp::startLocalAceStepServer() {
	if (isManagedAceStepServerRunning()) {
		logWithLevel(OF_LOG_NOTICE, "Local AceStep server is already running.");
		return;
	}

	const std::string configuredUrl = effectiveAceStepServerUrl(aceStepServerUrl);
	const std::string serverExe = findLocalAceStepServerExecutable(true);
	if (serverExe.empty()) {
		aceStepServerStatus = ServerStatusState::Unreachable;
		aceStepServerStatusMessage = "Local AceStep server executable not found.";
		logWithLevel(OF_LOG_ERROR, aceStepServerStatusMessage);
		return;
	}

	const std::string modelsDir = findLocalAceStepModelsDirectory(true);
	if (modelsDir.empty()) {
		aceStepServerStatus = ServerStatusState::Unreachable;
		aceStepServerStatusMessage =
			"No local GGUF AceStep models directory was found. Put the downloaded GGUF models in libs/acestep/bin/models or a shared models/acestep folder first.";
		logWithLevel(OF_LOG_ERROR, aceStepServerStatusMessage);
		return;
	}

	aceStepServerManager.startLocalServer(
		configuredUrl,
		modelsDir,
		aceStepServerManager.findLocalAdaptersDirectory(true));

	aceStepServerManagedByApp = aceStepServerManager.isManagedByApp();
	aceStepServerStatus = aceStepServerManager.getStatus();
	aceStepServerStatusMessage = aceStepServerManager.getStatusMessage();

	if (aceStepServerManagedByApp) {
		const auto [host, port] = parseAceStepServerHostPort(configuredUrl);
		logWithLevel(
			OF_LOG_NOTICE,
			"Started local AceStep server on " + host + ":" + ofToString(port) +
			" using models from " + modelsDir + ".");
	} else if (!aceStepServerStatusMessage.empty()) {
		logWithLevel(OF_LOG_ERROR, aceStepServerStatusMessage);
	}
}

void ofApp::stopLocalAceStepServer(bool logResult) {
	aceStepServerManager.stopLocalServer(logResult);
	aceStepServerManagedByApp = aceStepServerManager.isManagedByApp();
	aceStepServerStatus = aceStepServerManager.getStatus();
	aceStepServerStatusMessage = aceStepServerManager.getStatusMessage();

	if (logResult && !aceStepServerStatusMessage.empty()) {
		logWithLevel(OF_LOG_NOTICE, aceStepServerStatusMessage);
	}
}
