#pragma once

#include "ofxGgmlCodeAssistant.h"

#include <functional>
#include <string>
#include <vector>

struct ofxGgmlWorkspaceSettings {
	std::string workspaceRoot;
	bool applyPatchOperations = true;
	bool runVerification = true;
	bool dryRun = false;
	int maxVerificationAttempts = 2;
	bool autoRetryWithAssistant = true;
	bool stopOnApplyError = true;
	bool stopOnFirstFailedCommand = true;
};

struct ofxGgmlWorkspaceApplyResult {
	bool success = true;
	std::vector<std::string> touchedFiles;
	std::vector<std::string> messages;
};

struct ofxGgmlWorkspaceCommandResult {
	ofxGgmlCodeAssistantCommandSuggestion command;
	bool success = false;
	int exitCode = -1;
	float elapsedMs = 0.0f;
	std::string output;
};

struct ofxGgmlWorkspaceVerificationResult {
	bool success = true;
	int attempts = 0;
	std::vector<ofxGgmlWorkspaceCommandResult> commandResults;
	std::string summary;
};

struct ofxGgmlWorkspaceResult {
	std::vector<ofxGgmlCodeAssistantResult> assistantAttempts;
	ofxGgmlWorkspaceApplyResult applyResult;
	ofxGgmlWorkspaceVerificationResult verificationResult;
	bool success = false;
};

using ofxGgmlWorkspaceCommandRunner = std::function<ofxGgmlWorkspaceCommandResult(
	const ofxGgmlCodeAssistantCommandSuggestion &)>;

using ofxGgmlWorkspaceRetryProvider = std::function<ofxGgmlCodeAssistantStructuredResult(
	const ofxGgmlWorkspaceVerificationResult &,
	int attempt)>;

/// Workspace-level coding helper that can apply structured patches and
/// run verification commands around ofxGgmlCodeAssistant results.
class ofxGgmlWorkspaceAssistant {
public:
	void setCompletionExecutable(const std::string & path);
	void setEmbeddingExecutable(const std::string & path);
	ofxGgmlCodeAssistant & getCodeAssistant();
	const ofxGgmlCodeAssistant & getCodeAssistant() const;

	std::string resolveWorkspaceRoot(
		const ofxGgmlCodeAssistantContext & context,
		const ofxGgmlWorkspaceSettings & settings) const;

	ofxGgmlWorkspaceApplyResult applyPatchOperations(
		const std::vector<ofxGgmlCodeAssistantPatchOperation> & operations,
		const std::string & workspaceRoot,
		bool dryRun = false) const;

	ofxGgmlWorkspaceVerificationResult runVerification(
		const std::vector<ofxGgmlCodeAssistantCommandSuggestion> & commands,
		const ofxGgmlWorkspaceSettings & settings = {},
		ofxGgmlWorkspaceCommandRunner commandRunner = nullptr) const;

	ofxGgmlWorkspaceResult runTask(
		const std::string & modelPath,
		const ofxGgmlCodeAssistantRequest & request,
		const ofxGgmlCodeAssistantContext & context = {},
		const ofxGgmlWorkspaceSettings & workspaceSettings = {},
		const ofxGgmlInferenceSettings & inferenceSettings = {},
		const ofxGgmlPromptSourceSettings & sourceSettings = {},
		ofxGgmlWorkspaceCommandRunner commandRunner = nullptr,
		ofxGgmlWorkspaceRetryProvider retryProvider = nullptr) const;

private:
	ofxGgmlCodeAssistant m_codeAssistant;
};
