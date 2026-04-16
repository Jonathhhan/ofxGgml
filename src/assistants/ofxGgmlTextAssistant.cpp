#include "ofxGgmlTextAssistant.h"

#include <cctype>
#include <sstream>

namespace {

std::string trimCopy(const std::string & s) {
	size_t start = 0;
	while (start < s.size() &&
		std::isspace(static_cast<unsigned char>(s[start]))) {
		++start;
	}
	size_t end = s.size();
	while (end > start &&
		std::isspace(static_cast<unsigned char>(s[end - 1]))) {
		--end;
	}
	return s.substr(start, end - start);
}

} // namespace

void ofxGgmlTextAssistant::setCompletionExecutable(const std::string & path) {
	m_inference.setCompletionExecutable(path);
}

ofxGgmlInference & ofxGgmlTextAssistant::getInference() {
	return m_inference;
}

const ofxGgmlInference & ofxGgmlTextAssistant::getInference() const {
	return m_inference;
}

std::vector<ofxGgmlTextLanguageOption> ofxGgmlTextAssistant::defaultTranslateLanguages() {
	return {
		{"English"}, {"Spanish"}, {"French"}, {"German"}, {"Italian"},
		{"Portuguese"}, {"Chinese"}, {"Japanese"}, {"Korean"}, {"Russian"},
		{"Arabic"}, {"Hindi"}, {"Dutch"}, {"Swedish"}, {"Polish"}
	};
}

std::vector<ofxGgmlTextPromptTemplate> ofxGgmlTextAssistant::defaultPromptTemplates() {
	return {
		{
			"Code Reviewer",
			"You are an expert code reviewer. Analyze the provided code for bugs, "
			"security issues, performance problems, and style improvements. "
			"Provide specific, actionable feedback with line references."
		},
		{
			"Technical Writer",
			"You are a technical documentation writer. Generate clear, well-structured "
			"documentation with practical examples when helpful."
		},
		{
			"Editor",
			"You are a careful editor. Improve clarity, flow, tone, and correctness "
			"without changing the intended meaning."
		}
	};
}

std::string ofxGgmlTextAssistant::defaultTaskLabel(
	const ofxGgmlTextAssistantRequest & request) {
	switch (request.task) {
	case ofxGgmlTextTask::Summarize:
		return "Summarize text.";
	case ofxGgmlTextTask::KeyPoints:
		return "Extract key points.";
	case ofxGgmlTextTask::TlDr:
		return "Create a TL;DR.";
	case ofxGgmlTextTask::Rewrite:
		return "Rewrite text.";
	case ofxGgmlTextTask::Expand:
		return "Expand text.";
	case ofxGgmlTextTask::Polish:
		return "Polish text.";
	case ofxGgmlTextTask::MakeFormal:
		return "Make text more formal.";
	case ofxGgmlTextTask::MakeCasual:
		return "Make text more casual.";
	case ofxGgmlTextTask::FixGrammar:
		return "Fix grammar and spelling.";
	case ofxGgmlTextTask::Translate:
		return "Translate text.";
	case ofxGgmlTextTask::DetectLanguage:
		return "Detect language.";
	case ofxGgmlTextTask::Custom:
		return "Run custom prompt.";
	}
	return trimCopy(request.inputText);
}

ofxGgmlTextAssistantPreparedPrompt ofxGgmlTextAssistant::preparePrompt(
	const ofxGgmlTextAssistantRequest & request) const {
	ofxGgmlTextAssistantPreparedPrompt prepared;
	prepared.label = trimCopy(request.labelOverride);
	if (prepared.label.empty()) {
		prepared.label = defaultTaskLabel(request);
	}

	const std::string input = trimCopy(request.inputText);
	const std::string system = trimCopy(request.systemPrompt);
	const std::string sourceLanguage = trimCopy(request.sourceLanguage);
	const std::string targetLanguage = trimCopy(request.targetLanguage);

	std::ostringstream prompt;
	if (!system.empty()) {
		prompt << "System:\n" << system << "\n\n";
	}

	switch (request.task) {
	case ofxGgmlTextTask::Summarize:
		prompt << "Summarize this text concisely with key points:\n"
			<< input << "\n\nSummary:\n";
		break;
	case ofxGgmlTextTask::KeyPoints:
		prompt << "Extract key points from:\n"
			<< input << "\n\nKey points:\n";
		break;
	case ofxGgmlTextTask::TlDr:
		prompt << "Give a one-sentence TL;DR of:\n"
			<< input << "\n\nTL;DR:\n";
		break;
	case ofxGgmlTextTask::Rewrite:
		prompt << "Rewrite the following more clearly:\n"
			<< input << "\n\nImproved text:\n";
		break;
	case ofxGgmlTextTask::Expand:
		prompt << "Expand the following text with more detail while keeping the same intent:\n"
			<< input << "\n\nExpanded text:\n";
		break;
	case ofxGgmlTextTask::Polish:
		prompt << "Polish the following text for clarity, tone, and correctness:\n"
			<< input << "\n\nPolished text:\n";
		break;
	case ofxGgmlTextTask::MakeFormal:
		prompt << "Make this text more formal:\n"
			<< input << "\n\nFormal version:\n";
		break;
	case ofxGgmlTextTask::MakeCasual:
		prompt << "Make this text more casual:\n"
			<< input << "\n\nCasual version:\n";
		break;
	case ofxGgmlTextTask::FixGrammar:
		prompt << "Fix grammar and spelling in:\n"
			<< input << "\n\nCorrected text:\n";
		break;
	case ofxGgmlTextTask::Translate:
		prompt << "Translate the following";
		if (!sourceLanguage.empty()) {
			prompt << " from " << sourceLanguage;
		}
		if (!targetLanguage.empty()) {
			prompt << " to " << targetLanguage;
		}
		prompt << ":\n" << input << "\n\nTranslation:\n";
		break;
	case ofxGgmlTextTask::DetectLanguage:
		prompt << "Detect the language of the following text and explain briefly:\n"
			<< input << "\n\nAnswer:\n";
		break;
	case ofxGgmlTextTask::Custom:
		prompt << "User:\n" << input << "\n\nAssistant:\n";
		break;
	}

	prepared.prompt = prompt.str();
	return prepared;
}

ofxGgmlTextAssistantResult ofxGgmlTextAssistant::run(
	const std::string & modelPath,
	const ofxGgmlTextAssistantRequest & request,
	const ofxGgmlInferenceSettings & settings,
	std::function<bool(const std::string &)> onChunk) const {
	ofxGgmlTextAssistantResult result;
	result.prepared = preparePrompt(request);
	const bool useRealtimeInfo =
		request.realtimeInfo.enabled ||
		!request.realtimeInfo.explicitUrls.empty();
	result.inference = useRealtimeInfo
		? m_inference.generateWithRealtimeInfo(
			modelPath,
			result.prepared.prompt,
			request.inputText,
			settings,
			request.realtimeInfo,
			onChunk)
		: m_inference.generate(
			modelPath,
			result.prepared.prompt,
			settings,
			onChunk);
	return result;
}
