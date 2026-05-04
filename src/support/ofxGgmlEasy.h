#pragma once

#include "assistants/ofxGgmlCodingAgent.h"
#include "assistants/ofxGgmlChatAssistant.h"
#include "assistants/ofxGgmlTextAssistant.h"
#include "core/ofxGgmlTypes.h"
#include "inference/ofxGgmlCitationSearch.h"
#include "inference/ofxGgmlMediaPromptGenerator.h"
#include "inference/ofxGgmlLongVideoPlanner.h"
#include "inference/ofxGgmlRAGPipeline.h"
#include "inference/ofxGgmlSpeechInference.h"
#include "inference/ofxGgmlVideoPlanner.h"
#include "inference/ofxGgmlVisionInference.h"
#include "inference/ofxGgmlWebCrawler.h"
#include "support/ofxGgmlConversationManager.h"

#ifndef OFXGGML_ENABLE_COMPANION_WORKFLOWS
#define OFXGGML_ENABLE_COMPANION_WORKFLOWS 0
#endif

#if OFXGGML_ENABLE_COMPANION_WORKFLOWS
#include "inference/ofxGgmlAceStepBridge.h"
#include "inference/ofxGgmlMilkDropGenerator.h"
#include "inference/ofxGgmlMontagePreviewBridge.h"
#include "inference/ofxGgmlMontagePlanner.h"
#include "inference/ofxGgmlMusicGenerator.h"
#include "inference/ofxGgmlVideoEssayWorkflow.h"
#endif

#include <memory>
#include <optional>
#include <string>
#include <vector>

class ofxGgml;

struct ofxGgmlEasyTextConfig {
	std::string modelPath;
	std::string completionExecutable;
	std::string embeddingExecutable;
	std::string serverUrl;
	std::string serverModel;
	bool preferServer = false;
	ofxGgmlInferenceSettings settings;
};

struct ofxGgmlEasyVisionConfig {
	std::string modelPath;
	std::string serverUrl = "http://127.0.0.1:8080";
	std::string mmprojPath;
	int maxTokens = 384;
	float temperature = 0.2f;
};

struct ofxGgmlEasySpeechConfig {
	std::string modelPath;
	std::string cliExecutable = "whisper-cli";
	std::string serverUrl;
	std::string serverModel;
	bool preferServer = false;
	std::string languageHint;
	bool returnTimestamps = false;
};

struct ofxGgmlEasyCrawlerConfig {
	std::string executablePath;
	std::string outputDir;
	int maxDepth = 2;
	bool renderJavaScript = false;
	bool keepOutputFiles = true;
	bool stayOnStartDomain = true;
	int maxPages = 24;
	std::vector<std::string> allowedDomains;
	std::vector<std::string> extraArgs;
};

#if OFXGGML_ENABLE_COMPANION_WORKFLOWS
struct ofxGgmlEasyMontageResult {
	bool success = false;
	std::string error;
	ofxGgmlMontagePlannerResult planning;
	ofxGgmlMontagePreviewBundle previewBundle;
	ofxGgmlMontageSubtitleTrack montageTrack;
	ofxGgmlMontageSubtitleTrack sourceTrack;
	std::string editorBrief;
	std::string edlText;
	std::string srtText;
	std::string vttText;
};
#endif

struct ofxGgmlEasyVideoEditResult {
	bool success = false;
	std::string error;
	ofxGgmlVideoEditPlannerResult planning;
	ofxGgmlVideoEditWorkflow workflow;
	std::string editorBrief;
};

struct ofxGgmlEasyWorkflowResult {
	bool success = false;
	std::string error;
	std::vector<std::string> intermediateResults;
	std::string finalOutput;
	float totalElapsedMs = 0.0f;

	/// Add helper to extract specific intermediate result by index
	std::string getIntermediateResult(size_t index, const std::string& defaultValue = "") const {
		return (index < intermediateResults.size()) ? intermediateResults[index] : defaultValue;
	}
};

struct ofxGgmlEasyModelPreset {
	int preset = 0;
	std::string name;
	std::string filename;
	std::string url;
	std::string size;
	std::string bestFor;
	std::string sha256;
	std::string publisher;
	std::string sourceType;
	std::string sourceUrl;
	std::string verificationStatus;
	std::string catalogUpdatedAt;

	bool hasChecksum() const {
		return !sha256.empty();
	}

	bool checksumVerified() const {
		return verificationStatus == "verified-sha256";
	}
};

struct ofxGgmlEasyModelSetupReport {
	bool ready = false;
	bool catalogAvailable = false;
	bool modelPathExists = false;
	bool prefersServer = false;
	bool serverConfigured = false;
	std::string resolvedCatalogPath;
	std::optional<ofxGgmlEasyModelPreset> configuredPreset;
	std::optional<ofxGgmlEasyModelPreset> recommendedPreset;
	std::vector<std::string> errors;
	std::vector<std::string> warnings;
	std::vector<std::string> recommendations;
};

struct ofxGgmlEasyModelDownloadPlan {
	bool available = false;
	int preset = 0;
	std::string task;
	std::string name;
	std::string filename;
	std::string url;
	std::string sha256;
	std::string size;
	std::string outputDir;
	std::string catalogPath;
	std::string downloadScriptPath;
	std::string suggestedCommand;
	bool verifiedCatalogEntry = false;
	bool checksumRequired = false;
	std::string catalogTrustSummary;
	std::vector<std::string> warnings;
};

struct ofxGgmlEasyHealthSnapshot {
	bool textConfigured = false;
	bool localRuntimeAttached = false;
	bool localRuntimeReady = false;
	bool serverExpected = false;
	double averageLatencyMs = 0.0;
	double minLatencyMs = 0.0;
	double maxLatencyMs = 0.0;
	double averageTokensPerSecond = 0.0;
	double retrievalCacheHitRate = 0.0;
	double tokenCountCacheHitRate = 0.0;
	ofxGgmlMemoryUsage memoryUsage;
	ofxGgmlServerProbeResult serverProbe;
	ofxGgmlServerQueueStatus serverQueue;
	std::vector<std::string> warnings;
};

enum class ofxGgmlEasyDiagnosticSeverity {
	Info,
	Warning,
	Degraded,
	Blocking
};

struct ofxGgmlEasyDiagnosticIssue {
	ofxGgmlEasyDiagnosticSeverity severity = ofxGgmlEasyDiagnosticSeverity::Info;
	std::string component;
	std::string message;
};

struct ofxGgmlEasyDiagnosticsReport {
	bool ready = false;
	bool degraded = false;
	ofxGgmlEasyModelSetupReport setup;
	ofxGgmlEasyHealthSnapshot health;
	std::vector<ofxGgmlEasyDiagnosticIssue> issues;

	size_t countIssues(ofxGgmlEasyDiagnosticSeverity severity) const;
	std::string toJsonString() const;
};

/// High-level convenience facade for common text, vision, and speech workflows.
///
/// This wrapper keeps the underlying addon classes available, but gives apps a
/// shorter setup path for the most common tasks:
///
/// - complete(prompt)
/// - chat(userText)
/// - summarize(text)
/// - rewrite(text)
/// - translate(text, targetLanguage)
/// - describeImage(imagePath)
/// - askImage(imagePath, question)
/// - transcribeAudio(audioPath)
/// - translateAudio(audioPath)
class ofxGgmlEasy {
public:
	ofxGgmlEasy();

	void configureText(const ofxGgmlEasyTextConfig & config);
	void configureVision(const ofxGgmlEasyVisionConfig & config);
	void configureSpeech(const ofxGgmlEasySpeechConfig & config);
	void configureWebCrawler(const ofxGgmlEasyCrawlerConfig & config);

	const ofxGgmlEasyTextConfig & getTextConfig() const;
	const ofxGgmlEasyVisionConfig & getVisionConfig() const;
	const ofxGgmlEasySpeechConfig & getSpeechConfig() const;
	const ofxGgmlEasyCrawlerConfig & getWebCrawlerConfig() const;

	ofxGgmlInference & getInference();
	const ofxGgmlInference & getInference() const;
	ofxGgmlChatAssistant & getChatAssistant();
	const ofxGgmlChatAssistant & getChatAssistant() const;
	ofxGgmlTextAssistant & getTextAssistant();
	const ofxGgmlTextAssistant & getTextAssistant() const;
	ofxGgmlVisionInference & getVisionInference();
	const ofxGgmlVisionInference & getVisionInference() const;
	ofxGgmlSpeechInference & getSpeechInference();
	const ofxGgmlSpeechInference & getSpeechInference() const;
	ofxGgmlWebCrawler & getWebCrawler();
	const ofxGgmlWebCrawler & getWebCrawler() const;
	ofxGgmlCitationSearch & getCitationSearch();
	const ofxGgmlCitationSearch & getCitationSearch() const;
	ofxGgmlVideoPlanner & getVideoPlanner();
	const ofxGgmlVideoPlanner & getVideoPlanner() const;
	ofxGgmlMediaPromptGenerator & getMediaPromptGenerator();
	const ofxGgmlMediaPromptGenerator & getMediaPromptGenerator() const;
#if OFXGGML_ENABLE_COMPANION_WORKFLOWS
	ofxGgmlMusicGenerator & getMusicGenerator();
	const ofxGgmlMusicGenerator & getMusicGenerator() const;
	ofxGgmlAceStepBridge & getAceStepBridge();
	const ofxGgmlAceStepBridge & getAceStepBridge() const;
	ofxGgmlMilkDropGenerator & getMilkDropGenerator();
	const ofxGgmlMilkDropGenerator & getMilkDropGenerator() const;
	ofxGgmlVideoEssayWorkflow & getVideoEssayWorkflow();
	const ofxGgmlVideoEssayWorkflow & getVideoEssayWorkflow() const;
#endif
	ofxGgmlLongVideoPlanner & getLongVideoPlanner();
	const ofxGgmlLongVideoPlanner & getLongVideoPlanner() const;
	ofxGgmlCodingAgent & getCodingAgent();
	const ofxGgmlCodingAgent & getCodingAgent() const;
	ofxGgmlRAGPipeline & getRAGPipeline();
	const ofxGgmlRAGPipeline & getRAGPipeline() const;
	ofxGgmlConversationManager & getConversationManager();
	const ofxGgmlConversationManager & getConversationManager() const;
	std::vector<ofxGgmlEasyModelPreset> listTextModelPresets(
		const std::string & catalogPath = "") const;
	std::optional<ofxGgmlEasyModelPreset> recommendTextModelForTask(
		const std::string & task,
		const std::string & catalogPath = "") const;
	ofxGgmlEasyModelSetupReport inspectTextSetup(
		const std::string & task = "",
		const std::string & catalogPath = "") const;
	std::optional<ofxGgmlEasyModelDownloadPlan> planTextModelDownload(
		const std::string & task = "",
		int preset = 0,
		const std::string & catalogPath = "",
		const std::string & outputDir = "") const;
	ofxGgmlEasyHealthSnapshot inspectTextHealth(
		const ofxGgml * runtime = nullptr) const;
	ofxGgmlEasyDiagnosticsReport inspectTextDiagnostics(
		const std::string & task = "",
		const std::string & catalogPath = "",
		const ofxGgml * runtime = nullptr) const;

	ofxGgmlInferenceResult complete(const std::string & prompt) const;
	ofxGgmlChatAssistantResult chat(
		const std::string & userText,
		const std::string & responseLanguage = "Auto",
		const std::string & systemPrompt = "") const;
	ofxGgmlTextAssistantResult summarize(const std::string & text) const;
	ofxGgmlTextAssistantResult rewrite(const std::string & text) const;
	ofxGgmlTextAssistantResult translate(
		const std::string & text,
		const std::string & targetLanguage,
		const std::string & sourceLanguage = "Auto detect") const;
	ofxGgmlVisionResult describeImage(
		const std::string & imagePath,
		const std::string & prompt = "") const;
	ofxGgmlVisionResult askImage(
		const std::string & imagePath,
		const std::string & question) const;
	ofxGgmlSpeechResult transcribeAudio(const std::string & audioPath) const;
	ofxGgmlSpeechResult translateAudio(const std::string & audioPath) const;
#if OFXGGML_ENABLE_COMPANION_WORKFLOWS
	ofxGgmlMusicPromptResult generateMusicPrompt(
		const std::string & sourceConcept,
		const std::string & style = "cinematic instrumental soundtrack, expressive, high fidelity",
		const std::string & instrumentation = "",
		int targetDurationSeconds = 30,
		bool instrumentalOnly = true) const;
	ofxGgmlMusicNotationResult generateMusicNotation(
		const std::string & sourceConcept,
		const std::string & title = "Generated Theme",
		const std::string & style = "cinematic instrumental soundtrack",
		int bars = 16,
		const std::string & key = "Cm") const;
	ofxGgmlImageToMusicResult generateImageToMusicPrompt(
		const std::string & imageDescription,
		const std::string & musicalStyle = "cinematic instrumental soundtrack, expressive, high fidelity",
		const std::string & instrumentation = "",
		int targetDurationSeconds = 30,
		bool instrumentalOnly = true) const;
	std::string saveMusicNotation(
		const std::string & abcNotation,
		const std::string & outputPath) const;
	ofxGgmlAceStepGenerateResult generateAceStepMusic(
		const ofxGgmlAceStepRequest & request,
		const std::string & serverUrl = "") const;
	ofxGgmlAceStepUnderstandResult understandAceStepAudio(
		const ofxGgmlAceStepUnderstandRequest & request,
		const std::string & serverUrl = "") const;
#endif
	ofxGgmlWebCrawlerResult crawlWebsite(
		const std::string & startUrl,
		int maxDepth = -1) const;
	ofxGgmlCitationSearchResult findCitations(
		const std::string & topic,
		const std::vector<std::string> & sourceUrls = {},
		const std::string & crawlerUrl = "",
		size_t maxCitations = 100) const;
	ofxGgmlCitationSearchResult findCitationsFromInput(
		const std::string & userInput,
		const std::vector<std::string> & sourceUrls = {},
		const std::string & crawlerUrl = "",
		size_t maxCitations = 100,
		const ofxGgmlCitationSearchInputSettings & inputSettings = {}) const;
#if OFXGGML_ENABLE_COMPANION_WORKFLOWS
	ofxGgmlVideoEssayResult planVideoEssay(
		const ofxGgmlVideoEssayRequest & request) const;
#endif
	ofxGgmlLongVideoPlanResult planLongVideo(
		const ofxGgmlLongVideoPlanRequest & request) const;
	std::string buildLongVideoManifestJson(
		const ofxGgmlLongVideoPlanRequest & request) const;
	ofxGgmlCodingAgentResult runCodingAgent(
		const ofxGgmlCodingAgentRequest & request,
		const ofxGgmlCodeAssistantContext & context = {},
		const ofxGgmlCodingAgentSettings & settings = {});
#if OFXGGML_ENABLE_COMPANION_WORKFLOWS
	ofxGgmlEasyMontageResult planMontageFromSrt(
		const std::string & srtPath,
		const std::string & goal,
		size_t maxClips = 8,
		double minScore = 0.18,
		bool preserveChronology = true,
		const std::string & reelName = "AX",
		const std::string & edlTitle = "MONTAGE",
		int fps = 25) const;
#endif
	ofxGgmlEasyVideoEditResult planVideoEdit(
		const std::string & sourcePrompt,
		const std::string & editGoal,
		const std::string & sourceAnalysis = "",
		double targetDurationSeconds = 15.0,
		int clipCount = 5,
		bool preserveChronology = true,
		const ofxGgmlVideoEditWorkflowContext & workflowContext = {}) const;
#if OFXGGML_ENABLE_COMPANION_WORKFLOWS
	ofxGgmlMilkDropResult generateMilkDropPreset(
		const std::string & prompt,
		const std::string & category = "General",
		float randomness = 0.55f) const;
	ofxGgmlMilkDropVariantResult generateMilkDropVariants(
		const std::string & prompt,
		const std::string & category = "General",
		float randomness = 0.55f,
		int variantCount = 3) const;
	ofxGgmlMilkDropResult editMilkDropPreset(
		const std::string & existingPresetText,
		const std::string & editInstruction,
		const std::string & category = "General",
		float randomness = 0.45f) const;
	ofxGgmlMilkDropResult repairMilkDropPreset(
		const std::string & presetText,
		const std::string & category = "General",
		float randomness = 0.25f,
		const std::string & repairInstruction = "") const;
	ofxGgmlMilkDropValidation validateMilkDropPreset(
		const std::string & presetText) const;
	std::string saveMilkDropPreset(
		const std::string & presetText,
		const std::string & outputPath) const;
#endif

	/// Workflow Presets - Common multi-step AI workflows

	/// Summarize text and then translate the summary to target language.
	/// Returns workflow result with intermediate summary and final translation.
	ofxGgmlEasyWorkflowResult summarizeAndTranslate(
		const std::string & text,
		const std::string & targetLanguage,
		const std::string & sourceLanguage = "Auto detect",
		int maxSummaryWords = 150) const;

	/// Transcribe audio to text and then summarize the transcript.
	/// Returns workflow result with intermediate transcript and final summary.
	ofxGgmlEasyWorkflowResult transcribeAndSummarize(
		const std::string & audioPath,
		int maxSummaryWords = 100) const;

	/// Describe an image with vision model and then analyze the description with text model.
	/// Returns workflow result with intermediate description and final analysis.
	ofxGgmlEasyWorkflowResult describeAndAnalyze(
		const std::string & imagePath,
		const std::string & analysisPrompt = "Provide a detailed analysis of this scene",
		const std::string & descriptionPrompt = "") const;

	/// Crawl a website, extract content, and then summarize the findings.
	/// Returns workflow result with intermediate crawled content and final summary.
	ofxGgmlEasyWorkflowResult crawlAndSummarize(
		const std::string & startUrl,
		int maxDepth = 2,
		int maxSummaryWords = 200) const;

	/// Convenience method: run a full RAG query against documents already added
	/// to getRAGPipeline(). Requires configureText() to be called first.
	ofxGgmlRAGResult ragQuery(
		const std::string & query,
		size_t topK = 5,
		size_t chunkSize = 400,
		size_t chunkOverlap = 80,
		const std::string & promptPrefix = "") const;

private:
	ofxGgmlInferenceSettings makeTextSettings() const;
	ofxGgmlVisionModelProfile makeVisionProfile() const;
	ofxGgmlWebCrawlerRequest makeCrawlerRequest(
		const std::string & startUrl,
		int maxDepth = -1) const;
	void syncTextBackends();
	void syncCrawlerBackend();
	void syncSpeechBackend();
	ofxGgmlInference & ensureInference() const;
	ofxGgmlChatAssistant & ensureChatAssistant() const;
	ofxGgmlTextAssistant & ensureTextAssistant() const;
	ofxGgmlVisionInference & ensureVisionInference() const;
	ofxGgmlSpeechInference & ensureSpeechInference() const;
	ofxGgmlCitationSearch & ensureCitationSearch() const;
	ofxGgmlVideoPlanner & ensureVideoPlanner() const;
	ofxGgmlMediaPromptGenerator & ensureMediaPromptGenerator() const;
#if OFXGGML_ENABLE_COMPANION_WORKFLOWS
	ofxGgmlMusicGenerator & ensureMusicGenerator() const;
	ofxGgmlAceStepBridge & ensureAceStepBridge() const;
	ofxGgmlMilkDropGenerator & ensureMilkDropGenerator() const;
	ofxGgmlVideoEssayWorkflow & ensureVideoEssayWorkflow() const;
#endif
	ofxGgmlLongVideoPlanner & ensureLongVideoPlanner() const;
	ofxGgmlCodingAgent & ensureCodingAgent() const;
	ofxGgmlRAGPipeline & ensureRAGPipeline() const;
	ofxGgmlConversationManager & ensureConversationManager() const;

	ofxGgmlEasyTextConfig m_textConfig;
	ofxGgmlEasyVisionConfig m_visionConfig;
	ofxGgmlEasySpeechConfig m_speechConfig;
	ofxGgmlEasyCrawlerConfig m_crawlerConfig;

	mutable std::unique_ptr<ofxGgmlInference> m_inference;
	mutable std::unique_ptr<ofxGgmlChatAssistant> m_chatAssistant;
	mutable std::unique_ptr<ofxGgmlTextAssistant> m_textAssistant;
	mutable std::unique_ptr<ofxGgmlVisionInference> m_visionInference;
	mutable std::unique_ptr<ofxGgmlSpeechInference> m_speechInference;
	mutable std::unique_ptr<ofxGgmlCitationSearch> m_citationSearch;
	mutable std::unique_ptr<ofxGgmlVideoPlanner> m_videoPlanner;
	mutable std::unique_ptr<ofxGgmlMediaPromptGenerator> m_mediaPromptGenerator;
#if OFXGGML_ENABLE_COMPANION_WORKFLOWS
	mutable std::unique_ptr<ofxGgmlMusicGenerator> m_musicGenerator;
	mutable std::unique_ptr<ofxGgmlAceStepBridge> m_aceStepBridge;
	mutable std::unique_ptr<ofxGgmlMilkDropGenerator> m_milkDropGenerator;
	mutable std::unique_ptr<ofxGgmlVideoEssayWorkflow> m_videoEssayWorkflow;
#endif
	mutable std::unique_ptr<ofxGgmlLongVideoPlanner> m_longVideoPlanner;
	mutable std::unique_ptr<ofxGgmlCodingAgent> m_codingAgent;
	mutable std::unique_ptr<ofxGgmlRAGPipeline> m_ragPipeline;
	mutable std::unique_ptr<ofxGgmlConversationManager> m_conversationManager;
};
