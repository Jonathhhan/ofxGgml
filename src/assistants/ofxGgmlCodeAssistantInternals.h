#pragma once

#include "ofxGgmlCodeAssistant.h"

#include <vector>

namespace ofxGgmlCodeAssistantInternals {

std::vector<ofxGgmlCodeAssistantToolCall> buildProposedToolCalls(
	const ofxGgmlCodeAssistantPreparedPrompt & prepared,
	const ofxGgmlCodeAssistantRequest & request,
	const ofxGgmlCodeAssistantStructuredResult & structured,
	const std::vector<ofxGgmlCodeAssistantToolDefinition> & registry);

} // namespace ofxGgmlCodeAssistantInternals
