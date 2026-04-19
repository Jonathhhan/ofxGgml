#include "ofApp.h"

#include "utils/ImGuiHelpers.h"
#include "utils/PathHelpers.h"

namespace {

const char * const kVideoEssayToneLabels[] = {
	"Clear / balanced",
	"Urgent / investigative",
	"Reflective / essayistic",
	"Explainer / educational"
};

const char * const kVideoEssayAudienceLabels[] = {
	"General audience",
	"Curious beginner",
	"Creative / filmmaker",
	"Technical audience"
};

std::string videoEssayToneForIndex(int index) {
	switch (index) {
	case 1: return "urgent, investigative, and high-clarity";
	case 2: return "reflective, cinematic, and thoughtful";
	case 3: return "educational, accessible, and concrete";
	default: return "clear, engaging, and grounded";
	}
}

std::string videoEssayAudienceForIndex(int index) {
	switch (index) {
	case 1: return "curious beginner";
	case 2: return "creative filmmaker";
	case 3: return "technical audience";
	default: return "general audience";
	}
}

} // namespace

void ofApp::drawVideoEssayPanel() {
	drawPanelHeader("Video Essay", "topic to cited outline, script, and spoken narration");
	if (textInferenceBackend == TextInferenceBackend::LlamaServer &&
		!textServerCapabilityHint.empty()) {
		ImGui::TextDisabled("%s", textServerCapabilityHint.c_str());
	}

	ImGui::Text("Topic");
	ImGui::InputTextMultiline(
		"##VideoEssayTopic",
		videoEssayTopic,
		sizeof(videoEssayTopic),
		ImVec2(-1, 86));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}

	ImGui::InputText("Crawler URL", videoEssaySeedUrl, sizeof(videoEssaySeedUrl));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	if (ImGui::Checkbox("Use crawler URL##VideoEssay", &videoEssayUseCrawler)) {
		autoSaveSession();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(130);
	if (ImGui::SliderInt("Citations", &videoEssayCitationCount, 2, 12)) {
		autoSaveSession();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(170);
	if (ImGui::SliderFloat(
		"Target duration (s)",
		&videoEssayTargetDurationSeconds,
		30.0f,
		360.0f,
		"%.0f s")) {
		autoSaveSession();
	}

	ImGui::SetNextItemWidth(210);
	if (ImGui::Combo(
		"Tone",
		&videoEssayToneIndex,
		kVideoEssayToneLabels,
		IM_ARRAYSIZE(kVideoEssayToneLabels))) {
		autoSaveSession();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(200);
	if (ImGui::Combo(
		"Audience",
		&videoEssayAudienceIndex,
		kVideoEssayAudienceLabels,
		IM_ARRAYSIZE(kVideoEssayAudienceLabels))) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Checkbox(
		"Include counterpoint",
		&videoEssayIncludeCounterpoints)) {
		autoSaveSession();
	}

	ImGui::BeginDisabled(trim(chatInput).empty());
	if (ImGui::SmallButton("Use Chat Input##VideoEssay")) {
		copyStringToBuffer(videoEssayTopic, sizeof(videoEssayTopic), chatInput);
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(summarizeInput).empty());
	if (ImGui::SmallButton("Use Summarize Text##VideoEssay")) {
		copyStringToBuffer(videoEssayTopic, sizeof(videoEssayTopic), summarizeInput);
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(sourceUrlsInput).empty());
	if (ImGui::SmallButton("Use Loaded URLs##VideoEssay")) {
		const auto urls = splitStoredScriptSourceUrls(sourceUrlsInput);
		if (!urls.empty()) {
			copyStringToBuffer(videoEssaySeedUrl, sizeof(videoEssaySeedUrl), urls.front());
		}
		videoEssayUseCrawler = false;
		autoSaveSession();
	}
	ImGui::EndDisabled();

	const bool hasTopic = !trim(videoEssayTopic).empty();
	const bool canUseLoadedUrls = !trim(sourceUrlsInput).empty();
	const bool hasCrawlerUrl = !trim(videoEssaySeedUrl).empty();
	const bool canRun =
		!generating.load() &&
		hasTopic &&
		((videoEssayUseCrawler && hasCrawlerUrl) ||
			(!videoEssayUseCrawler && canUseLoadedUrls));

	ImGui::BeginDisabled(!canRun);
	if (ImGui::Button("Build Video Essay", ImVec2(160, 0))) {
		runVideoEssayWorkflow();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(videoEssayScript).empty());
	if (ImGui::Button("Synthesize Voiceover", ImVec2(160, 0))) {
		runTtsInferenceForText(
			videoEssayScript,
			"Video essay narration",
			true);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(videoEssayScript).empty());
	if (ImGui::SmallButton("Use in Write##VideoEssay")) {
		copyStringToBuffer(writeInput, sizeof(writeInput), videoEssayScript);
		activeMode = AiMode::Write;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(videoEssaySrtText).empty());
	if (ImGui::SmallButton("Copy SRT##VideoEssay")) {
		copyToClipboard(videoEssaySrtText);
	}
	ImGui::EndDisabled();

	if (!videoEssayStatus.empty()) {
		ImGui::TextWrapped("%s", videoEssayStatus.c_str());
	}

	if (!videoEssayCitations.empty()) {
		ImGui::Spacing();
		ImGui::Text("Research");
		ImGui::BeginChild("##VideoEssayCitations", ImVec2(0, 120), true);
		for (size_t i = 0; i < videoEssayCitations.size(); ++i) {
			const auto & item = videoEssayCitations[i];
			ImGui::PushID(static_cast<int>(i));
			ImGui::TextDisabled(
				"[Source %d] %s",
				item.sourceIndex,
				item.sourceLabel.empty() ? "unknown" : item.sourceLabel.c_str());
			ImGui::TextWrapped("\"%s\"", item.quote.c_str());
			if (!item.sourceUri.empty()) {
				ImGui::TextDisabled("%s", item.sourceUri.c_str());
			}
			ImGui::Spacing();
			ImGui::PopID();
		}
		ImGui::EndChild();
	}

	if (!videoEssayOutline.empty()) {
		ImGui::Spacing();
		ImGui::Text("Outline");
		ImGui::BeginChild("##VideoEssayOutline", ImVec2(0, 130), true);
		ImGui::TextWrapped("%s", videoEssayOutline.c_str());
		ImGui::EndChild();
	}

	if (!videoEssayScript.empty()) {
		ImGui::Spacing();
		ImGui::Text("Script");
		ImGui::BeginChild("##VideoEssayScript", ImVec2(0, 170), true);
		ImGui::TextWrapped("%s", videoEssayScript.c_str());
		ImGui::EndChild();
	}

	if (!videoEssaySections.empty()) {
		ImGui::Spacing();
		ImGui::Text("Voice Cues");
		ImGui::BeginChild("##VideoEssaySections", ImVec2(0, 150), true);
		for (const auto & section : videoEssaySections) {
			ImGui::PushID(section.index);
			ImGui::Text(
				"%d. %s  %.1f s",
				section.index + 1,
				section.title.c_str(),
				section.estimatedDurationSeconds);
			if (!section.summary.empty()) {
				ImGui::TextWrapped("%s", section.summary.c_str());
			}
			if (!section.sourceIndices.empty()) {
				std::string sources = "Sources:";
				for (const int sourceIndex : section.sourceIndices) {
					sources += " [Source " + ofToString(sourceIndex) + "]";
				}
				ImGui::TextDisabled("%s", sources.c_str());
			}
			ImGui::Spacing();
			ImGui::PopID();
		}
		ImGui::EndChild();
	}

	if (!videoEssaySrtText.empty()) {
		ImGui::Spacing();
		ImGui::Text("Subtitle / Cue Sheet");
		ImGui::BeginChild("##VideoEssaySrt", ImVec2(0, 140), true);
		ImGui::TextWrapped("%s", videoEssaySrtText.c_str());
		ImGui::EndChild();
	}
}

void ofApp::runVideoEssayWorkflow() {
	if (generating.load()) {
		return;
	}

	const std::string topic = trim(videoEssayTopic);
	const bool useCrawler = videoEssayUseCrawler;
	const std::string crawlerUrl = trim(videoEssaySeedUrl);
	const auto loadedUrls = splitStoredScriptSourceUrls(sourceUrlsInput);
	if (topic.empty()) {
		return;
	}
	if ((useCrawler && crawlerUrl.empty()) || (!useCrawler && loadedUrls.empty())) {
		return;
	}

	const AiMode requestMode = AiMode::VideoEssay;
	const std::string modelPath = getSelectedModelPath();
	const auto inferenceSettings = buildCurrentTextInferenceSettings(requestMode);
	const size_t maxCitations = static_cast<size_t>(std::clamp(videoEssayCitationCount, 2, 12));
	const double targetDurationSeconds = std::clamp(
		static_cast<double>(videoEssayTargetDurationSeconds),
		30.0,
		360.0);
	const std::string tone = videoEssayToneForIndex(videoEssayToneIndex);
	const std::string audience = videoEssayAudienceForIndex(videoEssayAudienceIndex);
	const bool includeCounterpoints = videoEssayIncludeCounterpoints;

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = requestMode;
	generatingStatus = "Researching sources and drafting video essay...";
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput =
			"Gathering citations, drafting an outline, and writing narration...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	workerThread = std::thread([this,
		topic,
		useCrawler,
		crawlerUrl,
		loadedUrls,
		modelPath,
		inferenceSettings,
		maxCitations,
		targetDurationSeconds,
		tone,
		audience,
		includeCounterpoints]() {
		try {
			videoEssayWorkflow.getTextAssistant().setCompletionExecutable(
				llmInference.getCompletionExecutable());
			videoEssayWorkflow.getCitationSearch().getInference().setCompletionExecutable(
				llmInference.getCompletionExecutable());
			ofxGgmlVideoEssayRequest request;
			request.modelPath = modelPath;
			request.topic = topic;
			request.maxCitations = maxCitations;
			request.useCrawler = useCrawler;
			request.sourceUrls = loadedUrls;
			request.targetDurationSeconds = targetDurationSeconds;
			request.tone = tone;
			request.audience = audience;
			request.includeCounterpoints = includeCounterpoints;
			request.inferenceSettings = inferenceSettings;
			request.sourceSettings.requestCitations = true;
			request.sourceSettings.maxSources = 6;
			request.sourceSettings.maxCharsPerSource = 2200;
			request.sourceSettings.maxTotalChars = 14000;
			if (useCrawler) {
				request.crawlerRequest.startUrl = crawlerUrl;
				request.crawlerRequest.maxDepth = 1;
				request.crawlerRequest.renderJavaScript = false;
				request.crawlerRequest.keepOutputFiles = true;
			}

			const ofxGgmlVideoEssayResult result = videoEssayWorkflow.run(request);
			std::string status;
			if (result.success) {
				status =
					"Built video essay with " +
					ofToString(result.citationResult.citations.size()) +
					" citations, " +
					ofToString(result.sections.size()) +
					" sections, and " +
					ofToString(result.voiceCues.size()) +
					" voice cues.";
			} else {
				status = "[Error] " + result.error;
			}

			{
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingVideoEssayDirty = true;
				pendingVideoEssayStatus = status;
				pendingVideoEssayOutline = result.outline;
				pendingVideoEssayScript = result.script;
				pendingVideoEssaySrtText = result.srtText;
				pendingVideoEssayCitations = result.citationResult.citations;
				pendingVideoEssaySections = result.sections;
				pendingVideoEssayVoiceCues = result.voiceCues;
			}
		} catch (const std::exception & e) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingVideoEssayDirty = true;
			pendingVideoEssayStatus = std::string("[Error] Internal exception: ") + e.what();
			pendingVideoEssayOutline.clear();
			pendingVideoEssayScript.clear();
			pendingVideoEssaySrtText.clear();
			pendingVideoEssayCitations.clear();
			pendingVideoEssaySections.clear();
			pendingVideoEssayVoiceCues.clear();
		} catch (...) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingVideoEssayDirty = true;
			pendingVideoEssayStatus = "[Error] Unknown video-essay exception.";
			pendingVideoEssayOutline.clear();
			pendingVideoEssayScript.clear();
			pendingVideoEssaySrtText.clear();
			pendingVideoEssayCitations.clear();
			pendingVideoEssaySections.clear();
			pendingVideoEssayVoiceCues.clear();
		}

		generating.store(false);
	});
}
