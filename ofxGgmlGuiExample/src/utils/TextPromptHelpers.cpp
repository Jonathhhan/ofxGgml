#include "TextPromptHelpers.h"
#include "utils/ImGuiHelpers.h"
#include "config/ModelPresets.h"
#include "ofxGgml.h"
#include "ofFileUtils.h"

#include <algorithm>
#include <sstream>
#include <filesystem>

std::string buildStructuredTextPrompt(
	const std::string & systemPrompt,
	const std::string & instruction,
	const std::string & inputHeading,
	const std::string & inputText,
	const std::string & outputHeading)
{
	std::ostringstream prompt;
	const std::string system = trim(systemPrompt);
	if (!system.empty()) {
		prompt << "System:\n" << system << "\n\n";
	}
	prompt << trim(instruction) << "\n";
	if (!trim(inputHeading).empty()) {
		prompt << trim(inputHeading) << ":\n";
	}
	prompt << inputText << "\n\n";
	if (!trim(outputHeading).empty()) {
		prompt << trim(outputHeading) << ":\n";
	}
	return prompt.str();
}

std::string cleanChatOutput(const std::string & text) {
	return ofxGgmlInference::sanitizeGeneratedText(text);
}

std::string clampPromptToContext(
	const std::string & prompt,
	size_t contextTokens,
	bool & trimmed)
{
	trimmed = false;
	if (contextTokens == 0) return prompt;
	const size_t charBudget = std::max<size_t>(512, contextTokens * 3);
	if (prompt.size() <= charBudget) return prompt;

	trimmed = true;
	const size_t head = std::min<size_t>(2048, charBudget / 4);
	if (charBudget <= head + 96) {
		return prompt.substr(prompt.size() - charBudget);
	}
	const size_t tail = charBudget - head - 32;
	return prompt.substr(0, head)
		+ "\n...[context trimmed to fit window]...\n"
		+ prompt.substr(prompt.size() - tail);
}

std::string truncatePromptPayload(const std::string & text, size_t maxChars) {
	if (text.size() <= maxChars) {
		return text;
	}
	return text.substr(0, maxChars) +
		"\n...[truncated " + std::to_string(text.size() - maxChars) + " chars]";
}

bool isLikelyCutoffOutput(const std::string & text, int aiMode) {
	const std::string t = trim(text);
	if (t.empty()) return false;
	if (t.rfind("[Error]", 0) == 0) return false;

	const char last = t.back();
	// AiMode::Script == 1
	if (aiMode == 1) {
		if (last == '\n' || last == '}' || last == ')' || last == ']' || last == ';') {
			return false;
		}
		return t.size() > 80;
	}

	if (last == '.' || last == '!' || last == '?' || last == '"' || last == '\'') {
		return false;
	}
	return t.size() > 80;
}
