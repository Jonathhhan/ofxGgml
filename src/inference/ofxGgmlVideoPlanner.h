#pragma once

#include "core/ofxGgmlResult.h"
#include "inference/ofxGgmlInference.h"

#include <string>
#include <vector>

struct ofxGgmlVideoPlanSubject {
	std::string id;
	std::string label;
	std::string description;
};

struct ofxGgmlVideoPlanEntity {
	std::string id;
	std::string label;
	std::string description;
	std::string role;
	std::string referenceImagePath;
};

struct ofxGgmlVideoPlanBeat {
	double startSeconds = 0.0;
	double endSeconds = 0.0;
	std::string summary;
	std::string camera;
	std::string scene;
	std::string motion;
	std::string visualGoal;
	std::vector<std::string> subjects;
};

struct ofxGgmlVideoPlanScene {
	int index = 0;
	std::string title;
	std::string summary;
	std::string eventPrompt;
	std::string background;
	std::string cameraMovement;
	std::string transition;
	double durationSeconds = 0.0;
	std::vector<std::string> entityIds;
};

struct ofxGgmlVideoPlan {
	std::string originalPrompt;
	std::string style;
	std::string overallScene;
	std::string overallCamera;
	std::string continuityNotes;
	std::string negativePrompt;
	std::vector<std::string> constraints;
	std::vector<ofxGgmlVideoPlanSubject> subjects;
	std::vector<ofxGgmlVideoPlanEntity> entities;
	std::vector<ofxGgmlVideoPlanBeat> beats;
	std::vector<ofxGgmlVideoPlanScene> scenes;
};

struct ofxGgmlVideoPlannerRequest {
	std::string prompt;
	double durationSeconds = 5.0;
	int beatCount = 4;
	int sceneCount = 3;
	bool multiScene = false;
	std::string preferredStyle;
	std::string negativePrompt;
};

struct ofxGgmlVideoPlannerResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string planningPrompt;
	std::string rawText;
	std::string error;
	ofxGgmlVideoPlan plan;
};

class ofxGgmlVideoPlanner {
public:
	static std::string buildPlanningPrompt(const ofxGgmlVideoPlannerRequest & request);
	static std::string extractJsonObject(const std::string & text);
	static Result<ofxGgmlVideoPlan> parsePlanJson(const std::string & jsonText);
	static std::string buildGenerationPrompt(const ofxGgmlVideoPlan & plan);
	static std::string buildScenePrompt(const ofxGgmlVideoPlan & plan, size_t sceneIndex);
	static std::string summarizePlan(const ofxGgmlVideoPlan & plan);

	ofxGgmlVideoPlannerResult plan(
		const std::string & modelPath,
		const ofxGgmlVideoPlannerRequest & request,
		const ofxGgmlInferenceSettings & settings,
		const ofxGgmlInference & inference) const;
};
