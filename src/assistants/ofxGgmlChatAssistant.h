#pragma once

#include "inference/ofxGgmlInference.h"

#include <functional>
#include <string>
#include <vector>

struct ofxGgmlChatLanguageOption {
	std::string name;
};

struct ofxGgmlChatAssistantRequest {
	std::string userText;
	std::string systemPrompt;
	std::string responseLanguage;
	std::string labelOverride;
};

struct ofxGgmlChatAssistantPreparedPrompt {
	std::string prompt;
	std::string label;
};

struct ofxGgmlChatAssistantResult {
	ofxGgmlChatAssistantPreparedPrompt prepared;
	ofxGgmlInferenceResult inference;
};

/// Small helper for conversation-style prompts on top of ofxGgmlInference.
class ofxGgmlChatAssistant {
public:
	void setCompletionExecutable(const std::string & path);
	ofxGgmlInference & getInference();
	const ofxGgmlInference & getInference() const;

	ofxGgmlChatAssistantPreparedPrompt preparePrompt(
		const ofxGgmlChatAssistantRequest & request) const;

	ofxGgmlChatAssistantResult run(
		const std::string & modelPath,
		const ofxGgmlChatAssistantRequest & request,
		const ofxGgmlInferenceSettings & settings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const;

	static std::vector<ofxGgmlChatLanguageOption> defaultResponseLanguages();

private:
	ofxGgmlInference m_inference;
};
