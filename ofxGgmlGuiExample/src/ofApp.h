#pragma once

#include "ofMain.h"
#include "ofxGgmlCore.h"
#include "ofxGgmlBasic.h"
#include "ofxGgmlModalities.h"
#include "ofxGgmlWorkflows.h"
#include "ofxGgmlAssistants.h"
#include "support/ofxGgmlEasy.h"
#include "ofxImGui.h"
#include "config/ModelPresets.h"
#include "panels/DeviceInfoPanel.h"
#include "panels/LogPanel.h"
#include "panels/PerformancePanel.h"
#include "panels/StatusBar.h"
#include "managers/TextServerManager.h"
#include "managers/SpeechServerManager.h"

#include <atomic>
#include <array>
#include <condition_variable>
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

// ---------------------------------------------------------------------------
// Message — a single chat/output entry.
// ---------------------------------------------------------------------------

struct Message {
	std::string role;   // "user", "assistant", "system"
	std::string text;
	float timestamp = 0.0f;
};

struct TtsPreviewRequestState {
	bool pending = false;

	void clear() {
		pending = false;
	}
};

struct TtsPreviewState {
	std::string statusMessage;
	TtsPreviewRequestState request;
	std::vector<ofxGgmlTtsAudioArtifact> audioFiles;
	int selectedAudioIndex = 0;
	std::string loadedAudioPath;
	bool playbackPaused = false;
	std::vector<float> playbackSamples;
	double playbackPositionFrames = 0.0;
	int playbackSampleRate = 0;
	int playbackChannels = 0;
	bool playbackLoaded = false;
	bool playbackActive = false;
	mutable std::mutex playbackMutex;

	bool isAudioLoaded() const {
		std::lock_guard<std::mutex> lock(playbackMutex);
		return playbackLoaded &&
			!loadedAudioPath.empty() &&
			playbackSampleRate > 0 &&
			playbackChannels > 0 &&
			!playbackSamples.empty();
	}

	bool isAudioPlaying() const {
		std::lock_guard<std::mutex> lock(playbackMutex);
		return playbackLoaded &&
			playbackActive &&
			!playbackPaused &&
			!playbackSamples.empty();
	}

	bool isPlaybackPaused() const {
		std::lock_guard<std::mutex> lock(playbackMutex);
		return playbackPaused;
	}

	void pausePlayback() {
		std::lock_guard<std::mutex> lock(playbackMutex);
		if (!playbackLoaded) {
			return;
		}
		playbackPaused = true;
		playbackActive = false;
	}

	void resumePlayback() {
		std::lock_guard<std::mutex> lock(playbackMutex);
		if (!playbackLoaded || playbackSamples.empty() || playbackChannels <= 0) {
			return;
		}
		const size_t totalFrames =
			playbackSamples.size() / static_cast<size_t>(playbackChannels);
		if (playbackPositionFrames >= static_cast<double>(totalFrames)) {
			playbackPositionFrames = 0.0;
		}
		playbackPaused = false;
		playbackActive = true;
	}

	void restartPlayback() {
		std::lock_guard<std::mutex> lock(playbackMutex);
		if (!playbackLoaded || playbackSamples.empty()) {
			return;
		}
		playbackPositionFrames = 0.0;
		playbackPaused = false;
		playbackActive = true;
	}

	void stopPlayback(bool clearLoadedPath = false) {
		std::lock_guard<std::mutex> lock(playbackMutex);
		playbackPaused = false;
		playbackActive = false;
		playbackPositionFrames = 0.0;
		if (clearLoadedPath) {
			loadedAudioPath.clear();
			playbackSamples.clear();
			playbackSampleRate = 0;
			playbackChannels = 0;
			playbackLoaded = false;
		}
	}

	void clearPreviewArtifacts() {
		statusMessage.clear();
		request.clear();
		audioFiles.clear();
		selectedAudioIndex = 0;
		stopPlayback(true);
	}
};

// ---------------------------------------------------------------------------
// ofApp — ofxGgml GUI example with ofxImGui
// ---------------------------------------------------------------------------

class ofApp : public ofBaseApp {
public:
	void setup();
	void update();
	void draw();
	void exit();
	void keyPressed(int key);
	void audioIn(ofSoundBuffer & input);
	void audioOut(ofSoundBuffer & output);

private:
	// -- ggml engine --
	ofxGgml ggml;
	ofxGgmlEasy easyApi;
	bool engineReady = false;
	std::string engineStatus;
	std::vector<ofxGgmlDeviceInfo> devices;
	bool deferredEngineInitPending = true;
	bool deferredPostInitPending = false;
	bool deferredAutoLoadSessionPending = false;
	bool deferredAutoLoadSessionArmed = false;

	// -- ImGui --
	ofxImGui::Gui gui;

	// -- UI Panels --
	DeviceInfoPanel deviceInfoPanel;
	LogPanel logPanel;
	PerformancePanel performancePanel;
	StatusBar statusBar;

	// -- Server Managers --
	TextServerManager textServerManager;
	SpeechServerManager speechServerManager;

	// -- mode --
	AiMode activeMode = AiMode::Chat;
	static constexpr int kModeCount = ::kModeCount;
	static const char * modeLabels[kModeCount];

	// -- input buffers --
	char chatInput[4096] = {};
	bool chatSpeakReplies = false;
	bool chatUseCustomTtsVoice = false;
	char chatTtsModelPath[1024] = {};
	char chatTtsSpeakerPath[1024] = {};
	bool chatUseUserTtsVoice = false;
	char chatUserTtsModelPath[1024] = {};
	char chatUserTtsSpeakerPath[1024] = {};
	bool summarizeSpeakOutput = false;
	char easyPrimaryInput[4096] = {};
	char easySecondaryInput[2048] = {};
	int easyActionIndex = 0;
	bool easyUseCrawler = false;
	int easyCitationCount = 100;
	float easyTargetDurationSeconds = 90.0f;
	char scriptInput[8192] = {};
	char scriptInlineInstruction[512] = {};
	int scriptAgentModeIndex = 0;
	int scriptAutocompleteSelectedIndex = 0;
	std::string scriptAutocompleteLastToken;
	char summarizeInput[8192] = {};
	char writeInput[4096] = {};
	char translateInput[4096] = {};
	int translateSourceLang = 0;
	int translateTargetLang = 1;
	bool translateUseCustomTtsVoice = false;
	char translateTtsModelPath[1024] = {};
	char translateTtsSpeakerPath[1024] = {};
	char voiceTranslatorAudioPath[1024] = {};
	bool voiceTranslatorSpeakOutput = true;
	char customInput[4096] = {};
	char customSystemPrompt[2048] = {};
	char sourceUrlsInput[2048] = {};
	char citationTopic[1024] = {};
	char citationSeedUrl[1024] = {};
	char textServerUrl[256] = "http://127.0.0.1:8080";
	char textServerModel[256] = {};
	char visionPrompt[4096] = {};
	char visionImagePath[1024] = {};
	char visionVideoPath[1024] = {};
	char holoscanVisionPrompt[2048] = {};
	char holoscanVisionSystemPrompt[2048] = {};
	char visionModelPath[1024] = {};
	char visionServerUrl[256] = "http://127.0.0.1:8080";
	char videoSidecarUrl[256] = {};
	char videoSidecarModel[256] = {};
	char visionSystemPrompt[1024] = {};
	char videoEditGoal[4096] = {};
	int visionTaskIndex = 0;
	int videoTaskIndex = 0;
	bool holoscanBridgeEnabled = false;
	bool holoscanBridgeRunning = false;
	bool holoscanUseCurrentVisionRequest = true;
	int holoscanVisionCompletedFrames = 0;
	int visionVideoMaxFrames = 6;
	char videoPlanJson[16384] = {};
	char videoEditPlanJson[16384] = {};
	int videoPlanBeatCount = 4;
	int videoPlanSceneCount = 3;
	int videoPlanGenerationMode = 0;
	int videoEditPresetIndex = 0;
	int videoEditClipCount = 5;
	int selectedVideoPlanSceneIndex = 0;
	bool videoPlanMultiScene = false;
	bool videoEditUseCurrentAnalysis = true;
	float videoPlanDurationSeconds = 5.0f;
	float videoEditTargetDurationSeconds = 15.0f;
	bool videoPlanUseForGeneration = true;
	int videoEditWorkflowActiveStepIndex = -1;
	std::vector<int> videoEditWorkflowCompletedStepIndices;
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
	bool samReturnMultipleMasks = true;
	bool speechServerManagedByApp = false;
	ServerStatusState speechServerStatus = ServerStatusState::Unknown;
	std::string speechServerStatusMessage;

	// -- conversation / output --
	std::deque<Message> chatMessages;
	std::string chatLastAssistantReply;
	TtsPreviewState chatTtsPreview;
	TtsPreviewState ttsPanelPreview;
	std::string easyOutput;
	std::string scriptOutput;
	std::string scriptInlineCompletionOutput;
	std::string scriptInlineCompletionTargetPath;
	std::deque<Message> scriptMessages;
	std::string summarizeOutput;
	TtsPreviewState summarizeTtsPreview;
	std::string writeOutput;
	std::string translateOutput;
	std::string voiceTranslatorStatus;
	std::string voiceTranslatorTranscript;
	TtsPreviewState translateTtsPreview;
	std::string customOutput;
	std::string citationOutput;
	std::string visionOutput;
	std::string holoscanVisionStatus;
	std::string holoscanVisionLatestOutput;
	std::string videoPlanSummary;
	std::string videoEditPlanSummary;
	std::string speechOutput;
	std::string ttsOutput;
	std::string diffusionOutput;
	ofImage visionPreviewImage;
	std::string visionPreviewImageLoadedPath;
	std::string visionPreviewImageError;
	ofVideoPlayer visionPreviewVideo;
	std::string visionPreviewVideoLoadedPath;
	std::string visionPreviewVideoError;
	bool visionPreviewVideoReady = false;
	ofImage visionOutputPreviewImage;
	std::string visionOutputPreviewLoadedPath;
	std::string visionOutputPreviewError;
	ofImage diffusionInitPreviewImage;
	std::string diffusionInitPreviewLoadedPath;
	std::string diffusionInitPreviewError;
	ofImage diffusionMaskPreviewImage;
	std::string diffusionMaskPreviewLoadedPath;
	std::string diffusionMaskPreviewError;
	ofImage diffusionOutputPreviewImage;
	std::string diffusionOutputPreviewLoadedPath;
	std::string diffusionOutputPreviewError;
	std::string deferredCitationTopic;
	bool hasDeferredCitationTopic = false;
	std::string deferredCitationSeedUrl;
	bool hasDeferredCitationSeedUrl = false;
	std::string deferredSpeechAudioPath;
	bool hasDeferredSpeechAudioPath = false;
	std::string deferredTranslateInput;
	bool hasDeferredTranslateInput = false;
	std::string deferredVoiceTranslatorAudioPath;
	bool hasDeferredVoiceTranslatorAudioPath = false;
	std::string deferredDiffusionPrompt;
	bool hasDeferredDiffusionPrompt = false;
	std::string deferredDiffusionOutputDir;
	bool hasDeferredDiffusionOutputDir = false;
#endif
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
	float clipElapsedMs = 0.0f;
	float samElapsedMs = 0.0f;
#endif
	std::string speechRecordedTempPath;
	bool speechRecording = false;
	float speechRecordingStartTime = 0.0f;
	bool speechLiveTranscriptionEnabled = false;
	int speechInputSampleRate = 16000;
	int speechInputChannels = 1;
	int speechInputBufferSize = 512;
	float speechLiveIntervalSeconds = 1.25f;
	float speechLiveWindowSeconds = 8.0f;
	float speechLiveOverlapSeconds = 0.75f;
	ofxGgmlLiveSpeechTranscriber speechLiveTranscriber;
	ofSoundStream speechInputStream;
	bool speechInputStreamConfigured = false;
	int speechInputStreamConfigSampleRate = 0;
	int speechInputStreamConfigChannels = 0;
	int speechInputStreamConfigBufferSize = 0;
	ofSoundStream ttsOutputStream;
	bool ttsOutputStreamConfigured = false;
	int ttsOutputSampleRate = 48000;
	int ttsOutputChannels = 2;
	int ttsOutputBufferSize = 512;
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
	std::string pendingScriptInlineCompletionOutput;
	std::string pendingScriptInlineCompletionTargetPath;
	ofxGgmlCodeAssistantSession pendingScriptAssistantSession;
	bool pendingScriptAssistantSessionDirty = false;
	std::vector<ofxGgmlCodeAssistantEvent> pendingScriptAssistantEvents;
	std::vector<ofxGgmlCodeAssistantToolCall> pendingScriptAssistantToolCalls;
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
	std::string pendingMusicToImagePromptOutput;
	std::string pendingMusicToImageStatus;
	bool pendingMusicToImageDirty = false;
	std::string pendingImageToMusicPromptOutput;
	std::string pendingImageToMusicNotationOutput;
	std::string pendingImageToMusicStatus;
	bool pendingImageToMusicDirty = false;
	std::string pendingCitationOutput;
	std::string pendingCitationBackendName;
	float pendingCitationElapsedMs = 0.0f;
	std::vector<ofxGgmlCitationItem> pendingCitationResults;
	bool pendingCitationDirty = false;
	std::string pendingVoiceTranslatorStatus;
	std::string pendingVoiceTranslatorTranscript;
	bool pendingVoiceTranslatorDirty = false;
	std::vector<ofxGgmlSampledVideoFrame> visionSampledVideoFrames;
	std::vector<ofxGgmlSampledVideoFrame> pendingVisionSampledVideoFrames;
	std::string pendingVideoPlanJson;
	std::string pendingVideoPlanSummary;
	std::string pendingVideoEditPlanJson;
	std::string pendingVideoEditPlanSummary;
	std::string pendingClipBackendName;
	float pendingClipElapsedMs = 0.0f;
	int pendingClipEmbeddingDimension = 0;
	std::vector<ofxGgmlClipSimilarityHit> pendingClipHits;
	std::string pendingSamBackendName;
	float pendingSamElapsedMs = 0.0f;
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
	std::array<int, kModeCount> modeMaxTokens = {512, 1024, 384, 512, 512, 512, 896, 896, 384, 512, 512, 512, 384, 640, 512, 384};
	std::array<int, kModeCount> modeTextBackendIndices = {1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0};
	TextInferenceBackend textInferenceBackend = TextInferenceBackend::LlamaServer;
	ServerStatusState textServerStatus = ServerStatusState::Unknown;
	std::string textServerStatusMessage;
	std::string textServerCapabilityHint;
	bool textServerManagedByApp = false;
	bool useModeTokenBudgets = true;
	bool autoContinueCutoff = false;
	bool usePromptCache = true;
	bool showDeviceInfo = false;
	bool showLog = false;
	bool showPerformance = false;
	bool showAdvancedGuiModes = false;
	ofLogLevel logLevel = OF_LOG_NOTICE;
	std::deque<std::string> logMessages;
	std::mutex logMutex;
	LiveContextMode liveContextMode = LiveContextMode::Offline;
	bool liveContextAllowPromptUrls = true;
	bool liveContextAllowDomainProviders = true;
	bool liveContextAllowGenericSearch = true;
	bool scriptSimpleUi = true;
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
	char customModelPath[1024] = {};
	std::vector<VideoRenderPreset> videoRenderPresets;
	int selectedVideoRenderPresetIndex = 0;
	int recommendedVideoRenderPresetIndex = 0;
	char customVideoRenderModelPath[1024] = {};
	std::array<int, kModeCount> taskDefaultModelIndices = {};
	mutable int cachedModelPathIndex = -1;
	mutable std::string cachedModelPath;
	mutable int cachedVideoRenderModelPathIndex = -1;
	mutable std::string cachedVideoRenderModelPath;

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
	std::string configuredClipBackendModelPath;
	int configuredClipBackendVerbosity = -1;
	bool configuredClipBackendNormalize = true;
	std::string configuredSamBackendModelPath;
	int configuredSamBackendThreads = -999;
	std::string citationBackendName;
	float citationElapsedMs = 0.0f;
	std::vector<ofxGgmlCitationItem> citationResults;
	bool citationUseCrawler = false;
	int citationMaxResults = 100;

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
	ofxGgmlCodingAgent scriptCodingAgent;
	ofxGgmlWorkspaceAssistant scriptWorkspaceAssistant;
	ofxGgmlTextAssistant textAssistant;
	ofxGgmlHoloscanBridge holoscanBridge;
	ofxGgmlVisionInference visionInference;
	ofxGgmlVideoInference videoInference;
	ofxGgmlSpeechInference speechInference;
	ofxGgmlTtsInference ttsInference;
	ofxGgmlDiffusionInference diffusionInference;
	ofxGgmlCitationSearch citationSearch;
#if OFXGGML_HAS_OFXSTABLEDIFFUSION
	std::shared_ptr<ofxStableDiffusion> stableDiffusionEngine;
#endif
	ofxGgmlInference llmInference;
	ofxGgmlCodeReview scriptCodeReview;
	ofxGgmlVideoPlanner videoPlanner;
	ofxGgmlProjectMemory scriptProjectMemory;
	std::string lastScriptRequest;
	std::vector<std::string> recentScriptTouchedFiles;
	std::string lastScriptFailureReason;
	ofxGgmlCodeAssistantSession scriptAssistantSession;
	std::vector<ofxGgmlCodeAssistantEvent> scriptAssistantEvents;
	std::vector<ofxGgmlCodeAssistantToolCall> scriptAssistantToolCalls;
	std::mutex scriptAssistantApprovalMutex;
	std::condition_variable scriptAssistantApprovalCv;
	bool scriptAssistantApprovalPending = false;
	bool scriptAssistantApprovalDecisionReady = false;
	bool scriptAssistantApprovalDecisionApproved = false;
	uint64_t scriptAssistantApprovalRequestId = 0;
	uint64_t scriptAssistantApprovalDecisionRequestId = 0;
	ofxGgmlCodeAssistantToolCall scriptAssistantPendingApprovalToolCall;
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
	struct ScriptPromptSnapshot {
		int selectedLanguageIndex = 0;
		int focusedFileIndex = -1;
		bool includeRepoContext = true;
		std::vector<ofxGgmlCodeLanguagePreset> languages;
		std::vector<std::string> recentTouchedFiles;
		std::string lastTask;
		std::string lastOutput;
		std::string lastFailureReason;
		std::string backendLabel;
	};
	struct InferenceModePromptSnapshot {
		int chatLanguageIndex = 0;
		int translateSourceLangIndex = 0;
		int translateTargetLangIndex = 1;
		std::vector<ofxGgmlChatLanguageOption> chatLanguages;
		std::vector<ofxGgmlTextLanguageOption> translateLanguages;
		ScriptPromptSnapshot script;
	};
	InferenceModePromptSnapshot makeInferenceModePromptSnapshot() const;
	ofxGgmlTextAssistantRequest buildTextAssistantRequestForMode(
		AiMode mode,
		const std::string & inputText,
		const std::string & systemPrompt,
		const InferenceModePromptSnapshot & snapshot) const;
	ofxGgmlCodeAssistantRequest buildScriptAssistantRequest(
		const std::string & inputText,
		const InferenceModePromptSnapshot & snapshot) const;
	ofxGgmlCodeAssistantContext buildScriptAssistantContext(
		const InferenceModePromptSnapshot & snapshot);
	std::string buildPromptForMode(
		AiMode mode,
		const std::string & inputText,
		const std::string & systemPrompt,
		const InferenceModePromptSnapshot & snapshot);
	std::string buildScriptContinuationPrompt(
		const std::string & partialOutput,
		const InferenceModePromptSnapshot & snapshot) const;
	void runPreparedTextRequest(
		AiMode mode,
		const ofxGgmlTextAssistantRequest & request,
		const ofxGgmlRealtimeInfoSettings & realtimeSettings = {});
	void setupHoloscanBridge();
	void drawHoloscanBridgeSection();
	void runEasyModeExample();
	void runVoiceTranslatorWorkflow(bool useAudioInput);
	void runScriptAssistantRequest(
		const ofxGgmlCodeAssistantRequest & request,
		const std::string & requestLabel,
		bool clearInputAfter = false,
		const ofxGgmlRealtimeInfoSettings & realtimeSettings = {},
		const ofxGgmlCodeAssistantContext * contextOverride = nullptr,
		bool forcePlanMode = false);
	void runScriptInlineCompletionRequest(
		const std::string & targetFilePath,
		const std::string & prefix,
		const std::string & suffix,
		const std::string & instruction);
	void runInference(AiMode mode, const std::string & userText,
		const std::string & systemPrompt = "",
		const std::string & overridePrompt = "",
		const ofxGgmlRealtimeInfoSettings & realtimeSettings = {});
	ofxGgmlRealtimeInfoSettings buildLiveContextSettings(
		const std::string & rawUrls,
		const std::string & heading,
		bool enableAutoLiveContext = false) const;
	void runHierarchicalReview(const std::string & overrideQuery = std::string());
	void applyScriptReviewPreset();
	ofxGgmlHoloscanVisionRequestTemplate makeHoloscanVisionRequestTemplate() const;
	void runVisionInference();
	void runVideoInference();
	void runVideoPlanning();
	void runVideoEditPlanning();
	void applyVideoEditPresetByIndex(int presetIndex);
	void resetVideoEditWorkflowState();
	bool isVideoEditWorkflowStepCompleted(int stepIndex) const;
	void setVideoEditWorkflowStepCompleted(int stepIndex, bool completed);
	void runSpeechInference();
	void runTtsInference();
	void runTtsInferenceForText(
		const std::string & text,
		const std::string & statusLabel = "Chat reply",
		bool mirrorIntoTtsInput = false,
		const std::string & modelPathOverride = std::string(),
		const std::string & speakerPathOverride = std::string());
	void speakLatestChatReply(bool mirrorIntoTtsInput = true);
	void speakLatestChatExchange(bool mirrorIntoTtsInput = false);
	void speakLatestSummary(bool mirrorIntoTtsInput = true);
	void speakTranslatedReply(bool mirrorIntoTtsInput = true);
	bool ensureTtsPanelAudioLoaded(int artifactIndex = -1, bool autoplay = false);
	void stopTtsPanelPlayback(bool clearLoadedPath = false);
	bool ensureChatTtsAudioLoaded(int artifactIndex = -1, bool autoplay = false);
	void stopChatTtsPlayback(bool clearLoadedPath = false);
	bool ensureSummaryTtsAudioLoaded(int artifactIndex = -1, bool autoplay = false);
	void stopSummaryTtsPlayback(bool clearLoadedPath = false);
	bool ensureTranslateTtsAudioLoaded(int artifactIndex = -1, bool autoplay = false);
	void stopTranslateTtsPlayback(bool clearLoadedPath = false);
	void runDiffusionInference();
	void runMusicToImagePromptGeneration();
	void runImageToMusicPromptGeneration();
	void runImageToMusicNotationGeneration();
	void runCitationSearch();
	void runClipInference();
		bool editExisting = false,
		bool generateVariants = false,
		bool repairExisting = false);
	void configureEasyApiFromCurrentUi();
	bool ensureClipBackendConfigured(
		const std::string & modelPath,
		int verbosity,
		bool normalizeEmbeddings);
	bool ensureDiffusionBackendConfigured();
	bool ensureSamBackendConfigured(const std::string & modelPath, int threads);
	static int clampSupportedDiffusionImageSize(int value);
	static constexpr float kDefaultInferenceTemp = 0.7f;
	static constexpr float kDefaultInferenceTopP = 0.9f;
	static constexpr float kDefaultInferenceRepeatPenalty = 1.1f;
	static constexpr size_t kDefaultMaxScriptContextFiles = 50;
	static constexpr size_t kDefaultMaxFocusedFileSnippetChars = 2000;
	ofxGgmlInferenceSettings buildCurrentTextInferenceSettings(AiMode mode) const;
	std::string getPreferredDiffusionReuseImagePath() const;
	void setDiffusionInitImagePath(const std::string & path, bool promoteTask = true);
	void copyDiffusionOutputsToClipPaths();
	ofxGgmlLiveSpeechSettings makeLiveSpeechSettings() const;
	void applyLiveSpeechTranscriberSettings();
	bool ensureSpeechInputStreamReady();
	bool ensureTtsOutputStreamReady();
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
	void stopGeneration(bool waitForCompletion = false);
	void reapFinishedWorkerThread();
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
	void updateTextServerStateFromResult(const TextServerEnsureResult & result);
	std::string findLocalTextServerExecutable(bool refresh = false);
	bool isManagedTextServerRunning();
	void startLocalTextServer();
	void stopLocalTextServer(bool logResult = true);
	std::string findLocalSpeechCliExecutable(bool refresh = false);
	std::string findLocalSpeechServerExecutable(bool refresh = false);
	bool isManagedSpeechServerRunning();
	void startLocalSpeechServer();
	void stopLocalSpeechServer(bool logResult = true);
	ofLogLevel mapGgmlLogLevel(int level) const;
	void probeLlamaCli(const std::string & customPath = "");
	void probeCliCapabilities();
	bool isLlamaCliReady() const;
	std::string getLlamaCliCommand() const;
	void killActiveInferenceProcess();
	std::string getSelectedVideoRenderModelPath() const;
	std::string getSelectedVideoRenderModelLabel() const;

	// -- UI panels --
	void drawMenuBar();
	void clearAllOutputs();
	void drawSidebar();
	void drawMainPanel();
	void drawChatPanel();
	void drawEasyPanel();
	void drawScriptPanel();
	void drawScriptSourcePanel();
	void drawSummarizePanel();
	void drawCitationSearchSection(
		const char * useInputButtonLabel,
		const std::string & suggestedTopic);
	void drawWritePanel();
	void drawTranslatePanel();
	void drawCustomPanel();
	void drawVisionPanel();
		const char * copyPromptButtonLabel,
		const std::string & suggestedPrompt);
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
		std::string * errorOut = nullptr) const;
#endif
	void ensureDiffusionPreviewResources();
	void drawDiffusionImagePreview(
		const char * label,
		const std::string & imagePath,
		ofImage & previewImage,
		const std::string & errorMessage,
		const char * childId);
		bool preferThumbnail,
		std::string & localPath,
		std::string & errorMessage) const;
	void drawSpeechPanel();
	void drawTtsPanel();
	void drawDiffusionPanel();
	void drawClipPanel();
	void drawSamPanel();
	void drawImageToMusicSection();
	bool saveImageToMusicNotationToConfiguredPath();
#endif
	bool ensureTtsProfilesLoaded();
	ofxGgmlTtsModelProfile getSelectedTtsProfile() const;
	void applyTtsProfileDefaults(
		const ofxGgmlTtsModelProfile & profile,
		bool onlyWhenEmpty);
	std::string resolveConfiguredTtsExecutable(
		const ofxGgmlTtsModelProfile & profile) const;
	std::shared_ptr<ofxGgmlTtsBackend> createConfiguredTtsBackend(
		const ofxGgmlTtsModelProfile & profile,
		const std::string & executableHint = "") const;
	void drawStatusBar();
	void drawDeviceInfoWindow();
	void drawLogWindow();
	void drawPerformanceWindow();
	void applyTheme(int index);
	void copyToClipboard(const std::string & text);
	void exportChatHistory(const std::string & path);

};
