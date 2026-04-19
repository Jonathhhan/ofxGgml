#pragma once

#include "assistants/ofxGgmlCodeAssistant.h"
#include "assistants/ofxGgmlWorkspaceAssistant.h"

#include <string>
#include <vector>

enum class ofxGgmlCodingAgentMode {
	Build = 0,
	Plan
};

struct ofxGgmlCodingAgentRequest {
	ofxGgmlCodeAssistantRequest assistantRequest;
	std::string taskLabel;
};

struct ofxGgmlCodingAgentSettings {
	ofxGgmlCodingAgentMode mode = ofxGgmlCodingAgentMode::Build;
	ofxGgmlWorkspaceSettings workspaceSettings;
	ofxGgmlInferenceSettings inferenceSettings;
	ofxGgmlPromptSourceSettings sourceSettings;
	ofxGgmlWorkspaceCommandRunner commandRunner = nullptr;
	bool autoApply = true;
	bool autoVerify = true;
	bool autoSuggestVerificationCommands = true;
	bool requireStructuredResult = true;
	bool preferUnifiedDiff = true;
};

struct ofxGgmlCodingAgentResult {
	bool success = false;
	bool readOnly = false;
	bool appliedChanges = false;
	bool verificationAttempted = false;
	bool verificationSuggested = false;
	std::string error;
	std::string workspaceRoot;
	std::string summary;
	ofxGgmlCodeAssistantResult assistantResult;
	ofxGgmlWorkspaceApplyResult applyResult;
	ofxGgmlWorkspaceVerificationResult verificationResult;
	std::vector<ofxGgmlCodeAssistantCommandSuggestion> effectiveVerificationCommands;
	std::vector<std::string> changedFiles;
	uint64_t sessionRevision = 0;
};

/// Small orchestration layer that turns the lower-level code and workspace
/// assistants into a reusable coding-agent surface with persistent session
/// memory, optional read-only planning mode, patch application, and
/// verification.
class ofxGgmlCodingAgent {
public:
	void setCompletionExecutable(const std::string & path);
	void setEmbeddingExecutable(const std::string & path);

	ofxGgmlCodeAssistant & getCodeAssistant();
	const ofxGgmlCodeAssistant & getCodeAssistant() const;
	ofxGgmlWorkspaceAssistant & getWorkspaceAssistant();
	const ofxGgmlWorkspaceAssistant & getWorkspaceAssistant() const;

	ofxGgmlCodeAssistantSession & getSession();
	const ofxGgmlCodeAssistantSession & getSession() const;
	void resetSession();

	ofxGgmlCodingAgentResult run(
		const std::string & modelPath,
		const ofxGgmlCodingAgentRequest & request,
		const ofxGgmlCodeAssistantContext & context = {},
		const ofxGgmlCodingAgentSettings & settings = {},
		ofxGgmlCodeAssistantApprovalCallback approvalCallback = nullptr,
		ofxGgmlCodeAssistantEventCallback eventCallback = nullptr,
		std::function<bool(const std::string &)> onChunk = nullptr);
	Result<ofxGgmlCodingAgentResult> runEx(
		const std::string & modelPath,
		const ofxGgmlCodingAgentRequest & request,
		const ofxGgmlCodeAssistantContext & context = {},
		const ofxGgmlCodingAgentSettings & settings = {},
		ofxGgmlCodeAssistantApprovalCallback approvalCallback = nullptr,
		ofxGgmlCodeAssistantEventCallback eventCallback = nullptr,
		std::function<bool(const std::string &)> onChunk = nullptr);

private:
	static std::vector<std::string> collectChangedFiles(
		const ofxGgmlCodeAssistantStructuredResult & structured,
		const ofxGgmlWorkspaceTransaction * transaction);
	static std::string summarizeResult(
		const ofxGgmlCodingAgentResult & result);

	ofxGgmlCodeAssistant m_codeAssistant;
	ofxGgmlWorkspaceAssistant m_workspaceAssistant;
	ofxGgmlCodeAssistantSession m_session;
};
