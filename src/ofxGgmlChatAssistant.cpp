#include "ofxGgmlChatAssistant.h"

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

void ofxGgmlChatAssistant::setCompletionExecutable(const std::string & path) {
	m_inference.setCompletionExecutable(path);
}

ofxGgmlInference & ofxGgmlChatAssistant::getInference() {
	return m_inference;
}

const ofxGgmlInference & ofxGgmlChatAssistant::getInference() const {
	return m_inference;
}

std::vector<ofxGgmlChatLanguageOption> ofxGgmlChatAssistant::defaultResponseLanguages() {
	return {
		{"Auto"},
		{"English"},
		{"German"},
		{"Spanish"},
		{"French"},
		{"Italian"},
		{"Portuguese"},
		{"Dutch"},
		{"Polish"},
		{"Swedish"},
		{"Chinese"},
		{"Japanese"},
		{"Korean"},
		{"Russian"},
		{"Arabic"},
		{"Hindi"}
	};
}

ofxGgmlChatAssistantPreparedPrompt ofxGgmlChatAssistant::preparePrompt(
	const ofxGgmlChatAssistantRequest & request) const {
	ofxGgmlChatAssistantPreparedPrompt prepared;
	prepared.label = trimCopy(request.labelOverride);
	if (prepared.label.empty()) {
		prepared.label = trimCopy(request.userText);
	}

	std::ostringstream prompt;
	const std::string systemPrompt = trimCopy(request.systemPrompt);
	const std::string responseLanguage = trimCopy(request.responseLanguage);
	if (!systemPrompt.empty()) {
		prompt << "System:\n" << systemPrompt << "\n\n";
	}
	if (!responseLanguage.empty() && responseLanguage != "Auto") {
		prompt << "System: Respond in " << responseLanguage
			<< ". Keep terminology natural for that language.\n\n";
	}
	prompt << "User:\n" << request.userText << "\n\nAssistant:\n";
	prepared.prompt = prompt.str();
	return prepared;
}

ofxGgmlChatAssistantResult ofxGgmlChatAssistant::run(
	const std::string & modelPath,
	const ofxGgmlChatAssistantRequest & request,
	const ofxGgmlInferenceSettings & settings,
	std::function<bool(const std::string &)> onChunk) const {
	ofxGgmlChatAssistantResult result;
	result.prepared = preparePrompt(request);
	result.inference = m_inference.generate(
		modelPath,
		result.prepared.prompt,
		settings,
		onChunk);
	return result;
}
