#pragma once

#include "ofxGgmlInference.h"

#include <functional>
#include <string>
#include <vector>

enum class ofxGgmlTextTask {
	Summarize = 0,
	KeyPoints,
	TlDr,
	Rewrite,
	Expand,
	Polish,
	MakeFormal,
	MakeCasual,
	FixGrammar,
	Translate,
	DetectLanguage,
	Custom
};

struct ofxGgmlTextLanguageOption {
	std::string name;
};

struct ofxGgmlTextPromptTemplate {
	std::string name;
	std::string systemPrompt;
};

struct ofxGgmlTextAssistantRequest {
	ofxGgmlTextTask task = ofxGgmlTextTask::Summarize;
	std::string inputText;
	std::string systemPrompt;
	std::string sourceLanguage;
	std::string targetLanguage;
	std::string labelOverride;
};

struct ofxGgmlTextAssistantPreparedPrompt {
	std::string prompt;
	std::string label;
};

struct ofxGgmlTextAssistantResult {
	ofxGgmlTextAssistantPreparedPrompt prepared;
	ofxGgmlInferenceResult inference;
};

/// High-level text-task helper built on top of ofxGgmlInference.
class ofxGgmlTextAssistant {
public:
	void setCompletionExecutable(const std::string & path);
	ofxGgmlInference & getInference();
	const ofxGgmlInference & getInference() const;

	ofxGgmlTextAssistantPreparedPrompt preparePrompt(
		const ofxGgmlTextAssistantRequest & request) const;

	ofxGgmlTextAssistantResult run(
		const std::string & modelPath,
		const ofxGgmlTextAssistantRequest & request,
		const ofxGgmlInferenceSettings & settings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const;

	static std::vector<ofxGgmlTextLanguageOption> defaultTranslateLanguages();
	static std::vector<ofxGgmlTextPromptTemplate> defaultPromptTemplates();
	static std::string defaultTaskLabel(
		const ofxGgmlTextAssistantRequest & request);

private:
	ofxGgmlInference m_inference;
};
