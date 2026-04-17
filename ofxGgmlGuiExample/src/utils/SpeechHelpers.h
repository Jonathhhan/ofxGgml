#pragma once

#include "ofxGgmlSpeechInference.h"
#include "ImHelpers.h"
#include <functional>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Speech Utilities
// ---------------------------------------------------------------------------

// Format a timestamp in HH:MM:SS.mmm format for speech output
std::string formatSpeechTimestamp(double seconds);

// Format a list of speech segments with timestamps
std::string formatSpeechSegments(const std::vector<ofxGgmlSpeechSegment> & segments);

// Get effective text server URL from config buffer (with default fallback)
std::string effectiveTextServerUrl(const char * buffer);

// Get effective speech server URL from config buffer
std::string effectiveSpeechServerUrl(const char * buffer);

// ---------------------------------------------------------------------------
// Speech Execution Plan
// ---------------------------------------------------------------------------

struct SpeechExecutionPlan {
	ofxGgmlSpeechRequest request;
	std::string effectiveExecutable;
};

// Build a speech execution plan from user inputs
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
	std::string & errorMessage);

// Execute a speech execution plan with server fallback
ofxGgmlSpeechResult executeSpeechExecutionPlan(
	const SpeechExecutionPlan & plan,
	const std::function<void(const std::string &)> & statusCallback,
	const std::function<void(ofLogLevel, const std::string &)> & logCallback);
