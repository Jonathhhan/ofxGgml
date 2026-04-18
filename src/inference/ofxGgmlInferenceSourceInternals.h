#pragma once

#include "ofxGgmlInference.h"

#include <string>
#include <vector>

class ofxGgmlScriptSource;

namespace ofxGgmlInferenceSourceInternals {

std::vector<ofxGgmlPromptSource> fetchUrlSources(
	const std::vector<std::string> & urls,
	const ofxGgmlPromptSourceSettings & sourceSettings);

std::vector<ofxGgmlPromptSource> collectScriptSourceDocuments(
	ofxGgmlScriptSource & scriptSource,
	const ofxGgmlPromptSourceSettings & sourceSettings);

std::string buildPromptWithSources(
	const std::string & prompt,
	const std::vector<ofxGgmlPromptSource> & sources,
	const ofxGgmlPromptSourceSettings & sourceSettings,
	std::vector<ofxGgmlPromptSource> * usedSources = nullptr);

std::vector<ofxGgmlPromptSource> fetchRealtimeSources(
	const std::string & queryOrPrompt,
	const ofxGgmlRealtimeInfoSettings & realtimeSettings);

std::string buildPromptWithRealtimeInfo(
	const std::string & prompt,
	const std::string & queryOrPrompt,
	const ofxGgmlRealtimeInfoSettings & realtimeSettings,
	std::vector<ofxGgmlPromptSource> * usedSources = nullptr);

} // namespace ofxGgmlInferenceSourceInternals
