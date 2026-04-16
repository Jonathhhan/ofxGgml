#pragma once

#include "ofMain.h"
#include "ofxGgml.h"
#include "ofxImGui.h"

#include <atomic>
#include <array>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#endif

// ---------------------------------------------------------------------------
// Mode — the active AI task tab.
// ---------------------------------------------------------------------------

enum class AiMode {
	Chat,
	Script,
	Summarize,
	Write,
	Translate,
	Custom,
	Vision,
	Speech
};

enum class LiveContextMode {
	Offline = 0,
	LoadedSourcesOnly,
	LiveContext,
	LiveContextStrictCitations
};

enum class TextInferenceBackend {
	Cli = 0,
	LlamaServer
};

enum class ServerStatusState {
	Unknown = 0,
	Reachable,
	Unreachable
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
	static constexpr int kModeCount = 8;
	static const char * modeLabels[kModeCount];

	// -- input buffers --
	char chatInput[4096] = {};
	char scriptInput[8192] = {};
	char summarizeInput[8192] = {};
	char writeInput[4096] = {};
	char translateInput[4096] = {};
	int translateSourceLang = 0;
	int translateTargetLang = 1;
	char customInput[4096] = {};
	char customSystemPrompt[2048] = {};
	char sourceUrlsInput[2048] = {};
	char textServerUrl[256] = "http://127.0.0.1:8080";
	char textServerModel[256] = {};
	char visionPrompt[4096] = {};
	char visionImagePath[1024] = {};
	char visionModelPath[1024] = {};
	char visionServerUrl[256] = "http://127.0.0.1:8080";
	char visionSystemPrompt[1024] = {};
	int visionTaskIndex = 0;
	char speechAudioPath[1024] = {};
	char speechExecutable[256] = "whisper-cli";
	char speechModelPath[1024] = {};
	char speechPrompt[1024] = {};
	char speechLanguageHint[64] = "auto";
	int speechTaskIndex = 0;
	bool speechReturnTimestamps = false;

	// -- conversation / output --
	std::deque<Message> chatMessages;
	std::string scriptOutput;
	std::deque<Message> scriptMessages;
	std::string summarizeOutput;
	std::string writeOutput;
	std::string translateOutput;
	std::string customOutput;
	std::string visionOutput;
	std::string speechOutput;

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
	bool lastScriptOutputLikelyCutoff = false;
	std::string lastScriptOutputTail;

	// -- settings --
	int maxTokens = 256;
	float temperature = 0.7f;
	float topP = 0.9f;
	int topK = 40;
	float minP = 0.0f;
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
	int chatLanguageIndex = 0;                       // 0=Auto, otherwise force response language
	std::array<int, kModeCount> modeMaxTokens = {512, 1024, 384, 512, 512, 512, 384, 512};
	std::array<int, kModeCount> modeTextBackendIndices = {1, 1, 0, 0, 0, 1, 0, 0};
	TextInferenceBackend textInferenceBackend = TextInferenceBackend::Cli;
	ServerStatusState textServerStatus = ServerStatusState::Unknown;
	std::string textServerStatusMessage;
	std::string textServerCapabilityHint;
	bool textServerManagedByApp = false;
#ifdef _WIN32
	HANDLE textServerProcessHandle = nullptr;
	DWORD textServerProcessId = 0;
#else
	pid_t textServerProcessId = 0;
#endif
	bool useModeTokenBudgets = true;
	bool autoContinueCutoff = false;
	bool usePromptCache = true;
	bool showDeviceInfo = false;
	bool showLog = false;
	bool showPerformance = false;
	ofLogLevel logLevel = OF_LOG_NOTICE;
	std::deque<std::string> logMessages;
	std::mutex logMutex;
	LiveContextMode liveContextMode = LiveContextMode::Offline;
	bool liveContextAllowPromptUrls = true;
	bool liveContextAllowDomainProviders = true;
	bool liveContextAllowGenericSearch = true;
	bool scriptIncludeRepoContext = true;
	bool stopAtNaturalBoundary = true;
	bool cliCapabilitiesProbed = false;
	bool cliSupportsTopK = true;
	bool cliSupportsMinP = true;
	bool cliSupportsMirostat = true;
	bool cliSupportsSingleTurn = true;
	std::unordered_map<std::string, int> tokenCountCache;
	std::mutex tokenCountCacheMutex;

	// -- performance tracking --
	float lastComputeMs = 0.0f;
	int lastNodeCount = 0;
	std::string lastBackendUsed;

	// -- model presets --
	std::vector<ModelPreset> modelPresets;
	int selectedModelIndex = 0;
	std::array<int, kModeCount> taskDefaultModelIndices = {};
	mutable int cachedModelPathIndex = -1;
	mutable std::string cachedModelPath;
	void initModelPresets();

	// -- script language presets --
	std::vector<ofxGgmlCodeLanguagePreset> scriptLanguages;
	int selectedLanguageIndex = 0;
	void initScriptLanguages();

	// -- prompt templates (Custom panel) --
	std::vector<ofxGgmlChatLanguageOption> chatLanguages;
	std::vector<ofxGgmlTextLanguageOption> translateLanguages;
	std::vector<PromptTemplate> promptTemplates;
	int selectedPromptTemplateIndex = -1;
	std::vector<ofxGgmlVisionModelProfile> visionProfiles;
	int selectedVisionProfileIndex = 0;
	std::vector<ofxGgmlSpeechModelProfile> speechProfiles;
	int selectedSpeechProfileIndex = 0;
	void initPromptTemplates();

	// -- script source (local folder / GitHub) --
	ofxGgmlScriptSource scriptSource;
	char scriptSourceGitHub[512] = {};               // "owner/repo" input
	char scriptSourceBranch[128] = {};               // branch name, default "main"
	char scriptSourceGitHubToken[512] = {};          // optional token override (not persisted)
	char scriptSourceInternetUrl[1024] = {};         // internet URL input
	int selectedScriptFileIndex = -1;
	ofxGgmlChatAssistant chatAssistant;
	ofxGgmlCodeAssistant scriptAssistant;
	ofxGgmlWorkspaceAssistant scriptWorkspaceAssistant;
	ofxGgmlTextAssistant textAssistant;
	ofxGgmlVisionInference visionInference;
	ofxGgmlSpeechInference speechInference;
	ofxGgmlInference llmInference;
	ofxGgmlCodeReview scriptCodeReview;
	ofxGgmlProjectMemory scriptProjectMemory;
	std::string lastScriptRequest;
	std::vector<ofxGgmlCodeAssistantCommandSuggestion> cachedScriptVerificationCommands;
	uint64_t cachedScriptVerificationGeneration = 0;
	std::string cachedScriptVerificationRoot;

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
		const std::string & systemPrompt = "",
		const std::string & overridePrompt = "",
		const ofxGgmlRealtimeInfoSettings & realtimeSettings = {});
	ofxGgmlRealtimeInfoSettings buildLiveContextSettings(
		const std::string & rawUrls,
		const std::string & heading,
		bool enableAutoLiveContext = false) const;
	void runHierarchicalReview();
	void runVisionInference();
	void runSpeechInference();
	bool runRealInference(AiMode mode, const std::string & prompt, std::string & output, std::string & error,
		std::function<void(const std::string &)> onStreamData = nullptr,
		bool preserveLlamaInstructions = false,
		bool suppressFallbackWarning = false);
	std::string getSelectedModelPath() const;
	void detectModelLayers();
	void applyPendingOutput();
	void stopGeneration();
	void applyLogLevel(ofLogLevel level);
	bool shouldLog(ofLogLevel level) const;
	void logWithLevel(ofLogLevel level, const std::string & message);
	void announceTextBackendChange();
	TextInferenceBackend preferredTextBackendForMode(AiMode mode) const;
	void rememberTextBackendForMode(AiMode mode, TextInferenceBackend backend);
	void syncTextBackendForActiveMode(bool announce = false);
	void checkTextServerStatus(bool logResult = true);
	std::string findLocalTextServerExecutable() const;
	bool isManagedTextServerRunning();
	void startLocalTextServer();
	void stopLocalTextServer(bool logResult = true);
	ofLogLevel mapGgmlLogLevel(int level) const;
	void probeLlamaCli(const std::string & customPath = "");
	void probeCliCapabilities();

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
	void drawVisionPanel();
	void drawSpeechPanel();
	void drawStatusBar();
	void drawDeviceInfoWindow();
	void drawLogWindow();
	void drawPerformanceWindow();
	void applyTheme(int index);
	void copyToClipboard(const std::string & text);
	void exportChatHistory(const std::string & path);

};
