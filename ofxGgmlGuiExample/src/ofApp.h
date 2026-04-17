#pragma once

#include "ofMain.h"
#include "ofxGgml.h"
#include "ofxImGui.h"
#include "config/ModelPresets.h"

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
// ofApp — ofxGgml AI Studio with ofxImGui
// ---------------------------------------------------------------------------

class ofApp : public ofBaseApp {
public:
	void setup();
	void update();
	void draw();
	void exit();
	void keyPressed(int key);
	void audioIn(ofSoundBuffer & input);

private:
	// -- ggml engine --
	ofxGgml ggml;
	bool engineReady = false;
	std::string engineStatus;
	std::vector<ofxGgmlDeviceInfo> devices;
	bool deferredEngineInitPending = true;
	bool deferredPostInitPending = false;
	bool deferredAutoLoadSessionPending = false;
	bool deferredAutoLoadSessionArmed = false;

	// -- ImGui --
	ofxImGui::Gui gui;

	// -- mode --
	AiMode activeMode = AiMode::Chat;
	static constexpr int kModeCount = 11;
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
	char visionVideoPath[1024] = {};
	char visionModelPath[1024] = {};
	char visionServerUrl[256] = "http://127.0.0.1:8080";
	char videoSidecarUrl[256] = {};
	char videoSidecarModel[256] = {};
	char visionSystemPrompt[1024] = {};
	int visionTaskIndex = 0;
	int videoTaskIndex = 0;
	int visionVideoMaxFrames = 6;
	char speechAudioPath[1024] = {};
	char speechExecutable[256] = "whisper-cli";
	char speechModelPath[1024] = {};
	char speechServerUrl[256] = {};
	char speechServerModel[128] = {};
	char speechPrompt[1024] = {};
	char speechLanguageHint[64] = "auto";
	int speechTaskIndex = 0;
	bool speechReturnTimestamps = false;
	char ttsInput[4096] = {};
	char ttsExecutablePath[1024] = {};
	char ttsModelPath[1024] = {};
	char ttsSpeakerPath[1024] = {};
	char ttsSpeakerReferencePath[1024] = {};
	char ttsOutputPath[1024] = {};
	char ttsPromptAudioPath[1024] = {};
	char ttsLanguage[64] = {};
	int ttsTaskIndex = 0;
	int ttsSeed = -1;
	int ttsMaxTokens = 0;
	float ttsTemperature = 0.4f;
	float ttsRepetitionPenalty = 1.1f;
	int ttsRepetitionRange = 64;
	int ttsTopK = 40;
	float ttsTopP = 0.9f;
	float ttsMinP = 0.05f;
	bool ttsStreamAudio = false;
	bool ttsNormalizeText = true;
	char diffusionPrompt[4096] = {};
	char diffusionInstruction[4096] = {};
	char diffusionNegativePrompt[4096] = {};
	char diffusionRankingPrompt[4096] = {};
	char diffusionModelPath[1024] = {};
	char diffusionVaePath[1024] = {};
	char diffusionInitImagePath[1024] = {};
	char diffusionMaskImagePath[1024] = {};
	char diffusionOutputDir[1024] = {};
	char diffusionOutputPrefix[128] = "diffusion";
	char diffusionSampler[64] = "euler_a";
	int diffusionTaskIndex = 0;
	int diffusionSelectionModeIndex = 0;
	int diffusionWidth = 1024;
	int diffusionHeight = 1024;
	int diffusionSteps = 20;
	int diffusionBatchCount = 1;
	int diffusionSeed = -1;
	float diffusionCfgScale = 7.0f;
	float diffusionStrength = 0.75f;
	bool diffusionNormalizeClipEmbeddings = true;
	bool diffusionSaveMetadata = true;
	char clipPrompt[4096] = {};
	char clipModelPath[1024] = {};
	char clipImagePaths[8192] = {};
	int clipTopK = 3;
	bool clipNormalizeEmbeddings = true;
	int clipVerbosity = 1;
	bool speechServerManagedByApp = false;
	ServerStatusState speechServerStatus = ServerStatusState::Unknown;
	std::string speechServerStatusMessage;
	std::string cachedSpeechCliExecutable;
	bool speechCliExecutableCached = false;
	std::string cachedSpeechServerExecutable;
	bool speechServerExecutableCached = false;
#ifdef _WIN32
	HANDLE speechServerProcessHandle = nullptr;
	DWORD speechServerProcessId = 0;
#else
	pid_t speechServerProcessId = 0;
#endif

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
	std::string ttsOutput;
	std::string diffusionOutput;
	std::string clipOutput;
	ofImage visionPreviewImage;
	std::string visionPreviewImageLoadedPath;
	std::string visionPreviewImageError;
	ofVideoPlayer visionPreviewVideo;
	std::string visionPreviewVideoLoadedPath;
	std::string visionPreviewVideoError;
	bool visionPreviewVideoReady = false;
	ofImage diffusionInitPreviewImage;
	std::string diffusionInitPreviewLoadedPath;
	std::string diffusionInitPreviewError;
	ofImage diffusionMaskPreviewImage;
	std::string diffusionMaskPreviewLoadedPath;
	std::string diffusionMaskPreviewError;
	ofImage diffusionOutputPreviewImage;
	std::string diffusionOutputPreviewLoadedPath;
	std::string diffusionOutputPreviewError;
	std::string speechDetectedLanguage;
	std::string speechTranscriptPath;
	std::string speechSrtPath;
	int speechSegmentCount = 0;
	std::string ttsBackendName;
	float ttsElapsedMs = 0.0f;
	std::string ttsResolvedSpeakerPath;
	std::vector<ofxGgmlTtsAudioArtifact> ttsAudioFiles;
	std::vector<std::pair<std::string, std::string>> ttsMetadata;
	std::string diffusionBackendName;
	float diffusionElapsedMs = 0.0f;
	std::vector<ofxGgmlGeneratedImage> diffusionGeneratedImages;
	std::vector<std::pair<std::string, std::string>> diffusionMetadata;
	std::string clipBackendName;
	float clipElapsedMs = 0.0f;
	int clipEmbeddingDimension = 0;
	std::vector<ofxGgmlClipSimilarityHit> clipHits;
	std::string speechRecordedTempPath;
	bool speechRecording = false;
	bool speechLiveTranscriptionEnabled = false;
	int speechInputSampleRate = 16000;
	int speechInputChannels = 1;
	int speechInputBufferSize = 512;
	float speechLiveIntervalSeconds = 1.25f;
	float speechLiveWindowSeconds = 8.0f;
	float speechLiveOverlapSeconds = 0.75f;
	ofxGgmlLiveSpeechTranscriber speechLiveTranscriber;
	ofSoundStream speechInputStream;
	std::vector<float> speechRecordedSamples;
	std::mutex speechRecordMutex;

	// -- generation state --
	std::atomic<bool> generating{false};
	std::atomic<bool> cancelRequested{false};
	std::string generatingStatus;
	std::thread workerThread;
	std::mutex outputMutex;
	std::string pendingOutput;
	std::string pendingRole;
	AiMode pendingMode = AiMode::Chat;
	std::string pendingSpeechDetectedLanguage;
	std::string pendingSpeechTranscriptPath;
	std::string pendingSpeechSrtPath;
	int pendingSpeechSegmentCount = 0;
	std::string pendingTtsBackendName;
	float pendingTtsElapsedMs = 0.0f;
	std::string pendingTtsResolvedSpeakerPath;
	std::vector<ofxGgmlTtsAudioArtifact> pendingTtsAudioFiles;
	std::vector<std::pair<std::string, std::string>> pendingTtsMetadata;
	std::string pendingDiffusionBackendName;
	float pendingDiffusionElapsedMs = 0.0f;
	std::vector<ofxGgmlGeneratedImage> pendingDiffusionImages;
	std::vector<std::pair<std::string, std::string>> pendingDiffusionMetadata;
	std::string pendingClipBackendName;
	float pendingClipElapsedMs = 0.0f;
	int pendingClipEmbeddingDimension = 0;
	std::vector<ofxGgmlClipSimilarityHit> pendingClipHits;
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
	std::array<int, kModeCount> modeMaxTokens = {512, 1024, 384, 512, 512, 512, 384, 512, 512, 512, 384};
	std::array<int, kModeCount> modeTextBackendIndices = {1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
	TextInferenceBackend textInferenceBackend = TextInferenceBackend::LlamaServer;
	ServerStatusState textServerStatus = ServerStatusState::Unknown;
	std::string textServerStatusMessage;
	std::string textServerCapabilityHint;
	bool textServerManagedByApp = false;
	std::string cachedTextServerExecutable;
	bool textServerExecutableCached = false;
	bool deferredTextServerWarmupPending = false;
	float deferredTextServerWarmupDeadline = 0.0f;
	float deferredTextServerWarmupNextProbeTime = 0.0f;
	std::string deferredTextServerWarmupUrl;
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

	// -- script language presets --
	std::vector<ofxGgmlCodeLanguagePreset> scriptLanguages;
	int selectedLanguageIndex = 0;

	// -- prompt templates (Custom panel) --
	std::vector<ofxGgmlChatLanguageOption> chatLanguages;
	std::vector<ofxGgmlTextLanguageOption> translateLanguages;
	std::vector<PromptTemplate> promptTemplates;
	int selectedPromptTemplateIndex = -1;
	std::vector<ofxGgmlVisionModelProfile> visionProfiles;
	int selectedVisionProfileIndex = 0;
	std::vector<ofxGgmlSpeechModelProfile> speechProfiles;
	int selectedSpeechProfileIndex = 0;
	std::vector<ofxGgmlTtsModelProfile> ttsProfiles;
	int selectedTtsProfileIndex = 0;
	std::vector<ofxGgmlImageGenerationModelProfile> diffusionProfiles;
	int selectedDiffusionProfileIndex = 0;

	// -- script source (local folder / GitHub) --
	ofxGgmlScriptSource scriptSource;
	char scriptSourceGitHub[512] = {};               // "owner/repo" input
	char scriptSourceBranch[128] = {};               // branch name, default "main"
	char scriptSourceGitHubToken[512] = {};          // optional token override (not persisted)
	char scriptSourceInternetUrl[1024] = {};         // internet URL input
	int selectedScriptFileIndex = -1;
	bool deferredScriptSourceRestorePending = false;
	ofxGgmlScriptSourceType deferredScriptSourceType = ofxGgmlScriptSourceType::None;
	std::string deferredScriptSourcePath;
	std::string deferredScriptSourceInternetUrls;
	ofxGgmlChatAssistant chatAssistant;
	ofxGgmlCodeAssistant scriptAssistant;
	ofxGgmlWorkspaceAssistant scriptWorkspaceAssistant;
	ofxGgmlTextAssistant textAssistant;
	ofxGgmlVisionInference visionInference;
	ofxGgmlVideoInference videoInference;
	ofxGgmlSpeechInference speechInference;
	ofxGgmlTtsInference ttsInference;
	ofxGgmlDiffusionInference diffusionInference;
	ofxGgmlClipInference clipInference;
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
	void clearDeferredScriptSourceRestore();
	bool restoreDeferredScriptSourceIfNeeded();
	std::string escapeSessionText(const std::string & text) const;
	std::string unescapeSessionText(const std::string & text) const;

	// -- graph execution helper --
	void initializeBackendEngine(bool announceReinit = false);
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
	void runHierarchicalReview(const std::string & overrideQuery = std::string());
	void runVisionInference();
	void runVideoInference();
	void runSpeechInference();
	void runTtsInference();
	void runDiffusionInference();
	void runClipInference();
	ofxGgmlLiveSpeechSettings makeLiveSpeechSettings() const;
	void applyLiveSpeechTranscriberSettings();
	bool startSpeechRecording();
	void stopSpeechRecording(bool keepBufferedAudio = true);
	std::string flushSpeechRecordingToTempWav();
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
	void applyServerFriendlyDefaultsForMode(AiMode mode);
	void syncTextBackendForActiveMode(bool announce = false, bool allowBlockingEnsure = true);
	void scheduleDeferredTextServerWarmup(const std::string & configuredUrl);
	void updateDeferredTextServerWarmup();
	void checkTextServerStatus(bool logResult = true);
	bool ensureTextServerReady(bool logResult = false, bool allowLaunch = true);
	bool ensureLlamaServerReadyForModel(
		const std::string & configuredUrl,
		const std::string & modelPath,
		bool logResult = false,
		bool allowLaunch = true,
		bool allowMmproj = false);
	std::string findLocalTextServerExecutable(bool refresh = false);
	bool isManagedTextServerRunning();
	void startLocalTextServer();
	void startLocalLlamaServerForModel(
		const std::string & configuredUrl,
		const std::string & modelPath,
		bool allowMmproj = false);
	void stopLocalTextServer(bool logResult = true);
	std::string findLocalSpeechCliExecutable(bool refresh = false);
	std::string findLocalSpeechServerExecutable(bool refresh = false);
	bool isManagedSpeechServerRunning();
	void startLocalSpeechServer();
	void stopLocalSpeechServer(bool logResult = true);
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
	void ensureVisionPreviewResources();
	void ensureLocalImagePreview(
		const std::string & imagePath,
		ofImage & previewImage,
		std::string & loadedPath,
		std::string & errorMessage);
	void drawLocalImagePreview(
		const char * label,
		const std::string & imagePath,
		ofImage & previewImage,
		const std::string & errorMessage,
		const char * childId);
	void drawVisionImagePreview(const std::string & imagePath);
	void drawVisionVideoPreview(const std::string & videoPath);
	void drawMediaTexturePreview(const ofBaseHasTexture & previewTexture, const char * childId);
	void ensureDiffusionPreviewResources();
	void drawDiffusionImagePreview(
		const char * label,
		const std::string & imagePath,
		ofImage & previewImage,
		const std::string & errorMessage,
		const char * childId);
	void drawSpeechPanel();
	void drawTtsPanel();
	void drawDiffusionPanel();
	void drawClipPanel();
	bool ensureTtsProfilesLoaded();
	ofxGgmlTtsModelProfile getSelectedTtsProfile() const;
	void applyTtsProfileDefaults(
		const ofxGgmlTtsModelProfile & profile,
		bool onlyWhenEmpty);
	std::string resolveConfiguredTtsExecutable() const;
	std::shared_ptr<ofxGgmlTtsBackend> createConfiguredTtsBackend(
		const std::string & executableHint = "") const;
	void drawStatusBar();
	void drawDeviceInfoWindow();
	void drawLogWindow();
	void drawPerformanceWindow();
	void applyTheme(int index);
	void copyToClipboard(const std::string & text);
	void exportChatHistory(const std::string & path);

};
