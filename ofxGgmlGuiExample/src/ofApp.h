#pragma once

#include "ofMain.h"
#include "ofxImGui.h"
#include "ofxGgml.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
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
// ScriptSourceType — where script files come from.
// ---------------------------------------------------------------------------

enum class ScriptSourceType {
	None,
	LocalFolder,
	GitHubRepo
};

// ---------------------------------------------------------------------------
// ScriptFileEntry — a file discovered from local or GitHub source.
// ---------------------------------------------------------------------------

struct ScriptFileEntry {
	std::string name;         // file name
	std::string fullPath;     // local path (or URL for GitHub)
	bool isDirectory = false;
};

// ---------------------------------------------------------------------------
// CodeTemplate — quick-start code skeleton for each language.
// ---------------------------------------------------------------------------

struct CodeTemplate {
	std::string name;       // e.g. "Hello World"
	std::string code;       // skeleton source
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

	// -- settings --
	int maxTokens = 256;
	float temperature = 0.7f;
	float topP = 0.9f;
	float repeatPenalty = 1.1f;
	int contextSize = 2048;
	int batchSize = 512;
	int gpuLayers = 0;
	int seed = -1;                                   // -1 = random
	int numThreads = 4;
	int selectedBackendIndex = 0;                    // 0=Auto, 1=CPU, 2=GPU
	int themeIndex = 0;                              // 0=Dark, 1=Light, 2=Classic
	int mirostatMode = 0;                            // 0=off, 1=Mirostat, 2=Mirostat 2.0
	float mirostatTau = 5.0f;
	float mirostatEta = 0.1f;
	bool showDeviceInfo = false;
	bool showLog = false;
	bool showPerformance = false;
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

	// -- code templates --
	std::vector<std::vector<CodeTemplate>> codeTemplates;  // per-language
	int selectedTemplateIndex = -1;
	void initCodeTemplates();

	// -- prompt templates (Custom panel) --
	std::vector<PromptTemplate> promptTemplates;
	int selectedPromptTemplateIndex = -1;
	void initPromptTemplates();

	// -- script source (local folder / GitHub) --
	ScriptSourceType scriptSourceType = ScriptSourceType::None;
	std::string scriptSourcePath;                    // local folder path
	char scriptSourceGitHub[512] = {};               // "owner/repo" input
	char scriptSourceBranch[128] = {};               // branch name, default "main"
	std::vector<ScriptFileEntry> scriptSourceFiles;  // discovered files
	int selectedScriptFileIndex = -1;
	std::string scriptSourceStatus;
	bool showScriptSourcePanel = false;

	void scanLocalFolder(const std::string & path);
	void scanGitHubRepo(const std::string & ownerRepo, const std::string & branch);
	void loadScriptFile(int index);
	void saveScriptToSource(const std::string & filename, const std::string & content);
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
	void runInference(AiMode mode, const std::string & userText,
		const std::string & systemPrompt = "");
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

	// -- ggml demo computation --
	std::string runDemoComputation(const std::string & input, AiMode mode,
		const std::string & systemPrompt);
};
