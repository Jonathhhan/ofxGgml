#pragma once

#include "inference/ofxGgmlInference.h"
#include "support/ofxGgmlProjectMemory.h"
#include "support/ofxGgmlScriptSource.h"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

enum class ofxGgmlCodeAssistantAction {
	Ask = 0,
	Generate,
	Explain,
	Debug,
	Optimize,
	Edit,
	Refactor,
	Review,
	NextEdit,
	SummarizeChanges,
	FixBuild,
	GroundedDocs,
	ContinueTask,
	Shorter,
	MoreDetail,
	ContinueCutoff
};

enum class ofxGgmlCodeAssistantPatchKind {
	WriteFile = 0,
	ReplaceTextOp,
	AppendText,
	DeleteFileOp
};

enum class ofxGgmlCodeAssistantToolCategory {
	Context = 0,
	Retrieval,
	Grounding,
	Patching,
	Verification,
	Analysis
};

enum class ofxGgmlCodeAssistantEventKind {
	SessionStarted = 0,
	PromptPrepared,
	OutputChunk,
	StructuredResultReady,
	ToolProposed,
	ApprovalRequested,
	ApprovalGranted,
	ApprovalDenied,
	Completed,
	Error
};

struct ofxGgmlCodeLanguagePreset {
	std::string name;
	std::string fileExtension;
	std::string systemPrompt;
};

struct ofxGgmlCodeAssistantSourceRange {
	int startLine = 0;
	int startColumn = 0;
	int endLine = 0;
	int endColumn = 0;
};

struct ofxGgmlCodeAssistantSymbolReference {
	std::string kind;
	std::string filePath;
	int line = 0;
	std::string preview;
	std::string callerSymbol;
	std::string targetSymbol;
	ofxGgmlCodeAssistantSourceRange range;
};

struct ofxGgmlCodeAssistantSymbol {
	std::string name;
	std::string kind;
	std::string filePath;
	int line = 0;
	std::string signature;
	std::string preview;
	std::string qualifiedName;
	std::string containerName;
	std::string semanticBackend;
	bool isDefinition = true;
	ofxGgmlCodeAssistantSourceRange range;
	float score = 0.0f;
	std::vector<ofxGgmlCodeAssistantSymbolReference> references;
};

struct ofxGgmlCodeAssistantFileIntent {
	std::string filePath;
	std::string reason;
	std::vector<std::string> symbols;
};

struct ofxGgmlCodeAssistantPatchOperation {
	ofxGgmlCodeAssistantPatchKind kind =
		ofxGgmlCodeAssistantPatchKind::WriteFile;
	std::string filePath;
	std::string summary;
	std::string searchText;
	std::string replacementText;
	std::string content;
};

struct ofxGgmlCodeAssistantToolDefinition {
	std::string name;
	std::string description;
	ofxGgmlCodeAssistantToolCategory category =
		ofxGgmlCodeAssistantToolCategory::Analysis;
	bool requiresApproval = false;
	bool enabledByDefault = true;
};

struct ofxGgmlCodeAssistantToolCall {
	std::string toolName;
	std::string summary;
	std::string payload;
	ofxGgmlCodeAssistantToolCategory category =
		ofxGgmlCodeAssistantToolCategory::Analysis;
	bool requiresApproval = false;
	bool approved = true;
};

struct ofxGgmlCodeAssistantSession {
	std::string activeMode;
	std::string selectedBackend;
	std::string focusedFilePath;
	std::vector<std::string> recentTouchedFiles;
	std::string lastFailureReason;
	std::vector<std::string> recentPrompts;
	std::vector<std::string> recentSummaries;
	size_t maxHistoryEntries = 8;
	uint64_t revision = 0;
};

struct ofxGgmlCodeAssistantEvent {
	ofxGgmlCodeAssistantEventKind kind =
		ofxGgmlCodeAssistantEventKind::PromptPrepared;
	std::string requestLabel;
	std::string message;
	std::string chunkText;
	ofxGgmlCodeAssistantToolCall toolCall;
	uint64_t sessionRevision = 0;
};

struct ofxGgmlCodeAssistantReviewFinding {
	int priority = 2;
	float confidence = 0.0f;
	std::string filePath;
	int line = 0;
	std::string title;
	std::string description;
	std::string fixSuggestion;
	std::string category;
	std::string reviewerPersona;
};

struct ofxGgmlCodeAssistantTestSuggestion {
	std::string name;
	std::string filePath;
	std::string rationale;
	std::string commandLabel;
	std::string commandTag;
	int priority = 2;
};

struct ofxGgmlCodeAssistantRiskAssessment {
	float score = 0.0f;
	std::string level;
	std::vector<std::string> reasons;
};

struct ofxGgmlCodeAssistantReviewerSimulation {
	std::string persona;
	std::vector<ofxGgmlCodeAssistantReviewFinding> findings;
};

struct ofxGgmlCodeAssistantSymbolQuery {
	std::string query;
	std::vector<std::string> targetSymbols;
	bool includeDefinitions = true;
	bool includeReferences = true;
	bool includeCallers = false;
	size_t maxDefinitions = 4;
	size_t maxReferences = 8;
};

struct ofxGgmlCodeAssistantSymbolContext {
	std::string query;
	std::vector<ofxGgmlCodeAssistantSymbol> definitions;
	std::vector<ofxGgmlCodeAssistantSymbolReference> relatedReferences;
	bool includesCallers = false;
};

struct ofxGgmlCodeAssistantSemanticIndex {
	std::string backendName;
	std::string compilationDatabasePath;
	bool hasCompilationDatabase = false;
	std::vector<ofxGgmlCodeAssistantSymbol> symbols;
	std::vector<ofxGgmlCodeAssistantSymbolReference> callers;
};

struct ofxGgmlCodeAssistantCodeMapEntry {
	std::string scope;
	std::string role;
	std::vector<std::string> files;
	std::vector<std::string> topSymbols;
	int symbolCount = 0;
};

struct ofxGgmlCodeAssistantCodeMap {
	std::string workspaceRoot;
	std::string backendName;
	uint64_t workspaceGeneration = 0;
	int totalFiles = 0;
	int totalSymbols = 0;
	std::vector<ofxGgmlCodeAssistantCodeMapEntry> entries;
};

struct ofxGgmlCodeAssistantBuildError {
	std::string filePath;
	int line = 0;
	int column = 0;
	std::string code;
	std::string message;
	std::string rawLine;
};

struct ofxGgmlCodeAssistantCommandSuggestion {
	std::string label;
	std::string workingDirectory;
	std::string executable;
	std::vector<std::string> arguments;
	std::string expectedOutcome;
	bool retryOnFailure = false;
};

struct ofxGgmlCodeAssistantStructuredResult {
	bool detectedStructuredOutput = false;
	std::string goalSummary;
	std::string approachSummary;
	std::vector<std::string> steps;
	std::vector<std::string> acceptanceCriteria;
	std::vector<ofxGgmlCodeAssistantFileIntent> filesToTouch;
	std::vector<ofxGgmlCodeAssistantPatchOperation> patchOperations;
	std::string unifiedDiff;
	std::vector<ofxGgmlCodeAssistantCommandSuggestion> verificationCommands;
	std::vector<ofxGgmlCodeAssistantReviewFinding> reviewFindings;
	std::vector<ofxGgmlCodeAssistantTestSuggestion> testSuggestions;
	std::vector<ofxGgmlCodeAssistantReviewerSimulation> reviewerSimulations;
	ofxGgmlCodeAssistantRiskAssessment riskAssessment;
	std::vector<std::string> risks;
	std::vector<std::string> questions;
};

struct ofxGgmlCodeAssistantContext {
	ofxGgmlScriptSource * scriptSource = nullptr;
	ofxGgmlProjectMemory * projectMemory = nullptr;
	int focusedFileIndex = -1;
	bool includeRepoContext = true;
	bool includeSymbolContext = true;
	bool attachScriptSourceDocuments = false;
	size_t maxRepoFiles = 50;
	size_t maxFocusedFileChars = 2000;
	size_t maxSymbols = 8;
	size_t maxSymbolReferences = 4;
	std::string activeMode;
	std::string selectedBackend;
	std::vector<std::string> recentTouchedFiles;
	std::string lastFailureReason;
	std::string projectMemoryHeading =
		"Project memory from previous coding requests:";
};

struct ofxGgmlCodeAssistantRequest {
	ofxGgmlCodeAssistantAction action = ofxGgmlCodeAssistantAction::Ask;
	ofxGgmlCodeLanguagePreset language;
	std::string userInput;
	std::string lastTask;
	std::string lastOutput;
	std::string bodyOverride;
	std::string labelOverride;
	std::vector<std::string> allowedFiles;
	std::vector<std::string> webUrls;
	std::vector<std::string> acceptanceCriteria;
	std::vector<std::string> constraints;
	std::string buildErrors;
	bool preservePublicApi = false;
	bool updateTests = false;
	bool forbidNewDependencies = false;
	bool specToCodeMode = false;
	bool synthesizeTests = false;
	bool simulateReviewers = false;
	bool includeCodeMap = false;
	bool requestStructuredResult = false;
	bool requestUnifiedDiff = false;
	bool preferGroundedEdits = true;
	bool runSelfCheck = true;
	ofxGgmlCodeAssistantSymbolQuery symbolQuery;
};

struct ofxGgmlCodeAssistantSpecToCodeRequest {
	ofxGgmlCodeLanguagePreset language;
	std::string specification;
	std::vector<std::string> acceptanceCriteria;
	std::vector<std::string> constraints;
	std::vector<std::string> allowedFiles;
	bool preservePublicApi = false;
	bool updateTests = true;
	bool forbidNewDependencies = true;
	bool requestUnifiedDiff = true;
};

struct ofxGgmlCodeAssistantInlineCompletionRequest {
	ofxGgmlCodeLanguagePreset language;
	std::string filePath;
	std::string prefix;
	std::string suffix;
	std::string instruction;
	int maxTokens = 128;
	bool singleLine = false;
	bool useFillInTheMiddle = true;
};

struct ofxGgmlCodeAssistantInlineCompletionPreparedPrompt {
	std::string prompt;
	std::string label;
};

struct ofxGgmlCodeAssistantInlineCompletionResult {
	ofxGgmlCodeAssistantInlineCompletionPreparedPrompt prepared;
	ofxGgmlInferenceResult inference;
	std::string completion;
};

struct ofxGgmlCodeAssistantPreparedPrompt {
	std::string prompt;
	std::string body;
	std::string requestLabel;
	std::string focusedFileName;
	bool includedRepoContext = false;
	bool includedFocusedFile = false;
	bool includedSymbolContext = false;
	bool includedCodeMap = false;
	bool requestsStructuredResult = false;
	bool requestedUnifiedDiff = false;
	bool includedTaskMemory = false;
	std::vector<ofxGgmlCodeAssistantSymbol> retrievedSymbols;
	ofxGgmlCodeAssistantSymbolContext retrievedSymbolContext;
	ofxGgmlCodeAssistantCodeMap codeMap;
};

struct ofxGgmlCodeAssistantResult {
	ofxGgmlCodeAssistantPreparedPrompt prepared;
	ofxGgmlInferenceResult inference;
	ofxGgmlCodeAssistantStructuredResult structured;
	std::vector<ofxGgmlCodeAssistantToolCall> proposedToolCalls;
	uint64_t sessionRevision = 0;
};

using ofxGgmlCodeAssistantApprovalCallback =
	std::function<bool(const ofxGgmlCodeAssistantToolCall &)>;
using ofxGgmlCodeAssistantEventCallback =
	std::function<bool(const ofxGgmlCodeAssistantEvent &)>;

/// Higher-level coding helper built on top of ofxGgmlInference,
/// ofxGgmlScriptSource, and ofxGgmlProjectMemory.
class ofxGgmlCodeAssistant {
public:
	void setCompletionExecutable(const std::string & path);
	void setEmbeddingExecutable(const std::string & path);
	ofxGgmlInference & getInference();
	const ofxGgmlInference & getInference() const;
	void registerTool(const ofxGgmlCodeAssistantToolDefinition & tool);
	void resetToolRegistry();
	std::vector<ofxGgmlCodeAssistantToolDefinition> getToolRegistry() const;

	ofxGgmlCodeAssistantPreparedPrompt preparePrompt(
		const ofxGgmlCodeAssistantRequest & request,
		const ofxGgmlCodeAssistantContext & context) const;

	ofxGgmlCodeAssistantResult run(
		const std::string & modelPath,
		const ofxGgmlCodeAssistantRequest & request,
		const ofxGgmlCodeAssistantContext & context = {},
		const ofxGgmlInferenceSettings & inferenceSettings = {},
		const ofxGgmlPromptSourceSettings & sourceSettings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const;
	ofxGgmlCodeAssistantResult runWithSession(
		const std::string & modelPath,
		const ofxGgmlCodeAssistantRequest & request,
		const ofxGgmlCodeAssistantContext & context = {},
		ofxGgmlCodeAssistantSession * session = nullptr,
		const ofxGgmlInferenceSettings & inferenceSettings = {},
		const ofxGgmlPromptSourceSettings & sourceSettings = {},
		ofxGgmlCodeAssistantApprovalCallback approvalCallback = nullptr,
		ofxGgmlCodeAssistantEventCallback eventCallback = nullptr,
		std::function<bool(const std::string &)> onChunk = nullptr) const;
	ofxGgmlCodeAssistantResult runSpecToCode(
		const std::string & modelPath,
		const ofxGgmlCodeAssistantSpecToCodeRequest & request,
		const ofxGgmlCodeAssistantContext & context = {},
		const ofxGgmlInferenceSettings & inferenceSettings = {},
		const ofxGgmlPromptSourceSettings & sourceSettings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const;

	std::vector<ofxGgmlCodeAssistantSymbol> retrieveSymbols(
		const std::string & query,
		const ofxGgmlCodeAssistantContext & context) const;
	ofxGgmlCodeAssistantSemanticIndex buildSemanticIndex(
		const ofxGgmlCodeAssistantContext & context) const;
	ofxGgmlCodeAssistantCodeMap buildCodeMap(
		const ofxGgmlCodeAssistantContext & context) const;
	ofxGgmlCodeAssistantSymbolContext buildSymbolContext(
		const ofxGgmlCodeAssistantSymbolQuery & query,
		const ofxGgmlCodeAssistantContext & context) const;
	std::vector<ofxGgmlCodeAssistantTestSuggestion> synthesizeTests(
		const ofxGgmlCodeAssistantRequest & request,
		const ofxGgmlCodeAssistantStructuredResult & structured,
		const ofxGgmlCodeAssistantContext & context) const;
	std::vector<ofxGgmlCodeAssistantReviewerSimulation> simulateReviewerPasses(
		const ofxGgmlCodeAssistantRequest & request,
		const ofxGgmlCodeAssistantStructuredResult & structured,
		const ofxGgmlCodeAssistantContext & context) const;
	ofxGgmlCodeAssistantRiskAssessment assessRisk(
		const ofxGgmlCodeAssistantRequest & request,
		const ofxGgmlCodeAssistantStructuredResult & structured,
		const ofxGgmlCodeAssistantContext & context) const;
	ofxGgmlCodeAssistantInlineCompletionPreparedPrompt prepareInlineCompletion(
		const ofxGgmlCodeAssistantInlineCompletionRequest & request) const;
	ofxGgmlCodeAssistantInlineCompletionResult runInlineCompletion(
		const std::string & modelPath,
		const ofxGgmlCodeAssistantInlineCompletionRequest & request,
		const ofxGgmlInferenceSettings & inferenceSettings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const;

	static std::vector<ofxGgmlCodeLanguagePreset> defaultLanguagePresets();
	static std::vector<ofxGgmlCodeAssistantToolDefinition> defaultToolRegistry();
	static std::string defaultActionBody(
		ofxGgmlCodeAssistantAction action,
		const std::string & userInput,
		bool hasFocusedFile,
		const std::string & lastTask = {},
		const std::string & lastOutput = {});
	static std::string defaultActionLabel(
		ofxGgmlCodeAssistantAction action,
		const std::string & userInput = {},
		const std::string & focusedFileName = {});
	static std::vector<ofxGgmlCodeAssistantSymbol> extractSymbols(
		const std::string & text,
		const std::string & filePath = {});
	static ofxGgmlCodeAssistantStructuredResult parseStructuredResult(
		const std::string & text);
	static std::string buildStructuredResponseInstructions();
	static std::string buildUnifiedDiffFromStructuredResult(
		const ofxGgmlCodeAssistantStructuredResult & structured);
	static std::vector<ofxGgmlCodeAssistantBuildError> parseBuildErrors(
		const std::string & text);
	static void seedContextFromSession(
		ofxGgmlCodeAssistantContext * context,
		const ofxGgmlCodeAssistantSession & session);
	static void updateSessionFromResult(
		ofxGgmlCodeAssistantSession * session,
		const ofxGgmlCodeAssistantRequest & request,
		const ofxGgmlCodeAssistantContext & context,
		const ofxGgmlCodeAssistantResult & result);

private:
	ofxGgmlInference m_inference;
	mutable std::mutex m_semanticCacheMutex;
	mutable std::mutex m_codeMapCacheMutex;
	mutable std::mutex m_repoInstructionCacheMutex;
	mutable std::mutex m_toolRegistryMutex;
	mutable std::string m_cachedWorkspaceRoot;
	mutable uint64_t m_cachedWorkspaceGeneration = 0;
	mutable ofxGgmlCodeAssistantSemanticIndex m_cachedSemanticIndex;
	mutable std::string m_cachedCodeMapWorkspaceRoot;
	mutable uint64_t m_cachedCodeMapWorkspaceGeneration = 0;
	mutable ofxGgmlCodeAssistantCodeMap m_cachedCodeMap;
	mutable std::string m_cachedInstructionWorkspaceRoot;
	mutable uint64_t m_cachedInstructionWorkspaceGeneration = 0;
	mutable std::string m_cachedInstructionTargetPath;
	mutable bool m_cachedInstructionIncludePathSpecific = false;
	mutable bool m_cachedRepoInstructionContextValid = false;
	mutable std::string m_cachedRepoInstructionContext;
	std::vector<ofxGgmlCodeAssistantToolDefinition> m_toolRegistry;
};
