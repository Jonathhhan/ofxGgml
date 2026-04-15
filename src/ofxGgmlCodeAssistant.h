#pragma once

#include "ofxGgmlInference.h"
#include "ofxGgmlProjectMemory.h"
#include "ofxGgmlScriptSource.h"

#include <functional>
#include <string>
#include <vector>

enum class ofxGgmlCodeAssistantAction {
	Ask = 0,
	Generate,
	Explain,
	Debug,
	Optimize,
	Refactor,
	Review,
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

struct ofxGgmlCodeAssistantSymbolReference {
	std::string filePath;
	int line = 0;
	std::string preview;
};

struct ofxGgmlCodeAssistantSymbol {
	std::string name;
	std::string kind;
	std::string filePath;
	int line = 0;
	std::string signature;
	std::string preview;
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
	std::vector<ofxGgmlCodeAssistantCommandSuggestion> verificationCommands;
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
	bool requestStructuredResult = false;
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
	std::vector<ofxGgmlCodeAssistantSymbol> retrievedSymbols;
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

private:
	ofxGgmlInference m_inference;
};
