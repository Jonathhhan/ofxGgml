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

struct ofxGgmlVideoEditClip {
	int index = 0;
	double startSeconds = 0.0;
	double endSeconds = 0.0;
	std::string purpose;
	std::string sourceDescription;
	std::string treatment;
	std::string transition;
	std::string textOverlay;
};

struct ofxGgmlVideoEditAction {
	int index = 0;
	std::string type;
	double startSeconds = 0.0;
	double endSeconds = 0.0;
	std::string instruction;
	std::string rationale;
	std::string assetHint;
};

struct ofxGgmlVideoEditPlan {
	std::string originalGoal;
	std::string sourceSummary;
	std::string overallDirection;
	std::string pacingStrategy;
	std::string visualStyle;
	std::string audioStrategy;
	double targetDurationSeconds = 0.0;
	std::vector<std::string> globalNotes;
	std::vector<std::string> assetSuggestions;
	std::vector<ofxGgmlVideoEditClip> clips;
	std::vector<ofxGgmlVideoEditAction> actions;
};

struct ofxGgmlVideoEditWorkflowContext {
	bool hasSourceVideo = false;
	bool hasSourceTimedPreview = false;
	bool hasMontageTimedPreview = false;
	bool hasSubtitlePreview = false;
};

struct ofxGgmlVideoEditWorkflowStep {
	int index = 0;
	std::string title;
	std::string detail;
	std::string handoffMode;
	std::string handoffText;
	double startSeconds = 0.0;
	double endSeconds = 0.0;
};

struct ofxGgmlVideoEditWorkflow {
	std::string headline;
	std::string nextAction;
	std::string previewHint;
	std::vector<std::string> checklist;
	std::vector<ofxGgmlVideoEditWorkflowStep> steps;
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

struct ofxGgmlVideoEditPlannerRequest {
	std::string sourcePrompt;
	std::string editGoal;
	std::string sourceAnalysis;
	double targetDurationSeconds = 15.0;
	int clipCount = 5;
	bool preserveChronology = true;
};

struct ofxGgmlVideoPlannerResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string planningPrompt;
	std::string rawText;
	std::string error;
	ofxGgmlVideoPlan plan;
};

struct ofxGgmlVideoEditPlannerResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string planningPrompt;
	std::string rawText;
	std::string error;
	ofxGgmlVideoEditPlan plan;
};

class ofxGgmlVideoPlanner {
public:
	static std::string buildPlanningPrompt(const ofxGgmlVideoPlannerRequest & request);
	static std::string buildEditingPrompt(const ofxGgmlVideoEditPlannerRequest & request);
	static std::string extractJsonObject(const std::string & text);
	static Result<ofxGgmlVideoPlan> parsePlanJson(const std::string & jsonText);
	static Result<ofxGgmlVideoEditPlan> parseEditPlanJson(const std::string & jsonText);
	static std::string buildGenerationPrompt(const ofxGgmlVideoPlan & plan);
	static std::string buildScenePrompt(const ofxGgmlVideoPlan & plan, size_t sceneIndex);
	static std::string buildSceneSequencePrompt(const ofxGgmlVideoPlan & plan);
	static std::string buildEditorBrief(const ofxGgmlVideoEditPlan & plan);
	static ofxGgmlVideoEditWorkflow buildEditWorkflow(
		const ofxGgmlVideoEditPlan & plan,
		const ofxGgmlVideoEditWorkflowContext & context = {});
	static std::string summarizePlan(const ofxGgmlVideoPlan & plan);
	static std::string summarizeEditPlan(const ofxGgmlVideoEditPlan & plan);
	static std::string summarizeEditWorkflow(const ofxGgmlVideoEditWorkflow & workflow);

	ofxGgmlVideoPlannerResult plan(
		const std::string & modelPath,
		const ofxGgmlVideoPlannerRequest & request,
		const ofxGgmlInferenceSettings & settings,
		const ofxGgmlInference & inference) const;
	ofxGgmlVideoEditPlannerResult planEdits(
		const std::string & modelPath,
		const ofxGgmlVideoEditPlannerRequest & request,
		const ofxGgmlInferenceSettings & settings,
		const ofxGgmlInference & inference) const;
};
