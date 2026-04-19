#pragma once

#include "ofMain.h"
#include "ofxGgml.h"
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

#if defined(__has_include)
#if __has_include("ofxVlc4.h")
#define OFXGGML_HAS_OFXVLC4 1
#include "ofxVlc4.h"
#else
#define OFXGGML_HAS_OFXVLC4 0
#endif
#else
#define OFXGGML_HAS_OFXVLC4 0
#endif

#if defined(__has_include)
#if __has_include("ofxProjectM.h")
#define OFXGGML_HAS_OFXPROJECTM 1
#include "ofxProjectM.h"
#else
#define OFXGGML_HAS_OFXPROJECTM 0
#endif
#else
#define OFXGGML_HAS_OFXPROJECTM 0
#endif

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
	ofSoundPlayer player;

	void clearPreviewArtifacts() {
		statusMessage.clear();
		request.clear();
		audioFiles.clear();
		selectedAudioIndex = 0;
		loadedAudioPath.clear();
		playbackPaused = false;
	}
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
	char scriptInput[8192] = {};
	char scriptInlineInstruction[512] = {};
	int scriptAutocompleteSelectedIndex = 0;
	std::string scriptAutocompleteLastToken;
	char summarizeInput[8192] = {};
	char writeInput[4096] = {};
	char translateInput[4096] = {};
	int translateSourceLang = 0;
	int translateTargetLang = 1;
	char voiceTranslatorAudioPath[1024] = {};
	bool voiceTranslatorSpeakOutput = true;
	char videoEssayTopic[1024] = {};
	char videoEssaySeedUrl[1024] = {};
	bool videoEssayUseCrawler = false;
	bool videoEssayIncludeCounterpoints = true;
	int videoEssayCitationCount = 5;
	float videoEssayTargetDurationSeconds = 90.0f;
	int videoEssayToneIndex = 0;
	int videoEssayAudienceIndex = 0;
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
	char imageSearchPrompt[1024] = {};
	char visionModelPath[1024] = {};
	char visionServerUrl[256] = "http://127.0.0.1:8080";
	char videoSidecarUrl[256] = {};
	char videoSidecarModel[256] = {};
	char visionSystemPrompt[1024] = {};
	char montageSubtitlePath[1024] = {};
	char montageGoal[4096] = {};
	char montageEdlTitle[128] = "MONTAGE";
	char montageReelName[32] = "AX";
	char videoEditGoal[4096] = {};
	int visionTaskIndex = 0;
	int videoTaskIndex = 0;
	int visionVideoMaxFrames = 6;
	char videoPlanJson[16384] = {};
	char videoEditPlanJson[16384] = {};
	int montageMaxClips = 8;
	int montageFps = 25;
	int videoPlanBeatCount = 4;
	int videoPlanSceneCount = 3;
	int videoPlanGenerationMode = 0;
	int videoEditPresetIndex = 0;
	int videoEditClipCount = 5;
	int selectedVideoPlanSceneIndex = 0;
	float montageMinScore = 0.18f;
	float montageTargetDurationSeconds = 12.0f;
	float montageMinSpacingSeconds = 1.5f;
	float montagePreRollSeconds = 0.25f;
	float montagePostRollSeconds = 0.35f;
	bool videoPlanMultiScene = false;
	bool montagePreserveChronology = true;
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
	int imageSearchMaxResults = 8;
	char clipPrompt[4096] = {};
	char clipModelPath[1024] = {};
	char clipImagePaths[8192] = {};
	int clipTopK = 3;
	bool clipNormalizeEmbeddings = true;
	int clipVerbosity = 1;
	char musicToImageDescription[4096] = {};
	char musicToImageLyrics[4096] = {};
	char musicToImageStyle[1024] = "cinematic still, richly lit, highly detailed";
	bool musicToImageIncludeLyrics = true;
	char imageToMusicDescription[4096] = {};
	char imageToMusicSceneNotes[2048] = {};
	char imageToMusicStyle[1024] = "cinematic instrumental soundtrack, expressive, high fidelity";
	char imageToMusicInstrumentation[512] = "analog synth bass, glassy pads, restrained percussion";
	char imageToMusicAbcTitle[256] = "Generated Theme";
	char imageToMusicAbcKey[64] = "Cm";
	char imageToMusicAbcOutputPath[1024] = {};
	int imageToMusicDurationSeconds = 30;
	int imageToMusicAbcBars = 16;
	bool imageToMusicInstrumentalOnly = true;
	char aceStepServerUrl[256] = "http://127.0.0.1:8085";
	char aceStepPrompt[4096] = {};
	char aceStepLyrics[4096] = {};
	char aceStepAudioPath[1024] = {};
	char aceStepOutputDir[1024] = {};
	char aceStepOutputPrefix[128] = "acestep";
	char aceStepKeyscale[64] = {};
	char aceStepTimesignature[64] = "4";
	int aceStepBpm = 0;
	int aceStepDurationSeconds = 30;
	int aceStepSeed = -1;
	bool aceStepUseWav = true;
	bool aceStepInstrumentalOnly = false;
	int musicVideoSectionCount = 4;
	int musicVideoStructureIndex = 0;
	float musicVideoCutIntensity = 0.7f;
	char milkdropPrompt[4096] = {};
	char milkdropPresetPath[1024] = {};
	int milkdropCategoryIndex = 0;
	int milkdropVariantCount = 3;
	float milkdropRandomness = 0.55f;
	bool milkdropAutoPreview = true;
	float milkdropPreviewBeatSensitivity = 1.25f;
	float milkdropPreviewPresetDuration = 24.0f;
	bool milkdropPreviewFeedMicWhileRecording = true;
	bool speechServerManagedByApp = false;
	ServerStatusState speechServerStatus = ServerStatusState::Unknown;
	std::string speechServerStatusMessage;

	// -- conversation / output --
	std::deque<Message> chatMessages;
	std::string chatLastAssistantReply;
	TtsPreviewState chatTtsPreview;
	std::string scriptOutput;
	std::string scriptInlineCompletionOutput;
	std::string scriptInlineCompletionTargetPath;
	std::deque<Message> scriptMessages;
	std::string summarizeOutput;
	std::string writeOutput;
	std::string translateOutput;
	std::string voiceTranslatorStatus;
	std::string voiceTranslatorTranscript;
	TtsPreviewState translateTtsPreview;
	std::string customOutput;
	std::string citationOutput;
	std::string visionOutput;
	std::string montageSummary;
	std::string montageEditorBrief;
	std::string montageEdlText;
	std::string montageSrtText;
	std::string montageVttText;
	std::string videoPlanSummary;
	std::string videoEditPlanSummary;
	std::string speechOutput;
	std::string ttsOutput;
	std::string diffusionOutput;
	std::string musicToImagePromptOutput;
	std::string musicToImageStatus;
	std::string musicVideoSectionSummary;
	std::string imageToMusicPromptOutput;
	std::string imageToMusicNotationOutput;
	std::string imageToMusicStatus;
	std::string imageToMusicSavedNotationPath;
	std::string aceStepStatus;
	std::string aceStepGeneratedRequestJson;
	std::string aceStepUnderstoodSummary;
	std::string aceStepUnderstoodCaption;
	std::string aceStepUnderstoodLyrics;
	std::string aceStepUsedServerUrl;
	std::vector<ofxGgmlGeneratedMusicTrack> aceStepGeneratedTracks;
	std::string imageSearchOutput;
	std::string clipOutput;
	std::string milkdropOutput;
	std::string milkdropSavedPresetPath;
	std::string milkdropPreviewStatus;
	ofxGgmlMilkDropValidation milkdropValidation;
	std::vector<ofxGgmlMilkDropVariant> milkdropVariants;
	int milkdropSelectedVariantIndex = -1;
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
	ofImage imageSearchPreviewImage;
	std::string imageSearchPreviewLoadedPath;
	std::string imageSearchPreviewError;
	std::string imageSearchPreviewSourceUrl;
	int selectedImageSearchResultIndex = -1;
	std::string deferredImageSearchPrompt;
	bool hasDeferredImageSearchPrompt = false;
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
	std::string deferredMontageSubtitlePath;
	bool hasDeferredMontageSubtitlePath = false;
	bool montageSubtitlePlaybackEnabled = false;
	int montagePreviewTimingModeIndex = 0;
	bool montagePreviewTimelinePlaying = false;
	double montagePreviewTimelineSeconds = 0.0;
	float montagePreviewTimelineLastTickTime = 0.0f;
	std::string montagePreviewSubtitleSlavePath;
	std::string montagePreviewStatusMessage;
#if OFXGGML_HAS_OFXVLC4
	ofxVlc4 montageVlcPreviewPlayer;
	bool montageVlcPreviewInitialized = false;
	std::string montageVlcPreviewLoadedVideoPath;
	std::string montageVlcPreviewLoadedSubtitlePath;
	std::string montageVlcPreviewError;
#endif
	int selectedMontageCueIndex = -1;
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
#if OFXGGML_HAS_OFXPROJECTM
	ofxProjectM milkdropPreviewPlayer;
	bool milkdropPreviewInitialized = false;
	std::string milkdropPreviewError;
#endif
	std::vector<float> milkdropPreviewAudioSamples;
	int milkdropPreviewAudioFrames = 0;
	int milkdropPreviewAudioChannels = 0;
	std::mutex milkdropPreviewAudioMutex;
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
	ofxGgmlMilkDropValidation pendingMilkDropValidation;
	std::vector<ofxGgmlMilkDropVariant> pendingMilkDropVariants;
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
	std::string pendingAceStepStatus;
	std::string pendingAceStepGeneratedRequestJson;
	std::string pendingAceStepUnderstoodSummary;
	std::string pendingAceStepUnderstoodCaption;
	std::string pendingAceStepUnderstoodLyrics;
	std::string pendingAceStepUsedServerUrl;
	std::vector<ofxGgmlGeneratedMusicTrack> pendingAceStepGeneratedTracks;
	bool pendingAceStepDirty = false;
	std::string pendingImageSearchOutput;
	std::string pendingImageSearchBackendName;
	float pendingImageSearchElapsedMs = 0.0f;
	std::vector<ofxGgmlImageSearchItem> pendingImageSearchResults;
	bool pendingImageSearchDirty = false;
	std::string pendingCitationOutput;
	std::string pendingCitationBackendName;
	float pendingCitationElapsedMs = 0.0f;
	std::vector<ofxGgmlCitationItem> pendingCitationResults;
	bool pendingCitationDirty = false;
	std::string pendingVoiceTranslatorStatus;
	std::string pendingVoiceTranslatorTranscript;
	bool pendingVoiceTranslatorDirty = false;
	std::string videoEssayStatus;
	std::string videoEssayOutline;
	std::string videoEssayScript;
	std::string videoEssaySrtText;
	std::vector<ofxGgmlCitationItem> videoEssayCitations;
	std::vector<ofxGgmlVideoEssaySection> videoEssaySections;
	std::vector<ofxGgmlVideoEssayVoiceCue> videoEssayVoiceCues;
	std::string pendingVideoEssayStatus;
	std::string pendingVideoEssayOutline;
	std::string pendingVideoEssayScript;
	std::string pendingVideoEssaySrtText;
	std::vector<ofxGgmlCitationItem> pendingVideoEssayCitations;
	std::vector<ofxGgmlVideoEssaySection> pendingVideoEssaySections;
	std::vector<ofxGgmlVideoEssayVoiceCue> pendingVideoEssayVoiceCues;
	bool pendingVideoEssayDirty = false;
	std::vector<ofxGgmlSampledVideoFrame> visionSampledVideoFrames;
	std::vector<ofxGgmlSampledVideoFrame> pendingVisionSampledVideoFrames;
	std::string pendingMontageSummary;
	std::string pendingMontageEditorBrief;
	std::string pendingMontageEdlText;
	std::string pendingMontageSrtText;
	std::string pendingMontageVttText;
	ofxGgmlMontagePreviewBundle montagePreviewBundle;
	ofxGgmlMontageSubtitleTrack montageSubtitleTrack;
	ofxGgmlMontageSubtitleTrack montageSourceSubtitleTrack;
	ofxGgmlMontagePreviewBundle pendingMontagePreviewBundle;
	ofxGgmlMontageSubtitleTrack pendingMontageSubtitleTrack;
	ofxGgmlMontageSubtitleTrack pendingMontageSourceSubtitleTrack;
	std::string pendingVideoPlanJson;
	std::string pendingVideoPlanSummary;
	std::string pendingVideoEditPlanJson;
	std::string pendingVideoEditPlanSummary;
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
	std::array<int, kModeCount> modeMaxTokens = {512, 1024, 384, 512, 512, 512, 896, 384, 512, 512, 512, 384, 640};
	std::array<int, kModeCount> modeTextBackendIndices = {1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1};
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
	std::vector<ofxGgmlMilkDropCategoryOption> milkdropCategories;
	std::string configuredClipBackendModelPath;
	int configuredClipBackendVerbosity = -1;
	bool configuredClipBackendNormalize = true;
	std::string imageSearchBackendName;
	float imageSearchElapsedMs = 0.0f;
	std::vector<ofxGgmlImageSearchItem> imageSearchResults;
	std::string citationBackendName;
	float citationElapsedMs = 0.0f;
	std::vector<ofxGgmlCitationItem> citationResults;
	bool citationUseCrawler = false;
	int citationMaxResults = 5;

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
	ofxGgmlMediaPromptGenerator mediaPromptGenerator;
	ofxGgmlMusicGenerator musicGenerator;
	ofxGgmlAceStepBridge aceStepBridge;
	ofxGgmlImageSearch imageSearch;
	ofxGgmlCitationSearch citationSearch;
	ofxGgmlClipInference clipInference;
	ofxGgmlMilkDropGenerator milkdropGenerator;
	ofxGgmlVideoEssayWorkflow videoEssayWorkflow;
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
	void runVoiceTranslatorWorkflow(bool useAudioInput);
	void runScriptAssistantRequest(
		const ofxGgmlCodeAssistantRequest & request,
		const std::string & requestLabel,
		bool clearInputAfter = false,
		const ofxGgmlRealtimeInfoSettings & realtimeSettings = {});
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
	void runVisionInference();
	void runVideoInference();
	void runVideoPlanning();
	void runMontagePlanning();
	void runVideoEditPlanning();
	void applyVideoEditPresetByIndex(int presetIndex);
	void applyMusicVideoWorkflowDefaults(bool overwriteVisionPrompt = false);
	void resetVideoEditWorkflowState();
	bool isVideoEditWorkflowStepCompleted(int stepIndex) const;
	void setVideoEditWorkflowStepCompleted(int stepIndex, bool completed);
	void runSpeechInference();
	void runTtsInference();
	void runTtsInferenceForText(
		const std::string & text,
		const std::string & statusLabel = "Chat reply",
		bool mirrorIntoTtsInput = false);
	void speakLatestChatReply(bool mirrorIntoTtsInput = true);
	void speakTranslatedReply(bool mirrorIntoTtsInput = true);
	bool ensureChatTtsAudioLoaded(int artifactIndex = -1, bool autoplay = false);
	void stopChatTtsPlayback(bool clearLoadedPath = false);
	bool ensureTranslateTtsAudioLoaded(int artifactIndex = -1, bool autoplay = false);
	void stopTranslateTtsPlayback(bool clearLoadedPath = false);
	void runDiffusionInference();
	void runMusicToImagePromptGeneration();
	void runImageToMusicPromptGeneration();
	void runImageToMusicNotationGeneration();
	void runAceStepMusicGeneration();
	void runAceStepAudioUnderstanding();
	void runImageSearch();
	void runCitationSearch();
	void runVideoEssayWorkflow();
	void runClipInference();
	void runMilkDropGeneration(
		bool editExisting = false,
		bool generateVariants = false,
		bool repairExisting = false);
	bool ensureClipBackendConfigured(
		const std::string & modelPath,
		int verbosity,
		bool normalizeEmbeddings);
	bool ensureDiffusionBackendConfigured();
	bool ensureDiffusionClipBackendConfigured();
	static int clampSupportedDiffusionImageSize(int value);
	ofxGgmlInferenceSettings buildCurrentTextInferenceSettings(AiMode mode) const;
	std::string getPreferredDiffusionReuseImagePath() const;
	void setDiffusionInitImagePath(const std::string & path, bool promoteTask = true);
	void copyDiffusionOutputsToClipPaths();
	ofxGgmlLiveSpeechSettings makeLiveSpeechSettings() const;
	void applyLiveSpeechTranscriberSettings();
	bool ensureSpeechInputStreamReady();
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

	// -- UI panels --
	void drawMenuBar();
	void drawSidebar();
	void drawMainPanel();
	void drawChatPanel();
	void drawScriptPanel();
	void drawScriptSourcePanel();
	void drawSummarizePanel();
	void drawCitationSearchSection(
		const char * useInputButtonLabel,
		const std::string & suggestedTopic);
	void drawWritePanel();
	void drawTranslatePanel();
	void drawCustomPanel();
	void drawVideoEssayPanel();
	void drawVisionPanel();
	void drawMilkDropPanel();
	void drawImageSearchPanel(
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
	int findActiveMontageSourceCueIndex() const;
	ofxGgmlMontagePreviewTimingMode getSelectedMontagePreviewTimingMode() const;
	const ofxGgmlMontagePreviewTrack * getSelectedMontagePreviewTrack() const;
	double getSelectedMontagePreviewTimeSeconds() const;
	int findActiveMontagePreviewCueIndex() const;
	std::string exportSelectedMontagePreviewTrack(
		ofxGgmlMontagePreviewTextFormat format,
		std::string * errorOut = nullptr) const;
#if OFXGGML_HAS_OFXVLC4
	bool ensureMontageVlcPreviewInitialized(std::string * errorOut = nullptr);
	bool loadMontageVlcPreview(std::string * errorOut = nullptr);
	void closeMontageVlcPreview();
	void drawMontageVlcPreview();
#endif
	void rebuildMontageSubtitleTrackFromText();
	void ensureDiffusionPreviewResources();
	void drawDiffusionImagePreview(
		const char * label,
		const std::string & imagePath,
		ofImage & previewImage,
		const std::string & errorMessage,
		const char * childId);
	void ensureImageSearchPreviewResources();
	bool cacheImageSearchResultAsset(
		const ofxGgmlImageSearchItem & item,
		bool preferThumbnail,
		std::string & localPath,
		std::string & errorMessage) const;
	void useImageSearchResultForVision(size_t index);
	void useImageSearchResultForDiffusion(size_t index);
	void drawSpeechPanel();
	void drawTtsPanel();
	void drawDiffusionPanel();
	void drawClipPanel();
	void drawImageToMusicSection();
	void drawAceStepMusicSection();
	void drawMusicVideoWorkflowSection();
	bool saveMilkDropPresetToConfiguredPath();
	bool saveImageToMusicNotationToConfiguredPath();
	std::string defaultMilkDropPresetDir() const;
#if OFXGGML_HAS_OFXPROJECTM
	bool ensureMilkDropPreviewReady();
	bool loadMilkDropPresetIntoPreview(const std::string & presetText);
#endif
	void validateCurrentMilkDropPreset();
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
