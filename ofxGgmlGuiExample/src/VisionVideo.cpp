#include "ofApp.h"

#include "ImHelpers.h"
#include "utils/ImGuiHelpers.h"
#include "utils/TextPromptHelpers.h"
#include "utils/ModelHelpers.h"
#include "utils/ServerHelpers.h"
#include "utils/VisionHelpers.h"
#include "utils/ConsoleHelpers.h"
#include "utils/PathHelpers.h"
#include "utils/BackendHelpers.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <thread>

namespace {

constexpr float kVisionWaitingDotsAnimationSpeed = 3.0f;
const char * const kVisionWaitingLabels[] = {
	"generating",
	"generating.",
	"generating..",
	"generating..."
};

constexpr const char * kDefaultManagedTextServerUrl = "http://127.0.0.1:8080";

struct VideoEditPresetDefinition {
	const char * name;
	const char * goal;
	int clipCount;
	float targetDurationSeconds;
	bool useCurrentAnalysis;
};

constexpr VideoEditPresetDefinition kVideoEditPresets[] = {
	{
		"Trailer",
		"Turn this source clip into a punchy trailer with a strong hook, fast escalation, and a clean payoff. Favor dramatic pacing, bold transitions, and one memorable closing beat.",
		6,
		20.0f,
		true
	},
	{
		"Montage",
		"Turn this source clip into a rhythmic montage that prioritizes emotional callbacks, visual variety, and smooth continuity between the strongest moments.",
		7,
		30.0f,
		true
	},
	{
		"Recap",
		"Turn this source clip into a concise recap edit that preserves the important beats, clarifies the story progression, and trims repetition.",
		5,
		25.0f,
		true
	},
	{
		"Music Video",
		"Turn this source clip into a music-video style edit with rhythm-aware pacing, visual texture, stylized inserts, and strong transitions.",
		8,
		35.0f,
		true
	},
	{
		"Social Short",
		"Turn this source clip into a short-form social edit with an immediate hook, aggressive trimming, readable text moments, and a clear ending beat.",
		4,
		12.0f,
		true
	},
	{
		"Product Teaser",
		"Turn this source clip into a compact product teaser that spotlights the clearest features, keeps momentum high, and ends on a memorable value moment.",
		5,
		18.0f,
		true
	}
};

constexpr int kVideoEditPresetCount =
	static_cast<int>(sizeof(kVideoEditPresets) / sizeof(kVideoEditPresets[0]));

} // namespace

void ofApp::resetVideoEditWorkflowState() {
	videoEditWorkflowActiveStepIndex = -1;
	videoEditWorkflowCompletedStepIndices.clear();
}

bool ofApp::isVideoEditWorkflowStepCompleted(int stepIndex) const {
	return std::find(
		videoEditWorkflowCompletedStepIndices.begin(),
		videoEditWorkflowCompletedStepIndices.end(),
		stepIndex) != videoEditWorkflowCompletedStepIndices.end();
}

void ofApp::setVideoEditWorkflowStepCompleted(int stepIndex, bool completed) {
	if (stepIndex < 0) {
		return;
	}
	auto it = std::find(
		videoEditWorkflowCompletedStepIndices.begin(),
		videoEditWorkflowCompletedStepIndices.end(),
		stepIndex);
	if (completed) {
		if (it == videoEditWorkflowCompletedStepIndices.end()) {
			videoEditWorkflowCompletedStepIndices.push_back(stepIndex);
			std::sort(
				videoEditWorkflowCompletedStepIndices.begin(),
				videoEditWorkflowCompletedStepIndices.end());
		}
	} else if (it != videoEditWorkflowCompletedStepIndices.end()) {
		videoEditWorkflowCompletedStepIndices.erase(it);
	}
}

void ofApp::drawVisionPanel() {
	drawPanelHeader("Vision", "image / video-to-text via llama-server multimodal models");
	const float compactModeFieldWidth = std::min(280.0f, ImGui::GetContentRegionAvail().x);
	ensureVisionPreviewResources();
	if (hasDeferredMontageSubtitlePath) {
		copyStringToBuffer(
			montageSubtitlePath,
			sizeof(montageSubtitlePath),
			deferredMontageSubtitlePath);
		hasDeferredMontageSubtitlePath = false;
		deferredMontageSubtitlePath.clear();
	}

	const auto applyVisionProfileDefaults =
		[this](const ofxGgmlVisionModelProfile & profile, bool onlyWhenEmpty) {
			if (!profile.serverUrl.empty() &&
				(!onlyWhenEmpty || trim(visionServerUrl).empty())) {
				copyStringToBuffer(
					visionServerUrl,
					sizeof(visionServerUrl),
					profile.serverUrl);
			}
			const std::string suggestedPath =
				suggestedModelPath(profile.modelPath, profile.modelFileHint);
			if (!suggestedPath.empty() &&
				(!onlyWhenEmpty || trim(visionModelPath).empty())) {
				copyStringToBuffer(
					visionModelPath,
					sizeof(visionModelPath),
					suggestedPath);
			}
		};

	bool loadedVisionProfiles = false;
	if (visionProfiles.empty()) {
		visionProfiles = ofxGgmlVisionInference::defaultProfiles();
		loadedVisionProfiles = !visionProfiles.empty();
	}
	selectedVisionProfileIndex = std::clamp(
		selectedVisionProfileIndex,
		0,
		std::max(0, static_cast<int>(visionProfiles.size()) - 1));
	if (loadedVisionProfiles && !visionProfiles.empty()) {
		applyVisionProfileDefaults(
			visionProfiles[static_cast<size_t>(selectedVisionProfileIndex)],
			true);
	}

	ImGui::TextWrapped(
		"Use a llama-server instance that is already running with a multimodal GGUF model. "
		"This panel sends OpenAI-compatible image requests with local files encoded as data URLs, and can also route sampled video frames through the same server.");

	if (!visionProfiles.empty()) {
		std::vector<const char *> profileNames;
		profileNames.reserve(visionProfiles.size());
		for (const auto & profile : visionProfiles) {
			profileNames.push_back(profile.name.c_str());
		}
		ImGui::SetNextItemWidth(280);
		if (ImGui::Combo(
				"Vision profile",
				&selectedVisionProfileIndex,
				profileNames.data(),
				static_cast<int>(profileNames.size()))) {
			const auto & profile = visionProfiles[static_cast<size_t>(selectedVisionProfileIndex)];
			applyVisionProfileDefaults(profile, false);
		}
		const auto & profile = visionProfiles[static_cast<size_t>(selectedVisionProfileIndex)];
		const std::string recommendedModelPath =
			suggestedModelPath(profile.modelPath, profile.modelFileHint);
		const std::string recommendedDownloadUrl =
			effectiveSuggestedModelDownloadUrl(
				profile.modelDownloadUrl,
				profile.modelRepoHint,
				profile.modelFileHint);
		ImGui::TextDisabled("Architecture: %s", profile.architecture.c_str());
		if (!profile.modelRepoHint.empty()) {
			ImGui::TextDisabled("Recommended server model: %s", profile.modelRepoHint.c_str());
		}
		if (isEuRestrictedVisionProfile(profile)) {
			ImGui::TextColored(
				ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
				"EU note: this Meta model is gated and currently unavailable for download from the EU on Hugging Face.");
		}
		if (!profile.modelFileHint.empty()) {
			ImGui::TextDisabled("Recommended file: %s", profile.modelFileHint.c_str());
		}
		if (!recommendedModelPath.empty()) {
			ImGui::TextDisabled("Recommended local path: %s", recommendedModelPath.c_str());
			ImGui::TextDisabled(
				pathExists(recommendedModelPath)
					? "Recommended model is already present."
					: "Recommended model is not downloaded yet.");
			ImGui::BeginDisabled(trim(visionModelPath) == recommendedModelPath);
			if (ImGui::SmallButton("Use recommended path##Vision")) {
				const std::string previousVisionModelPath = trim(visionModelPath);
				copyStringToBuffer(
					visionModelPath,
					sizeof(visionModelPath),
					recommendedModelPath);
				if (previousVisionModelPath != trim(visionModelPath) &&
					shouldManageLocalTextServer(trim(visionServerUrl).empty() ? profile.serverUrl : trim(visionServerUrl))) {
					stopLocalTextServer(false);
				}
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered()) {
				showWrappedTooltip("Sets the model path to the profile's recommended file under the shared addon models/ folder.");
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(recommendedDownloadUrl.empty());
			const bool opensModelPage =
				recommendedDownloadUrl.find("/tree/") != std::string::npos ||
				recommendedDownloadUrl.find("/blob/") != std::string::npos;
			if (ImGui::SmallButton(opensModelPage ? "Open model page##Vision" : "Download model##Vision")) {
				ofLaunchBrowser(recommendedDownloadUrl);
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered()) {
				if (isEuRestrictedVisionProfile(profile)) {
					showWrappedTooltip("Opens the model page in your browser. Meta currently blocks this download in the EU.");
				} else if (opensModelPage) {
					showWrappedTooltip("Opens the recommended model page in your browser so you can pick the exact GGUF file.");
				} else {
					showWrappedTooltip("Opens the recommended multimodal model in your browser.");
				}
			}
		}
		if (profile.mayRequireMmproj) {
			ImGui::TextDisabled("Note: some variants also need a matching mmproj file on the server side.");
		}
		ImGui::TextDisabled(
			"OCR: %s | Multi-image: %s",
			profile.supportsOcr ? "supported" : "not ideal",
			profile.supportsMultipleImages ? "supported" : "single-image oriented");
	}

	const bool visionProfileMayRequireMmproj =
		!visionProfiles.empty() &&
		selectedVisionProfileIndex >= 0 &&
		selectedVisionProfileIndex < static_cast<int>(visionProfiles.size()) &&
		visionProfiles[static_cast<size_t>(selectedVisionProfileIndex)].mayRequireMmproj;

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Server URL", visionServerUrl, sizeof(visionServerUrl));
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip("Example: http://127.0.0.1:8080");
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Model path", visionModelPath, sizeof(visionModelPath));
	ImGui::SameLine();
	if (ImGui::Button("Browse model...", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select vision model", false);
		if (result.bSuccess) {
			const std::string previousVisionModelPath = trim(visionModelPath);
			copyStringToBuffer(visionModelPath, sizeof(visionModelPath), result.getPath());
			if (previousVisionModelPath != trim(visionModelPath) &&
				shouldManageLocalTextServer(trim(visionServerUrl))) {
				stopLocalTextServer(false);
			}
		}
	}
	if (visionProfileMayRequireMmproj && !trim(visionModelPath).empty()) {
		const std::string mmprojPath = findMatchingMmprojPath(trim(visionModelPath));
		if (mmprojPath.empty()) {
			ImGui::TextColored(
				ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
				"No matching mmproj .gguf found next to the selected model.");
			drawHelpMarker(
				"Many local multimodal llama.cpp models need a separate mmproj projector file in the same folder. "
				"The local server launcher will add --mmproj automatically when it finds one.");
		} else {
			ImGui::TextColored(
				ImVec4(0.35f, 0.8f, 0.45f, 1.0f),
				"Using mmproj: %s",
				ofFilePath::getFileName(mmprojPath).c_str());
		}
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Image path", visionImagePath, sizeof(visionImagePath));
	ImGui::SameLine();
	if (ImGui::Button("Browse...", ImVec2(90, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select image", false);
		if (result.bSuccess) {
			copyStringToBuffer(visionImagePath, sizeof(visionImagePath), result.getPath());
			autoSaveSession();
		}
	}
	drawVisionImagePreview(trim(visionImagePath));
	drawImageSearchPanel("Use Vision prompt", trim(visionPrompt));

	static const char * visionTaskLabels[] = { "Describe", "OCR", "Ask" };
	ImGui::SetNextItemWidth(180);
	ImGui::Combo("Task", &visionTaskIndex, visionTaskLabels, 3);

	if (ImGui::Button("Scene Describe", ImVec2(130, 0))) {
		visionTaskIndex = static_cast<int>(ofxGgmlVisionTask::Describe);
		copyStringToBuffer(
			visionPrompt,
			sizeof(visionPrompt),
			"Describe the scene professionally. Cover the main subject, layout, visible text, state, and anything a teammate would need to know quickly.");
		copyStringToBuffer(
			visionSystemPrompt,
			sizeof(visionSystemPrompt),
			"You are a precise multimodal assistant. Report only what is visually supported and organize the answer cleanly.");
	}
	ImGui::SameLine();
	if (ImGui::Button("Screenshot Review", ImVec2(140, 0))) {
		visionTaskIndex = static_cast<int>(ofxGgmlVisionTask::Ask);
		copyStringToBuffer(
			visionPrompt,
			sizeof(visionPrompt),
			"Review this screenshot like a professional product teammate. Summarize the UI, key controls, current state, visible warnings, and likely user intent.");
		copyStringToBuffer(
			visionSystemPrompt,
			sizeof(visionSystemPrompt),
			"You are a grounded multimodal assistant. Stay anchored to the image and avoid guessing about hidden state.");
	}
	ImGui::SameLine();
	if (ImGui::Button("Document OCR", ImVec2(120, 0))) {
		visionTaskIndex = static_cast<int>(ofxGgmlVisionTask::Ocr);
		copyStringToBuffer(
			visionPrompt,
			sizeof(visionPrompt),
			"Extract the readable text from this document image. Preserve headings, paragraphs, and line breaks where they matter.");
		copyStringToBuffer(
			visionSystemPrompt,
			sizeof(visionSystemPrompt),
			"You are an OCR assistant. Preserve reading order when possible and do not invent unreadable text.");
	}

	ImGui::InputTextMultiline(
		"Vision prompt",
		visionPrompt,
		sizeof(visionPrompt),
		ImVec2(-1, 100));
	ImGui::InputTextMultiline(
		"Vision system prompt",
		visionSystemPrompt,
		sizeof(visionSystemPrompt),
		ImVec2(-1, 70));

	ImGui::Separator();
	ImGui::TextDisabled("Optional video workflow");
	if (ImGui::Button("Action Analysis", ImVec2(130, 0))) {
		videoTaskIndex = static_cast<int>(ofxGgmlVideoTask::Action);
		copyStringToBuffer(
			visionPrompt,
			sizeof(visionPrompt),
			"Analyze this clip like a professional action-recognition assistant. Identify the primary action, any secondary actions, the evidence frames, and a grounded confidence estimate.");
		copyStringToBuffer(
			visionSystemPrompt,
			sizeof(visionSystemPrompt),
			"You are a temporal video analysis assistant. Report only actions supported by the observed clip and keep uncertainty explicit.");
	}
	ImGui::SameLine();
	if (ImGui::Button("Emotion Analysis", ImVec2(130, 0))) {
		videoTaskIndex = static_cast<int>(ofxGgmlVideoTask::Emotion);
		copyStringToBuffer(
			visionPrompt,
			sizeof(visionPrompt),
			"Analyze this clip like a professional emotion-recognition assistant. Identify the dominant emotion, any secondary emotions, visible evidence, and a grounded confidence estimate.");
		copyStringToBuffer(
			visionSystemPrompt,
			sizeof(visionSystemPrompt),
			"You are a multimodal emotion analysis assistant. Infer only emotions supported by visible evidence and keep uncertainty explicit.");
	}
	static const char * videoTaskLabels[] = { "Summarize", "OCR", "Ask", "Action", "Emotion" };
	ImGui::SetNextItemWidth(180);
	ImGui::Combo("Video task", &videoTaskIndex, videoTaskLabels, 5);
	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Video path", visionVideoPath, sizeof(visionVideoPath));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse video...", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select video", false);
		if (result.bSuccess) {
			copyStringToBuffer(visionVideoPath, sizeof(visionVideoPath), result.getPath());
			autoSaveSession();
		}
	}
	drawVisionVideoPreview(trim(visionVideoPath));
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Sampled frames", &visionVideoMaxFrames, 1, 12);
	ImGui::Separator();
	ImGui::TextDisabled("LLM-grounded video planning");
	ImGui::TextWrapped(
		"Generate either a beat-based clip plan or a multi-scene script with recurring entities, then reuse it to strengthen the video-generation prompt.");
	ImGui::Checkbox("Multi-scene plan", &videoPlanMultiScene);
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Plan beats", &videoPlanBeatCount, 1, 12);
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Plan scenes", &videoPlanSceneCount, 1, 8);
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(180);
	ImGui::SliderFloat("Plan duration", &videoPlanDurationSeconds, 1.0f, 30.0f, "%.1f s");
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::Checkbox("Use plan for generation", &videoPlanUseForGeneration);
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	if (videoPlanUseForGeneration) {
		ImGui::TextDisabled("When enabled, Run Video injects the structured plan into the prompt automatically.");
	}
	const std::string storedPlanJson = trim(videoPlanJson);
	bool planAvailable = !storedPlanJson.empty();
	ofxGgmlVideoPlan parsedPlan;
	if (planAvailable) {
		const auto parsedResult = ofxGgmlVideoPlanner::parsePlanJson(storedPlanJson);
		if (parsedResult.isOk()) {
			parsedPlan = parsedResult.value();
			if (videoPlanSummary.empty()) {
				videoPlanSummary = ofxGgmlVideoPlanner::summarizePlan(parsedPlan);
			}
			if (!parsedPlan.scenes.empty()) {
				selectedVideoPlanSceneIndex = std::clamp(
					selectedVideoPlanSceneIndex,
					0,
					std::max(0, static_cast<int>(parsedPlan.scenes.size()) - 1));
			}
		}
	}
	if (ImGui::Button(videoPlanMultiScene ? "Plan Multi-Scene" : "Plan Video", ImVec2(160, 0))) {
		runVideoPlanning();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!planAvailable);
	if (ImGui::Button("Apply plan to prompt", ImVec2(170, 0))) {
		const auto parsedResult = ofxGgmlVideoPlanner::parsePlanJson(trim(videoPlanJson));
		if (parsedResult.isOk()) {
			std::string plannedPrompt;
			if (videoPlanMultiScene && !parsedResult.value().scenes.empty()) {
				const int clampedSceneIndex = std::clamp(
					selectedVideoPlanSceneIndex,
					0,
					std::max(0, static_cast<int>(parsedResult.value().scenes.size()) - 1));
				plannedPrompt =
					videoPlanGenerationMode == 1
						? ofxGgmlVideoPlanner::buildSceneSequencePrompt(parsedResult.value())
						: ofxGgmlVideoPlanner::buildScenePrompt(
							parsedResult.value(),
							static_cast<size_t>(clampedSceneIndex));
			} else {
				plannedPrompt = ofxGgmlVideoPlanner::buildGenerationPrompt(parsedResult.value());
			}
			copyStringToBuffer(visionPrompt, sizeof(visionPrompt), plannedPrompt);
			videoPlanSummary = ofxGgmlVideoPlanner::summarizePlan(parsedResult.value());
			autoSaveSession();
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!planAvailable);
	if (ImGui::Button("Clear plan", ImVec2(120, 0))) {
		videoPlanJson[0] = '\0';
		videoPlanSummary.clear();
		autoSaveSession();
	}
	ImGui::EndDisabled();
	if (!videoPlanSummary.empty()) {
		ImGui::TextWrapped("%s", videoPlanSummary.c_str());
	}
	if (!parsedPlan.entities.empty()) {
		ImGui::TextDisabled("Entities");
		for (const auto & entity : parsedPlan.entities) {
			std::string label = !entity.label.empty() ? entity.label : entity.id;
			if (!entity.role.empty()) {
				label += " (" + entity.role + ")";
			}
			ImGui::BulletText("%s", label.c_str());
		}
	}
	if (!parsedPlan.scenes.empty()) {
		ImGui::TextDisabled("Scenes");
		std::vector<const char *> sceneLabels;
		sceneLabels.reserve(parsedPlan.scenes.size());
		std::vector<std::string> sceneLabelStorage;
		sceneLabelStorage.reserve(parsedPlan.scenes.size());
		for (const auto & scene : parsedPlan.scenes) {
			std::string label = ofToString(scene.index) + ": ";
			label += scene.title.empty() ? scene.summary : scene.title;
			sceneLabelStorage.push_back(label);
		}
		for (const auto & label : sceneLabelStorage) {
			sceneLabels.push_back(label.c_str());
		}
		ImGui::SetNextItemWidth(-1);
		ImGui::Combo(
			"Scene focus",
			&selectedVideoPlanSceneIndex,
			sceneLabels.data(),
			static_cast<int>(sceneLabels.size()));
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			autoSaveSession();
		}
		static const char * generationModeLabels[] = {
			"Selected scene",
			"Full sequence"
		};
		ImGui::SetNextItemWidth(180);
		ImGui::Combo("Generation mode", &videoPlanGenerationMode, generationModeLabels, 2);
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			autoSaveSession();
		}
		const auto & selectedScene = parsedPlan.scenes[static_cast<size_t>(selectedVideoPlanSceneIndex)];
		ImGui::TextWrapped("%s", selectedScene.summary.c_str());
		if (selectedScene.durationSeconds > 0.0) {
			ImGui::TextDisabled("Duration: %.1f s", selectedScene.durationSeconds);
		}
		if (!selectedScene.eventPrompt.empty()) {
			ImGui::TextDisabled("Scene prompt: %s", selectedScene.eventPrompt.c_str());
		}
		if (!selectedScene.background.empty()) {
			ImGui::TextDisabled("Background: %s", selectedScene.background.c_str());
		}
		if (!selectedScene.cameraMovement.empty()) {
			ImGui::TextDisabled("Camera: %s", selectedScene.cameraMovement.c_str());
		}
		if (!selectedScene.transition.empty()) {
			ImGui::TextDisabled("Transition: %s", selectedScene.transition.c_str());
		}
		if (!selectedScene.entityIds.empty()) {
			std::ostringstream entitiesLabel;
			for (size_t entityIndex = 0; entityIndex < selectedScene.entityIds.size(); ++entityIndex) {
				if (entityIndex > 0) {
					entitiesLabel << ", ";
				}
				entitiesLabel << selectedScene.entityIds[entityIndex];
			}
			ImGui::TextDisabled("Entities: %s", entitiesLabel.str().c_str());
		}
		ImGui::BeginDisabled(parsedPlan.scenes.empty());
		if (ImGui::SmallButton("Use selected scene in Diffusion")) {
			copyStringToBuffer(
				diffusionPrompt,
				sizeof(diffusionPrompt),
				ofxGgmlVideoPlanner::buildScenePrompt(
					parsedPlan,
					static_cast<size_t>(selectedVideoPlanSceneIndex)));
			activeMode = AiMode::Diffusion;
		}
		ImGui::EndDisabled();
	}
	ImGui::InputTextMultiline(
		"Plan JSON",
		videoPlanJson,
		sizeof(videoPlanJson),
		ImVec2(0, 180));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		const auto parsedResult = ofxGgmlVideoPlanner::parsePlanJson(trim(videoPlanJson));
		videoPlanSummary = parsedResult.isOk()
			? ofxGgmlVideoPlanner::summarizePlan(parsedResult.value())
			: std::string();
		autoSaveSession();
	}

	ImGui::Separator();
	ImGui::TextDisabled("Subtitle montage automat");
	ImGui::TextWrapped(
		"Build a montage from subtitle similarity, then export a CMX-style EDL that can be used in external editors.");
	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Subtitle / SRT path", montageSubtitlePath, sizeof(montageSubtitlePath));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse SRT...", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select subtitle file", false);
		if (result.bSuccess) {
			copyStringToBuffer(montageSubtitlePath, sizeof(montageSubtitlePath), result.getPath());
			autoSaveSession();
		}
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(speechSrtPath).empty());
	if (ImGui::Button("Use speech SRT", ImVec2(120, 0))) {
		deferredMontageSubtitlePath = speechSrtPath;
		hasDeferredMontageSubtitlePath = true;
		autoSaveSession();
	}
	ImGui::EndDisabled();
	if (trim(speechSrtPath).empty()) {
		ImGui::TextDisabled("Tip: run Speech with timestamps to generate an SRT you can reuse here.");
	} else {
		ImGui::TextDisabled("Latest speech SRT: %s", ofFilePath::getFileName(speechSrtPath).c_str());
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(visionPrompt).empty());
	if (ImGui::SmallButton("Use Vision prompt##MontageGoal")) {
		copyStringToBuffer(montageGoal, sizeof(montageGoal), trim(visionPrompt));
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::InputTextMultiline(
		"Montage goal",
		montageGoal,
		sizeof(montageGoal),
		ImVec2(-1, 80));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(180);
	ImGui::InputText("EDL title", montageEdlTitle, sizeof(montageEdlTitle));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(140);
	ImGui::InputText("Reel name", montageReelName, sizeof(montageReelName));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Montage clips", &montageMaxClips, 1, 24);
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("EDL FPS", &montageFps, 12, 60);
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(180);
	ImGui::SliderFloat("Min subtitle score", &montageMinScore, 0.0f, 1.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::Checkbox("Preserve subtitle chronology", &montagePreserveChronology);
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	static const char * montagePreviewTimingLabels[] = {
		"Source-timed",
		"Montage-timed"
	};
	ImGui::SetNextItemWidth(180);
	if (ImGui::Combo(
			"Preview timing",
			&montagePreviewTimingModeIndex,
			montagePreviewTimingLabels,
			IM_ARRAYSIZE(montagePreviewTimingLabels))) {
		montagePreviewTimingModeIndex = std::clamp(montagePreviewTimingModeIndex, 0, 1);
		montagePreviewTimelinePlaying = false;
		montagePreviewTimelineLastTickTime = 0.0f;
#if OFXGGML_HAS_OFXVLC4
		if (montageVlcPreviewInitialized) {
			std::string error;
			if (loadMontageVlcPreview(&error)) {
				montagePreviewStatusMessage =
					"Reloaded the ofxVlc4 preview for the selected subtitle timing.";
			} else if (!error.empty()) {
				montagePreviewStatusMessage = error;
			}
		}
#endif
		autoSaveSession();
	}
	ImGui::Checkbox("Live subtitle playback with preview video", &montageSubtitlePlaybackEnabled);
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	if (montageSubtitlePlaybackEnabled &&
		getSelectedMontagePreviewTimingMode() == ofxGgmlMontagePreviewTimingMode::Source &&
		trim(visionVideoPath).empty()) {
		ImGui::TextDisabled("Load a source video above to preview source-timed subtitle cues.");
	} else if (montageSubtitlePlaybackEnabled &&
		getSelectedMontagePreviewTimingMode() == ofxGgmlMontagePreviewTimingMode::Montage) {
		ImGui::TextDisabled("Montage-timed preview uses the generated subtitle timeline below and is ready for ofxVlc4 subtitle-slave export.");
	}
	const bool montageAvailable = !trim(montageEdlText).empty();
	const bool canPlanMontage =
		!generating.load() &&
		std::strlen(montageSubtitlePath) > 0 &&
		trim(montageGoal).size() > 0;
	ImGui::BeginDisabled(!canPlanMontage);
	if (ImGui::Button("Plan Montage", ImVec2(150, 0))) {
		runMontagePlanning();
	}
	ImGui::EndDisabled();
	if (!canPlanMontage) {
		ImGui::SameLine();
		if (std::strlen(montageSubtitlePath) == 0) {
			ImGui::TextDisabled("Select an SRT file first.");
		} else if (trim(montageGoal).empty()) {
			ImGui::TextDisabled("Add a montage goal to enable planning.");
		}
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!montageAvailable || trim(montageEditorBrief).empty());
	if (ImGui::Button("Use brief in Write##Montage", ImVec2(190, 0))) {
		copyStringToBuffer(writeInput, sizeof(writeInput), montageEditorBrief);
		activeMode = AiMode::Write;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!montageAvailable);
	if (ImGui::Button("Copy EDL", ImVec2(110, 0))) {
		copyToClipboard(montageEdlText);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!montageAvailable);
	if (ImGui::Button("Copy SRT", ImVec2(110, 0))) {
		copyToClipboard(montageSrtText);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!montageAvailable);
	if (ImGui::Button("Copy VTT", ImVec2(110, 0))) {
		copyToClipboard(montageVttText);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!montageAvailable);
	if (ImGui::Button("Clear montage", ImVec2(130, 0))) {
		montageSummary.clear();
		montageEditorBrief.clear();
		montageEdlText.clear();
		montageSrtText.clear();
		montageVttText.clear();
		montagePreviewBundle = {};
		montageSubtitleTrack = {};
		montageSourceSubtitleTrack = {};
		montagePreviewSubtitleSlavePath.clear();
		montagePreviewStatusMessage.clear();
		montagePreviewTimelineSeconds = 0.0;
		montagePreviewTimelinePlaying = false;
		montagePreviewTimelineLastTickTime = 0.0f;
#if OFXGGML_HAS_OFXVLC4
		closeMontageVlcPreview();
#endif
		selectedMontageCueIndex = -1;
		autoSaveSession();
	}
	ImGui::EndDisabled();
	if (!montageSummary.empty()) {
		ImGui::TextWrapped("%s", montageSummary.c_str());
	}
	const ofxGgmlMontagePreviewTrack * activePreviewTrack = getSelectedMontagePreviewTrack();
	const bool hasActivePreviewTrack = activePreviewTrack != nullptr;
	if (hasActivePreviewTrack) {
		ImGui::TextDisabled(
			"%s",
			ofxGgmlMontagePreviewBridge::summarizeTrack(*activePreviewTrack).c_str());
	}
	if (!montagePreviewStatusMessage.empty()) {
		ImGui::TextDisabled("%s", montagePreviewStatusMessage.c_str());
	}
	if (hasActivePreviewTrack &&
		getSelectedMontagePreviewTimingMode() == ofxGgmlMontagePreviewTimingMode::Montage) {
		double previewDurationSeconds =
			ofxGgmlMontagePreviewBridge::getTrackDuration(*activePreviewTrack);
		if (previewDurationSeconds > 0.0) {
			if (ImGui::Button(
					montagePreviewTimelinePlaying ? "Pause montage preview" : "Play montage preview",
					ImVec2(170, 0))) {
				montagePreviewTimelinePlaying = !montagePreviewTimelinePlaying;
				montagePreviewTimelineLastTickTime =
					montagePreviewTimelinePlaying ? ofGetElapsedTimef() : 0.0f;
			}
			ImGui::SameLine();
			if (ImGui::Button("Restart montage preview", ImVec2(170, 0))) {
				montagePreviewTimelineSeconds = 0.0;
				montagePreviewTimelinePlaying = false;
				montagePreviewTimelineLastTickTime = 0.0f;
			}
			ImGui::SameLine();
			ImGui::TextDisabled("%.2fs / %.2fs", montagePreviewTimelineSeconds, previewDurationSeconds);

			float previewTimelinePosition =
				static_cast<float>(std::clamp(montagePreviewTimelineSeconds, 0.0, previewDurationSeconds));
			ImGui::SetNextItemWidth(-1);
			if (ImGui::SliderFloat(
					"Montage preview time",
					&previewTimelinePosition,
					0.0f,
					static_cast<float>(previewDurationSeconds),
					"%.2f s")) {
				montagePreviewTimelineSeconds = previewTimelinePosition;
				montagePreviewTimelinePlaying = false;
				montagePreviewTimelineLastTickTime = 0.0f;
			}
		}
	}
	if (montageSubtitlePlaybackEnabled && hasActivePreviewTrack) {
		const int activeCueIndex = findActiveMontagePreviewCueIndex();
		if (activeCueIndex >= 0 &&
			activeCueIndex < static_cast<int>(activePreviewTrack->cues.size())) {
			const auto & cue = activePreviewTrack->cues[static_cast<size_t>(activeCueIndex)];
			ImGui::TextDisabled(
				getSelectedMontagePreviewTimingMode() == ofxGgmlMontagePreviewTimingMode::Source
					? "Current source-timed cue"
					: "Current montage-timed cue");
			ImGui::TextWrapped("%s", cue.text.c_str());
		}
	}
	if (!montageEditorBrief.empty()) {
		if (ImGui::TreeNode("Editor brief")) {
			ImGui::TextWrapped("%s", montageEditorBrief.c_str());
			ImGui::TreePop();
		}
	}
	if (hasActivePreviewTrack) {
		ImGui::Separator();
		ImGui::TextDisabled("ofxVlc4 subtitle-slave export");
		ImGui::TextWrapped(
			"Export the selected preview timing as SRT/VTT and attach it later in ofxVlc4 via addSubtitleSlave(path).");
		if (ImGui::Button("Export active SRT", ImVec2(150, 0))) {
			std::string error;
			const std::string exportedPath =
				exportSelectedMontagePreviewTrack(ofxGgmlMontagePreviewTextFormat::Srt, &error);
			if (!exportedPath.empty()) {
				montagePreviewSubtitleSlavePath = exportedPath;
				montagePreviewStatusMessage = "Prepared SRT subtitle slave: " + exportedPath;
			} else {
				montagePreviewStatusMessage =
					error.empty() ? std::string("Failed to export subtitle slave.") : error;
			}
			autoSaveSession();
		}
		ImGui::SameLine();
		if (ImGui::Button("Export active VTT", ImVec2(150, 0))) {
			std::string error;
			const std::string exportedPath =
				exportSelectedMontagePreviewTrack(ofxGgmlMontagePreviewTextFormat::Vtt, &error);
			if (!exportedPath.empty()) {
				montagePreviewSubtitleSlavePath = exportedPath;
				montagePreviewStatusMessage = "Prepared VTT subtitle preview: " + exportedPath;
			} else {
				montagePreviewStatusMessage =
					error.empty() ? std::string("Failed to export subtitle preview.") : error;
			}
			autoSaveSession();
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(trim(montagePreviewSubtitleSlavePath).empty());
		if (ImGui::Button("Copy path", ImVec2(110, 0))) {
			copyToClipboard(montagePreviewSubtitleSlavePath);
		}
		ImGui::EndDisabled();
		if (!trim(montagePreviewSubtitleSlavePath).empty()) {
			ImGui::TextDisabled("%s", montagePreviewSubtitleSlavePath.c_str());
		}
#if OFXGGML_HAS_OFXVLC4
		ImGui::Separator();
		ImGui::TextDisabled("Direct ofxVlc4 preview");
		ImGui::TextWrapped(
			"When the example is regenerated with ofxVlc4, the active source-timed or montage-timed subtitle track can be previewed directly here.");
		if (ImGui::Button("Load in ofxVlc4 preview", ImVec2(190, 0))) {
			std::string error;
			if (loadMontageVlcPreview(&error)) {
				montagePreviewStatusMessage =
					"Loaded active subtitle track into the optional ofxVlc4 preview.";
			} else {
				montagePreviewStatusMessage =
					error.empty() ? std::string("Failed to load the optional ofxVlc4 preview.") : error;
			}
			autoSaveSession();
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(!montageVlcPreviewInitialized);
		if (ImGui::Button("Close ofxVlc4 preview", ImVec2(190, 0))) {
			closeMontageVlcPreview();
			montagePreviewStatusMessage = "Closed the optional ofxVlc4 preview.";
			autoSaveSession();
		}
		ImGui::EndDisabled();
		if (montageVlcPreviewInitialized) {
			drawMontageVlcPreview();
		}
#else
		ImGui::Separator();
		ImGui::TextDisabled(
			"Regenerate this example with ofxVlc4 in addons.make to enable direct subtitle-slave preview here.");
#endif
	}
	const ofxGgmlMontagePreviewTrack * subtitlePreviewTrack =
		hasActivePreviewTrack ? activePreviewTrack : nullptr;
	static ofxGgmlMontagePreviewTrack fallbackMontageTrack;
	if (subtitlePreviewTrack == nullptr && !montageSubtitleTrack.cues.empty()) {
		fallbackMontageTrack.title = montageSubtitleTrack.title;
		fallbackMontageTrack.timingMode = ofxGgmlMontagePreviewTimingMode::Montage;
		fallbackMontageTrack.cues = montageSubtitleTrack.cues;
		subtitlePreviewTrack = &fallbackMontageTrack;
	}
	if (subtitlePreviewTrack != nullptr && !subtitlePreviewTrack->cues.empty()) {
		ImGui::TextDisabled(
			subtitlePreviewTrack->timingMode == ofxGgmlMontagePreviewTimingMode::Source
				? "Source-timed subtitle preview"
				: "Montage-timed subtitle preview");
		ImGui::BeginChild("MontageSubtitlePreview", ImVec2(0, 180), true);
		for (size_t i = 0; i < subtitlePreviewTrack->cues.size(); ++i) {
			const auto & cue = subtitlePreviewTrack->cues[i];
			std::ostringstream label;
			label << cue.index << ". "
				<< ofxGgmlVideoInference::formatTimestamp(cue.startSeconds)
				<< " - "
				<< ofxGgmlVideoInference::formatTimestamp(cue.endSeconds)
				<< "  " << cue.text;
			if (ImGui::Selectable(
					label.str().c_str(),
					selectedMontageCueIndex == static_cast<int>(i))) {
				selectedMontageCueIndex = static_cast<int>(i);
#if OFXGGML_HAS_OFXVLC4
				if (montageVlcPreviewInitialized) {
					montageVlcPreviewPlayer.setTime(
						static_cast<int>(std::max(0.0, cue.startSeconds) * 1000.0));
					montagePreviewStatusMessage =
						"Synced the ofxVlc4 preview to the selected subtitle cue.";
				}
#endif
			}
		}
		ImGui::EndChild();
		if (selectedMontageCueIndex >= 0 &&
			selectedMontageCueIndex < static_cast<int>(subtitlePreviewTrack->cues.size())) {
			const auto & cue = subtitlePreviewTrack->cues[static_cast<size_t>(selectedMontageCueIndex)];
			ImGui::TextDisabled("Selected cue");
			ImGui::TextWrapped("%s", cue.text.c_str());
		}
	}
	if (!montageEdlText.empty()) {
		ImGui::TextDisabled("EDL");
		ImGui::BeginChild("MontageEdlPreview", ImVec2(0, 180), true);
		ImGui::TextUnformatted(montageEdlText.c_str());
		ImGui::EndChild();
	}
	if (!montageSrtText.empty()) {
		if (ImGui::TreeNode("Montage SRT")) {
			ImGui::BeginChild("MontageSrtPreview", ImVec2(0, 160), true);
			ImGui::TextUnformatted(montageSrtText.c_str());
			ImGui::EndChild();
			ImGui::TreePop();
		}
	}
	if (!montageVttText.empty()) {
		if (ImGui::TreeNode("Montage VTT")) {
			ImGui::BeginChild("MontageVttPreview", ImVec2(0, 160), true);
			ImGui::TextUnformatted(montageVttText.c_str());
			ImGui::EndChild();
			ImGui::TreePop();
		}
	}

	ImGui::Separator();
	ImGui::TextDisabled("AI-assisted video editing");
	ImGui::TextWrapped(
		"Turn the current clip plus your editing goal into a structured edit plan with timeline clips, edit actions, and asset suggestions.");
	std::vector<const char *> videoEditPresetNames;
	videoEditPresetNames.reserve(kVideoEditPresetCount);
	for (const auto & preset : kVideoEditPresets) {
		videoEditPresetNames.push_back(preset.name);
	}
	videoEditPresetIndex = std::clamp(videoEditPresetIndex, 0, kVideoEditPresetCount - 1);
	const auto applyVideoEditPreset =
		[this](int presetIndex) {
			const int clampedIndex = std::clamp(presetIndex, 0, kVideoEditPresetCount - 1);
			const auto & preset = kVideoEditPresets[clampedIndex];
			videoEditPresetIndex = clampedIndex;
			copyStringToBuffer(videoEditGoal, sizeof(videoEditGoal), preset.goal);
			videoEditClipCount = std::clamp(preset.clipCount, 1, 12);
			videoEditTargetDurationSeconds = std::clamp(preset.targetDurationSeconds, 1.0f, 120.0f);
			videoEditUseCurrentAnalysis = preset.useCurrentAnalysis;
			resetVideoEditWorkflowState();
			autoSaveSession();
		};
	ImGui::SetNextItemWidth(220);
	ImGui::Combo(
		"Edit preset",
		&videoEditPresetIndex,
		videoEditPresetNames.data(),
		static_cast<int>(videoEditPresetNames.size()));
	ImGui::SameLine();
	if (ImGui::SmallButton("Use preset")) {
		applyVideoEditPreset(videoEditPresetIndex);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Trailer")) {
		applyVideoEditPreset(0);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Montage")) {
		applyVideoEditPreset(1);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Recap")) {
		applyVideoEditPreset(2);
	}
	const auto & selectedEditPreset = kVideoEditPresets[videoEditPresetIndex];
	ImGui::TextDisabled(
		"Preset defaults: %d clips | %.1f s | %s grounding",
		selectedEditPreset.clipCount,
		selectedEditPreset.targetDurationSeconds,
		selectedEditPreset.useCurrentAnalysis ? "analysis-aware" : "prompt-only");
	ImGui::InputTextMultiline(
		"Edit goal",
		videoEditGoal,
		sizeof(videoEditGoal),
		ImVec2(-1, 90));
	ImGui::Checkbox("Use current analysis as edit context", &videoEditUseCurrentAnalysis);
	if (videoEditUseCurrentAnalysis) {
		ImGui::TextDisabled("The current Vision/Video output will be fed into the edit planner as grounding.");
	}
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Edit clips", &videoEditClipCount, 1, 12);
	ImGui::SetNextItemWidth(180);
	ImGui::SliderFloat("Target edit duration", &videoEditTargetDurationSeconds, 1.0f, 120.0f, "%.1f s");

	const std::string storedEditPlanJson = trim(videoEditPlanJson);
	bool editPlanAvailable = !storedEditPlanJson.empty();
	ofxGgmlVideoEditPlan parsedEditPlan;
	ofxGgmlVideoEditWorkflow editWorkflow;
	bool editWorkflowAvailable = false;
	if (editPlanAvailable) {
		const auto parsedResult = ofxGgmlVideoPlanner::parseEditPlanJson(storedEditPlanJson);
		if (parsedResult.isOk()) {
			parsedEditPlan = parsedResult.value();
			if (videoEditPlanSummary.empty()) {
				videoEditPlanSummary = ofxGgmlVideoPlanner::summarizeEditPlan(parsedEditPlan);
			}
			ofxGgmlVideoEditWorkflowContext workflowContext;
			workflowContext.hasSourceVideo = !trim(visionVideoPath).empty();
			workflowContext.hasSourceTimedPreview = !montageSourceSubtitleTrack.cues.empty();
			workflowContext.hasMontageTimedPreview = !montageSubtitleTrack.cues.empty();
			workflowContext.hasSubtitlePreview =
				workflowContext.hasSourceTimedPreview ||
				workflowContext.hasMontageTimedPreview;
			editWorkflow =
				ofxGgmlVideoPlanner::buildEditWorkflow(parsedEditPlan, workflowContext);
			editWorkflowAvailable =
				!editWorkflow.steps.empty() ||
				!trim(editWorkflow.nextAction).empty() ||
				!trim(editWorkflow.previewHint).empty();
		}
	}
	if (editWorkflowAvailable &&
		videoEditWorkflowActiveStepIndex < 0 &&
		!editWorkflow.steps.empty()) {
		videoEditWorkflowActiveStepIndex = editWorkflow.steps.front().index;
	}
	const auto applyEditWorkflowStep =
		[this](const ofxGgmlVideoEditWorkflowStep & step) {
			const std::string handoffText = trim(step.handoffText);
			if (handoffText.empty()) {
				return;
			}
			videoEditWorkflowActiveStepIndex = step.index;
			if (step.handoffMode == "Write") {
				copyStringToBuffer(writeInput, sizeof(writeInput), handoffText);
				activeMode = AiMode::Write;
			} else if (step.handoffMode == "Diffusion") {
				copyStringToBuffer(diffusionPrompt, sizeof(diffusionPrompt), handoffText);
				activeMode = AiMode::Diffusion;
			} else if (step.handoffMode == "Vision") {
				copyStringToBuffer(visionPrompt, sizeof(visionPrompt), handoffText);
				activeMode = AiMode::Vision;
			} else if (step.handoffMode == "Montage") {
				copyStringToBuffer(montageGoal, sizeof(montageGoal), handoffText);
				activeMode = AiMode::Vision;
#if OFXGGML_HAS_OFXVLC4
				if (montageVlcPreviewInitialized || !trim(visionVideoPath).empty()) {
					std::string error;
					if (loadMontageVlcPreview(&error) && step.startSeconds >= 0.0) {
						montageVlcPreviewPlayer.setTime(
							static_cast<int>(std::max(0.0, step.startSeconds) * 1000.0));
						montagePreviewStatusMessage =
							"Opened the montage handoff and synced the ofxVlc4 preview.";
					} else if (!error.empty()) {
						montagePreviewStatusMessage = error;
					}
				}
#endif
			} else {
				copyStringToBuffer(customInput, sizeof(customInput), handoffText);
				activeMode = AiMode::Custom;
			}
			autoSaveSession();
		};
	const bool canPlanEdit =
		!generating.load() &&
		std::strlen(visionVideoPath) > 0 &&
		trim(videoEditGoal).size() > 0 &&
		(!videoEditUseCurrentAnalysis || !trim(visionOutput).empty());
	ImGui::BeginDisabled(!canPlanEdit);
	if (ImGui::Button("Plan Edit", ImVec2(140, 0))) {
		runVideoEditPlanning();
	}
	ImGui::EndDisabled();
	if (!canPlanEdit) {
		ImGui::SameLine();
		if (std::strlen(visionVideoPath) == 0) {
			ImGui::TextDisabled("Select a video first.");
		} else if (trim(videoEditGoal).empty()) {
			ImGui::TextDisabled("Add an edit goal to enable planning.");
		} else if (videoEditUseCurrentAnalysis && trim(visionOutput).empty()) {
			ImGui::TextDisabled("Run Video first or disable analysis grounding.");
		}
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!editPlanAvailable);
	if (ImGui::Button("Use brief in Write", ImVec2(160, 0))) {
		copyStringToBuffer(
			writeInput,
			sizeof(writeInput),
			ofxGgmlVideoPlanner::buildEditorBrief(parsedEditPlan));
		activeMode = AiMode::Write;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!editPlanAvailable);
	if (ImGui::Button("Clear edit plan", ImVec2(140, 0))) {
		videoEditPlanJson[0] = '\0';
		videoEditPlanSummary.clear();
		resetVideoEditWorkflowState();
		autoSaveSession();
	}
	ImGui::EndDisabled();
	if (!videoEditPlanSummary.empty()) {
		ImGui::TextWrapped("%s", videoEditPlanSummary.c_str());
	}
	if (editWorkflowAvailable) {
		ImGui::Separator();
		ImGui::TextDisabled("Editor workflow");
		if (!trim(editWorkflow.headline).empty()) {
			ImGui::TextWrapped("%s", editWorkflow.headline.c_str());
		}
		if (!trim(editWorkflow.nextAction).empty()) {
			ImGui::TextDisabled("Next action: %s", editWorkflow.nextAction.c_str());
		}
		if (!trim(editWorkflow.previewHint).empty()) {
			ImGui::TextDisabled("Preview: %s", editWorkflow.previewHint.c_str());
		}
		int completedSteps = 0;
		for (const auto & step : editWorkflow.steps) {
			if (isVideoEditWorkflowStepCompleted(step.index)) {
				++completedSteps;
			}
		}
		ImGui::TextDisabled(
			"Progress: %d / %d steps complete",
			completedSteps,
			static_cast<int>(editWorkflow.steps.size()));
		int nextPendingStepIndex = -1;
		for (const auto & step : editWorkflow.steps) {
			if (!isVideoEditWorkflowStepCompleted(step.index)) {
				nextPendingStepIndex = step.index;
				break;
			}
		}
		ImGui::BeginDisabled(nextPendingStepIndex < 0);
		if (ImGui::SmallButton("Open next step")) {
			const auto it = std::find_if(
				editWorkflow.steps.begin(),
				editWorkflow.steps.end(),
				[nextPendingStepIndex](const ofxGgmlVideoEditWorkflowStep & step) {
					return step.index == nextPendingStepIndex;
				});
			if (it != editWorkflow.steps.end()) {
				applyEditWorkflowStep(*it);
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::SmallButton("Reset workflow")) {
			resetVideoEditWorkflowState();
			if (!editWorkflow.steps.empty()) {
				videoEditWorkflowActiveStepIndex = editWorkflow.steps.front().index;
			}
			autoSaveSession();
		}
		if (!editWorkflow.checklist.empty()) {
			ImGui::TextDisabled("Checklist");
			for (const auto & item : editWorkflow.checklist) {
				ImGui::BulletText("%s", item.c_str());
			}
		}
		if (!editWorkflow.steps.empty()) {
			ImGui::TextDisabled("Actionable steps");
			for (const auto & step : editWorkflow.steps) {
				const bool isCompleted = isVideoEditWorkflowStepCompleted(step.index);
				const bool isActive = videoEditWorkflowActiveStepIndex == step.index;
				ImGui::PushID(step.index);
				std::ostringstream stepLabel;
				stepLabel << step.index << ". ";
				if (isCompleted) {
					stepLabel << "[Done] ";
				} else if (isActive) {
					stepLabel << "[Active] ";
				}
				stepLabel << step.title;
				if (step.endSeconds > step.startSeconds) {
					stepLabel << " (" << ofxGgmlVideoInference::formatTimestamp(step.startSeconds)
						<< " - " << ofxGgmlVideoInference::formatTimestamp(step.endSeconds) << ")";
				}
				ImGui::TextWrapped("%s", stepLabel.str().c_str());
				if (!trim(step.detail).empty()) {
					ImGui::TextWrapped("%s", step.detail.c_str());
				}
				ImGui::BeginDisabled(trim(step.handoffText).empty());
				std::string openLabel =
					"Open in " +
					(trim(step.handoffMode).empty() ? std::string("Custom") : step.handoffMode);
				if (ImGui::SmallButton(openLabel.c_str())) {
					applyEditWorkflowStep(step);
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
#if OFXGGML_HAS_OFXVLC4
				const bool canPreviewStepInVlc =
					step.startSeconds >= 0.0 &&
					!trim(visionVideoPath).empty();
				ImGui::BeginDisabled(!canPreviewStepInVlc);
				if (ImGui::SmallButton("Preview in VLC")) {
					std::string error;
					if (loadMontageVlcPreview(&error)) {
						montageVlcPreviewPlayer.setTime(
							static_cast<int>(std::max(0.0, step.startSeconds) * 1000.0));
						montagePreviewStatusMessage =
							"Synced the ofxVlc4 preview to workflow step " +
							std::to_string(step.index) + ".";
					} else if (!error.empty()) {
						montagePreviewStatusMessage = error;
					}
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
#endif
				if (ImGui::SmallButton(isActive ? "Focused" : "Focus")) {
					videoEditWorkflowActiveStepIndex = step.index;
					autoSaveSession();
				}
				ImGui::SameLine();
				if (ImGui::SmallButton(isCompleted ? "Undo done" : "Mark done")) {
					setVideoEditWorkflowStepCompleted(step.index, !isCompleted);
					if (!isCompleted && nextPendingStepIndex == step.index) {
						for (const auto & candidate : editWorkflow.steps) {
							if (!isVideoEditWorkflowStepCompleted(candidate.index)) {
								videoEditWorkflowActiveStepIndex = candidate.index;
								break;
							}
						}
					} else if (isCompleted) {
						videoEditWorkflowActiveStepIndex = step.index;
					}
					autoSaveSession();
				}
				ImGui::SameLine();
				ImGui::BeginDisabled(trim(step.handoffText).empty());
				if (ImGui::SmallButton("Copy step")) {
					copyToClipboard(step.handoffText);
				}
				ImGui::EndDisabled();
				ImGui::PopID();
			}
		}
	}
	if (!parsedEditPlan.clips.empty()) {
		ImGui::TextDisabled("Suggested timeline");
		for (const auto & clip : parsedEditPlan.clips) {
			std::ostringstream label;
			label << clip.index << ". "
				<< ofxGgmlVideoInference::formatTimestamp(clip.startSeconds)
				<< " - " << ofxGgmlVideoInference::formatTimestamp(clip.endSeconds)
				<< " | " << (!clip.purpose.empty() ? clip.purpose : clip.sourceDescription);
			ImGui::BulletText("%s", label.str().c_str());
		}
	}
	if (!parsedEditPlan.actions.empty()) {
		ImGui::TextDisabled("Edit actions");
		for (const auto & action : parsedEditPlan.actions) {
			std::string label = !action.type.empty() ? action.type : "edit";
			if (!action.instruction.empty()) {
				label += ": " + action.instruction;
			} else if (!action.rationale.empty()) {
				label += ": " + action.rationale;
			}
			ImGui::BulletText("%s", label.c_str());
		}
	}
	ImGui::InputTextMultiline(
		"Edit Plan JSON",
		videoEditPlanJson,
		sizeof(videoEditPlanJson),
		ImVec2(0, 180));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		const auto parsedResult = ofxGgmlVideoPlanner::parseEditPlanJson(trim(videoEditPlanJson));
		videoEditPlanSummary = parsedResult.isOk()
			? ofxGgmlVideoPlanner::summarizeEditPlan(parsedResult.value())
			: std::string();
		resetVideoEditWorkflowState();
		autoSaveSession();
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Sidecar URL", videoSidecarUrl, sizeof(videoSidecarUrl));
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip("Optional temporal sidecar endpoint for Action and Emotion tasks. Example: http://127.0.0.1:8090/analyze");
	}
	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Sidecar model", videoSidecarModel, sizeof(videoSidecarModel));
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip("Optional model or route hint forwarded to the temporal sidecar.");
	}
	if (!visionProfiles.empty()) {
		const auto & profile = visionProfiles[static_cast<size_t>(selectedVisionProfileIndex)];
		if (!profile.supportsMultipleImages) {
			ImGui::TextDisabled("This profile is single-image oriented. Video analysis will use one representative frame.");
		}
	}
	if ((videoTaskIndex == static_cast<int>(ofxGgmlVideoTask::Action) ||
		 videoTaskIndex == static_cast<int>(ofxGgmlVideoTask::Emotion)) &&
		trim(videoSidecarUrl).empty()) {
		ImGui::TextDisabled("Action and Emotion can run through the current vision server, but improve with a temporal sidecar.");
	}

	ImGui::BeginDisabled(generating.load() || std::strlen(visionImagePath) == 0);
	if (ImGui::Button("Run Vision", ImVec2(140, 0))) {
		runVisionInference();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(generating.load() || std::strlen(visionVideoPath) == 0);
	if (ImGui::Button("Run Video", ImVec2(140, 0))) {
		runVideoInference();
	}
	ImGui::EndDisabled();

	ImGui::Separator();
	ImGui::Text("Output:");
	if (!visionOutput.empty()) {
		ImGui::SameLine();
		if (ImGui::SmallButton("Copy##VisionCopy")) copyToClipboard(visionOutput);
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear##VisionClear")) {
			visionOutput.clear();
			visionSampledVideoFrames.clear();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(%d chars)", static_cast<int>(visionOutput.size()));
	}
	if (!visionSampledVideoFrames.empty()) {
		ImGui::TextDisabled(
			"Analyzed frames: %d",
			static_cast<int>(visionSampledVideoFrames.size()));
		drawLocalImagePreview(
			"Analyzed frame preview",
			visionOutputPreviewLoadedPath,
			visionOutputPreviewImage,
			visionOutputPreviewError,
			"##VisionOutputPreview");
		for (const auto & frame : visionSampledVideoFrames) {
			std::ostringstream line;
			line << frame.label;
			if (frame.timestampSeconds >= 0.0) {
				line << " @ " << ofxGgmlVideoInference::formatTimestamp(frame.timestampSeconds);
			}
			ImGui::BulletText("%s", line.str().c_str());
		}
	}
	if (generating.load() && activeGenerationMode == AiMode::Vision) {
		ImGui::BeginChild("##VisionOut", ImVec2(0, 0), true);
		std::string partial;
		{
			std::lock_guard<std::mutex> lock(streamMutex);
			partial = streamingOutput;
		}
		if (partial.empty()) {
			int dots = static_cast<int>(ImGui::GetTime() * kVisionWaitingDotsAnimationSpeed) % 4;
			ImGui::TextColored(
				ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
				"%s",
				kVisionWaitingLabels[dots]);
		} else {
			ImGui::TextWrapped("%s", partial.c_str());
		}
		ImGui::EndChild();
	} else {
		ImGui::BeginChild("##VisionOut", ImVec2(0, 0), true);
		if (visionOutput.empty()) {
			ImGui::TextDisabled("Vision responses appear here.");
		} else {
			ImGui::TextWrapped("%s", visionOutput.c_str());
		}
		ImGui::EndChild();
	}
}

void ofApp::runVisionInference() {
	if (generating.load()) return;

	if (visionProfiles.empty()) {
		visionProfiles = ofxGgmlVisionInference::defaultProfiles();
	}
	if (visionProfiles.empty()) {
		visionOutput = "[Error] No vision profiles are available.";
		return;
	}

	selectedVisionProfileIndex = std::clamp(
		selectedVisionProfileIndex,
		0,
		std::max(0, static_cast<int>(visionProfiles.size()) - 1));

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::Vision;
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = "Preparing multimodal request...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	const ofxGgmlVisionModelProfile profileBase =
		visionProfiles[static_cast<size_t>(selectedVisionProfileIndex)];
	const std::string prompt = trim(visionPrompt);
	const std::string imagePath = trim(visionImagePath);
	const std::string modelPath = trim(visionModelPath);
	const std::string serverUrl = trim(visionServerUrl);
	const std::string systemPrompt = trim(visionSystemPrompt);
	const int taskIndex = std::clamp(visionTaskIndex, 0, 2);
	const int requestedMaxTokens = std::clamp(maxTokens, 64, 4096);
	const float requestedTemperature = std::isfinite(temperature)
		? std::clamp(temperature, 0.0f, 2.0f)
		: 0.2f;

	workerThread = std::thread([this, profileBase, prompt, imagePath, modelPath, serverUrl, systemPrompt, taskIndex, requestedMaxTokens, requestedTemperature]() {
		auto setPending = [this](const std::string & text) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingOutput = text;
			pendingRole = "assistant";
			pendingMode = AiMode::Vision;
		};
		auto clearPendingVisionArtifacts = [this]() {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingVisionSampledVideoFrames.clear();
		};

		try {
			if (imagePath.empty()) {
				clearPendingVisionArtifacts();
				setPending("[Error] Select an image first.");
				generating.store(false);
				return;
			}

			ofxGgmlVisionModelProfile profile = profileBase;
			if (!serverUrl.empty()) {
				profile.serverUrl = serverUrl;
			}
			if (!modelPath.empty()) {
				profile.modelPath = modelPath;
			} else if (trim(profile.modelPath).empty() &&
				!trim(profile.modelFileHint).empty()) {
				profile.modelPath = resolveModelPathHint(trim(profile.modelFileHint));
			}
			const std::string effectiveServerUrl = trim(profile.serverUrl).empty()
				? std::string(kDefaultManagedTextServerUrl)
				: trim(profile.serverUrl);
			const bool serverReady = ensureLlamaServerReadyForModel(
				effectiveServerUrl,
				profile.modelPath,
				false,
				shouldManageLocalTextServer(effectiveServerUrl),
				true);
			if (!serverReady) {
				std::string detail = textServerStatusMessage;
				if (detail.empty() && shouldManageLocalTextServer(effectiveServerUrl)) {
					if (profile.modelPath.empty()) {
						detail = "Select a multimodal GGUF model first, or start llama-server manually.";
					} else {
						detail = "Local multimodal llama-server is not ready.";
					}
				}
				if (detail.empty()) {
					detail = "Vision server is not reachable.";
				}
				clearPendingVisionArtifacts();
				setPending("[Error] " + detail);
				generating.store(false);
				return;
			}
			const std::string capabilityDetail =
				visionCapabilityFailureDetail(effectiveServerUrl, profile.modelPath);
			if (!capabilityDetail.empty()) {
				clearPendingVisionArtifacts();
				setPending("[Error] " + capabilityDetail);
				generating.store(false);
				return;
			}

			ofxGgmlVisionRequest request;
			request.task = static_cast<ofxGgmlVisionTask>(taskIndex);
			request.prompt = prompt;
			request.systemPrompt = systemPrompt;
			request.maxTokens = requestedMaxTokens;
			request.temperature = requestedTemperature;
			if (chatLanguageIndex > 0 &&
				chatLanguageIndex < static_cast<int>(chatLanguages.size())) {
				request.responseLanguage =
					chatLanguages[static_cast<size_t>(chatLanguageIndex)].name;
			}
			std::string imageUploadNote;
			const std::string preparedImagePath =
				prepareVisionImageForUpload(imagePath, &imageUploadNote);
			if (!imageUploadNote.empty()) {
				logWithLevel(OF_LOG_NOTICE, imageUploadNote);
			}
			request.images.push_back({preparedImagePath, "Input image", ""});

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput =
					"Contacting " + ofxGgmlVisionInference::normalizeServerUrl(effectiveServerUrl);
			}

			const ofxGgmlVisionResult result = visionInference.runServerRequest(profile, request);
			if (cancelRequested.load()) {
				clearPendingVisionArtifacts();
				setPending("[Cancelled] Vision request cancelled.");
			} else if (result.success) {
				clearPendingVisionArtifacts();
				setPending(result.text);
				logWithLevel(
					OF_LOG_NOTICE,
					"Vision request completed in " +
						ofxGgmlHelpers::formatDurationMs(result.elapsedMs) +
						" via " + result.usedServerUrl);
			} else {
				clearPendingVisionArtifacts();
				setPending("[Error] " + result.error);
				if (!result.responseJson.empty()) {
					logWithLevel(OF_LOG_WARNING, "Vision response: " + result.responseJson);
				}
			}
		} catch (const std::exception & e) {
			clearPendingVisionArtifacts();
			setPending(std::string("[Error] Vision inference failed: ") + e.what());
		} catch (...) {
			clearPendingVisionArtifacts();
			setPending("[Error] Unknown failure during vision inference.");
		}

		{
			std::lock_guard<std::mutex> lock(streamMutex);
			streamingOutput.clear();
		}
		generating.store(false);
	});
}

void ofApp::runVideoPlanning() {
	if (generating.load()) return;

	const std::string sourcePrompt = trim(visionPrompt);
	if (sourcePrompt.empty()) {
		visionOutput = "[Error] Enter a video prompt before generating a plan.";
		return;
	}

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::Vision;
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = "Planning video timeline with the current text model...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	const std::string modelPath = getSelectedModelPath();
	const auto inferenceSettings = buildCurrentTextInferenceSettings(AiMode::Vision);
	const int beatCount = std::clamp(videoPlanBeatCount, 1, 12);
	const int sceneCount = std::clamp(videoPlanSceneCount, 1, 8);
	const double durationSeconds = std::clamp(static_cast<double>(videoPlanDurationSeconds), 1.0, 30.0);
	const std::string preferredStyle = trim(diffusionPrompt);
	const std::string negativePrompt = trim(diffusionNegativePrompt);
	const bool multiScene = videoPlanMultiScene;

	workerThread = std::thread([this, modelPath, inferenceSettings, sourcePrompt, beatCount, sceneCount, durationSeconds, preferredStyle, negativePrompt, multiScene]() {
		auto setPending = [this](const std::string & text) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingOutput = text;
			pendingRole = "assistant";
			pendingMode = AiMode::Vision;
		};
		auto clearPendingPlan = [this]() {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingVideoPlanJson.clear();
			pendingVideoPlanSummary.clear();
		};

		try {
			ofxGgmlVideoPlannerRequest request;
			request.prompt = sourcePrompt;
			request.beatCount = beatCount;
			request.sceneCount = sceneCount;
			request.multiScene = multiScene;
			request.durationSeconds = durationSeconds;
			request.preferredStyle = preferredStyle;
			request.negativePrompt = negativePrompt;

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput = "Generating structured video plan...";
			}

			const ofxGgmlVideoPlannerResult result =
				videoPlanner.plan(modelPath, request, inferenceSettings, llmInference);
			if (cancelRequested.load()) {
				clearPendingPlan();
				setPending("[Cancelled] Video plan generation cancelled.");
			} else if (result.success) {
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingVideoPlanJson = ofxGgmlVideoPlanner::extractJsonObject(result.rawText);
				pendingVideoPlanSummary = ofxGgmlVideoPlanner::summarizePlan(result.plan);
				pendingOutput =
					"Video plan ready.\n\n" +
					pendingVideoPlanSummary +
					"\n\nUse \"Apply plan to prompt\" to turn it into a stronger generation prompt, or keep \"Use plan for generation\" enabled to inject it automatically.";
				pendingRole = "assistant";
				pendingMode = AiMode::Vision;
			} else {
				clearPendingPlan();
				setPending("[Error] " + result.error);
			}
		} catch (const std::exception & e) {
			clearPendingPlan();
			setPending(std::string("[Error] Video planning failed: ") + e.what());
		} catch (...) {
			clearPendingPlan();
			setPending("[Error] Unknown failure during video planning.");
		}

		{
			std::lock_guard<std::mutex> lock(streamMutex);
			streamingOutput.clear();
		}
		generating.store(false);
	});
}

void ofApp::runMontagePlanning() {
	if (generating.load()) return;

	const std::string srtPath = trim(montageSubtitlePath);
	if (srtPath.empty()) {
		visionOutput = "[Error] Select a subtitle / SRT file before planning a montage.";
		return;
	}

	const std::string goal = trim(montageGoal);
	if (goal.empty()) {
		visionOutput = "[Error] Enter a montage goal before planning.";
		return;
	}

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::Vision;
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = "Scoring subtitle segments and building montage EDL...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	const std::string reelName = trim(montageReelName);
	const std::string edlTitle = trim(montageEdlTitle);
	const size_t maxClips = static_cast<size_t>(std::clamp(montageMaxClips, 1, 24));
	const int fps = std::clamp(montageFps, 12, 60);
	const double minScore = std::clamp(static_cast<double>(montageMinScore), 0.0, 1.0);
	const bool preserveChronology = montagePreserveChronology;

	workerThread = std::thread([this, srtPath, goal, reelName, edlTitle, maxClips, fps, minScore, preserveChronology]() {
		auto setPending = [this](const std::string & text) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingOutput = text;
			pendingRole = "assistant";
			pendingMode = AiMode::Vision;
		};
		auto clearPendingMontage = [this]() {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingMontageSummary.clear();
			pendingMontageEditorBrief.clear();
			pendingMontageEdlText.clear();
			pendingMontageSrtText.clear();
			pendingMontageVttText.clear();
			pendingMontagePreviewBundle = {};
			pendingMontageSubtitleTrack = {};
			pendingMontageSourceSubtitleTrack = {};
		};

		try {
			const auto segmentsResult = ofxGgmlMontagePlanner::loadSegmentsFromSrt(srtPath, reelName);
			if (!segmentsResult.isOk()) {
				clearPendingMontage();
				setPending("[Error] " + segmentsResult.error().message);
			} else {
				ofxGgmlMontagePlannerRequest request;
				request.goal = goal;
				request.segments = segmentsResult.value();
				request.maxClips = maxClips;
				request.minScore = minScore;
				request.preserveChronology = preserveChronology;
				request.fallbackReelName = reelName.empty() ? "AX" : reelName;

				const ofxGgmlMontagePlannerResult result = ofxGgmlMontagePlanner::plan(request);
				if (cancelRequested.load()) {
					clearPendingMontage();
					setPending("[Cancelled] Montage planning cancelled.");
				} else if (result.success) {
					const std::string safeTitle = edlTitle.empty() ? "MONTAGE" : edlTitle;
					const ofxGgmlMontagePreviewBundle previewBundle =
						ofxGgmlMontagePreviewBridge::buildBundle(
							result.plan,
							safeTitle,
							trim(visionVideoPath));
					const ofxGgmlMontageSubtitleTrack montageTrack =
						ofxGgmlMontagePlanner::buildSubtitleTrack(result.plan, safeTitle);
					const ofxGgmlMontageSubtitleTrack sourceTrack =
						ofxGgmlMontagePlanner::buildSourceSubtitleTrack(result.plan, safeTitle);
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingMontageSummary = ofxGgmlMontagePlanner::summarizePlan(result.plan);
					pendingMontageEditorBrief = ofxGgmlMontagePlanner::buildEditorBrief(result.plan);
					pendingMontageEdlText = ofxGgmlMontagePlanner::buildEdl(
						result.plan,
						safeTitle,
						fps);
					pendingMontageSrtText = ofxGgmlMontagePlanner::buildSrt(montageTrack);
					pendingMontageVttText = ofxGgmlMontagePlanner::buildVtt(montageTrack);
					pendingMontagePreviewBundle = previewBundle;
					pendingMontageSubtitleTrack = montageTrack;
					pendingMontageSourceSubtitleTrack = sourceTrack;
					pendingOutput =
						"Montage plan ready.\n\n" +
						pendingMontageSummary +
						"\n\nCMX EDL, montage-timed SRT, VTT, and an ofxVlc4-ready subtitle preview export are ready below.";
					pendingRole = "assistant";
					pendingMode = AiMode::Vision;
				} else {
					clearPendingMontage();
					setPending("[Error] " + result.error);
				}
			}
		} catch (const std::exception & e) {
			clearPendingMontage();
			setPending(std::string("[Error] Montage planning failed: ") + e.what());
		} catch (...) {
			clearPendingMontage();
			setPending("[Error] Unknown failure during montage planning.");
		}

		{
			std::lock_guard<std::mutex> lock(streamMutex);
			streamingOutput.clear();
		}
		generating.store(false);
	});
}

void ofApp::runVideoEditPlanning() {
	if (generating.load()) return;

	const std::string videoPath = trim(visionVideoPath);
	if (videoPath.empty()) {
		visionOutput = "[Error] Select a source video before planning edits.";
		return;
	}

	const std::string editGoal = trim(videoEditGoal);
	if (editGoal.empty()) {
		visionOutput = "[Error] Enter an edit goal before planning edits.";
		return;
	}

	const std::string analysisText = trim(visionOutput);
	if (videoEditUseCurrentAnalysis && analysisText.empty()) {
		visionOutput = "[Error] Run Video first or disable analysis grounding before planning edits.";
		return;
	}

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::Vision;
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = "Planning video edit strategy with the current text model...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	const std::string modelPath = getSelectedModelPath();
	const auto inferenceSettings = buildCurrentTextInferenceSettings(AiMode::Vision);
	const std::string sourcePrompt = trim(visionPrompt);
	const int clipCount = std::clamp(videoEditClipCount, 1, 12);
	const double targetDurationSeconds = std::clamp(static_cast<double>(videoEditTargetDurationSeconds), 1.0, 120.0);
	const bool useCurrentAnalysis = videoEditUseCurrentAnalysis;
	const std::vector<ofxGgmlSampledVideoFrame> sampledFrames = visionSampledVideoFrames;

	workerThread = std::thread([this, modelPath, inferenceSettings, sourcePrompt, editGoal, analysisText, clipCount, targetDurationSeconds, useCurrentAnalysis, sampledFrames]() {
		auto setPending = [this](const std::string & text) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingOutput = text;
			pendingRole = "assistant";
			pendingMode = AiMode::Vision;
		};
		auto clearPendingPlan = [this]() {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingVideoEditPlanJson.clear();
			pendingVideoEditPlanSummary.clear();
		};

		try {
			ofxGgmlVideoEditPlannerRequest request;
			request.sourcePrompt = sourcePrompt;
			request.editGoal = editGoal;
			request.clipCount = clipCount;
			request.targetDurationSeconds = targetDurationSeconds;
			request.preserveChronology = true;
			if (useCurrentAnalysis) {
				std::ostringstream groundedAnalysis;
				groundedAnalysis << analysisText;
				if (!sampledFrames.empty()) {
					groundedAnalysis << "\n\nSampled frames:";
					for (const auto & frame : sampledFrames) {
						groundedAnalysis << "\n- " << frame.label;
						if (frame.timestampSeconds >= 0.0) {
							groundedAnalysis << " @ " << ofxGgmlVideoInference::formatTimestamp(frame.timestampSeconds);
						}
					}
				}
				request.sourceAnalysis = groundedAnalysis.str();
			}

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput = "Generating structured video edit plan...";
			}

			const ofxGgmlVideoEditPlannerResult result =
				videoPlanner.planEdits(modelPath, request, inferenceSettings, llmInference);
			if (cancelRequested.load()) {
				clearPendingPlan();
				setPending("[Cancelled] Video edit planning cancelled.");
			} else if (result.success) {
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingVideoEditPlanJson = ofxGgmlVideoPlanner::extractJsonObject(result.rawText);
				pendingVideoEditPlanSummary = ofxGgmlVideoPlanner::summarizeEditPlan(result.plan);
				pendingOutput =
					"Video edit plan ready.\n\n" +
					pendingVideoEditPlanSummary +
					"\n\nUse \"Use brief in Write\" to turn it into an editor brief, or keep refining the JSON plan directly.";
				pendingRole = "assistant";
				pendingMode = AiMode::Vision;
			} else {
				clearPendingPlan();
				setPending("[Error] " + result.error);
			}
		} catch (const std::exception & e) {
			clearPendingPlan();
			setPending(std::string("[Error] Video edit planning failed: ") + e.what());
		} catch (...) {
			clearPendingPlan();
			setPending("[Error] Unknown failure during video edit planning.");
		}

		{
			std::lock_guard<std::mutex> lock(streamMutex);
			streamingOutput.clear();
		}
		generating.store(false);
	});
}

void ofApp::runVideoInference() {
	if (generating.load()) return;

	if (visionProfiles.empty()) {
		visionProfiles = ofxGgmlVisionInference::defaultProfiles();
	}
	if (visionProfiles.empty()) {
		visionOutput = "[Error] No vision profiles are available.";
		return;
	}

	selectedVisionProfileIndex = std::clamp(
		selectedVisionProfileIndex,
		0,
		std::max(0, static_cast<int>(visionProfiles.size()) - 1));

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::Vision;
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = "Sampling video frames...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	const ofxGgmlVisionModelProfile profileBase =
		visionProfiles[static_cast<size_t>(selectedVisionProfileIndex)];
	std::string prompt = trim(visionPrompt);
	const std::string videoPath = trim(visionVideoPath);
	const std::string modelPath = trim(visionModelPath);
	const std::string serverUrl = trim(visionServerUrl);
	const std::string sidecarUrl = trim(videoSidecarUrl);
	const std::string sidecarModel = trim(videoSidecarModel);
	const std::string systemPrompt = trim(visionSystemPrompt);
	const int taskIndex = std::clamp(videoTaskIndex, 0, 4);
	const int generationMode = std::clamp(videoPlanGenerationMode, 0, 1);
	const int requestedMaxTokens = std::clamp(maxTokens, 96, 4096);
	const float requestedTemperature = std::isfinite(temperature)
		? std::clamp(temperature, 0.0f, 2.0f)
		: 0.2f;
	const int sampledFrames = std::clamp(visionVideoMaxFrames, 1, 12);

	if (videoPlanUseForGeneration && !trim(videoPlanJson).empty()) {
		const auto parsedPlan = ofxGgmlVideoPlanner::parsePlanJson(videoPlanJson);
		if (parsedPlan.isOk()) {
			if (videoPlanMultiScene && !parsedPlan.value().scenes.empty()) {
				if (generationMode == 1) {
					prompt = ofxGgmlVideoPlanner::buildSceneSequencePrompt(parsedPlan.value());
				} else {
					const int clampedSceneIndex = std::clamp(
						selectedVideoPlanSceneIndex,
						0,
						std::max(0, static_cast<int>(parsedPlan.value().scenes.size()) - 1));
					prompt = ofxGgmlVideoPlanner::buildScenePrompt(
						parsedPlan.value(),
						static_cast<size_t>(clampedSceneIndex));
				}
			} else {
				prompt = ofxGgmlVideoPlanner::buildGenerationPrompt(parsedPlan.value());
			}
		}
	}

	if (videoPath.empty()) {
		visionOutput = "[Error] Select a video first.";
		generating.store(false);
		return;
	}

	ofxGgmlVideoRequest requestBase;
	requestBase.task = static_cast<ofxGgmlVideoTask>(taskIndex);
	requestBase.videoPath = videoPath;
	requestBase.prompt = prompt;
	requestBase.systemPrompt = systemPrompt;
	requestBase.sidecarUrl = sidecarUrl;
	requestBase.sidecarModel = sidecarModel;
	requestBase.maxTokens = requestedMaxTokens;
	requestBase.temperature = requestedTemperature;
	const int effectiveFrames = profileBase.supportsMultipleImages
		? sampledFrames
		: 1;
	requestBase.maxFrames = effectiveFrames;
	if (chatLanguageIndex > 0 &&
		chatLanguageIndex < static_cast<int>(chatLanguages.size())) {
		requestBase.responseLanguage =
			chatLanguages[static_cast<size_t>(chatLanguageIndex)].name;
	}

	std::string samplingError;
	std::vector<ofxGgmlSampledVideoFrame> preSampledFrames;
	try {
		preSampledFrames = videoInference.sampleFrames(requestBase, samplingError);
	} catch (const std::exception & e) {
		visionOutput = std::string("[Error] Video frame sampling failed: ") + e.what();
		generating.store(false);
		return;
	} catch (...) {
		visionOutput = "[Error] Unknown failure while sampling video frames.";
		generating.store(false);
		return;
	}

	if (preSampledFrames.empty()) {
		visionOutput = "[Error] " +
			(samplingError.empty() ? std::string("no frames were sampled from the video") : samplingError);
		generating.store(false);
		return;
	}

	workerThread = std::thread([
		this,
		profileBase,
		modelPath,
		serverUrl,
		sampledFrames,
		requestBase,
		preSampledFrames = std::move(preSampledFrames)
	]() {
		auto setPending = [this](const std::string & text) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingOutput = text;
			pendingRole = "assistant";
			pendingMode = AiMode::Vision;
		};
		auto clearPendingVisionArtifacts = [this]() {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingVisionSampledVideoFrames.clear();
		};

		try {
			ofxGgmlVisionModelProfile profile = profileBase;
			if (!serverUrl.empty()) {
				profile.serverUrl = serverUrl;
			}
			if (!modelPath.empty()) {
				profile.modelPath = modelPath;
			} else if (trim(profile.modelPath).empty() &&
				!trim(profile.modelFileHint).empty()) {
				profile.modelPath = resolveModelPathHint(trim(profile.modelFileHint));
			}
			const std::string effectiveServerUrl = trim(profile.serverUrl).empty()
				? std::string(kDefaultManagedTextServerUrl)
				: trim(profile.serverUrl);
			const bool serverReady = ensureLlamaServerReadyForModel(
				effectiveServerUrl,
				profile.modelPath,
				false,
				shouldManageLocalTextServer(effectiveServerUrl),
				true);
			if (!serverReady) {
				std::string detail = textServerStatusMessage;
				if (detail.empty() && shouldManageLocalTextServer(effectiveServerUrl)) {
					if (profile.modelPath.empty()) {
						detail = "Select a multimodal GGUF model first, or start llama-server manually.";
					} else {
						detail = "Local multimodal llama-server is not ready.";
					}
				}
				if (detail.empty()) {
					detail = "Vision server is not reachable.";
				}
				clearPendingVisionArtifacts();
				setPending("[Error] " + detail);
				generating.store(false);
				return;
			}
			const std::string capabilityDetail =
				visionCapabilityFailureDetail(effectiveServerUrl, profile.modelPath);
			if (!capabilityDetail.empty()) {
				clearPendingVisionArtifacts();
				setPending("[Error] " + capabilityDetail);
				generating.store(false);
				return;
			}

			ofxGgmlVideoRequest request = requestBase;

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				const bool prefersTemporalSidecar =
					(request.task == ofxGgmlVideoTask::Action ||
					 request.task == ofxGgmlVideoTask::Emotion) &&
					!trim(request.sidecarUrl).empty();
				streamingOutput = prefersTemporalSidecar
					? "Contacting " + ofxGgmlVideoInference::normalizeSidecarUrl(request.sidecarUrl)
					: "Contacting " + ofxGgmlVisionInference::normalizeServerUrl(effectiveServerUrl);
			}

			const bool prefersTemporalSidecar =
				(request.task == ofxGgmlVideoTask::Action ||
				 request.task == ofxGgmlVideoTask::Emotion) &&
				!trim(request.sidecarUrl).empty();
			const ofxGgmlVideoResult result = prefersTemporalSidecar
				? videoInference.runTemporalSidecarRequest(request, preSampledFrames)
				: videoInference.runServerRequest(profile, request, preSampledFrames);
			if (cancelRequested.load()) {
				clearPendingVisionArtifacts();
				setPending("[Cancelled] Video request cancelled.");
			} else if (result.success) {
				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingVisionSampledVideoFrames = result.sampledFrames;
				}
				std::ostringstream output;
				output << ((request.task == ofxGgmlVideoTask::Action)
					? "Video action analysis"
					: (request.task == ofxGgmlVideoTask::Emotion)
						? "Video emotion analysis"
						: "Video analysis");
				if (!result.backendName.empty()) {
					output << " (" << result.backendName << ")";
				}
				output << "\n";
				output << "Sampled frames: " << result.sampledFrames.size() << "\n";
				if (!result.usedServerUrl.empty()) {
					output << "Backend URL: " << result.usedServerUrl << "\n";
				}
				if (!profile.supportsMultipleImages && sampledFrames > 1) {
					output << "Note: selected profile is single-image oriented, so video analysis used one representative frame.\n";
				}
				output << "\n" << result.text;
				setPending(output.str());
				logWithLevel(
					OF_LOG_NOTICE,
					"Video request completed in " +
						ofxGgmlHelpers::formatDurationMs(result.elapsedMs) +
						" using " + result.backendName);
			} else {
				clearPendingVisionArtifacts();
				setPending("[Error] " + result.error);
				if (!result.visionResult.responseJson.empty()) {
					logWithLevel(OF_LOG_WARNING, "Video vision response: " + result.visionResult.responseJson);
				}
			}
		} catch (const std::exception & e) {
			clearPendingVisionArtifacts();
			setPending(std::string("[Error] Video inference failed: ") + e.what());
		} catch (...) {
			clearPendingVisionArtifacts();
			setPending("[Error] Unknown failure during video inference.");
		}

		{
			std::lock_guard<std::mutex> lock(streamMutex);
			streamingOutput.clear();
		}
		generating.store(false);
	});
}
