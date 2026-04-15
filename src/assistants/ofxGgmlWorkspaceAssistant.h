#pragma once

#include "assistants/ofxGgmlCodeAssistant.h"

#include <functional>
#include <string>
#include <vector>

struct ofxGgmlWorkspaceSettings {
	std::string workspaceRoot;
	std::vector<std::string> allowedFiles;
	bool applyPatchOperations = true;
	bool runVerification = true;
	bool validatePatchesBeforeApply = true;
	bool rollbackOnVerificationFailure = false;
	bool autoSelectVerificationCommands = true;
	bool useShadowWorkspace = false;
	bool keepShadowWorkspace = false;
	bool syncShadowChangesOnSuccess = true;
	std::string shadowWorkspaceRoot;
	bool dryRun = false;
	int maxVerificationAttempts = 2;
	bool autoRetryWithAssistant = true;
	bool stopOnApplyError = true;
	bool stopOnFirstFailedCommand = true;
};

struct ofxGgmlWorkspacePatchValidationResult {
	bool success = true;
	std::vector<std::string> messages;
	std::vector<std::string> validatedFiles;
};

struct ofxGgmlWorkspaceUnifiedDiffLine {
	char kind = ' ';
	std::string text;
};

struct ofxGgmlWorkspaceUnifiedDiffHunk {
	int oldStart = 0;
	int oldCount = 0;
	int newStart = 0;
	int newCount = 0;
	std::vector<ofxGgmlWorkspaceUnifiedDiffLine> lines;
};

struct ofxGgmlWorkspaceUnifiedDiffFile {
	std::string oldPath;
	std::string newPath;
	std::string normalizedPath;
	bool isNewFile = false;
	bool isDeleteFile = false;
	std::vector<ofxGgmlWorkspaceUnifiedDiffHunk> hunks;
};

struct ofxGgmlWorkspaceBackupEntry {
	std::string filePath;
	bool existedBefore = false;
	std::string originalContent;
};

struct ofxGgmlWorkspaceTransaction {
	std::string workspaceRoot;
	std::vector<ofxGgmlCodeAssistantPatchOperation> operations;
	std::string unifiedDiff;
	std::vector<ofxGgmlWorkspaceUnifiedDiffFile> parsedDiffFiles;
	std::vector<ofxGgmlWorkspaceBackupEntry> backups;
	ofxGgmlWorkspacePatchValidationResult validationResult;
	std::string unifiedDiffPreview;
	bool usesUnifiedDiff = false;
	bool applied = false;
	bool rolledBack = false;
};

struct ofxGgmlWorkspaceApplyResult {
	bool success = true;
	std::vector<std::string> touchedFiles;
	std::vector<std::string> messages;
	std::string unifiedDiffPreview;
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
	std::string originalWorkspaceRoot;
	std::string executionWorkspaceRoot;
	std::string shadowWorkspaceRoot;
	std::vector<std::string> synchronizedFiles;
	bool usedShadowWorkspace = false;
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
		const std::vector<std::string> & allowedFiles = {},
		bool dryRun = false) const;
	ofxGgmlWorkspacePatchValidationResult validatePatchOperations(
		const std::vector<ofxGgmlCodeAssistantPatchOperation> & operations,
		const std::string & workspaceRoot,
		const std::vector<std::string> & allowedFiles = {}) const;
	ofxGgmlWorkspacePatchValidationResult validateUnifiedDiff(
		const std::string & unifiedDiff,
		const std::string & workspaceRoot,
		const std::vector<std::string> & allowedFiles = {}) const;
	ofxGgmlWorkspaceTransaction beginTransaction(
		const std::vector<ofxGgmlCodeAssistantPatchOperation> & operations,
		const std::string & workspaceRoot,
		const std::vector<std::string> & allowedFiles = {}) const;
	ofxGgmlWorkspaceTransaction beginUnifiedDiffTransaction(
		const std::string & unifiedDiff,
		const std::string & workspaceRoot,
		const std::vector<std::string> & allowedFiles = {}) const;
	ofxGgmlWorkspaceApplyResult applyTransaction(
		ofxGgmlWorkspaceTransaction & transaction,
		bool dryRun = false) const;
	bool rollbackTransaction(
		ofxGgmlWorkspaceTransaction & transaction,
		std::vector<std::string> * messages = nullptr) const;
	ofxGgmlWorkspaceApplyResult applyUnifiedDiff(
		const std::string & unifiedDiff,
		const std::string & workspaceRoot,
		const std::vector<std::string> & allowedFiles = {},
		bool dryRun = false) const;

	ofxGgmlWorkspaceVerificationResult runVerification(
		const std::vector<ofxGgmlCodeAssistantCommandSuggestion> & commands,
		const ofxGgmlWorkspaceSettings & settings = {},
		ofxGgmlWorkspaceCommandRunner commandRunner = nullptr) const;
	std::vector<ofxGgmlCodeAssistantCommandSuggestion> suggestVerificationCommands(
		const std::vector<std::string> & changedFiles,
		const std::string & workspaceRoot,
		const ofxGgmlScriptSourceWorkspaceInfo * workspaceInfo = nullptr) const;
	std::string createShadowWorkspace(
		const std::string & workspaceRoot,
		const std::string & preferredRoot = {}) const;
	bool synchronizeShadowWorkspace(
		const std::string & shadowWorkspaceRoot,
		const std::string & workspaceRoot,
		const std::vector<std::string> & touchedFiles,
		std::vector<std::string> * messages = nullptr) const;

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
