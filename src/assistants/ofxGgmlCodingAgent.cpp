#include "ofxGgmlCodingAgent.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

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

void addUniquePath(
	std::vector<std::string> * paths,
	std::set<std::string> * seen,
	const std::string & path) {
	const std::string trimmed = trimCopy(path);
	if (trimmed.empty()) {
		return;
	}
	const std::string normalized =
		std::filesystem::path(trimmed).generic_string();
	if (seen->insert(normalized).second) {
		paths->push_back(normalized);
	}
}

bool structuredHasProposedChanges(
	const ofxGgmlCodeAssistantStructuredResult & structured) {
	return !trimCopy(structured.unifiedDiff).empty() ||
		!structured.patchOperations.empty();
}

const ofxGgmlCodeAssistantToolCall * findToolCall(
	const std::vector<ofxGgmlCodeAssistantToolCall> & toolCalls,
	const std::string & toolName) {
	const auto it = std::find_if(
		toolCalls.begin(),
		toolCalls.end(),
		[&](const ofxGgmlCodeAssistantToolCall & toolCall) {
			return toolCall.toolName == toolName;
		});
	return it == toolCalls.end() ? nullptr : &(*it);
}

bool isApprovedToolCall(
	const std::vector<ofxGgmlCodeAssistantToolCall> & toolCalls,
	const std::string & toolName) {
	const auto * toolCall = findToolCall(toolCalls, toolName);
	if (toolCall == nullptr) {
		return true;
	}
	return !toolCall->requiresApproval || toolCall->approved;
}

bool containsApprovalDenialText(const std::string & text) {
	return text.find("approval was denied") != std::string::npos ||
		text.find("Approval denied") != std::string::npos;
}

const ofxGgmlScriptSourceWorkspaceInfo * getWorkspaceInfoIfLocal(
	const ofxGgmlCodeAssistantContext & context,
	ofxGgmlScriptSourceWorkspaceInfo * snapshot) {
	if (context.scriptSource == nullptr || snapshot == nullptr) {
		return nullptr;
	}
	if (context.scriptSource->getSourceType() !=
		ofxGgmlScriptSourceType::LocalFolder) {
		return nullptr;
	}
	*snapshot = context.scriptSource->getWorkspaceInfo();
	return snapshot;
}

} // namespace

void ofxGgmlCodingAgent::setCompletionExecutable(const std::string & path) {
	m_codeAssistant.setCompletionExecutable(path);
	m_workspaceAssistant.setCompletionExecutable(path);
}

void ofxGgmlCodingAgent::setEmbeddingExecutable(const std::string & path) {
	m_codeAssistant.setEmbeddingExecutable(path);
	m_workspaceAssistant.setEmbeddingExecutable(path);
}

ofxGgmlCodeAssistant & ofxGgmlCodingAgent::getCodeAssistant() {
	return m_codeAssistant;
}

const ofxGgmlCodeAssistant & ofxGgmlCodingAgent::getCodeAssistant() const {
	return m_codeAssistant;
}

ofxGgmlWorkspaceAssistant & ofxGgmlCodingAgent::getWorkspaceAssistant() {
	return m_workspaceAssistant;
}

const ofxGgmlWorkspaceAssistant & ofxGgmlCodingAgent::getWorkspaceAssistant() const {
	return m_workspaceAssistant;
}

ofxGgmlCodeAssistantSession & ofxGgmlCodingAgent::getSession() {
	return m_session;
}

const ofxGgmlCodeAssistantSession & ofxGgmlCodingAgent::getSession() const {
	return m_session;
}

void ofxGgmlCodingAgent::resetSession() {
	m_session = {};
}

std::vector<std::string> ofxGgmlCodingAgent::collectChangedFiles(
	const ofxGgmlCodeAssistantStructuredResult & structured,
	const ofxGgmlWorkspaceTransaction * transaction) {
	std::vector<std::string> changedFiles;
	std::set<std::string> seen;
	for (const auto & fileIntent : structured.filesToTouch) {
		addUniquePath(&changedFiles, &seen, fileIntent.filePath);
	}
	for (const auto & patch : structured.patchOperations) {
		addUniquePath(&changedFiles, &seen, patch.filePath);
	}
	if (transaction != nullptr) {
		for (const auto & diffFile : transaction->parsedDiffFiles) {
			if (!trimCopy(diffFile.normalizedPath).empty()) {
				addUniquePath(&changedFiles, &seen, diffFile.normalizedPath);
			} else if (!trimCopy(diffFile.newPath).empty() &&
				diffFile.newPath != "/dev/null") {
				addUniquePath(&changedFiles, &seen, diffFile.newPath);
			} else {
				addUniquePath(&changedFiles, &seen, diffFile.oldPath);
			}
		}
	}
	return changedFiles;
}

std::string ofxGgmlCodingAgent::summarizeResult(
	const ofxGgmlCodingAgentResult & result) {
	if (!trimCopy(result.error).empty()) {
		return result.error;
	}
	for (const auto & message : result.applyResult.messages) {
		if (containsApprovalDenialText(message)) {
			return message;
		}
	}
	if (containsApprovalDenialText(result.verificationResult.summary)) {
		return result.verificationResult.summary;
	}
	if (!result.assistantResult.inference.success) {
		return "Coding agent failed before producing a usable result.";
	}
	if (result.readOnly) {
		return "Plan generated without applying workspace changes.";
	}
	if (result.appliedChanges && result.verificationAttempted) {
		return result.verificationResult.success
			? "Changes applied and verification passed."
			: "Changes applied but verification failed.";
	}
	if (result.appliedChanges) {
		return "Changes applied without verification.";
	}
	if (result.verificationSuggested) {
		return "No changes were applied; verification commands were suggested.";
	}
	return "Assistant run completed without workspace edits.";
}

ofxGgmlCodingAgentResult ofxGgmlCodingAgent::run(
	const std::string & modelPath,
	const ofxGgmlCodingAgentRequest & request,
	const ofxGgmlCodeAssistantContext & context,
	const ofxGgmlCodingAgentSettings & settings,
	ofxGgmlCodeAssistantApprovalCallback approvalCallback,
	ofxGgmlCodeAssistantEventCallback eventCallback,
	std::function<bool(const std::string &)> onChunk) {
	ofxGgmlCodingAgentResult result;
	result.readOnly = (settings.mode == ofxGgmlCodingAgentMode::Plan);

	ofxGgmlCodeAssistantRequest effectiveRequest = request.assistantRequest;
	if (trimCopy(effectiveRequest.labelOverride).empty()) {
		effectiveRequest.labelOverride = trimCopy(request.taskLabel);
	}
	if (settings.requireStructuredResult) {
		effectiveRequest.requestStructuredResult = true;
	}
	if (settings.preferUnifiedDiff &&
		!result.readOnly &&
		settings.autoApply) {
		effectiveRequest.requestUnifiedDiff = true;
	}

	result.assistantResult = m_codeAssistant.runWithSession(
		modelPath,
		effectiveRequest,
		context,
		&m_session,
		settings.inferenceSettings,
		settings.sourceSettings,
		std::move(approvalCallback),
		std::move(eventCallback),
		std::move(onChunk));
	result.sessionRevision = result.assistantResult.sessionRevision;
	result.workspaceRoot = m_workspaceAssistant.resolveWorkspaceRoot(
		context,
		settings.workspaceSettings);
	result.verificationSuggested =
		!result.assistantResult.structured.verificationCommands.empty();

	if (!result.assistantResult.inference.success) {
		result.error = trimCopy(result.assistantResult.inference.error);
		result.summary = summarizeResult(result);
		return result;
	}

	if (result.readOnly || !settings.autoApply) {
		result.changedFiles = collectChangedFiles(
			result.assistantResult.structured,
			nullptr);
		result.success = true;
		result.summary = summarizeResult(result);
		return result;
	}

	ofxGgmlWorkspaceSettings workspaceSettings = settings.workspaceSettings;
	const auto effectiveAllowedFiles = workspaceSettings.allowedFiles.empty()
		? effectiveRequest.allowedFiles
		: workspaceSettings.allowedFiles;
	ofxGgmlWorkspaceTransaction transaction;
	const bool hasProposedChanges =
		structuredHasProposedChanges(result.assistantResult.structured) &&
		!trimCopy(result.workspaceRoot).empty();
	const bool patchToolApproved = isApprovedToolCall(
		result.assistantResult.proposedToolCalls,
		"apply_patch");
	const bool patchApplicationDenied =
		settings.autoApply &&
		structuredHasProposedChanges(result.assistantResult.structured) &&
		!patchToolApproved;
	if (hasProposedChanges && !patchApplicationDenied) {
		transaction = !trimCopy(result.assistantResult.structured.unifiedDiff).empty()
			? m_workspaceAssistant.beginUnifiedDiffTransaction(
				result.assistantResult.structured.unifiedDiff,
				result.workspaceRoot,
				effectiveAllowedFiles)
			: m_workspaceAssistant.beginTransaction(
				result.assistantResult.structured.patchOperations,
				result.workspaceRoot,
				effectiveAllowedFiles);
		result.changedFiles = collectChangedFiles(
			result.assistantResult.structured,
			&transaction);
		result.applyResult = m_workspaceAssistant.applyTransaction(
			transaction,
			workspaceSettings.dryRun);
		result.appliedChanges = result.applyResult.success &&
			!result.applyResult.touchedFiles.empty();
	} else {
		result.changedFiles = collectChangedFiles(
			result.assistantResult.structured,
			nullptr);
		result.applyResult.success = true;
		if (patchApplicationDenied) {
			result.applyResult.messages.push_back(
				"Patch application skipped because approval was denied for apply_patch.");
		} else if (trimCopy(result.workspaceRoot).empty() && structuredHasProposedChanges(result.assistantResult.structured)) {
			result.applyResult.success = false;
			result.error = "Coding agent could not resolve a workspace root for patch application.";
		}
	}

	if (!result.applyResult.success) {
		if (trimCopy(result.error).empty() &&
			!result.applyResult.messages.empty()) {
			result.error = result.applyResult.messages.front();
		}
		result.summary = summarizeResult(result);
		return result;
	}

	auto verificationCommands = result.assistantResult.structured.verificationCommands;
	if (settings.autoVerify &&
		verificationCommands.empty() &&
		settings.autoSuggestVerificationCommands &&
		!result.changedFiles.empty() &&
		!trimCopy(result.workspaceRoot).empty()) {
		ofxGgmlScriptSourceWorkspaceInfo workspaceInfoSnapshot;
		const auto * workspaceInfo = getWorkspaceInfoIfLocal(
			context,
			&workspaceInfoSnapshot);
		verificationCommands = m_workspaceAssistant.suggestVerificationCommands(
			result.changedFiles,
			result.workspaceRoot,
			workspaceInfo);
	}
	result.effectiveVerificationCommands = verificationCommands;

	const bool verificationToolApproved = isApprovedToolCall(
		result.assistantResult.proposedToolCalls,
		"run_verification");
	const bool verificationBlockedByPatchDenial =
		patchApplicationDenied && !verificationCommands.empty();
	if (settings.autoVerify && !verificationCommands.empty()) {
		if (!verificationToolApproved) {
			result.verificationResult.success = true;
			result.verificationResult.summary =
				"Verification skipped because approval was denied for run_verification.";
		} else if (verificationBlockedByPatchDenial) {
			result.verificationResult.success = true;
			result.verificationResult.summary =
				"Verification skipped because the proposed patch was not applied.";
		} else {
			result.verificationAttempted = true;
			result.verificationResult = m_workspaceAssistant.runVerification(
				verificationCommands,
				workspaceSettings,
				settings.commandRunner);
			if (!result.verificationResult.success &&
				workspaceSettings.rollbackOnVerificationFailure &&
				transaction.applied) {
				std::vector<std::string> rollbackMessages;
				(void)m_workspaceAssistant.rollbackTransaction(
					transaction,
					&rollbackMessages);
				result.applyResult.messages.insert(
					result.applyResult.messages.end(),
					rollbackMessages.begin(),
					rollbackMessages.end());
			}
		}
	} else {
		result.verificationResult.success = true;
		result.verificationResult.summary = verificationCommands.empty()
			? "No verification commands were provided."
			: "Verification skipped by settings.";
	}

	result.success = result.assistantResult.inference.success &&
		result.applyResult.success &&
		(!result.verificationAttempted || result.verificationResult.success);
	if (!result.success && trimCopy(result.error).empty()) {
		if (result.verificationAttempted &&
			!result.verificationResult.success) {
			result.error = trimCopy(result.verificationResult.summary);
		}
	}
	result.summary = summarizeResult(result);
	return result;
}

Result<ofxGgmlCodingAgentResult> ofxGgmlCodingAgent::runEx(
	const std::string & modelPath,
	const ofxGgmlCodingAgentRequest & request,
	const ofxGgmlCodeAssistantContext & context,
	const ofxGgmlCodingAgentSettings & settings,
	ofxGgmlCodeAssistantApprovalCallback approvalCallback,
	ofxGgmlCodeAssistantEventCallback eventCallback,
	std::function<bool(const std::string &)> onChunk) {
	const ofxGgmlCodingAgentResult result = run(
		modelPath,
		request,
		context,
		settings,
		std::move(approvalCallback),
		std::move(eventCallback),
		std::move(onChunk));
	if (result.success) {
		return result;
	}
	return ofxGgmlError(
		ofxGgmlErrorCode::ComputeFailed,
		trimCopy(result.error).empty()
			? trimCopy(result.summary)
			: trimCopy(result.error));
}
