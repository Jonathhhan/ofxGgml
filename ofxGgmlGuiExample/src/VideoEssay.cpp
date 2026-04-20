#include "ofApp.h"

#include "utils/ImGuiHelpers.h"
#include "utils/PathHelpers.h"

#if OFXGGML_HAS_OFXSTABLEDIFFUSION
#include "ofxStableDiffusionVideoWorkflowHelpers.h"
#endif

#include <cctype>
#include <filesystem>
#include <sstream>

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

const char * const kLongVideoStructureLabels[] = {
	"Three-act cinematic",
	"Trailer / short-form",
	"Music video rise",
	"Loopable ambient",
	"Documentary / essay",
	"Product / brand spot"
};

const char * const kLongVideoPacingLabels[] = {
	"Balanced rise",
	"Gentle build",
	"Aggressive escalation",
	"Punchy short-form"
};

const char * const kLongVideoRenderPresetLabels[] = {
	"Fast preview",
	"Low VRAM",
	"Balanced",
	"Quality",
	"Batch storyboard"
};

const char * const kLongVideoRenderModeLabels[] = {
	"Standard",
	"Loop",
	"Ping-pong",
	"Boomerang"
};

std::string sanitizeVideoFilenameStem(
	const std::string & text,
	const std::string & fallback = "segment") {
	std::string sanitized;
	sanitized.reserve(text.size());
	for (const unsigned char c : text) {
		if (std::isalnum(c)) {
			sanitized.push_back(static_cast<char>(std::tolower(c)));
		} else if (c == '-' || c == '_') {
			sanitized.push_back(static_cast<char>(c));
		} else if (std::isspace(c) || c == '.' || c == ',') {
			sanitized.push_back('_');
		}
	}
	while (!sanitized.empty() && sanitized.front() == '_') {
		sanitized.erase(sanitized.begin());
	}
	while (!sanitized.empty() && sanitized.back() == '_') {
		sanitized.pop_back();
	}
	return sanitized.empty() ? fallback : sanitized;
}

struct TtsPreviewUiLabels {
	const char * comboLabel;
	const char * pauseLabel;
	const char * resumeLabel;
	const char * playLabel;
	const char * restartLabel;
	const char * stopLabel;
	const char * filePrefix;
	const char * pausedStatus;
	const char * playingStatus;
	const char * restartedStatus;
	const char * stoppedStatus;
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

std::string longVideoStructureHintForIndex(int index) {
	switch (index) {
	case 1: return "short-form trailer progression with a hook, escalation, and strong closing beat";
	case 2: return "music-driven rise with a strong payoff section";
	case 3: return "loopable ambient progression with a seamless ending";
	case 4: return "documentary essay with discovery, evidence, and takeaway beats";
	case 5: return "product-led spot with feature reveals, clean hero shots, and a memorable final value beat";
	default: return "three-act cinematic progression";
	}
}

std::string longVideoPacingProfileForIndex(int index) {
	switch (index) {
	case 1: return "gentle build with longer observation beats before the payoff";
	case 2: return "aggressive escalation with shorter setup and denser climax beats";
	case 3: return "punchy short-form pacing with an immediate hook and tight transitions";
	default: return "balanced rise with stronger emphasis near the climax";
	}
}

std::string videoEssayPlanningStatusSuffix(const ofxGgmlVideoEssayResult & result) {
	std::vector<std::string> notes;
	if (!trim(result.visualConcept).empty()) {
		notes.push_back("visual concept ready");
	}
	if (!trim(result.scenePlanJson).empty()) {
		notes.push_back("scene plan ready");
	} else if (!trim(result.scenePlanningError).empty()) {
		notes.push_back("scene planning fallback");
	}
	if (!trim(result.editPlanJson).empty()) {
		notes.push_back("edit plan ready");
	} else if (!trim(result.editPlanningError).empty()) {
		notes.push_back("edit planning fallback");
	}
	if (notes.empty()) {
		return std::string();
	}
	std::ostringstream output;
	output << " | ";
	for (size_t i = 0; i < notes.size(); ++i) {
		if (i > 0) {
			output << ", ";
		}
		output << notes[i];
	}
	return output.str();
}

template <typename EnsureLoadedFn, typename StopPlaybackFn>
void drawTtsPreviewControls(
	const bool generating,
	std::vector<ofxGgmlTtsAudioArtifact> & audioFiles,
	int & selectedAudioIndex,
	const std::string & loadedAudioPath,
	bool & playbackPaused,
	ofSoundPlayer & player,
	std::string & statusMessage,
	const TtsPreviewUiLabels & labels,
	EnsureLoadedFn && ensureLoaded,
	StopPlaybackFn && stopPlayback) {
	if (audioFiles.empty()) {
		return;
	}

	selectedAudioIndex = std::clamp(
		selectedAudioIndex,
		0,
		std::max(0, static_cast<int>(audioFiles.size()) - 1));
	if (audioFiles.size() > 1) {
		std::string previewLabel =
			ofFilePath::getBaseName(audioFiles[static_cast<size_t>(selectedAudioIndex)].path);
		if (previewLabel.empty()) {
			previewLabel = "Audio " + std::to_string(selectedAudioIndex + 1);
		}
		if (ImGui::BeginCombo(labels.comboLabel, previewLabel.c_str())) {
			for (int i = 0; i < static_cast<int>(audioFiles.size()); ++i) {
				const std::string fileLabel =
					ofFilePath::getBaseName(audioFiles[static_cast<size_t>(i)].path);
				const std::string itemLabel = fileLabel.empty()
					? "Audio " + std::to_string(i + 1)
					: fileLabel;
				const bool selected = (selectedAudioIndex == i);
				if (ImGui::Selectable(itemLabel.c_str(), selected)) {
					selectedAudioIndex = i;
					ensureLoaded(selectedAudioIndex, false);
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}

	const bool canPreviewAudio = !generating && !audioFiles.empty();
	ImGui::BeginDisabled(!canPreviewAudio);
	const bool audioLoaded = player.isLoaded() && !loadedAudioPath.empty();
	const bool audioPlaying = audioLoaded && player.isPlaying();
	const char * playPauseLabel = audioPlaying
		? labels.pauseLabel
		: (playbackPaused ? labels.resumeLabel : labels.playLabel);
	if (ImGui::SmallButton(playPauseLabel)) {
		if (!audioLoaded) {
			ensureLoaded(selectedAudioIndex, true);
		} else if (audioPlaying) {
			player.setPaused(true);
			playbackPaused = true;
			statusMessage = labels.pausedStatus;
		} else {
			if (playbackPaused) {
				player.setPaused(false);
			} else {
				player.play();
			}
			playbackPaused = false;
			statusMessage = labels.playingStatus;
		}
	}
	ImGui::SameLine();
	if (ImGui::SmallButton(labels.restartLabel)) {
		if (ensureLoaded(selectedAudioIndex, false)) {
			player.stop();
			player.play();
			playbackPaused = false;
			statusMessage = labels.restartedStatus;
		}
	}
	ImGui::SameLine();
	if (ImGui::SmallButton(labels.stopLabel)) {
		stopPlayback(false);
		statusMessage = labels.stoppedStatus;
	}
	ImGui::EndDisabled();
	ImGui::TextDisabled(
		"%s %s",
		labels.filePrefix,
		audioFiles[static_cast<size_t>(selectedAudioIndex)].path.c_str());
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
	ImGui::InputText("Source video", videoEssaySourceVideoPath, sizeof(videoEssaySourceVideoPath));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse video...##VideoEssay", ImVec2(120, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select video essay source video", false);
		if (result.bSuccess) {
			copyStringToBuffer(videoEssaySourceVideoPath, sizeof(videoEssaySourceVideoPath), result.getPath());
			autoSaveSession();
		}
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(visionVideoPath).empty());
	if (ImGui::SmallButton("Use Vision video##VideoEssay")) {
		copyStringToBuffer(videoEssaySourceVideoPath, sizeof(videoEssaySourceVideoPath), visionVideoPath);
		autoSaveSession();
	}
	ImGui::EndDisabled();
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
		speakVideoEssayReply(true);
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
	ImGui::BeginDisabled(trim(videoEssayVisualConcept).empty());
	if (ImGui::SmallButton("Use in Vision##VideoEssay")) {
		copyStringToBuffer(visionPrompt, sizeof(visionPrompt), videoEssayVisualConcept);
		activeMode = AiMode::Vision;
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
	if (!videoEssayTtsPreview.statusMessage.empty()) {
		ImGui::TextDisabled("%s", videoEssayTtsPreview.statusMessage.c_str());
	}
	if (!videoEssayVlcPreviewStatusMessage.empty()) {
		ImGui::TextDisabled("%s", videoEssayVlcPreviewStatusMessage.c_str());
	}

	if (!videoEssayTtsPreview.audioFiles.empty()) {
		ImGui::Spacing();
		ImGui::Text("Voiceover Audio");
		const TtsPreviewUiLabels ttsLabels = {
			"##VideoEssayVoiceoverArtifact",
			"Pause voiceover",
			"Resume voiceover",
			"Play voiceover",
			"Restart voiceover",
			"Stop voiceover",
			"Voiceover file:",
			"Paused video essay voiceover.",
			"Playing video essay voiceover.",
			"Restarted video essay voiceover.",
			"Stopped video essay voiceover."
		};
		drawTtsPreviewControls(
			generating.load(),
			videoEssayTtsPreview.audioFiles,
			videoEssayTtsPreview.selectedAudioIndex,
			videoEssayTtsPreview.loadedAudioPath,
			videoEssayTtsPreview.playbackPaused,
			videoEssayTtsPreview.player,
			videoEssayTtsPreview.statusMessage,
			ttsLabels,
			[this](int artifactIndex, bool autoplay) {
				return ensureVideoEssayTtsAudioLoaded(artifactIndex, autoplay);
			},
			[this](bool clearLoadedPath) {
				stopVideoEssayTtsPlayback(clearLoadedPath);
			});
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

	if (!videoEssayVisualConcept.empty()) {
		ImGui::Spacing();
		ImGui::Text("Visual Concept");
		ImGui::BeginChild("##VideoEssayVisualConcept", ImVec2(0, 110), true);
		ImGui::TextWrapped("%s", videoEssayVisualConcept.c_str());
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

	ofxGgmlVideoPlan parsedScenePlan;
	bool scenePlanAvailable = false;
	if (!trim(videoEssayScenePlanJson).empty()) {
		const auto parsedResult =
			ofxGgmlVideoPlanner::parsePlanJson(trim(videoEssayScenePlanJson));
		if (parsedResult.isOk()) {
			parsedScenePlan = parsedResult.value();
			scenePlanAvailable = true;
			selectedVideoEssaySceneIndex = std::clamp(
				selectedVideoEssaySceneIndex,
				0,
				std::max(0, static_cast<int>(parsedScenePlan.scenes.size()) - 1));
		}
	}

	if (!videoEssayScenePlanSummary.empty() || !trim(videoEssayScenePlanningError).empty()) {
		ImGui::Spacing();
		ImGui::Text("Scenes");
		if (!videoEssayScenePlanSummary.empty()) {
			ImGui::TextWrapped("%s", videoEssayScenePlanSummary.c_str());
		}
		if (!trim(videoEssayScenePlanningError).empty()) {
			ImGui::TextDisabled("%s", videoEssayScenePlanningError.c_str());
		}
		if (scenePlanAvailable && !parsedScenePlan.scenes.empty()) {
			ImGui::SetNextItemWidth(220);
			if (ImGui::SliderInt(
					"Selected scene",
					&selectedVideoEssaySceneIndex,
					0,
					static_cast<int>(parsedScenePlan.scenes.size()) - 1,
					"%d")) {
				autoSaveSession();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Load in Vision##VideoEssayScenePlan")) {
				copyStringToBuffer(videoPlanJson, sizeof(videoPlanJson), videoEssayScenePlanJson);
				videoPlanSummary = videoEssayScenePlanSummary;
				copyStringToBuffer(visionPrompt, sizeof(visionPrompt), videoEssayVisualConcept);
				videoPlanMultiScene = true;
				activeMode = AiMode::Vision;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Use scene in Diffusion##VideoEssay")) {
				copyStringToBuffer(
					diffusionPrompt,
					sizeof(diffusionPrompt),
					ofxGgmlVideoPlanner::buildScenePrompt(
						parsedScenePlan,
						static_cast<size_t>(selectedVideoEssaySceneIndex)));
				activeMode = AiMode::Diffusion;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Use sequence in Vision##VideoEssay")) {
				copyStringToBuffer(
					visionPrompt,
					sizeof(visionPrompt),
					ofxGgmlVideoPlanner::buildSceneSequencePrompt(parsedScenePlan));
				activeMode = AiMode::Vision;
			}
			const auto & scene =
				parsedScenePlan.scenes[static_cast<size_t>(selectedVideoEssaySceneIndex)];
			ImGui::BeginChild("##VideoEssayScenePreview", ImVec2(0, 130), true);
			ImGui::Text("%d. %s", scene.index, scene.title.c_str());
			if (!scene.summary.empty()) {
				ImGui::TextWrapped("%s", scene.summary.c_str());
			}
			if (!scene.transition.empty()) {
				ImGui::TextDisabled("Transition: %s", scene.transition.c_str());
			}
			if (!scene.eventPrompt.empty()) {
				ImGui::Spacing();
				ImGui::TextWrapped("%s", scene.eventPrompt.c_str());
			}
			ImGui::EndChild();
		}
	}

#if OFXGGML_HAS_OFXVLC4
	if (!trim(videoEssaySrtText).empty()) {
		ImGui::Spacing();
		ImGui::Text("ofxVlc4 Preview / Render");
		ImGui::TextWrapped(
			"Preview the narrated essay subtitles against a source video, then record the VLC texture and mux it with the generated voiceover.");
		ImGui::BeginDisabled(trim(videoEssaySourceVideoPath).empty());
		if (ImGui::Button("Load in ofxVlc4 preview##VideoEssay", ImVec2(190, 0))) {
			std::string error;
			if (loadVideoEssayVlcPreview(&error)) {
				videoEssayVlcPreviewStatusMessage =
					"Loaded the video essay subtitles into the optional ofxVlc4 preview.";
			} else {
				videoEssayVlcPreviewStatusMessage =
					error.empty() ? std::string("Failed to load the video essay VLC preview.") : error;
			}
			autoSaveSession();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!videoEssayVlcPreviewInitialized);
		if (ImGui::Button("Close preview##VideoEssayVlc", ImVec2(150, 0))) {
			closeVideoEssayVlcPreview();
			videoEssayVlcPreviewStatusMessage = "Closed the optional video essay VLC preview.";
			autoSaveSession();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(trim(videoEssaySourceVideoPath).empty());
		if (ImGui::Button(
				(videoEssayVlcPreviewInitialized && videoEssayVlcPreviewPlayer.isVideoRecording())
					? "Stop + Render Video##VideoEssay"
					: "Record Preview##VideoEssay",
				ImVec2(190, 0))) {
			std::string error;
			if (videoEssayVlcPreviewInitialized && videoEssayVlcPreviewPlayer.isVideoRecording()) {
				if (!stopVideoEssayVlcRecording(&error)) {
					videoEssayVlcPreviewStatusMessage =
						error.empty() ? std::string("Failed to finalize the video essay render.") : error;
				}
			} else {
				if (!startVideoEssayVlcRecording(&error)) {
					videoEssayVlcPreviewStatusMessage =
						error.empty() ? std::string("Failed to start recording the video essay preview.") : error;
				}
			}
			autoSaveSession();
		}
		ImGui::EndDisabled();

		if (videoEssayVlcPreviewInitialized) {
			const double sectionStartSeconds =
				getVideoEssaySectionStartSeconds(selectedVideoEssaySceneIndex);
			ImGui::BeginDisabled(sectionStartSeconds <= 0.0);
			if (ImGui::SmallButton("Sync preview to selected section##VideoEssay")) {
				videoEssayVlcPreviewPlayer.setTime(
					static_cast<int>(std::max(0.0, sectionStartSeconds) * 1000.0));
				videoEssayVlcPreviewStatusMessage =
					"Synced the VLC preview to the selected video essay section.";
			}
			ImGui::EndDisabled();
			drawVideoEssayVlcPreview();
		}
		if (!trim(videoEssayLastRenderedVideoPath).empty()) {
			ImGui::TextDisabled("%s", videoEssayLastRenderedVideoPath.c_str());
		}
	}
#else
	if (!trim(videoEssaySrtText).empty()) {
		ImGui::Spacing();
		ImGui::TextDisabled(
			"Regenerate this example with ofxVlc4 in addons.make to enable direct video essay preview and render export here.");
	}
#endif

	if (!videoEssayEditPlanSummary.empty() ||
		!videoEssayEditorBrief.empty() ||
		!trim(videoEssayEditPlanningError).empty()) {
		ImGui::Spacing();
		ImGui::Text("Edit");
		if (!videoEssayEditPlanSummary.empty()) {
			ImGui::TextWrapped("%s", videoEssayEditPlanSummary.c_str());
		}
		if (!trim(videoEssayEditPlanningError).empty()) {
			ImGui::TextDisabled("%s", videoEssayEditPlanningError.c_str());
		}
		ImGui::BeginDisabled(trim(videoEssayEditPlanJson).empty());
		if (ImGui::SmallButton("Load edit plan in Vision##VideoEssay")) {
			copyStringToBuffer(videoEditPlanJson, sizeof(videoEditPlanJson), videoEssayEditPlanJson);
			videoEditPlanSummary = videoEssayEditPlanSummary;
			resetVideoEditWorkflowState();
			activeMode = AiMode::Vision;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(trim(videoEssayEditorBrief).empty());
		if (ImGui::SmallButton("Use brief in Write##VideoEssay")) {
			copyStringToBuffer(writeInput, sizeof(writeInput), videoEssayEditorBrief);
			activeMode = AiMode::Write;
		}
		ImGui::EndDisabled();
		if (!videoEssayEditorBrief.empty()) {
			ImGui::BeginChild("##VideoEssayEditorBrief", ImVec2(0, 135), true);
			ImGui::TextWrapped("%s", videoEssayEditorBrief.c_str());
			ImGui::EndChild();
		}
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
					" voice cues." +
					videoEssayPlanningStatusSuffix(result);
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
				pendingVideoEssayVisualConcept = result.visualConcept;
				pendingVideoEssayScenePlanJson = result.scenePlanJson;
				pendingVideoEssayScenePlanSummary = result.scenePlanSummary;
				pendingVideoEssayScenePlanningError = result.scenePlanningError;
				pendingVideoEssayEditPlanJson = result.editPlanJson;
				pendingVideoEssayEditPlanSummary = result.editPlanSummary;
				pendingVideoEssayEditPlanningError = result.editPlanningError;
				pendingVideoEssayEditorBrief = result.editorBrief;
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
			pendingVideoEssayVisualConcept.clear();
			pendingVideoEssayScenePlanJson.clear();
			pendingVideoEssayScenePlanSummary.clear();
			pendingVideoEssayScenePlanningError.clear();
			pendingVideoEssayEditPlanJson.clear();
			pendingVideoEssayEditPlanSummary.clear();
			pendingVideoEssayEditPlanningError.clear();
			pendingVideoEssayEditorBrief.clear();
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
			pendingVideoEssayVisualConcept.clear();
			pendingVideoEssayScenePlanJson.clear();
			pendingVideoEssayScenePlanSummary.clear();
			pendingVideoEssayScenePlanningError.clear();
			pendingVideoEssayEditPlanJson.clear();
			pendingVideoEssayEditPlanSummary.clear();
			pendingVideoEssayEditPlanningError.clear();
			pendingVideoEssayEditorBrief.clear();
			pendingVideoEssayCitations.clear();
			pendingVideoEssaySections.clear();
			pendingVideoEssayVoiceCues.clear();
		}

		generating.store(false);
	});
}

void ofApp::drawLongVideoPanel() {
	drawPanelHeader("Video", "plan chunked video prompts with continuity and shot progression");
	ImGui::TextWrapped(
		"Use this mode for trailers, shorts, music videos, ads, mood loops, or longer sequences. "
		"It builds a continuity bible, segment prompts, and a reusable manifest from one brief.");

	ImGui::Spacing();
	ImGui::Text("Project brief");
	ImGui::InputTextMultiline(
		"##LongVideoConcept",
		longVideoConcept,
		sizeof(longVideoConcept),
		ImVec2(-1, 96));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}

	ImGui::InputText("Visual direction", longVideoStyle, sizeof(longVideoStyle));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::InputText("Avoid", longVideoNegativeStyle, sizeof(longVideoNegativeStyle));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::InputTextMultiline(
		"Creative / continuity goal",
		longVideoContinuityGoal,
		sizeof(longVideoContinuityGoal),
		ImVec2(-1, 70));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}

	if (ImGui::SliderFloat(
		"Target duration (s)##LongVideo",
		&longVideoTargetDurationSeconds,
		8.0f,
		360.0f,
		"%.0f s")) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::SliderInt("Segments##LongVideo", &longVideoChunkCount, 1, 16)) {
		autoSaveSession();
	}

	ImGui::SetNextItemWidth(240);
	if (ImGui::Combo(
		"Structure##LongVideo",
		&longVideoStructureIndex,
		kLongVideoStructureLabels,
		IM_ARRAYSIZE(kLongVideoStructureLabels))) {
		autoSaveSession();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(220);
	if (ImGui::Combo(
		"Pacing##LongVideo",
		&longVideoPacingIndex,
		kLongVideoPacingLabels,
		IM_ARRAYSIZE(kLongVideoPacingLabels))) {
		autoSaveSession();
	}
	if (ImGui::Checkbox(
		"Favor loopable ending##LongVideo",
		&longVideoFavorLoopableEnding)) {
		autoSaveSession();
	}

	if (ImGui::SliderInt("Width##LongVideo", &longVideoWidth, 128, 1280)) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::SliderInt("Height##LongVideo", &longVideoHeight, 128, 1280)) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::SliderInt("FPS##LongVideo", &longVideoFps, 1, 30)) {
		autoSaveSession();
	}

	if (ImGui::SliderInt(
		"Frames / segment##LongVideo",
		&longVideoFramesPerChunk,
		8,
		160)) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::InputInt("Seed##LongVideo", &longVideoSeed)) {
		autoSaveSession();
	}
	if (ImGui::Checkbox(
		"Use prompt inheritance##LongVideo",
		&longVideoUsePromptInheritance)) {
		autoSaveSession();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(visionPrompt).empty());
	if (ImGui::SmallButton("Use Vision prompt##LongVideo")) {
		copyStringToBuffer(longVideoConcept, sizeof(longVideoConcept), visionPrompt);
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(videoEssayVisualConcept).empty());
	if (ImGui::SmallButton("Use Video Essay concept##LongVideo")) {
		copyStringToBuffer(longVideoConcept, sizeof(longVideoConcept), videoEssayVisualConcept);
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(diffusionPrompt).empty());
	if (ImGui::SmallButton("Use Diffusion prompt##LongVideo")) {
		copyStringToBuffer(longVideoConcept, sizeof(longVideoConcept), diffusionPrompt);
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(visionOutput).empty());
	if (ImGui::SmallButton("Use Vision analysis##LongVideo")) {
		copyStringToBuffer(longVideoContinuityGoal, sizeof(longVideoContinuityGoal), visionOutput);
		autoSaveSession();
	}
	ImGui::EndDisabled();

	ImGui::BeginDisabled(generating.load() || trim(longVideoConcept).empty());
	if (ImGui::Button("Plan Video", ImVec2(170, 0))) {
		runLongVideoPlanning();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(longVideoManifestJson).empty());
	if (ImGui::SmallButton("Copy manifest##LongVideo")) {
		copyToClipboard(longVideoManifestJson);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(longVideoContinuityBible).empty());
	if (ImGui::SmallButton("Copy continuity bible##LongVideo")) {
		copyToClipboard(longVideoContinuityBible);
	}
	ImGui::EndDisabled();

	if (!longVideoStatus.empty()) {
		ImGui::Spacing();
		ImGui::TextWrapped("%s", longVideoStatus.c_str());
	}

	if (!longVideoContinuityBible.empty()) {
		ImGui::Spacing();
		ImGui::Text("Continuity Bible");
		ImGui::BeginChild("##LongVideoContinuity", ImVec2(0, 120), true);
		ImGui::TextWrapped("%s", longVideoContinuityBible.c_str());
		ImGui::EndChild();
	}

	if (!longVideoChunks.empty()) {
		ImGui::Spacing();
		ImGui::Text("Segments");
		for (size_t i = 0; i < longVideoChunks.size(); ++i) {
			const auto & chunk = longVideoChunks[i];
			if (ImGui::CollapsingHeader(
				(chunk.id + " - " + chunk.title).c_str(),
				ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::TextWrapped("Goal: %s", chunk.sectionGoal.c_str());
				ImGui::TextWrapped("Continuity: %s", chunk.continuityNote.c_str());
				ImGui::TextWrapped(
					"Timing %.1fs -> %.1fs | Progression %.2f",
					chunk.startSeconds,
					chunk.endSeconds,
					chunk.progressionWeight);
				ImGui::TextWrapped("Transition: %s", chunk.transitionHint.c_str());
				ImGui::TextWrapped(
					"Duration %.1fs | %dx%d | %d fps | %d frames | seed %lld",
					chunk.targetDurationSeconds,
					chunk.width,
					chunk.height,
					chunk.fps,
					chunk.frameCount,
					static_cast<long long>(chunk.seed));
				if (ImGui::SmallButton(("Use in Vision##LongVideo" + std::to_string(i)).c_str())) {
					copyStringToBuffer(visionPrompt, sizeof(visionPrompt), chunk.prompt);
					activeMode = AiMode::Vision;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton(("Use in Diffusion##LongVideo" + std::to_string(i)).c_str())) {
					copyStringToBuffer(diffusionPrompt, sizeof(diffusionPrompt), chunk.prompt);
					copyStringToBuffer(
						diffusionNegativePrompt,
						sizeof(diffusionNegativePrompt),
						chunk.negativePrompt);
					activeMode = AiMode::Diffusion;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton(("Copy prompt##LongVideo" + std::to_string(i)).c_str())) {
					copyToClipboard(chunk.prompt);
				}
				ImGui::BeginChild(
					("##LongVideoPrompt" + std::to_string(i)).c_str(),
					ImVec2(0, 90),
					true);
				ImGui::TextWrapped("%s", chunk.prompt.c_str());
				ImGui::EndChild();
			}
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	ImGui::Text("Render Video");
#if OFXGGML_HAS_OFXSTABLEDIFFUSION
	ImGui::TextWrapped(
		"Render the current project as source-image-based video with the local ofxStableDiffusion backend. "
		"Use a source frame plus one planned segment prompt, or fall back to the project brief if you have not planned segments yet.");

	const std::string selectedRenderModelLabel = getSelectedVideoRenderModelLabel();
	if (!selectedRenderModelLabel.empty()) {
		ImGui::TextDisabled("Render model: %s", selectedRenderModelLabel.c_str());
	}
	ImGui::TextDisabled(
		"Backend: ofxStableDiffusion | current runtime path is image-to-video, not native text-to-video.");

	longVideoRenderSelectedChunkIndex = std::clamp(
		longVideoRenderSelectedChunkIndex,
		0,
		std::max(0, static_cast<int>(longVideoChunks.size()) - 1));

	if (!longVideoChunks.empty()) {
		std::string selectedChunkLabel = "Project brief fallback";
		if (longVideoRenderSelectedChunkIndex >= 0 &&
			longVideoRenderSelectedChunkIndex < static_cast<int>(longVideoChunks.size())) {
			const auto & selectedChunk =
				longVideoChunks[static_cast<size_t>(longVideoRenderSelectedChunkIndex)];
			selectedChunkLabel = selectedChunk.id + " - " + selectedChunk.title;
		}
		if (ImGui::BeginCombo("Segment", selectedChunkLabel.c_str())) {
			for (int i = 0; i < static_cast<int>(longVideoChunks.size()); ++i) {
				const auto & chunk = longVideoChunks[static_cast<size_t>(i)];
				const std::string label = chunk.id + " - " + chunk.title;
				const bool selected = (longVideoRenderSelectedChunkIndex == i);
				if (ImGui::Selectable(label.c_str(), selected)) {
					longVideoRenderSelectedChunkIndex = i;
					autoSaveSession();
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}

	ImGui::SetNextItemWidth(220);
	if (ImGui::Combo(
		"Render preset",
		&longVideoRenderPresetIndex,
		kLongVideoRenderPresetLabels,
		IM_ARRAYSIZE(kLongVideoRenderPresetLabels))) {
		autoSaveSession();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(180);
	if (ImGui::Combo(
		"Motion mode",
		&longVideoRenderModeIndex,
		kLongVideoRenderModeLabels,
		IM_ARRAYSIZE(kLongVideoRenderModeLabels))) {
		autoSaveSession();
	}

	ImGui::InputText(
		"Source image",
		longVideoRenderSourceImagePath,
		sizeof(longVideoRenderSourceImagePath));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse...##LongVideoRenderSource", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select source image for video render", false);
		if (result.bSuccess) {
			copyStringToBuffer(
				longVideoRenderSourceImagePath,
				sizeof(longVideoRenderSourceImagePath),
				result.getPath());
			autoSaveSession();
		}
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(visionImagePath).empty());
	if (ImGui::SmallButton("Use Vision image##LongVideoRender")) {
		copyStringToBuffer(
			longVideoRenderSourceImagePath,
			sizeof(longVideoRenderSourceImagePath),
			visionImagePath);
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(diffusionInitImagePath).empty());
	if (ImGui::SmallButton("Use Image init##LongVideoRender")) {
		copyStringToBuffer(
			longVideoRenderSourceImagePath,
			sizeof(longVideoRenderSourceImagePath),
			diffusionInitImagePath);
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(getPreferredDiffusionReuseImagePath().empty());
	if (ImGui::SmallButton("Use last Image output##LongVideoRender")) {
		copyStringToBuffer(
			longVideoRenderSourceImagePath,
			sizeof(longVideoRenderSourceImagePath),
			getPreferredDiffusionReuseImagePath());
		autoSaveSession();
	}
	ImGui::EndDisabled();

	ImGui::InputText(
		"End image (optional)",
		longVideoRenderEndImagePath,
		sizeof(longVideoRenderEndImagePath));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse...##LongVideoRenderEnd", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select optional end image for video render", false);
		if (result.bSuccess) {
			copyStringToBuffer(
				longVideoRenderEndImagePath,
				sizeof(longVideoRenderEndImagePath),
				result.getPath());
			autoSaveSession();
		}
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Clear##LongVideoRenderEnd")) {
		longVideoRenderEndImagePath[0] = '\0';
		autoSaveSession();
	}

	drawDiffusionImagePreview(
		"Source preview",
		trim(longVideoRenderSourceImagePath),
		longVideoRenderSourcePreviewImage,
		longVideoRenderSourcePreviewError,
		"##LongVideoRenderSourcePreview");
	if (!trim(longVideoRenderEndImagePath).empty()) {
		drawDiffusionImagePreview(
			"End preview",
			trim(longVideoRenderEndImagePath),
			longVideoRenderEndPreviewImage,
			longVideoRenderEndPreviewError,
			"##LongVideoRenderEndPreview");
	}

	ImGui::InputText(
		"Output dir",
		longVideoRenderOutputDir,
		sizeof(longVideoRenderOutputDir));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Use data/generated##LongVideoRender")) {
		copyStringToBuffer(
			longVideoRenderOutputDir,
			sizeof(longVideoRenderOutputDir),
			ofToDataPath("generated/video", true));
		autoSaveSession();
	}
	ImGui::InputText(
		"Output prefix",
		longVideoRenderOutputPrefix,
		sizeof(longVideoRenderOutputPrefix));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}

	const bool canRenderLongVideo =
		!generating.load() &&
		!trim(longVideoRenderSourceImagePath).empty();
	ImGui::BeginDisabled(!canRenderLongVideo);
	if (ImGui::Button("Render Segment", ImVec2(170, 0))) {
		runLongVideoRenderGeneration();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(longVideoRenderManifestJson).empty());
	if (ImGui::SmallButton("Copy render manifest##LongVideo")) {
		copyToClipboard(longVideoRenderManifestJson);
	}
	ImGui::EndDisabled();

	if (!longVideoRenderStatus.empty()) {
		ImGui::TextWrapped("%s", longVideoRenderStatus.c_str());
	}
	if (!longVideoRenderOutputDirectory.empty()) {
		ImGui::TextDisabled("Frames: %s", longVideoRenderOutputDirectory.c_str());
	}
	if (!longVideoRenderMetadataPath.empty()) {
		ImGui::TextDisabled("Clip metadata: %s", longVideoRenderMetadataPath.c_str());
	}
	if (!longVideoRenderManifestPath.empty()) {
		ImGui::TextDisabled("Render manifest: %s", longVideoRenderManifestPath.c_str());
	}
	if (!longVideoRenderManifestJson.empty()) {
		ImGui::BeginChild("##LongVideoRenderManifest", ImVec2(0, 120), true);
		ImGui::TextWrapped("%s", longVideoRenderManifestJson.c_str());
		ImGui::EndChild();
	}
#else
	ImGui::TextWrapped(
		"Add ofxStableDiffusion to the generated example project to enable local source-image video rendering here.");
#endif

	if (!longVideoManifestJson.empty()) {
		ImGui::Spacing();
		ImGui::Text("Manifest");
		ImGui::BeginChild("##LongVideoManifest", ImVec2(0, 180), true);
		ImGui::TextWrapped("%s", longVideoManifestJson.c_str());
		ImGui::EndChild();
	}
}

void ofApp::runLongVideoPlanning() {
	if (generating.load()) {
		return;
	}

	const std::string conceptText = trim(longVideoConcept);
	if (conceptText.empty()) {
		longVideoStatus = "[Error] Video concept is empty.";
		return;
	}

	configureEasyApiFromCurrentUi();
	const std::string modelPath = getSelectedModelPath();
	const ofxGgmlInferenceSettings inferenceSettings =
		buildCurrentTextInferenceSettings(AiMode::LongVideo);
	const std::string style = trim(longVideoStyle);
	const std::string negativeStyle = trim(longVideoNegativeStyle);
	const std::string continuityGoal = trim(longVideoContinuityGoal);
	const double targetDurationSeconds = std::clamp(
		static_cast<double>(longVideoTargetDurationSeconds),
		8.0,
		360.0);
	const int chunkCount = std::clamp(longVideoChunkCount, 1, 16);
	const int width = std::clamp(longVideoWidth, 128, 1920);
	const int height = std::clamp(longVideoHeight, 128, 1920);
	const int fps = std::clamp(longVideoFps, 1, 60);
	const int framesPerChunk = std::clamp(longVideoFramesPerChunk, 8, 240);
	const int64_t seedValue = static_cast<int64_t>(longVideoSeed);
	const bool usePromptInheritance = longVideoUsePromptInheritance;
	const std::string structureHint = longVideoStructureHintForIndex(longVideoStructureIndex);
	const std::string pacingProfile = longVideoPacingProfileForIndex(longVideoPacingIndex);
	const bool favorLoopableEnding = longVideoFavorLoopableEnding;
	const std::string renderModelPath = getSelectedVideoRenderModelPath();
	const std::string renderModelName = getSelectedVideoRenderModelLabel();
	std::string renderModelUrl;
	if (!videoRenderPresets.empty() &&
		selectedVideoRenderPresetIndex >= 0 &&
		selectedVideoRenderPresetIndex < static_cast<int>(videoRenderPresets.size())) {
		renderModelUrl =
			videoRenderPresets[static_cast<size_t>(selectedVideoRenderPresetIndex)].url;
	}

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::LongVideo;
	generatingStatus = "Planning video chunks and continuity...";
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput =
			"Building a video continuity bible, chunk plan, and manifest...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	workerThread = std::thread([this,
		modelPath,
		inferenceSettings,
		conceptText,
		style,
		negativeStyle,
		continuityGoal,
		targetDurationSeconds,
		chunkCount,
		width,
		height,
		fps,
		framesPerChunk,
		seedValue,
		usePromptInheritance,
		structureHint,
		pacingProfile,
		favorLoopableEnding,
		renderModelName,
		renderModelPath,
		renderModelUrl]() {
		try {
			ofxGgmlLongVideoPlanRequest request;
			request.modelPath = modelPath;
			request.conceptText = conceptText;
			request.style = style;
			request.negativeStyle = negativeStyle;
			request.continuityGoal = continuityGoal;
			request.targetDurationSeconds = targetDurationSeconds;
			request.chunkCount = chunkCount;
			request.width = width;
			request.height = height;
			request.fps = fps;
			request.framesPerChunk = framesPerChunk;
			request.seed = seedValue;
			request.usePromptInheritance = usePromptInheritance;
			request.structureHint = structureHint;
			request.pacingProfile = pacingProfile;
			request.favorLoopableEnding = favorLoopableEnding;
			request.renderBackend = "ofxStableDiffusion";
			request.renderModelName = renderModelName;
			request.renderModelPath = renderModelPath;
			request.renderModelUrl = renderModelUrl;
			request.inferenceSettings = inferenceSettings;

			const ofxGgmlLongVideoPlanResult result = easyApi.planLongVideo(request);
			std::string status;
			if (result.success) {
				status =
					"Planned " +
					ofToString(result.chunks.size()) +
					" video segments with " +
					structureHint +
					" structure and built a manifest.";
			} else {
				status = "[Error] " + result.error;
			}

			{
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingLongVideoDirty = true;
				pendingLongVideoStatus = status;
				pendingLongVideoContinuityBible = result.continuityBible;
				pendingLongVideoManifestJson = result.manifestJson;
				pendingLongVideoChunks = result.chunks;
			}
		} catch (const std::exception & e) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingLongVideoDirty = true;
			pendingLongVideoStatus =
				std::string("[Error] Video planning exception: ") + e.what();
			pendingLongVideoContinuityBible.clear();
			pendingLongVideoManifestJson.clear();
			pendingLongVideoChunks.clear();
		} catch (...) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingLongVideoDirty = true;
			pendingLongVideoStatus = "[Error] Unknown video planning exception.";
			pendingLongVideoContinuityBible.clear();
			pendingLongVideoManifestJson.clear();
			pendingLongVideoChunks.clear();
		}

		generating.store(false);
	});
}

void ofApp::runLongVideoRenderGeneration() {
	if (generating.load()) {
		return;
	}

#if !OFXGGML_HAS_OFXSTABLEDIFFUSION
	longVideoRenderStatus =
		"[Error] Video rendering needs ofxStableDiffusion in this build.";
	return;
#else
	if (!ensureDiffusionBackendConfigured() || !stableDiffusionEngine) {
		longVideoRenderStatus =
			"[Error] ofxStableDiffusion is not attached in this build.";
		return;
	}

	const std::string sourceImagePath = trim(longVideoRenderSourceImagePath);
	if (sourceImagePath.empty()) {
		longVideoRenderStatus = "[Error] Select a source image first.";
		return;
	}

	const std::string renderModelPath = trim(getSelectedVideoRenderModelPath());
	const std::string renderModelLabel = getSelectedVideoRenderModelLabel();
	if (renderModelPath.empty()) {
		longVideoRenderStatus =
			"[Error] Select or load a Video Render Model first.";
		return;
	}

	const std::string outputDirText = trim(longVideoRenderOutputDir);
	const std::string outputDir = outputDirText.empty()
		? ofToDataPath("generated/video", true)
		: outputDirText;
	const std::string outputPrefix = sanitizeVideoFilenameStem(
		trim(longVideoRenderOutputPrefix),
		"video_segment");
	const int presetIndex = std::clamp(longVideoRenderPresetIndex, 0, 4);
	const int modeIndex = std::clamp(longVideoRenderModeIndex, 0, 3);
	const int chunkIndex = std::clamp(
		longVideoRenderSelectedChunkIndex,
		0,
		std::max(0, static_cast<int>(longVideoChunks.size()) - 1));
	const std::string endImagePath = trim(longVideoRenderEndImagePath);
	const std::string vaePath = trim(diffusionVaePath);
	const std::string projectBrief = trim(longVideoConcept);
	const std::string visualDirection = trim(longVideoStyle);
	const std::string avoidText = trim(longVideoNegativeStyle);
	const std::string continuityGoal = trim(longVideoContinuityGoal);

	ofxGgmlLongVideoPlanChunk selectedChunk;
	if (!longVideoChunks.empty() &&
		chunkIndex >= 0 &&
		chunkIndex < static_cast<int>(longVideoChunks.size())) {
		selectedChunk = longVideoChunks[static_cast<size_t>(chunkIndex)];
	} else {
		selectedChunk.index = 0;
		selectedChunk.id = "project_brief";
		selectedChunk.title = "Project Brief";
		selectedChunk.prompt = projectBrief;
		selectedChunk.negativePrompt = avoidText;
		selectedChunk.sectionGoal = continuityGoal;
		selectedChunk.continuityNote = continuityGoal;
		selectedChunk.width = std::clamp(longVideoWidth, 128, 1920);
		selectedChunk.height = std::clamp(longVideoHeight, 128, 1920);
		selectedChunk.fps = std::clamp(longVideoFps, 1, 60);
		selectedChunk.frameCount = std::clamp(longVideoFramesPerChunk, 8, 240);
		selectedChunk.seed = static_cast<int64_t>(longVideoSeed);
	}

	const std::string chunkPrompt = trim(selectedChunk.prompt).empty()
		? projectBrief
		: trim(selectedChunk.prompt);
	if (chunkPrompt.empty()) {
		longVideoRenderStatus =
			"[Error] Render prompt is empty. Plan the video first or add a project brief.";
		return;
	}

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::LongVideo;
	generatingStatus = "Rendering video segment...";
	longVideoRenderStatus = "Rendering video segment...";
	longVideoRenderOutputDirectory.clear();
	longVideoRenderMetadataPath.clear();
	longVideoRenderManifestPath.clear();
	longVideoRenderManifestJson.clear();
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = "Preparing source images and video render request...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	workerThread = std::thread([this,
		sourceImagePath,
		endImagePath,
		renderModelPath,
		outputDir,
		outputPrefix,
		presetIndex,
		modeIndex,
		selectedChunk,
		chunkPrompt,
		projectBrief,
		visualDirection,
		avoidText,
		continuityGoal,
		vaePath,
		renderModelLabel]() {
		auto clearPendingRender = [this]() {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingLongVideoRenderDirty = true;
			pendingLongVideoRenderStatus.clear();
			pendingLongVideoRenderOutputDirectory.clear();
			pendingLongVideoRenderMetadataPath.clear();
			pendingLongVideoRenderManifestPath.clear();
			pendingLongVideoRenderManifestJson.clear();
		};

		try {
			std::error_code fileEc;
			if (!std::filesystem::exists(sourceImagePath, fileEc) || fileEc) {
				clearPendingRender();
				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingLongVideoRenderStatus =
						"[Error] Source image was not found: " + sourceImagePath;
				}
				generating.store(false);
				return;
			}
			fileEc.clear();
			if (!std::filesystem::exists(renderModelPath, fileEc) || fileEc) {
				clearPendingRender();
				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingLongVideoRenderStatus =
						"[Error] Video render model was not found: " + renderModelPath;
				}
				generating.store(false);
				return;
			}

			ofImage sourceImage;
			if (!sourceImage.load(sourceImagePath)) {
				clearPendingRender();
				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingLongVideoRenderStatus =
						"[Error] Failed to load source image: " + sourceImagePath;
				}
				generating.store(false);
				return;
			}

			ofImage endImage;
			if (!endImagePath.empty() && !endImage.load(endImagePath)) {
				clearPendingRender();
				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingLongVideoRenderStatus =
						"[Error] Failed to load end image: " + endImagePath;
				}
				generating.store(false);
				return;
			}

			ofxStableDiffusionContextSettings context =
				stableDiffusionEngine->getContextSettings();
			context.modelPath = renderModelPath;
			context.diffusionModelPath.clear();
			context.clipLPath.clear();
			context.clipGPath.clear();
			context.t5xxlPath.clear();
			context.vaePath = vaePath;
			stableDiffusionEngine->configureContext(context);

			ofxStableDiffusionVideoRequest request;
			request.prompt = chunkPrompt;
			if (!visualDirection.empty()) {
				request.prompt += "\n\nVisual direction: " + visualDirection;
			}
			if (!trim(selectedChunk.sectionGoal).empty()) {
				request.prompt += "\nSection goal: " + trim(selectedChunk.sectionGoal);
			}
			if (!trim(selectedChunk.continuityNote).empty()) {
				request.prompt += "\nContinuity note: " + trim(selectedChunk.continuityNote);
			}
			if (!continuityGoal.empty()) {
				request.prompt += "\nProject continuity goal: " + continuityGoal;
			}
			request.negativePrompt = trim(selectedChunk.negativePrompt).empty()
				? avoidText
				: trim(selectedChunk.negativePrompt);
			request.width = std::max(128, selectedChunk.width);
			request.height = std::max(128, selectedChunk.height);
			request.frameCount = std::max(8, selectedChunk.frameCount);
			request.fps = std::max(1, selectedChunk.fps);
			request.seed = selectedChunk.seed;
			request.initImage = {
				static_cast<uint32_t>(sourceImage.getWidth()),
				static_cast<uint32_t>(sourceImage.getHeight()),
				static_cast<uint32_t>(sourceImage.getPixels().getNumChannels()),
				const_cast<unsigned char *>(sourceImage.getPixels().getData())
			};
			if (!endImagePath.empty()) {
				request.endImage = {
					static_cast<uint32_t>(endImage.getWidth()),
					static_cast<uint32_t>(endImage.getPixels().getHeight()),
					static_cast<uint32_t>(endImage.getPixels().getNumChannels()),
					const_cast<unsigned char *>(endImage.getPixels().getData())
				};
			}

			switch (modeIndex) {
			case 1: request.mode = ofxStableDiffusionVideoMode::Loop; break;
			case 2: request.mode = ofxStableDiffusionVideoMode::PingPong; break;
			case 3: request.mode = ofxStableDiffusionVideoMode::Boomerang; break;
			default:
				request.mode = ofxStableDiffusionVideoMode::Standard;
				break;
			}

			const auto preset =
				static_cast<ofxStableDiffusionVideoWorkflowPreset>(presetIndex);
			ofxStableDiffusionApplyVideoWorkflowPreset(&request, preset);

			const auto validation =
				ofxStableDiffusionValidateVideoPromptWorkflow(request);
			if (!validation.ok) {
				std::ostringstream errorText;
				for (size_t i = 0; i < validation.errors.size(); ++i) {
					if (i > 0) {
						errorText << " ";
					}
					errorText << validation.errors[i];
				}
				clearPendingRender();
				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingLongVideoRenderStatus =
						"[Error] " + errorText.str();
				}
				generating.store(false);
				return;
			}

			const std::string chunkStem = sanitizeVideoFilenameStem(
				trim(selectedChunk.id).empty() ? selectedChunk.title : selectedChunk.id,
				"segment");
			const std::filesystem::path renderDirectory =
				std::filesystem::path(outputDir) / chunkStem;
			std::filesystem::create_directories(renderDirectory);

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput =
					"Rendering " + chunkStem + " with " +
					ofxStableDiffusionVideoWorkflowPresetLabel(preset) + " preset...";
			}

			stableDiffusionEngine->generateVideo(request);
			while (stableDiffusionEngine->isGenerating()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}

			const ofxStableDiffusionResult result =
				stableDiffusionEngine->getLastResult();
			if (!result.success || !result.hasVideo()) {
				const std::string backendError = trim(stableDiffusionEngine->getLastError());
				const std::string effectiveError =
					!backendError.empty() ? backendError : result.error;
				std::ostringstream errorText;
				if (!effectiveError.empty()) {
					errorText << effectiveError;
				} else {
					errorText << "Video render backend returned no video.";
				}
				if (!renderModelLabel.empty()) {
					errorText << " Model: " << renderModelLabel << ".";
				}
				errorText << " Path: " << renderModelPath << ".";
				clearPendingRender();
				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingLongVideoRenderStatus = "[Error] " + errorText.str();
				}
				generating.store(false);
				return;
			}

			const std::string framePrefix = outputPrefix + "_" + chunkStem;
			const std::string metadataFilename = "metadata.json";
			if (!stableDiffusionEngine->saveVideoFramesWithMetadata(
					renderDirectory.string(),
					framePrefix,
					metadataFilename)) {
				clearPendingRender();
				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingLongVideoRenderStatus =
						"[Error] Video frames rendered, but saving the frame sequence failed.";
				}
				generating.store(false);
				return;
			}

			const std::filesystem::path metadataPath =
				renderDirectory / metadataFilename;
			const std::filesystem::path manifestPath =
				renderDirectory / "render-manifest.json";
			ofJson manifest = ofxStableDiffusionBuildVideoRenderManifest(
				request,
				result.video,
				preset,
				renderDirectory.string(),
				metadataPath.string());
			manifest["project_brief"] = projectBrief;
			manifest["visual_direction"] = visualDirection;
			manifest["avoid"] = avoidText;
			manifest["continuity_goal"] = continuityGoal;
			manifest["render_model"]["path"] = renderModelPath;
			manifest["render_model"]["label"] = renderModelLabel;
			manifest["source_image"] = sourceImagePath;
			manifest["end_image"] = endImagePath;
			manifest["chunk"] = {
				{"id", selectedChunk.id},
				{"title", selectedChunk.title},
				{"section_goal", selectedChunk.sectionGoal},
				{"continuity_note", selectedChunk.continuityNote},
				{"transition_hint", selectedChunk.transitionHint},
				{"target_duration_seconds", selectedChunk.targetDurationSeconds},
				{"progression_weight", selectedChunk.progressionWeight}
			};
			ofSavePrettyJson(manifestPath.string(), manifest);

			std::ostringstream status;
			status << "Rendered "
				<< (trim(selectedChunk.title).empty() ? chunkStem : trim(selectedChunk.title))
				<< " as "
				<< static_cast<int>(result.video.frames.size())
				<< " frames at "
				<< request.fps
				<< " fps using "
				<< ofxStableDiffusionVideoWorkflowPresetLabel(preset)
				<< ".";

			{
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingLongVideoRenderDirty = true;
				pendingLongVideoRenderStatus = status.str();
				pendingLongVideoRenderOutputDirectory = renderDirectory.string();
				pendingLongVideoRenderMetadataPath = metadataPath.string();
				pendingLongVideoRenderManifestPath = manifestPath.string();
				pendingLongVideoRenderManifestJson = manifest.dump(2);
			}
		} catch (const std::exception & e) {
			clearPendingRender();
			{
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingLongVideoRenderStatus =
					std::string("[Error] Video render exception: ") + e.what();
			}
		} catch (...) {
			clearPendingRender();
			{
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingLongVideoRenderStatus =
					"[Error] Unknown video render exception.";
			}
		}

		generating.store(false);
	});
#endif
}
