#pragma once

#include "assistants/ofxGgmlTextAssistant.h"
#include "inference/ofxGgmlCitationSearch.h"
#include "inference/ofxGgmlVideoPlanner.h"

#include <string>
#include <vector>

struct ofxGgmlVideoEssayRequest {
	std::string modelPath;
	std::string topic;
	size_t maxCitations = 100;
	bool useCrawler = false;
	ofxGgmlWebCrawlerRequest crawlerRequest;
	std::vector<std::string> sourceUrls;
	double targetDurationSeconds = 90.0;
	std::string tone = "clear, engaging, and grounded";
	std::string audience = "general audience";
	bool includeCounterpoints = true;
	ofxGgmlInferenceSettings inferenceSettings;
	ofxGgmlPromptSourceSettings sourceSettings;
};

struct ofxGgmlVideoEssaySection {
	int index = 0;
	std::string title;
	std::string summary;
	std::string narrationText;
	double estimatedDurationSeconds = 0.0;
	std::vector<int> sourceIndices;
};

struct ofxGgmlVideoEssayVoiceCue {
	int index = 0;
	int sectionIndex = -1;
	std::string text;
	double startSeconds = 0.0;
	double endSeconds = 0.0;
};

struct ofxGgmlVideoEssayValidation {
	bool ok = true;
	std::vector<std::string> errors;
	std::vector<std::string> warnings;
};

struct ofxGgmlVideoEssayResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string backendName;
	std::string error;
	ofxGgmlVideoEssayValidation validation;
	ofxGgmlCitationSearchResult citationResult;
	ofxGgmlTextAssistantResult outlineResult;
	ofxGgmlTextAssistantResult scriptResult;
	std::string outline;
	std::string script;
	std::string visualConcept;
	std::vector<ofxGgmlVideoEssaySection> sections;
	std::vector<ofxGgmlVideoEssayVoiceCue> voiceCues;
	std::string srtText;
	std::string scenePlanJson;
	std::string scenePlanSummary;
	std::string editPlanJson;
	std::string editPlanSummary;
	std::string editorBrief;
	std::string workflowManifestJson;
	std::string scenePlanningError;
	std::string editPlanningError;
	ofxGgmlVideoPlan scenePlan;
	ofxGgmlVideoEditPlan editPlan;
};

class ofxGgmlVideoEssayWorkflow {
public:
	ofxGgmlCitationSearch & getCitationSearch();
	const ofxGgmlCitationSearch & getCitationSearch() const;
	ofxGgmlTextAssistant & getTextAssistant();
	const ofxGgmlTextAssistant & getTextAssistant() const;
	ofxGgmlVideoPlanner & getVideoPlanner();
	const ofxGgmlVideoPlanner & getVideoPlanner() const;

	ofxGgmlVideoEssayResult run(
		const ofxGgmlVideoEssayRequest & request) const;

	static std::string buildOutlinePrompt(
		const ofxGgmlVideoEssayRequest & request,
		const ofxGgmlCitationSearchResult & citationResult);
	static std::string buildScriptPrompt(
		const ofxGgmlVideoEssayRequest & request,
		const ofxGgmlCitationSearchResult & citationResult,
		const std::string & outline);
	static std::string buildVisualConceptPrompt(
		const ofxGgmlVideoEssayRequest & request,
		const ofxGgmlCitationSearchResult & citationResult,
		const std::vector<ofxGgmlVideoEssaySection> & sections);
	static std::vector<ofxGgmlVideoEssaySection> parseSectionsFromScript(
		const std::string & script,
		double targetDurationSeconds = 0.0);
	static std::vector<ofxGgmlVideoEssayVoiceCue> buildVoiceCueSheet(
		const std::vector<ofxGgmlVideoEssaySection> & sections);
	static std::string buildSrt(
		const std::vector<ofxGgmlVideoEssayVoiceCue> & cues);
	static std::string buildEditSourceSummary(
		const std::vector<ofxGgmlVideoEssaySection> & sections);
	static ofxGgmlVideoEssayValidation validateRequest(
		const ofxGgmlVideoEssayRequest & request);
	static std::string buildWorkflowManifest(
		const ofxGgmlVideoEssayRequest & request,
		const ofxGgmlVideoEssayResult & result);

private:
	ofxGgmlCitationSearch m_citationSearch;
	ofxGgmlTextAssistant m_textAssistant;
	ofxGgmlVideoPlanner m_videoPlanner;
};
