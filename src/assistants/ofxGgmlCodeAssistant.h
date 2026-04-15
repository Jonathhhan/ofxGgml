#pragma once

#include "inference/ofxGgmlInference.h"
#include "support/ofxGgmlProjectMemory.h"
#include "support/ofxGgmlScriptSource.h"

#include <functional>
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

struct ofxGgmlCodeAssistantReviewFinding {
	int priority = 2;
	float confidence = 0.0f;
	std::string filePath;
	int line = 0;
	std::string title;
	std::string description;
	std::string fixSuggestion;
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
	std::vector<ofxGgmlCodeAssistantFileIntent> filesToTouch;
	std::vector<ofxGgmlCodeAssistantPatchOperation> patchOperations;
	std::string unifiedDiff;
	std::vector<ofxGgmlCodeAssistantCommandSuggestion> verificationCommands;
	std::vector<ofxGgmlCodeAssistantReviewFinding> reviewFindings;
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
	std::string buildErrors;
	bool preservePublicApi = false;
	bool updateTests = false;
	bool forbidNewDependencies = false;
	bool requestStructuredResult = false;
	bool requestUnifiedDiff = false;
	ofxGgmlCodeAssistantSymbolQuery symbolQuery;
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
	bool requestsStructuredResult = false;
	bool requestedUnifiedDiff = false;
	std::vector<ofxGgmlCodeAssistantSymbol> retrievedSymbols;
	ofxGgmlCodeAssistantSymbolContext retrievedSymbolContext;
};

struct ofxGgmlCodeAssistantResult {
	ofxGgmlCodeAssistantPreparedPrompt prepared;
	ofxGgmlInferenceResult inference;
	ofxGgmlCodeAssistantStructuredResult structured;
};

/// Higher-level coding helper built on top of ofxGgmlInference,
/// ofxGgmlScriptSource, and ofxGgmlProjectMemory.
class ofxGgmlCodeAssistant {
public:
	void setCompletionExecutable(const std::string & path);
	void setEmbeddingExecutable(const std::string & path);
	ofxGgmlInference & getInference();
	const ofxGgmlInference & getInference() const;

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

	std::vector<ofxGgmlCodeAssistantSymbol> retrieveSymbols(
		const std::string & query,
		const ofxGgmlCodeAssistantContext & context) const;
	ofxGgmlCodeAssistantSemanticIndex buildSemanticIndex(
		const ofxGgmlCodeAssistantContext & context) const;
	ofxGgmlCodeAssistantSymbolContext buildSymbolContext(
		const ofxGgmlCodeAssistantSymbolQuery & query,
		const ofxGgmlCodeAssistantContext & context) const;
	ofxGgmlCodeAssistantInlineCompletionPreparedPrompt prepareInlineCompletion(
		const ofxGgmlCodeAssistantInlineCompletionRequest & request) const;
	ofxGgmlCodeAssistantInlineCompletionResult runInlineCompletion(
		const std::string & modelPath,
		const ofxGgmlCodeAssistantInlineCompletionRequest & request,
		const ofxGgmlInferenceSettings & inferenceSettings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const;

	static std::vector<ofxGgmlCodeLanguagePreset> defaultLanguagePresets();
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

private:
	ofxGgmlInference m_inference;
};
