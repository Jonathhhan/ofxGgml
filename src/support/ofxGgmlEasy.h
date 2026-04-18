#pragma once

#include "assistants/ofxGgmlChatAssistant.h"
#include "assistants/ofxGgmlTextAssistant.h"
#include "inference/ofxGgmlCitationSearch.h"
#include "inference/ofxGgmlMilkDropGenerator.h"
#include "inference/ofxGgmlMontagePreviewBridge.h"
#include "inference/ofxGgmlMontagePlanner.h"
#include "inference/ofxGgmlSpeechInference.h"
#include "inference/ofxGgmlVideoPlanner.h"
#include "inference/ofxGgmlVisionInference.h"
#include "inference/ofxGgmlWebCrawler.h"

#include <string>
#include <vector>

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
	std::vector<std::string> allowedDomains;
	std::vector<std::string> extraArgs;
};

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

struct ofxGgmlEasyVideoEditResult {
	bool success = false;
	std::string error;
	ofxGgmlVideoEditPlannerResult planning;
	ofxGgmlVideoEditWorkflow workflow;
	std::string editorBrief;
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
	ofxGgmlMilkDropGenerator & getMilkDropGenerator();
	const ofxGgmlMilkDropGenerator & getMilkDropGenerator() const;

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
	ofxGgmlWebCrawlerResult crawlWebsite(
		const std::string & startUrl,
		int maxDepth = -1) const;
	ofxGgmlCitationSearchResult findCitations(
		const std::string & topic,
		const std::vector<std::string> & sourceUrls = {},
		const std::string & crawlerUrl = "",
		size_t maxCitations = 5) const;
	ofxGgmlEasyMontageResult planMontageFromSrt(
		const std::string & srtPath,
		const std::string & goal,
		size_t maxClips = 8,
		double minScore = 0.18,
		bool preserveChronology = true,
		const std::string & reelName = "AX",
		const std::string & edlTitle = "MONTAGE",
		int fps = 25) const;
	ofxGgmlEasyVideoEditResult planVideoEdit(
		const std::string & sourcePrompt,
		const std::string & editGoal,
		const std::string & sourceAnalysis = "",
		double targetDurationSeconds = 15.0,
		int clipCount = 5,
		bool preserveChronology = true,
		const ofxGgmlVideoEditWorkflowContext & workflowContext = {}) const;
	ofxGgmlMilkDropResult generateMilkDropPreset(
		const std::string & prompt,
		const std::string & category = "General",
		float randomness = 0.55f) const;
	ofxGgmlMilkDropResult editMilkDropPreset(
		const std::string & existingPresetText,
		const std::string & editInstruction,
		const std::string & category = "General",
		float randomness = 0.45f) const;

private:
	ofxGgmlInferenceSettings makeTextSettings() const;
	ofxGgmlVisionModelProfile makeVisionProfile() const;
	ofxGgmlWebCrawlerRequest makeCrawlerRequest(
		const std::string & startUrl,
		int maxDepth = -1) const;
	void syncCrawlerBackend();
	void syncSpeechBackend();

	ofxGgmlEasyTextConfig m_textConfig;
	ofxGgmlEasyVisionConfig m_visionConfig;
	ofxGgmlEasySpeechConfig m_speechConfig;
	ofxGgmlEasyCrawlerConfig m_crawlerConfig;

	ofxGgmlInference m_inference;
	ofxGgmlChatAssistant m_chatAssistant;
	ofxGgmlTextAssistant m_textAssistant;
	ofxGgmlVisionInference m_visionInference;
	ofxGgmlSpeechInference m_speechInference;
	ofxGgmlWebCrawler m_webCrawler;
	ofxGgmlCitationSearch m_citationSearch;
	ofxGgmlVideoPlanner m_videoPlanner;
	ofxGgmlMilkDropGenerator m_milkDropGenerator;
};
