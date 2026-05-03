#include "ofxGgmlCodeAssistant.h"
#include "ofxGgmlCodeAssistantInternals.h"

#include "core/ofxGgmlHelpers.h"

#include <algorithm>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using ofxGgmlHelpers::trim;

std::string joinStrings(
	const std::vector<std::string> & values,
	const std::string & separator) {
	std::ostringstream joined;
	for (size_t i = 0; i < values.size(); ++i) {
		if (i > 0) {
			joined << separator;
		}
		joined << values[i];
	}
	return joined.str();
}

std::string summarizeVerificationCommands(
	const std::vector<ofxGgmlCodeAssistantCommandSuggestion> & commands) {
	std::vector<std::string> labels;
	for (const auto & command : commands) {
		if (!trim(command.label).empty()) {
			labels.push_back(command.label);
		} else if (!trim(command.executable).empty()) {
			labels.push_back(command.executable);
		}
	}
	return joinStrings(labels, ", ");
}

std::string summarizeTouchedFiles(
	const std::vector<ofxGgmlCodeAssistantFileIntent> & files) {
	std::vector<std::string> paths;
	for (const auto & file : files) {
		if (!trim(file.filePath).empty()) {
			paths.push_back(file.filePath);
		}
	}
	return joinStrings(paths, ", ");
}

std::optional<ofxGgmlCodeAssistantToolDefinition> findToolDefinition(
	const std::vector<ofxGgmlCodeAssistantToolDefinition> & registry,
	const std::string & toolName) {
	const auto it = std::find_if(
		registry.begin(),
		registry.end(),
		[&](const ofxGgmlCodeAssistantToolDefinition & tool) {
			return tool.name == toolName;
		});
	if (it == registry.end()) {
		return std::nullopt;
	}
	return *it;
}

void appendToolCallIfEnabled(
	std::vector<ofxGgmlCodeAssistantToolCall> * calls,
	const std::vector<ofxGgmlCodeAssistantToolDefinition> & registry,
	const std::string & toolName,
	const std::string & summary,
	const std::string & payload = {}) {
	if (calls == nullptr) {
		return;
	}
	const auto tool = findToolDefinition(registry, toolName);
	if (!tool || !tool->enabledByDefault) {
		return;
	}
	ofxGgmlCodeAssistantToolCall call;
	call.toolName = tool->name;
	call.summary = summary;
	call.payload = payload;
	call.category = tool->category;
	call.requiresApproval = tool->requiresApproval;
	calls->push_back(std::move(call));
}

} // namespace

void ofxGgmlCodeAssistant::registerTool(
	const ofxGgmlCodeAssistantToolDefinition & tool) {
	std::lock_guard<std::mutex> lock(m_toolRegistryMutex);
	if (m_toolRegistry.empty()) {
		m_toolRegistry = defaultToolRegistry();
	}
	auto it = std::find_if(
		m_toolRegistry.begin(),
		m_toolRegistry.end(),
		[&](const ofxGgmlCodeAssistantToolDefinition & existing) {
			return existing.name == tool.name;
		});
	if (it != m_toolRegistry.end()) {
		*it = tool;
		return;
	}
	m_toolRegistry.push_back(tool);
}

void ofxGgmlCodeAssistant::resetToolRegistry() {
	std::lock_guard<std::mutex> lock(m_toolRegistryMutex);
	m_toolRegistry = defaultToolRegistry();
}

std::vector<ofxGgmlCodeAssistantToolDefinition>
ofxGgmlCodeAssistant::getToolRegistry() const {
	std::lock_guard<std::mutex> lock(m_toolRegistryMutex);
	if (!m_toolRegistry.empty()) {
		return m_toolRegistry;
	}
	return defaultToolRegistry();
}

std::vector<ofxGgmlCodeAssistantToolDefinition>
ofxGgmlCodeAssistant::defaultToolRegistry() {
	return {
		{
			"read_repo_context",
			"Read repository instructions, focused files, and nearby snippets.",
			ofxGgmlCodeAssistantToolCategory::Context,
			false,
			true
		},
		{
			"search_symbols",
			"Retrieve semantic definitions, references, and caller context.",
			ofxGgmlCodeAssistantToolCategory::Retrieval,
			false,
			true
		},
		{
			"fetch_grounding_sources",
			"Load explicit web or documentation sources for grounded answers.",
			ofxGgmlCodeAssistantToolCategory::Grounding,
			false,
			true
		},
		{
			"apply_patch",
			"Apply structured file edits or unified diffs in the workspace.",
			ofxGgmlCodeAssistantToolCategory::Patching,
			true,
			true
		},
		{
			"run_verification",
			"Run build, test, or verification commands for the proposed change.",
			ofxGgmlCodeAssistantToolCategory::Verification,
			true,
			true
		},
		{
			"review_changes",
			"Inspect proposed patches, findings, and risks before execution.",
			ofxGgmlCodeAssistantToolCategory::Analysis,
			false,
			true
		}
	};
}

std::string ofxGgmlCodeAssistant::describeToolPolicyProfile(
	ofxGgmlCodeAssistantToolPolicyProfile profile) {
	switch (profile) {
	case ofxGgmlCodeAssistantToolPolicyProfile::ReadOnly:
		return "read-only";
	case ofxGgmlCodeAssistantToolPolicyProfile::WorkspaceSafe:
		return "workspace-safe";
	case ofxGgmlCodeAssistantToolPolicyProfile::Strict:
		return "strict";
	case ofxGgmlCodeAssistantToolPolicyProfile::Balanced:
	default:
		return "balanced";
	}
}

namespace ofxGgmlCodeAssistantInternals {

std::vector<ofxGgmlCodeAssistantToolCall> buildProposedToolCalls(
	const ofxGgmlCodeAssistantPreparedPrompt & prepared,
	const ofxGgmlCodeAssistantRequest & request,
	const ofxGgmlCodeAssistantStructuredResult & structured,
	const std::vector<ofxGgmlCodeAssistantToolDefinition> & registry) {
	std::vector<ofxGgmlCodeAssistantToolCall> calls;

	if (prepared.includedRepoContext || prepared.includedFocusedFile ||
		prepared.includedTaskMemory) {
		appendToolCallIfEnabled(
			&calls,
			registry,
			"read_repo_context",
			"Use repository instructions, focused files, and task memory.",
			prepared.focusedFileName);
	}

	if (prepared.includedSymbolContext || prepared.includedCodeMap) {
		appendToolCallIfEnabled(
			&calls,
			registry,
			"search_symbols",
			"Use semantic symbol retrieval and code-map context.",
			prepared.retrievedSymbolContext.query);
	}

	if (!request.webUrls.empty()) {
		appendToolCallIfEnabled(
			&calls,
			registry,
			"fetch_grounding_sources",
			"Use explicit external grounding sources.",
			joinStrings(request.webUrls, ", "));
	}

	if (!structured.patchOperations.empty() ||
		!trim(structured.unifiedDiff).empty()) {
		appendToolCallIfEnabled(
			&calls,
			registry,
			"apply_patch",
			"Apply the proposed patch set to touched files.",
			summarizeTouchedFiles(structured.filesToTouch));
	}

	if (!structured.verificationCommands.empty()) {
		appendToolCallIfEnabled(
			&calls,
			registry,
			"run_verification",
			"Run verification commands for the proposed change.",
			summarizeVerificationCommands(structured.verificationCommands));
	}

	if (!structured.reviewFindings.empty() ||
		!structured.riskAssessment.reasons.empty()) {
		appendToolCallIfEnabled(
			&calls,
			registry,
			"review_changes",
			"Inspect findings, risks, and suggested follow-up work.",
			trim(structured.riskAssessment.level));
	}

	return calls;
}

} // namespace ofxGgmlCodeAssistantInternals
