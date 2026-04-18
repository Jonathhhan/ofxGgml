#include "SpeechHelpers.h"
#include "ImGuiHelpers.h"
#include "ofxGgmlSpeechInference.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>

// Default server URLs (matching ofApp.cpp constants)
static const char * const kDefaultTextServerUrl = "http://127.0.0.1:8080";
static const char * const kDefaultSpeechServerUrl = "http://127.0.0.1:8081";

std::string formatSpeechTimestamp(double seconds) {
	if (seconds < 0.0) {
		seconds = 0.0;
	}
	const int totalMillis = static_cast<int>(std::round(seconds * 1000.0));
	const int hours = totalMillis / 3600000;
	const int minutes = (totalMillis / 60000) % 60;
	const int secs = (totalMillis / 1000) % 60;
	const int millis = totalMillis % 1000;
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d",
		hours, minutes, secs, millis);
	return buffer;
}

std::string formatSpeechSegments(
	const std::vector<ofxGgmlSpeechSegment> & segments) {
	if (segments.empty()) {
		return {};
	}
	std::ostringstream out;
	for (const auto & segment : segments) {
		out << "[" << formatSpeechTimestamp(segment.startSeconds)
			<< " - " << formatSpeechTimestamp(segment.endSeconds) << "] "
			<< segment.text << "\n";
	}
	return trim(out.str());
}

std::string effectiveTextServerUrl(const char * buffer) {
	const std::string value = trim(buffer ? std::string(buffer) : std::string());
	return value.empty() ? std::string(kDefaultTextServerUrl) : value;
}

std::string effectiveSpeechServerUrl(const char * buffer) {
	return trim(buffer ? std::string(buffer) : std::string());
}

bool buildSpeechExecutionPlan(
	const ofxGgmlSpeechModelProfile & profileBase,
	const std::string & audioPath,
	const std::string & executable,
	const std::string & modelPath,
	const std::string & serverUrl,
	const std::string & serverModel,
	const std::string & prompt,
	const std::string & languageHint,
	int taskIndex,
	bool returnTimestamps,
	SpeechExecutionPlan & plan,
	std::string & errorMessage) {
	if (audioPath.empty()) {
		errorMessage = "Select an audio file first.";
		return false;
	}

	std::string effectiveExecutable =
		executable.empty() ? trim(profileBase.executable) : executable;
	if (effectiveExecutable.empty()) {
		effectiveExecutable = "whisper-cli";
	}
	effectiveExecutable =
		ofxGgmlSpeechInference::resolveWhisperCliExecutable(effectiveExecutable);

	std::string effectiveModelPath = modelPath.empty()
		? trim(profileBase.modelPath)
		: modelPath;
	if (effectiveModelPath.empty() && !trim(profileBase.modelFileHint).empty()) {
		const std::filesystem::path suggested =
			std::filesystem::path(ofToDataPath("models", true)) /
			trim(profileBase.modelFileHint);
		std::error_code ec;
		if (std::filesystem::exists(suggested, ec) && !ec) {
			effectiveModelPath = suggested.string();
		}
	}

	plan.request.task = static_cast<ofxGgmlSpeechTask>(taskIndex);
	plan.request.audioPath = audioPath;
	plan.request.modelPath = effectiveModelPath;
	plan.request.serverUrl = serverUrl;
	plan.request.serverModel = serverModel;
	plan.request.languageHint = languageHint;
	plan.request.prompt = prompt;
	plan.request.returnTimestamps = returnTimestamps;
	plan.effectiveExecutable = effectiveExecutable;
	return true;
}

ofxGgmlSpeechResult executeSpeechExecutionPlan(
	const SpeechExecutionPlan & plan,
	const std::function<void(const std::string &)> & statusCallback,
	const std::function<void(ofLogLevel, const std::string &)> & logCallback) {
	ofxGgmlSpeechInference localInference;
	ofxGgmlSpeechResult result;
	bool attemptedServer = false;

	if (!plan.request.serverUrl.empty()) {
		attemptedServer = true;
		localInference.setBackend(
			ofxGgmlSpeechInference::createWhisperServerBackend(
				plan.request.serverUrl,
				plan.request.serverModel));
		if (statusCallback) {
			statusCallback("Calling speech server...");
		}
		result = localInference.transcribe(plan.request);
		if (!result.success && !plan.effectiveExecutable.empty() && logCallback) {
			logCallback(
				OF_LOG_WARNING,
				"Speech server failed, falling back to " + plan.effectiveExecutable +
					": " + result.error);
		}
	}

	if (!result.success) {
		localInference.setBackend(
			ofxGgmlSpeechInference::createWhisperCliBackend(
				plan.effectiveExecutable));
		if (statusCallback) {
			statusCallback(
				attemptedServer
					? "Falling back to " + plan.effectiveExecutable + "..."
					: "Running " + plan.effectiveExecutable + "...");
		}
		result = localInference.transcribe(plan.request);
	}

	return result;
}
