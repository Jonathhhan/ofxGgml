#pragma once

#include "ofMain.h"
#include "ofxGgml.h"
#include "ofxImGui.h"

#include <atomic>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Mode — the active AI task tab.
// ---------------------------------------------------------------------------

enum class AiMode {
	Chat,
	Script,
	Summarize,
	Write,
	Translate,
	Custom
};

// ---------------------------------------------------------------------------
// Message — a single chat/output entry.
// ---------------------------------------------------------------------------

struct Message {
	std::string role;   // "user", "assistant", "system"
	std::string text;
	float timestamp = 0.0f;
};

// ---------------------------------------------------------------------------
// ModelPreset — a recommended model with download metadata.
// ---------------------------------------------------------------------------

struct ModelPreset {
	std::string name;         // e.g. "TinyLlama 1.1B Chat"
	std::string filename;     // e.g. "tinyllama-1.1b-chat-v1.0.Q4_0.gguf"
	std::string url;
	std::string description;
	std::string sizeMB;       // human-readable, e.g. "~600 MB"
	std::string bestFor;      // e.g. "chat, general"
};

// ---------------------------------------------------------------------------
// ScriptLanguage — language presets for the scripting panel.
// ---------------------------------------------------------------------------

struct ScriptLanguage {
	std::string name;          // e.g. "C++"
	std::string fileExt;       // e.g. ".cpp"
	std::string systemPrompt;  // language-specific system prompt prefix
};

// ---------------------------------------------------------------------------
// ScriptFileReviewInfo — precomputed data for hierarchical review.
// ---------------------------------------------------------------------------

struct ScriptFileReviewInfo {
	std::string name;
	std::string fullPath;
	std::string content;
	std::string truncatedContent;
	std::vector<std::string> dependencies;
	size_t loc = 0;
	size_t complexity = 0;
	size_t dependencyFanOut = 0;
	size_t dependencyFanIn = 0;
	float recencyScore = 0.0f;
	float importanceScore = 0.0f;
	float similarityScore = 0.0f;
	float priorityScore = 0.0f;
	int tokenCount = 0;
	bool truncated = false;
	bool selected = false;
	std::vector<float> embedding;
	std::string summary;
};

// ---------------------------------------------------------------------------
// PromptTemplate — predefined system prompt for the Custom panel.
// ---------------------------------------------------------------------------

struct PromptTemplate {
	std::string name;           // e.g. "Code Reviewer"
	std::string systemPrompt;   // the system prompt text
};

// ---------------------------------------------------------------------------
// ofApp — ofxGgml AI Studio with ofxImGui
// ---------------------------------------------------------------------------

class ofApp : public ofBaseApp {
public:
	void setup();
	void update();
	void draw();
	void exit();
	void keyPressed(int key);

private:
	// -- ggml engine --
	ofxGgml ggml;
	bool engineReady = false;
	std::string engineStatus;
	std::vector<ofxGgmlDeviceInfo> devices;

	// -- ImGui --
	ofxImGui::Gui gui;

	// -- mode --
	AiMode activeMode = AiMode::Chat;
	static constexpr int kModeCount = 6;
	static const char * modeLabels[kModeCount];

	// -- input buffers --
	char chatInput[4096] = {};
	char scriptInput[8192] = {};
	char summarizeInput[8192] = {};
	char writeInput[4096] = {};
	char translateInput[4096] = {};
	int translateSourceLang = 0;        // index into kTranslateLanguages
	int translateTargetLang = 1;        // index into kTranslateLanguages
	char customInput[4096] = {};
	char customSystemPrompt[2048] = {};

	// -- conversation / output --
	std::deque<Message> chatMessages;
	std::string scriptOutput;
	std::string summarizeOutput;
	std::string writeOutput;
	std::string translateOutput;
	std::string customOutput;

	// -- generation state --
	std::atomic<bool> generating{false};
	std::atomic<bool> cancelRequested{false};
	std::string generatingStatus;
	std::thread workerThread;
	std::mutex outputMutex;
	std::string pendingOutput;
	std::string pendingRole;
	AiMode pendingMode = AiMode::Chat;
	AiMode activeGenerationMode = AiMode::Chat;
	float generationStartTime = 0.0f;
	std::string streamingOutput;
	std::mutex streamMutex;

	// -- settings --
	int maxTokens = 256;
	float temperature = 0.7f;
	float topP = 0.9f;
	float repeatPenalty = 1.1f;
	int contextSize = 2048;
	int batchSize = 512;
	int gpuLayers = 0;
	int detectedModelLayers = 0;                     // auto-detected from GGUF metadata (0=unknown)
	int seed = -1;                                   // -1 = random
	int numThreads = 4;
	int selectedBackendIndex = 0;                    // direct index into backendNames
	std::vector<std::string> backendNames;           // raw ggml device names (populated at setup)
	int themeIndex = 0;                              // 0=Dark, 1=Light, 2=Classic
	int mirostatMode = 0;                            // 0=off, 1=Mirostat, 2=Mirostat 2.0
	float mirostatTau = 5.0f;
	float mirostatEta = 0.1f;
	bool showDeviceInfo = false;
	bool showLog = false;
	bool showPerformance = false;
	bool verbose = false;
	std::deque<std::string> logMessages;
	std::mutex logMutex;

	// -- performance tracking --
	float lastComputeMs = 0.0f;
	int lastNodeCount = 0;
	std::string lastBackendUsed;

	// -- model presets --
	std::vector<ModelPreset> modelPresets;
	int selectedModelIndex = 0;
	void initModelPresets();

	// -- script language presets --
	std::vector<ScriptLanguage> scriptLanguages;
	int selectedLanguageIndex = 0;
	void initScriptLanguages();

	// -- prompt templates (Custom panel) --
	std::vector<PromptTemplate> promptTemplates;
	int selectedPromptTemplateIndex = -1;
	void initPromptTemplates();

	// -- script source (local folder / GitHub) --
	ofxGgmlScriptSource scriptSource;
	char scriptSourceGitHub[512] = {};               // "owner/repo" input
	char scriptSourceBranch[128] = {};               // branch name, default "main"
	int selectedScriptFileIndex = -1;
	ofxGgmlProjectMemory scriptProjectMemory;
	std::string lastScriptRequest;
	ofxGgmlInference scriptReviewInference;
	ofxGgmlEmbeddingIndex scriptEmbeddingIndex;
	std::vector<ScriptFileReviewInfo> lastScriptReviewFiles;
	std::string lastScriptReviewStatus;

	std::string buildScriptFilename() const;

	// -- session persistence --
	std::string sessionDir;
	std::string lastSessionPath;
	bool saveSession(const std::string & path);
	bool loadSession(const std::string & path);
	void autoSaveSession();
	void autoLoadSession();
	std::string escapeSessionText(const std::string & text) const;
	std::string unescapeSessionText(const std::string & text) const;

	// -- graph execution helper --
	void reinitBackend();
	void syncSelectedBackendIndex();
	void runInference(AiMode mode, const std::string & userText,
		const std::string & systemPrompt = "");
	void runHierarchicalReview();
	bool runRealInference(const std::string & prompt, std::string & output, std::string & error,
		std::function<void(const std::string &)> onStreamData = nullptr);
	std::string buildPromptForMode(AiMode mode, const std::string & userText,
		const std::string & systemPrompt) const;
	std::string getSelectedModelPath() const;
	void detectModelLayers();
	void applyPendingOutput();
	void stopGeneration();

	// -- UI panels --
	void drawMenuBar();
	void drawSidebar();
	void drawMainPanel();
	void drawChatPanel();
	void drawScriptPanel();
	void drawScriptSourcePanel();
	void drawSummarizePanel();
	void drawWritePanel();
	void drawTranslatePanel();
	void drawCustomPanel();
	void drawStatusBar();
	void drawDeviceInfoWindow();
	void drawLogWindow();
	void drawPerformanceWindow();
	void applyTheme(int index);
	void copyToClipboard(const std::string & text);
	void exportChatHistory(const std::string & path);

	// -- hierarchical review helpers --
	std::vector<ScriptFileReviewInfo> collectScriptFilesForReview(std::string & status);
	void computeFileHeuristics(std::vector<ScriptFileReviewInfo> & files,
		const std::string & baseFolder);
	void computeDependencyFanIn(std::vector<ScriptFileReviewInfo> & files);
	int countTokensAccurate(const std::string & text, int fallback = -1);
	std::string buildRepoTableOfContents(const std::vector<ScriptFileReviewInfo> & files,
		size_t maxFiles) const;
	std::string buildRepoTree(const std::vector<ScriptFileReviewInfo> & files) const;
	std::vector<ScriptFileReviewInfo *> selectFilesForReview(
		std::vector<ScriptFileReviewInfo> & files,
		const std::string & reviewQuery,
		int availableTokens,
		int responseReserveTokens);
	std::string runFileSummary(const ScriptFileReviewInfo & info,
		const std::string & reviewQuery,
		int perFileBudget,
		const std::string & modelPath);
	std::string runArchitectureReview(
		const std::vector<ScriptFileReviewInfo *> & files,
		const std::string & repoTree,
		const std::string & reviewQuery,
		const std::string & modelPath);
	std::string runIntegrationReview(
		const std::vector<ScriptFileReviewInfo *> & files,
		const std::string & repoTree,
		const std::string & reviewQuery,
		const std::string & modelPath);
};
