#pragma once

#include "assistants/ofxGgmlTextAssistant.h"

#include <string>
#include <vector>

struct ofxGgmlLongVideoPlanRequest {
	std::string modelPath;
	std::string conceptText;
	std::string style = "cinematic, coherent, detailed, motion-aware";
	std::string negativeStyle = "flicker, identity drift, abrupt style changes, broken continuity";
	std::string tone = "clear, cinematic, and continuity-aware";
	std::string structureHint = "three-act cinematic progression";
	std::string pacingProfile = "balanced rise with stronger emphasis near the climax";
	std::string continuityGoal =
		"Preserve subject identity, camera language, palette, and motion logic across adjacent chunks.";
	double targetDurationSeconds = 60.0;
	int chunkCount = 6;
	int width = 640;
	int height = 384;
	int fps = 12;
	int framesPerChunk = 49;
	int64_t seed = -1;
	bool usePromptInheritance = true;
	bool favorLoopableEnding = false;
	ofxGgmlInferenceSettings inferenceSettings;
	ofxGgmlPromptSourceSettings sourceSettings;
};

struct ofxGgmlLongVideoPlanChunk {
	int index = 0;
	std::string id;
	std::string title;
	std::string sectionGoal;
	std::string continuityNote;
	std::string transitionHint;
	std::string prompt;
	std::string negativePrompt;
	double startSeconds = 0.0;
	double endSeconds = 0.0;
	double targetDurationSeconds = 0.0;
	double progressionWeight = 1.0;
	int width = 640;
	int height = 384;
	int fps = 12;
	int frameCount = 49;
	int64_t seed = -1;
	bool usePreviousLastFrame = true;
};

struct ofxGgmlLongVideoPlanValidation {
	bool ok = true;
	std::vector<std::string> errors;
	std::vector<std::string> warnings;
};

struct ofxGgmlLongVideoPlanResult {
	bool success = false;
	std::string error;
	ofxGgmlLongVideoPlanValidation validation;
	std::string continuityBible;
	std::string plannerPrompt;
	std::vector<ofxGgmlLongVideoPlanChunk> chunks;
	std::string manifestJson;
};

class ofxGgmlLongVideoPlanner {
public:
	ofxGgmlTextAssistant & getTextAssistant();
	const ofxGgmlTextAssistant & getTextAssistant() const;

	ofxGgmlLongVideoPlanResult run(const ofxGgmlLongVideoPlanRequest & request) const;

	static ofxGgmlLongVideoPlanValidation validateRequest(
		const ofxGgmlLongVideoPlanRequest & request);
	static std::string buildPlanningPrompt(
		const ofxGgmlLongVideoPlanRequest & request);
	static std::string buildManifestJson(
		const ofxGgmlLongVideoPlanRequest & request,
		const std::vector<ofxGgmlLongVideoPlanChunk> & chunks,
		const std::string & continuityBible);

private:
	ofxGgmlTextAssistant m_textAssistant;
};
