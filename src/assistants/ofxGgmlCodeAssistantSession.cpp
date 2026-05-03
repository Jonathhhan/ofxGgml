#include "ofxGgmlCodeAssistant.h"

#include "core/ofxGgmlHelpers.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace {

using ofxGgmlHelpers::trim;

void appendBoundedHistory(
	std::vector<std::string> * history,
	const std::string & value,
	size_t maxEntries) {
	if (history == nullptr || trim(value).empty() || maxEntries == 0) {
		return;
	}
	history->push_back(value);
	if (history->size() > maxEntries) {
		history->erase(
			history->begin(),
			history->begin() +
				static_cast<std::ptrdiff_t>(history->size() - maxEntries));
	}
}

} // namespace

void ofxGgmlCodeAssistant::seedContextFromSession(
	ofxGgmlCodeAssistantContext * context,
	const ofxGgmlCodeAssistantSession & session) {
	if (context == nullptr) {
		return;
	}
	if (trim(context->activeMode).empty()) {
		context->activeMode = session.activeMode;
	}
	if (trim(context->selectedBackend).empty()) {
		context->selectedBackend = session.selectedBackend;
	}
	if (context->recentTouchedFiles.empty()) {
		context->recentTouchedFiles = session.recentTouchedFiles;
	}
	if (trim(context->lastFailureReason).empty()) {
		context->lastFailureReason = session.lastFailureReason;
	}
}

void ofxGgmlCodeAssistant::updateSessionFromResult(
	ofxGgmlCodeAssistantSession * session,
	const ofxGgmlCodeAssistantRequest & request,
	const ofxGgmlCodeAssistantContext & context,
	const ofxGgmlCodeAssistantResult & result) {
	if (session == nullptr) {
		return;
	}

	if (!trim(context.activeMode).empty()) {
		session->activeMode = context.activeMode;
	}
	if (!trim(context.selectedBackend).empty()) {
		session->selectedBackend = context.selectedBackend;
	}
	if (!trim(result.prepared.focusedFileName).empty()) {
		session->focusedFilePath = result.prepared.focusedFileName;
	}

	std::vector<std::string> touchedFiles;
	for (const auto & fileIntent : result.structured.filesToTouch) {
		if (!trim(fileIntent.filePath).empty()) {
			touchedFiles.push_back(fileIntent.filePath);
		}
	}
	if (!touchedFiles.empty()) {
		session->recentTouchedFiles = touchedFiles;
	} else if (!context.recentTouchedFiles.empty()) {
		session->recentTouchedFiles = context.recentTouchedFiles;
	}

	appendBoundedHistory(
		&session->recentPrompts,
		trim(request.userInput).empty()
			? result.prepared.body
			: request.userInput,
		session->maxHistoryEntries);
	appendBoundedHistory(
		&session->recentSummaries,
		!trim(result.structured.goalSummary).empty()
			? result.structured.goalSummary
			: result.prepared.requestLabel,
		session->maxHistoryEntries);

	session->lastFailureReason = result.inference.success
		? std::string()
		: trim(result.inference.error);
	session->revision += 1;
}
