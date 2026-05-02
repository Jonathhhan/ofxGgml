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
#include <cctype>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace {

constexpr float kVisionWaitingDotsAnimationSpeed = 3.0f;
const char * const kVisionWaitingLabels[] = {
	"generating",
	"generating.",
	"generating..",
	"generating..."
};

constexpr const char * kDefaultManagedTextServerUrl = "http://127.0.0.1:8080";

std::string lowerAsciiCopy(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

bool containsSmolVlm2Token(const std::string & text) {
	const std::string lower = lowerAsciiCopy(text);
	return lower.find("smolvlm2") != std::string::npos ||
		lower.find("smol-vlm2") != std::string::npos;
}

bool isSmolVlm2VisionProfile(const ofxGgmlVisionModelProfile & profile) {
	return containsSmolVlm2Token(profile.name) ||
		containsSmolVlm2Token(profile.architecture) ||
		containsSmolVlm2Token(profile.modelRepoHint) ||
		containsSmolVlm2Token(profile.modelFileHint) ||
		containsSmolVlm2Token(std::filesystem::path(profile.modelPath).filename().string());
}

void appendPathToBuffer(char * buffer, size_t bufferSize, const std::string & path) {
	const std::string trimmedPath = trim(path);
	if (trimmedPath.empty()) {
		return;
	}

	std::vector<std::string> paths = extractPathList(buffer ? std::string(buffer) : std::string());
	if (std::find(paths.begin(), paths.end(), trimmedPath) == paths.end()) {
		paths.push_back(trimmedPath);
	}

	std::ostringstream joined;
	for (size_t i = 0; i < paths.size(); ++i) {
		if (i > 0) {
			joined << '\n';
		}
		joined << paths[i];
	}
	copyStringToBuffer(buffer, bufferSize, joined.str());
}

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

void ofApp::setupHoloscanBridge() {
	if (visionProfiles.empty()) {
		visionProfiles = ofxGgmlVisionInference::defaultProfiles();
	}
	selectedVisionProfileIndex = std::clamp(
		selectedVisionProfileIndex,
		0,
		std::max(0, static_cast<int>(visionProfiles.size()) - 1));

	copyStringToBuffer(holoscanVisionPrompt, sizeof(holoscanVisionPrompt), visionPrompt);
	copyStringToBuffer(
		holoscanVisionSystemPrompt,
		sizeof(holoscanVisionSystemPrompt),
		visionSystemPrompt);

	ofxGgmlHoloscanSettings settings;
	settings.enabled = true;
	settings.useEventScheduler = true;
	settings.workerThreads = 2;
	holoscanBridge.setup(&visionInference, settings);
	if (!visionProfiles.empty()) {
		holoscanBridge.submitProfile(
			visionProfiles[static_cast<size_t>(selectedVisionProfileIndex)]);
	}
	holoscanBridge.submitRequestTemplate(makeHoloscanVisionRequestTemplate());
	holoscanBridgeEnabled = true;
#if defined(TARGET_LINUX)
	holoscanVisionStatus = holoscanBridge.isHoloscanAvailable()
		? "Holoscan bridge ready. Native runtime will activate when the SDK is available."
		: "Holoscan bridge configured. Install the NVIDIA Holoscan SDK to enable the native runtime.";
#else
	holoscanVisionStatus =
		"Holoscan is Linux-only for now. The bridge stays in addon fallback mode on this platform.";
#endif
}

ofxGgmlHoloscanVisionRequestTemplate ofApp::makeHoloscanVisionRequestTemplate() const {
	ofxGgmlHoloscanVisionRequestTemplate requestTemplate;
	requestTemplate.task = static_cast<ofxGgmlVisionTask>(visionTaskIndex);
	requestTemplate.prompt = trim(
		holoscanUseCurrentVisionRequest ? visionPrompt : holoscanVisionPrompt);
	requestTemplate.systemPrompt = trim(
		holoscanUseCurrentVisionRequest ? visionSystemPrompt : holoscanVisionSystemPrompt);
	requestTemplate.maxTokens = std::max(32, maxTokens);
	requestTemplate.temperature = std::max(0.0f, temperature);
	return requestTemplate;
}

void ofApp::drawHoloscanBridgeSection() {
	ImGui::Separator();
	if (!ImGui::CollapsingHeader("Holoscan Bridge", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::Checkbox("Enable Holoscan lane", &holoscanBridgeEnabled);
#if defined(TARGET_LINUX)
	if (holoscanBridge.isHoloscanAvailable()) {
		ImGui::TextDisabled("Native Holoscan runtime is available on this machine.");
	} else {
		ImGui::TextDisabled("Native Holoscan runtime is not installed. The bridge will use the addon fallback path.");
	}
#else
	ImGui::TextDisabled("Holoscan is Linux-only for now. This panel stays in addon fallback mode on this platform.");
#endif
	if (!holoscanVisionStatus.empty()) {
		ImGui::TextWrapped("%s", holoscanVisionStatus.c_str());
	}

	ImGui::Checkbox("Use current Vision request", &holoscanUseCurrentVisionRequest);
	if (!holoscanUseCurrentVisionRequest) {
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextMultiline(
			"Holoscan Prompt",
			holoscanVisionPrompt,
			sizeof(holoscanVisionPrompt),
			ImVec2(-1.0f, 90.0f));
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextMultiline(
			"Holoscan System",
			holoscanVisionSystemPrompt,
			sizeof(holoscanVisionSystemPrompt),
			ImVec2(-1.0f, 70.0f));
	}

	ImGui::BeginDisabled(!holoscanBridgeEnabled);
	if (!holoscanBridgeRunning) {
		if (ImGui::Button("Start Bridge", ImVec2(140, 0))) {
			if (!visionProfiles.empty()) {
				holoscanBridge.submitProfile(
					visionProfiles[static_cast<size_t>(selectedVisionProfileIndex)]);
			}
			holoscanBridge.submitRequestTemplate(makeHoloscanVisionRequestTemplate());
			if (holoscanBridge.startVisionPipeline()) {
				holoscanBridgeRunning = true;
				holoscanVisionStatus = holoscanBridge.isHoloscanAvailable()
					? "Holoscan bridge started."
					: "Holoscan bridge started in addon fallback mode.";
			} else {
				holoscanVisionStatus = trim(holoscanBridge.getLastError());
			}
		}
	} else if (ImGui::Button("Stop Bridge", ImVec2(140, 0))) {
		holoscanBridge.stop();
		holoscanBridgeRunning = false;
		holoscanVisionStatus = "Holoscan bridge stopped.";
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(std::strlen(visionImagePath) == 0);
	if (ImGui::Button("Submit Loaded Image", ImVec2(170, 0))) {
		if (!holoscanBridgeRunning) {
			if (!visionProfiles.empty()) {
				holoscanBridge.submitProfile(
					visionProfiles[static_cast<size_t>(selectedVisionProfileIndex)]);
			}
			holoscanBridge.submitRequestTemplate(makeHoloscanVisionRequestTemplate());
			if (!holoscanBridge.startVisionPipeline()) {
				holoscanVisionStatus = trim(holoscanBridge.getLastError());
				ImGui::EndDisabled();
				ImGui::EndDisabled();
				return;
			}
			holoscanBridgeRunning = true;
		}

		ofPixels pixels;
		if (!ofLoadImage(pixels, trim(visionImagePath))) {
			holoscanVisionStatus = "Failed to load the selected vision image for Holoscan bridge submission.";
		} else {
			if (!visionProfiles.empty()) {
				holoscanBridge.submitProfile(
					visionProfiles[static_cast<size_t>(selectedVisionProfileIndex)]);
			}
			holoscanBridge.submitRequestTemplate(makeHoloscanVisionRequestTemplate());
			holoscanBridge.submitFrame(pixels, ofGetElapsedTimef(), "vision-image");
			holoscanVisionStatus = "Submitted the loaded image to the Holoscan bridge.";
		}
	}
	ImGui::EndDisabled();
	ImGui::EndDisabled();

	ImGui::TextDisabled("Completed frames: %d", holoscanVisionCompletedFrames);
	if (holoscanBridge.hasPreviewFrame()) {
		const ofTexture & previewTexture = holoscanBridge.getPreviewTexture();
		const float availWidth = std::max(160.0f, ImGui::GetContentRegionAvail().x);
		const float maxWidth = std::min(availWidth, 420.0f);
		const float texWidth = std::max(1.0f, previewTexture.getWidth());
		const float texHeight = std::max(1.0f, previewTexture.getHeight());
		const float scale = std::min(maxWidth / texWidth, 240.0f / texHeight);
		const ImVec2 drawSize(
			std::max(1.0f, texWidth * scale),
			std::max(1.0f, texHeight * scale));

		ImGui::BeginChild("##HoloscanVisionPreview", ImVec2(0, drawSize.y + 12.0f), true);
		ofxImGui::AddImage(previewTexture, glm::vec2(drawSize.x, drawSize.y));
		ImGui::EndChild();
	}
	if (!holoscanVisionLatestOutput.empty()) {
		ImGui::TextWrapped("%s", holoscanVisionLatestOutput.c_str());
	}
}

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
		"Use a local multimodal GGUF model for image and video understanding. "
		"Most profiles use OpenAI-compatible llama-server requests; SmolVLM2 image requests use llama.cpp's multimodal /completion endpoint with the server-reported media marker.");

	if (ImGui::CollapsingHeader("Setup & Source", ImGuiTreeNodeFlags_DefaultOpen)) {
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
			ImGui::TextDisabled("Preset server model: %s", profile.modelRepoHint.c_str());
		}
		if (!profile.modelFileHint.empty()) {
			ImGui::TextDisabled("Preset file: %s", profile.modelFileHint.c_str());
		}
		if (!recommendedModelPath.empty()) {
			ImGui::TextDisabled("Preset local path: %s", recommendedModelPath.c_str());
			ImGui::TextDisabled(
				pathExists(recommendedModelPath)
					? "Preset model is already present."
					: "Preset model is not downloaded yet.");
			ImGui::BeginDisabled(trim(visionModelPath) == recommendedModelPath);
			if (ImGui::SmallButton("Use preset path##Vision")) {
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
				showWrappedTooltip("Sets the model path to the profile's preset file under the shared addon models/ folder.");
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
				if (opensModelPage) {
					showWrappedTooltip("Opens the preset model page in your browser so you can pick the exact GGUF file.");
				} else {
					showWrappedTooltip("Opens the preset multimodal model in your browser.");
				}
			}
		}
		if (profile.mayRequireMmproj) {
			ImGui::TextDisabled("Note: some variants also need a matching mmproj file on the server side.");
			if (profile.modelRepoHint == "ggml-org/SmolVLM2-500M-Video-Instruct-GGUF") {
				const char * smolVlmMmprojFile =
					"mmproj-SmolVLM2-500M-Video-Instruct-Q8_0.gguf";
				const std::string smolVlmMmprojPath = suggestedModelPath("", smolVlmMmprojFile);
				ImGui::TextDisabled("Expected projector: %s", smolVlmMmprojFile);
				ImGui::TextDisabled("SmolVLM2 image mode uses llama-server /props + /completion.");
				ImGui::TextDisabled(
					pathExists(smolVlmMmprojPath)
						? "SmolVLM2 projector is already present."
						: "Download the SmolVLM2 projector next to the text model.");
				ImGui::SameLine();
				if (ImGui::SmallButton("Download projector##SmolVLM2")) {
					ofLaunchBrowser(
						"https://huggingface.co/ggml-org/"
						"SmolVLM2-500M-Video-Instruct-GGUF/resolve/main/"
						"mmproj-SmolVLM2-500M-Video-Instruct-Q8_0.gguf");
				}
			}
		}
		ImGui::TextDisabled(
			"Task-fit hints | OCR: %s | Multi-image: %s",
			profile.supportsOcr ? "usual fit" : "manual / backend-dependent",
			profile.supportsMultipleImages ? "usual fit" : "single-image oriented");
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
	}

	if (ImGui::CollapsingHeader("Image Analysis", ImGuiTreeNodeFlags_DefaultOpen)) {
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
	}

	if (ImGui::CollapsingHeader("Video Workflow")) {
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
	ImGui::SetNextItemWidth(180);
	ImGui::SliderFloat("Target duration (s)", &montageTargetDurationSeconds, 1.0f, 120.0f, "%.1f");
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(180);
	ImGui::SliderFloat("Min clip spacing (s)", &montageMinSpacingSeconds, 0.0f, 15.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(180);
	ImGui::SliderFloat("Pre-roll handle (s)", &montagePreRollSeconds, 0.0f, 5.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(180);
	ImGui::SliderFloat("Post-roll handle (s)", &montagePostRollSeconds, 0.0f, 5.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::Checkbox("Preserve subtitle chronology", &montagePreserveChronology);
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::TextDisabled(
		"Spacing reduces adjacent picks; handles add visual lead-in/out around subtitle hits.");
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
		montageClipPlaylistManifestPath.clear();
		montageClipPlaylistStatusMessage.clear();
		montageClipRenderOutputPath.clear();
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
		closeMontageClipVlcPreview();
#endif
		selectedMontageCueIndex = -1;
		autoSaveSession();
	}
	ImGui::EndDisabled();
	if (!montageSummary.empty()) {
		ImGui::TextWrapped("%s", montageSummary.c_str());
	}
	if (!montagePreviewBundle.playlistClips.empty()) {
		double estimatedDurationSeconds = 0.0;
		for (const auto & clip : montagePreviewBundle.playlistClips) {
			estimatedDurationSeconds += std::max(0.0, clip.endSeconds - clip.startSeconds);
		}
		ImGui::TextDisabled(
			"Estimated edit length: %.2f s across %d clip(s)",
			estimatedDurationSeconds,
			static_cast<int>(montagePreviewBundle.playlistClips.size()));
		int themeBucketCount = 0;
		{
			std::unordered_set<std::string> buckets;
			for (const auto & clip : montagePreviewBundle.playlistClips) {
				if (!clip.themeBucket.empty()) {
					buckets.insert(clip.themeBucket);
				}
			}
			themeBucketCount = static_cast<int>(buckets.size());
		}
		if (themeBucketCount > 0) {
			ImGui::TextDisabled("Theme buckets: %d", themeBucketCount);
		}
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
		ImGui::TextDisabled("Optional VLC subtitle preview");
		ImGui::TextWrapped(
			"Load the active source-timed or montage-timed subtitle track into the optional VLC preview lane.");
		if (ImGui::Button("Load in VLC preview", ImVec2(190, 0))) {
			std::string error;
			if (loadMontageVlcPreview(&error)) {
				montagePreviewStatusMessage =
					"Loaded active subtitle track into the optional VLC preview.";
			} else {
				montagePreviewStatusMessage =
					error.empty() ? std::string("Failed to load the optional VLC preview.") : error;
			}
			autoSaveSession();
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(!montageVlcPreviewInitialized);
		if (ImGui::Button("Close VLC preview", ImVec2(190, 0))) {
			closeMontageVlcPreview();
			montagePreviewStatusMessage = "Closed the optional VLC preview.";
			autoSaveSession();
		}
		ImGui::EndDisabled();
		if (montageVlcPreviewInitialized) {
			drawMontageVlcPreview();
		}
#else
		ImGui::Separator();
		ImGui::TextDisabled(
			"Optional VLC subtitle preview is unavailable in this build. Add ofxVlc4 back to addons.make only if you want it.");
#endif
	}
	ImGui::Separator();
	ImGui::TextDisabled("Clip playlist export");
	ImGui::TextWrapped(
		"Collect rendered or generated clip paths, export a small playlist manifest, and optionally preview or record the sequence through the separate VLC lane.");
	ImGui::InputTextMultiline(
		"Clip paths (one per line)",
		montageClipPaths,
		sizeof(montageClipPaths),
		ImVec2(-1, 90));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::BeginDisabled(trim(visionVideoPath).empty());
	if (ImGui::Button("Add source video", ImVec2(150, 0))) {
		appendPathToBuffer(montageClipPaths, sizeof(montageClipPaths), trim(visionVideoPath));
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(videoEssayLastRenderedVideoPath).empty());
	if (ImGui::Button("Add essay render", ImVec2(150, 0))) {
		appendPathToBuffer(
			montageClipPaths,
			sizeof(montageClipPaths),
			trim(videoEssayLastRenderedVideoPath));
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Use generated outputs", ImVec2(180, 0))) {
		std::string status;
		if (populateMontageClipPlaylistFromGeneratedOutputs(&status)) {
			montageClipPlaylistStatusMessage = status;
		} else {
			montageClipPlaylistStatusMessage =
				status.empty() ? std::string("No generated video outputs are ready yet.") : status;
		}
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse clip...", ImVec2(130, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select clip", false);
		if (result.bSuccess) {
			appendPathToBuffer(montageClipPaths, sizeof(montageClipPaths), result.getPath());
			autoSaveSession();
		}
	}

	ImGui::InputText("Mux audio track", montageRenderAudioPath, sizeof(montageRenderAudioPath));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(aceStepGeneratedTracks.empty());
	if (ImGui::SmallButton("Use selected music")) {
		const int selectedTrackIndex = std::clamp(
			aceStepSelectedTrackIndex,
			0,
			std::max(0, static_cast<int>(aceStepGeneratedTracks.size()) - 1));
		copyStringToBuffer(
			montageRenderAudioPath,
			sizeof(montageRenderAudioPath),
			trim(aceStepGeneratedTracks[static_cast<size_t>(selectedTrackIndex)].path));
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::SmallButton("Browse audio...")) {
		ofFileDialogResult result = ofSystemLoadDialog("Select mux audio track", false);
		if (result.bSuccess) {
			copyStringToBuffer(montageRenderAudioPath, sizeof(montageRenderAudioPath), result.getPath());
			autoSaveSession();
		}
	}

	if (ImGui::Button("Export playlist manifest", ImVec2(180, 0))) {
		std::string error;
		const std::string manifestPath = exportMontageClipPlaylistManifest(&error);
		if (!manifestPath.empty()) {
			montageClipPlaylistManifestPath = manifestPath;
			montageClipPlaylistStatusMessage = "Exported montage clip manifest: " + manifestPath;
		} else {
			montageClipPlaylistStatusMessage =
				error.empty() ? std::string("Failed to export the montage clip manifest.") : error;
		}
		autoSaveSession();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(montageClipPlaylistManifestPath).empty());
	if (ImGui::Button("Copy manifest path", ImVec2(170, 0))) {
		copyToClipboard(montageClipPlaylistManifestPath);
	}
	ImGui::EndDisabled();
	if (!trim(montageClipPlaylistManifestPath).empty()) {
		ImGui::TextDisabled("%s", montageClipPlaylistManifestPath.c_str());
	}

#if OFXGGML_HAS_OFXVLC4
	if (ImGui::Button("Auto-fill + Preview/Record", ImVec2(220, 0))) {
		std::string error;
		if (startMontageGeneratedClipPreviewAndRecording(&error)) {
			// status set by the helper
		} else {
			montageClipPlaylistStatusMessage =
				error.empty() ? std::string("Failed to start the generated clip playlist export.") : error;
		}
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Button("Load clip playlist in VLC", ImVec2(220, 0))) {
		std::string error;
		if (loadMontageClipVlcPreview(&error)) {
			montageClipPlaylistStatusMessage =
				"Loaded the montage clip playlist into the optional VLC preview.";
		} else {
			montageClipPlaylistStatusMessage =
				error.empty() ? std::string("Failed to load the montage clip playlist preview.") : error;
		}
		autoSaveSession();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!montageClipVlcInitialized);
	if (ImGui::Button("Close clip playlist", ImVec2(170, 0))) {
		closeMontageClipVlcPreview();
		montageClipPlaylistStatusMessage = "Closed the montage clip playlist preview.";
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!montageClipVlcInitialized || montageClipVlcPlayer.isVideoRecording());
	if (ImGui::Button("Record playlist", ImVec2(150, 0))) {
		std::string error;
		if (startMontageClipVlcRecording(&error)) {
			montageClipPlaylistStatusMessage = "Recording the montage clip playlist preview...";
		} else {
			montageClipPlaylistStatusMessage =
				error.empty() ? std::string("Failed to start the montage clip playlist recording.") : error;
		}
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!montageClipVlcInitialized || !montageClipVlcPlayer.isVideoRecording());
	if (ImGui::Button("Stop + Render playlist", ImVec2(190, 0))) {
		std::string error;
		if (stopMontageClipVlcRecording(&error)) {
			// status set by the helper
		} else {
			montageClipPlaylistStatusMessage =
				error.empty() ? std::string("Failed to finalize the montage clip playlist render.") : error;
		}
		autoSaveSession();
	}
	ImGui::EndDisabled();
	if (montageClipVlcInitialized) {
		drawMontageClipVlcPreview();
	}
#else
	ImGui::TextDisabled(
		"Optional VLC playlist preview / recording is unavailable in this build. Add ofxVlc4 back to addons.make only if you want that extra lane.");
#endif
	if (!montageClipPlaylistStatusMessage.empty()) {
		ImGui::TextDisabled("%s", montageClipPlaylistStatusMessage.c_str());
	}
	if (!trim(montageClipRenderOutputPath).empty()) {
		ImGui::TextDisabled("Rendered playlist: %s", montageClipRenderOutputPath.c_str());
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
			auto selectedClip = std::find_if(
				montagePreviewBundle.playlistClips.begin(),
				montagePreviewBundle.playlistClips.end(),
				[&cue](const ofxGgmlMontageClip & clip) {
					return clip.sourceId == cue.sourceId;
				});
			if (selectedClip != montagePreviewBundle.playlistClips.end()) {
				if (!selectedClip->themeBucket.empty()) {
					ImGui::TextDisabled("Theme: %s", selectedClip->themeBucket.c_str());
				}
				if (!selectedClip->transitionSuggestion.empty()) {
					ImGui::TextWrapped("Transition: %s", selectedClip->transitionSuggestion.c_str());
				}
			}
		}
	}
	if (!montagePreviewBundle.playlistClips.empty() &&
		ImGui::TreeNode("Cut suggestions")) {
		for (const auto & clip : montagePreviewBundle.playlistClips) {
			std::ostringstream label;
			label << clip.index << ". " << clip.clipName;
			if (!clip.themeBucket.empty()) {
				label << " [" << clip.themeBucket << "]";
			}
			ImGui::BulletText("%s", label.str().c_str());
			if (!clip.transitionSuggestion.empty()) {
				ImGui::TextWrapped("    %s", clip.transitionSuggestion.c_str());
			}
		}
		ImGui::TreePop();
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
			applyVideoEditPresetByIndex(presetIndex);
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
	ImGui::SameLine();
	if (ImGui::SmallButton("Music Video")) {
		applyVideoEditPreset(3);
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
	}

	if (ImGui::CollapsingHeader("Run & Output", ImGuiTreeNodeFlags_DefaultOpen)) {
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
	drawHoloscanBridgeSection();

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

	if (ImGui::CollapsingHeader("Creative Extensions")) {
		drawImageSearchPanel("Use Vision prompt", trim(visionPrompt));
		drawImageToMusicSection();
		drawAceStepMusicSection();
		drawMusicVideoWorkflowSection();
	}
}

bool ofApp::saveImageToMusicNotationToConfiguredPath() {
	const std::string outputPath = trim(imageToMusicAbcOutputPath);
	if (outputPath.empty()) {
		imageToMusicStatus = "[Error] Choose an output .abc path first.";
		return false;
	}
	const std::string savedPath =
		musicGenerator.saveAbcNotation(imageToMusicNotationOutput, outputPath);
	if (savedPath.empty()) {
		imageToMusicStatus = "[Error] Failed to save the generated ABC notation.";
		return false;
	}
	imageToMusicSavedNotationPath = savedPath;
	copyStringToBuffer(
		imageToMusicAbcOutputPath,
		sizeof(imageToMusicAbcOutputPath),
		savedPath);
	imageToMusicStatus = "Saved ABC sketch to " + savedPath;
	return true;
}

void ofApp::applyVideoEditPresetByIndex(int presetIndex) {
	const int clampedIndex = std::clamp(presetIndex, 0, kVideoEditPresetCount - 1);
	const auto & preset = kVideoEditPresets[clampedIndex];
	videoEditPresetIndex = clampedIndex;
	copyStringToBuffer(videoEditGoal, sizeof(videoEditGoal), preset.goal);
	videoEditClipCount = std::clamp(preset.clipCount, 1, 12);
	videoEditTargetDurationSeconds = std::clamp(preset.targetDurationSeconds, 1.0f, 120.0f);
	videoEditUseCurrentAnalysis = preset.useCurrentAnalysis;
	resetVideoEditWorkflowState();
	autoSaveSession();
}

void ofApp::applyMusicVideoWorkflowDefaults(bool overwriteVisionPrompt) {
	applyVideoEditPresetByIndex(3);
	videoPlanMultiScene = true;
	videoPlanGenerationMode = 1;
	videoPlanBeatCount = std::max(videoPlanBeatCount, 8);
	videoPlanSceneCount = std::max(videoPlanSceneCount, 4);
	videoPlanDurationSeconds = std::max(videoPlanDurationSeconds, 24.0f);
	musicVideoSectionCount = std::max(musicVideoSectionCount, 4);
	musicVideoCutIntensity = std::max(musicVideoCutIntensity, 0.7f);
	if (overwriteVisionPrompt) {
		const std::string visualConcept = trim(musicToImagePromptOutput);
		if (!visualConcept.empty()) {
			copyStringToBuffer(visionPrompt, sizeof(visionPrompt), visualConcept);
		} else {
			const std::string fallback =
				!trim(musicToImageDescription).empty()
					? trim(musicToImageDescription)
					: trim(musicToImageLyrics);
			if (!fallback.empty()) {
				copyStringToBuffer(visionPrompt, sizeof(visionPrompt), fallback);
			}
		}
	}
	autoSaveSession();
}

void ofApp::drawImageToMusicSection() {
	if (trim(imageToMusicAbcOutputPath).empty()) {
		std::filesystem::path baseDir = addonRootPath() / "generated" / "music";
		if (baseDir.empty()) {
			baseDir = std::filesystem::path(ofToDataPath("generated/music", true));
		}
		const std::string suggestion = (baseDir /
			ofxGgmlMusicGenerator::makeSuggestedFileName(
				trim(imageToMusicDescription).empty()
					? trim(visionPrompt)
					: trim(imageToMusicDescription))).lexically_normal().string();
		copyStringToBuffer(
			imageToMusicAbcOutputPath,
			sizeof(imageToMusicAbcOutputPath),
			suggestion);
	}

	ImGui::Separator();
	ImGui::Text("Image / Prompt -> Music");
	ImGui::TextWrapped(
		"Turn a visual description into a backend-ready music prompt, then optionally sketch a local ABC theme. "
		"This keeps the addon local-first: prompt translation and notation use the current text model, while actual audio rendering can be attached later through a music backend bridge.");

	ImGui::BeginDisabled(trim(visionOutput).empty());
	if (ImGui::Button("Use Vision Output", ImVec2(140, 0))) {
		copyStringToBuffer(
			imageToMusicDescription,
			sizeof(imageToMusicDescription),
			trim(visionOutput));
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(visionPrompt).empty());
	if (ImGui::Button("Use Vision Prompt", ImVec2(140, 0))) {
		copyStringToBuffer(
			imageToMusicDescription,
			sizeof(imageToMusicDescription),
			trim(visionPrompt));
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(customInput).empty());
	if (ImGui::Button("Use Custom Input", ImVec2(140, 0))) {
		copyStringToBuffer(
			imageToMusicDescription,
			sizeof(imageToMusicDescription),
			trim(customInput));
		autoSaveSession();
	}
	ImGui::EndDisabled();

	ImGui::InputTextMultiline(
		"Visual description / scene",
		imageToMusicDescription,
		sizeof(imageToMusicDescription),
		ImVec2(-1, 90));
	ImGui::InputTextMultiline(
		"Scene notes",
		imageToMusicSceneNotes,
		sizeof(imageToMusicSceneNotes),
		ImVec2(-1, 70));
	ImGui::InputText(
		"Musical style",
		imageToMusicStyle,
		sizeof(imageToMusicStyle));
	ImGui::InputText(
		"Instrumentation",
		imageToMusicInstrumentation,
		sizeof(imageToMusicInstrumentation));
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt(
		"Target music duration",
		&imageToMusicDurationSeconds,
		8,
		90,
		"%d s");
	ImGui::SameLine();
	ImGui::Checkbox("Instrumental only", &imageToMusicInstrumentalOnly);

	const bool hasMusicInput =
		!generating.load() &&
		(!trim(imageToMusicDescription).empty() || !trim(imageToMusicSceneNotes).empty());
	ImGui::BeginDisabled(!hasMusicInput);
	if (ImGui::Button("Generate Music Prompt", ImVec2(170, 0))) {
		runImageToMusicPromptGeneration();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!hasMusicInput);
	if (ImGui::Button("Generate ABC Sketch", ImVec2(170, 0))) {
		runImageToMusicNotationGeneration();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(imageToMusicPromptOutput).empty());
	if (ImGui::Button("Use in Custom", ImVec2(120, 0))) {
		copyStringToBuffer(customInput, sizeof(customInput), imageToMusicPromptOutput);
		activeMode = AiMode::Custom;
	}
	ImGui::EndDisabled();

	ImGui::SetNextItemWidth(220);
	ImGui::InputText(
		"ABC title",
		imageToMusicAbcTitle,
		sizeof(imageToMusicAbcTitle));
	ImGui::SameLine();
	ImGui::SetNextItemWidth(120);
	ImGui::InputText(
		"Key",
		imageToMusicAbcKey,
		sizeof(imageToMusicAbcKey));
	ImGui::SameLine();
	ImGui::SetNextItemWidth(140);
	ImGui::SliderInt(
		"Bars",
		&imageToMusicAbcBars,
		8,
		32);

	ImGui::InputText(
		"ABC output path",
		imageToMusicAbcOutputPath,
		sizeof(imageToMusicAbcOutputPath));
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(imageToMusicNotationOutput).empty());
	if (ImGui::Button("Save .abc", ImVec2(90, 0))) {
		saveImageToMusicNotationToConfiguredPath();
	}
	ImGui::EndDisabled();

	if (!imageToMusicStatus.empty()) {
		ImGui::TextWrapped("%s", imageToMusicStatus.c_str());
	}
	if (!imageToMusicSavedNotationPath.empty()) {
		ImGui::TextDisabled("Last saved notation: %s", imageToMusicSavedNotationPath.c_str());
	}

	ImGui::Text("Generated music prompt:");
	ImGui::BeginChild("##ImageToMusicPromptOut", ImVec2(0, 90.0f), true);
	if (imageToMusicPromptOutput.empty()) {
		ImGui::TextDisabled("Music prompt output will appear here.");
	} else {
		ImGui::TextUnformatted(imageToMusicPromptOutput.c_str());
	}
	ImGui::EndChild();

	ImGui::Text("Generated ABC sketch:");
	ImGui::BeginChild("##ImageToMusicAbcOut", ImVec2(0, 140.0f), true);
	if (imageToMusicNotationOutput.empty()) {
		ImGui::TextDisabled("ABC notation output will appear here.");
	} else {
		ImGui::TextUnformatted(imageToMusicNotationOutput.c_str());
	}
	ImGui::EndChild();
}

void ofApp::drawAceStepMusicSection() {
	if (trim(aceStepOutputDir).empty()) {
		std::filesystem::path baseDir = addonRootPath() / "generated" / "music";
		if (baseDir.empty()) {
			baseDir = std::filesystem::path(ofToDataPath("generated/music", true));
		}
		copyStringToBuffer(
			aceStepOutputDir,
			sizeof(aceStepOutputDir),
			baseDir.lexically_normal().string());
	}

	ImGui::Separator();
	ImGui::Text("AceStep Music Backend");
	ImGui::TextWrapped(
		"Use an acestep.cpp-compatible server for rendered audio generation and audio understanding. "
		"The bridge keeps prompt prep local-first, then hands prompt, lyrics, BPM, key, and duration to the AceStep API.");
	ImGui::TextDisabled(
		"No AceStep install is needed for local music-prompt or ABC-sketch generation. "
		"You only need AceStep when you want rendered audio tracks or audio understanding. "
		"When the server URL is local, the app can auto-start ace-server.exe for you.");
	if (ImGui::SmallButton("Check / Start AceStep server")) {
		const std::string serverUrl = effectiveAceStepServerUrl(aceStepServerUrl);
		if (ensureAceStepServerReady(false, true)) {
			const ofxGgmlAceStepHealthResult health = aceStepBridge.healthCheck(serverUrl);
			const ofxGgmlAceStepPropsResult props = aceStepBridge.fetchProps(serverUrl);
			std::ostringstream status;
			status << "AceStep server is reachable";
			if (!trim(health.status).empty()) {
				status << " (" << trim(health.status) << ")";
			}
			status << " at " << serverUrl;
			if (aceStepServerManagedByApp) {
				status << " [managed locally]";
			}
			if (props.success) {
				status << ". /lm: "
					<< (trim(props.lmStatus).empty() ? std::string("ok") : trim(props.lmStatus))
					<< ", /synth: "
					<< (trim(props.synthStatus).empty() ? std::string("ok") : trim(props.synthStatus));
				if (props.maxBatch > 0) {
					status << ", max batch " << props.maxBatch;
				}
			}
			aceStepStatus = status.str() + ".";
			aceStepUsedServerUrl = health.usedServerUrl;
		} else {
			aceStepStatus = "[Setup] " + aceStepServerStatusMessage;
			aceStepUsedServerUrl = serverUrl;
		}
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(findLocalAceStepServerExecutable().empty() || findLocalAceStepModelsDirectory().empty() || isManagedAceStepServerRunning());
	if (ImGui::SmallButton("Start local AceStep")) {
		startLocalAceStepServer();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!isManagedAceStepServerRunning());
	if (ImGui::SmallButton("Stop local AceStep")) {
		stopLocalAceStepServer(true);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::SmallButton("Copy install command##AceStep")) {
		copyToClipboard(
			"powershell -ExecutionPolicy Bypass -File .\\scripts\\install-acestep.ps1");
		aceStepStatus =
			"Copied the AceStep installer command. It installs under libs\\acestep and does not affect prompt-only music tools.";
	}
	ImGui::SameLine();
	ImGui::TextDisabled("Installer: scripts\\install-acestep.ps1");

	const std::string localAceStepExe = findLocalAceStepServerExecutable();
	const std::string localAceStepModels = findLocalAceStepModelsDirectory();
	if (!localAceStepExe.empty()) {
		ImGui::TextDisabled("Local AceStep exe: %s", ofFilePath::getFileName(localAceStepExe).c_str());
		if (ImGui::IsItemHovered()) {
			showWrappedTooltip(localAceStepExe);
		}
	} else {
		ImGui::TextDisabled("Local AceStep exe: not found");
	}
	if (!localAceStepModels.empty()) {
		ImGui::TextDisabled("Local AceStep models: %s", localAceStepModels.c_str());
	} else {
		ImGui::TextDisabled("Local AceStep models: not found");
	}
	if (!aceStepServerStatusMessage.empty()) {
		const ImVec4 statusColor =
			aceStepServerStatus == ServerStatusState::Reachable
				? ImVec4(0.55f, 0.9f, 0.6f, 1.0f)
				: ImVec4(0.95f, 0.45f, 0.4f, 1.0f);
		ImGui::TextColored(statusColor, "%s", aceStepServerStatusMessage.c_str());
	}

	ImGui::SetNextItemWidth(260);
	ImGui::InputText(
		"AceStep server",
		aceStepServerUrl,
		sizeof(aceStepServerUrl));

	ImGui::BeginDisabled(trim(imageToMusicPromptOutput).empty());
	if (ImGui::Button("Use Image->Music Prompt", ImVec2(180, 0))) {
		copyStringToBuffer(
			aceStepPrompt,
			sizeof(aceStepPrompt),
			trim(imageToMusicPromptOutput));
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(speechOutput).empty());
	if (ImGui::Button("Use Speech Transcript", ImVec2(180, 0))) {
		copyStringToBuffer(
			aceStepLyrics,
			sizeof(aceStepLyrics),
			trim(speechOutput));
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(writeInput).empty());
	if (ImGui::Button("Use Write Text", ImVec2(140, 0))) {
		copyStringToBuffer(
			aceStepLyrics,
			sizeof(aceStepLyrics),
			trim(writeInput));
		autoSaveSession();
	}
	ImGui::EndDisabled();

	ImGui::InputTextMultiline(
		"Music prompt",
		aceStepPrompt,
		sizeof(aceStepPrompt),
		ImVec2(-1, 90));
	ImGui::InputTextMultiline(
		"Lyrics",
		aceStepLyrics,
		sizeof(aceStepLyrics),
		ImVec2(-1, 80));

	ImGui::SetNextItemWidth(120);
	ImGui::InputInt("BPM", &aceStepBpm);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(140);
	ImGui::SliderInt("Duration", &aceStepDurationSeconds, 8, 180, "%d s");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(140);
	ImGui::InputInt("Seed", &aceStepSeed);

	ImGui::SetNextItemWidth(160);
	ImGui::InputText(
		"Key / scale",
		aceStepKeyscale,
		sizeof(aceStepKeyscale));
	ImGui::SameLine();
	ImGui::SetNextItemWidth(120);
	ImGui::InputText(
		"Time signature",
		aceStepTimesignature,
		sizeof(aceStepTimesignature));
	ImGui::SameLine();
	ImGui::Checkbox("WAV output", &aceStepUseWav);
	ImGui::SameLine();
	ImGui::Checkbox("Instrumental", &aceStepInstrumentalOnly);

	ImGui::InputText(
		"Output dir",
		aceStepOutputDir,
		sizeof(aceStepOutputDir));
	ImGui::InputText(
		"File prefix",
		aceStepOutputPrefix,
		sizeof(aceStepOutputPrefix));
	ImGui::InputText(
		"Audio for understand",
		aceStepAudioPath,
		sizeof(aceStepAudioPath));
	ImGui::SameLine();
	if (ImGui::Button("Browse audio...", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select audio", false);
		if (result.bSuccess) {
			copyStringToBuffer(
				aceStepAudioPath,
				sizeof(aceStepAudioPath),
				result.getPath());
			autoSaveSession();
		}
	}

	const bool canGenerateMusic =
		!generating.load() &&
		!trim(aceStepPrompt).empty();
	ImGui::BeginDisabled(!canGenerateMusic);
	if (ImGui::Button("Generate Music", ImVec2(160, 0))) {
		runAceStepMusicGeneration();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	const bool canUnderstandAudio =
		!generating.load() &&
		!trim(aceStepAudioPath).empty();
	ImGui::BeginDisabled(!canUnderstandAudio);
	if (ImGui::Button("Understand Audio", ImVec2(160, 0))) {
		runAceStepAudioUnderstanding();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	const bool hasUnderstoodMusic =
		!trim(aceStepUnderstoodCaption).empty() ||
		!trim(aceStepUnderstoodLyrics).empty();
	ImGui::BeginDisabled(!hasUnderstoodMusic);
	if (ImGui::Button("Use in Music -> Image", ImVec2(180, 0))) {
		copyStringToBuffer(
			musicToImageDescription,
			sizeof(musicToImageDescription),
			trim(aceStepUnderstoodCaption));
		copyStringToBuffer(
			musicToImageLyrics,
			sizeof(musicToImageLyrics),
			trim(aceStepUnderstoodLyrics));
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!hasUnderstoodMusic);
	if (ImGui::Button("Use in Music Video", ImVec2(180, 0))) {
		copyStringToBuffer(
			musicToImageDescription,
			sizeof(musicToImageDescription),
			trim(aceStepUnderstoodCaption));
		copyStringToBuffer(
			musicToImageLyrics,
			sizeof(musicToImageLyrics),
			trim(aceStepUnderstoodLyrics));
		applyMusicVideoWorkflowDefaults(false);
		autoSaveSession();
	}
	ImGui::EndDisabled();

	if (!aceStepStatus.empty()) {
		ImGui::TextWrapped("%s", aceStepStatus.c_str());
	}
	if (!trim(aceStepUsedServerUrl).empty()) {
		ImGui::TextDisabled("Server: %s", aceStepUsedServerUrl.c_str());
	}
	if (!aceStepGeneratedTracks.empty()) {
		aceStepSelectedTrackIndex = std::clamp(
			aceStepSelectedTrackIndex,
			0,
			std::max(0, static_cast<int>(aceStepGeneratedTracks.size()) - 1));
		ImGui::TextDisabled("Generated audio");

		std::string selectedTrackLabel =
			ofFilePath::getBaseName(
				aceStepGeneratedTracks[static_cast<size_t>(aceStepSelectedTrackIndex)].path);
		if (selectedTrackLabel.empty()) {
			selectedTrackLabel =
				"Track " + std::to_string(aceStepSelectedTrackIndex + 1);
		}
		if (ImGui::BeginCombo("Generated tracks##AceStep", selectedTrackLabel.c_str())) {
			for (int i = 0; i < static_cast<int>(aceStepGeneratedTracks.size()); ++i) {
				const auto & track = aceStepGeneratedTracks[static_cast<size_t>(i)];
				std::string label = ofFilePath::getBaseName(track.path);
				if (!trim(track.label).empty()) {
					if (!label.empty()) {
						label += " - ";
					}
					label += trim(track.label);
				}
				if (label.empty()) {
					label = "Track " + std::to_string(i + 1);
				}
				const bool selected = (aceStepSelectedTrackIndex == i);
				if (ImGui::Selectable(label.c_str(), selected)) {
					aceStepSelectedTrackIndex = i;
					autoSaveSession();
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
#if OFXGGML_HAS_OFXVLC4
		if (ImGui::Button("Preview in ofxVlc4##AceStep", ImVec2(190, 0))) {
			std::string error;
			if (!loadAceStepVlcPreview(aceStepSelectedTrackIndex, &error) && !error.empty()) {
				aceStepStatus = error;
			}
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(!aceStepVlcInitialized);
		if (ImGui::Button("Close preview##AceStep", ImVec2(150, 0))) {
			closeAceStepVlcPreview();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
#endif
		if (ImGui::Button("Use for montage export##AceStep", ImVec2(190, 0))) {
			copyStringToBuffer(
				montageRenderAudioPath,
				sizeof(montageRenderAudioPath),
				trim(aceStepGeneratedTracks[static_cast<size_t>(aceStepSelectedTrackIndex)].path));
			aceStepStatus = "Connected the selected generated track to the montage export audio path.";
			autoSaveSession();
		}
		ImGui::SameLine();
		if (ImGui::Button("Use for understand##AceStep", ImVec2(170, 0))) {
			copyStringToBuffer(
				aceStepAudioPath,
				sizeof(aceStepAudioPath),
				trim(aceStepGeneratedTracks[static_cast<size_t>(aceStepSelectedTrackIndex)].path));
			autoSaveSession();
		}
		const auto & selectedTrack =
			aceStepGeneratedTracks[static_cast<size_t>(aceStepSelectedTrackIndex)];
		if (!trim(selectedTrack.label).empty()) {
			ImGui::TextDisabled("%s", selectedTrack.label.c_str());
		}
		ImGui::TextDisabled("%s", selectedTrack.path.c_str());
#if OFXGGML_HAS_OFXVLC4
		if (aceStepVlcInitialized) {
			drawAceStepVlcPreview();
		}
#else
		ImGui::TextDisabled(
			"Regenerate this example with ofxVlc4 in addons.make to enable direct audio preview here.");
#endif
	}
	if (!trim(aceStepUnderstoodSummary).empty()) {
		ImGui::TextWrapped("%s", aceStepUnderstoodSummary.c_str());
	}

	ImGui::Text("Enriched AceStep request:");
	ImGui::BeginChild("##AceStepRequestOut", ImVec2(0, 110.0f), true);
	if (trim(aceStepGeneratedRequestJson).empty()) {
		ImGui::TextDisabled("Enriched request JSON appears here after /lm or /understand.");
	} else {
		ImGui::TextUnformatted(aceStepGeneratedRequestJson.c_str());
	}
	ImGui::EndChild();
}

void ofApp::drawMusicVideoWorkflowSection() {
	musicVideoSectionSummary.clear();
	if (!trim(videoPlanJson).empty()) {
		const auto parsedResult = ofxGgmlVideoPlanner::parsePlanJson(trim(videoPlanJson));
		if (parsedResult.isOk() && !parsedResult.value().sections.empty()) {
			std::ostringstream summary;
			summary << "Detected song sections:";
			for (const auto & section : parsedResult.value().sections) {
				summary << "\n- ";
				if (section.index > 0) {
					summary << section.index << ". ";
				}
				summary << (!trim(section.label).empty() ? trim(section.label) : std::string("section"));
				if (!trim(section.role).empty()) {
					summary << " (" << trim(section.role) << ")";
				}
				if (!trim(section.cutDensity).empty()) {
					summary << " | cuts: " << trim(section.cutDensity);
				}
				if (!trim(section.visualFocus).empty()) {
					summary << " | focus: " << trim(section.visualFocus);
				}
			}
			musicVideoSectionSummary = summary.str();
		}
	}

	ImGui::Separator();
	ImGui::Text("Music Video");
	ImGui::TextWrapped(
		"Use the song description or lyrics to build a visual concept, then hand that concept into the existing "
		"video planner, diffusion prompt, and music-video edit workflow.");

	ImGui::BeginDisabled(trim(speechOutput).empty());
	if (ImGui::Button("Use Speech Transcript##MusicVideo", ImVec2(170, 0))) {
		copyStringToBuffer(musicToImageLyrics, sizeof(musicToImageLyrics), trim(speechOutput));
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(writeInput).empty());
	if (ImGui::Button("Use Write Text##MusicVideo", ImVec2(150, 0))) {
		copyStringToBuffer(musicToImageDescription, sizeof(musicToImageDescription), trim(writeInput));
		autoSaveSession();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(visionPrompt).empty());
	if (ImGui::Button("Use Vision Prompt##MusicVideo", ImVec2(160, 0))) {
		copyStringToBuffer(musicToImageDescription, sizeof(musicToImageDescription), trim(visionPrompt));
		autoSaveSession();
	}
	ImGui::EndDisabled();

	ImGui::InputTextMultiline(
		"Song / mood",
		musicToImageDescription,
		sizeof(musicToImageDescription),
		ImVec2(-1, 70));
	ImGui::InputTextMultiline(
		"Lyrics / transcript",
		musicToImageLyrics,
		sizeof(musicToImageLyrics),
		ImVec2(-1, 70));
	ImGui::InputText(
		"Visual concept style",
		musicToImageStyle,
		sizeof(musicToImageStyle));
	ImGui::Checkbox("Include lyrics in visual concept", &musicToImageIncludeLyrics);
	static const char * musicVideoStructureLabels[] = {
		"Intro / Verse / Chorus / Bridge / Outro",
		"Verse / Chorus loop",
		"Slow build to drop",
		"Three-act escalation",
		"Performance cut"
	};
	ImGui::SetNextItemWidth(260);
	ImGui::Combo(
		"Song structure",
		&musicVideoStructureIndex,
		musicVideoStructureLabels,
		IM_ARRAYSIZE(musicVideoStructureLabels));
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Section count", &musicVideoSectionCount, 2, 8);
	ImGui::SetNextItemWidth(180);
	ImGui::SliderFloat("Cut intensity", &musicVideoCutIntensity, 0.0f, 1.0f, "%.2f");

	const bool canGenerateConcept =
		!generating.load() &&
		(!trim(musicToImageDescription).empty() || !trim(musicToImageLyrics).empty());
	ImGui::BeginDisabled(!canGenerateConcept);
	if (ImGui::Button("Generate Visual Concept##MusicVideo", ImVec2(200, 0))) {
		runMusicToImagePromptGeneration();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Apply Music Video Defaults", ImVec2(200, 0))) {
		applyMusicVideoWorkflowDefaults(false);
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(musicToImagePromptOutput).empty());
	if (ImGui::Button("Use in Diffusion##MusicVideo", ImVec2(170, 0))) {
		copyStringToBuffer(diffusionPrompt, sizeof(diffusionPrompt), musicToImagePromptOutput);
		activeMode = AiMode::Diffusion;
		autoSaveSession();
	}
	ImGui::EndDisabled();

	const bool hasVisualSource =
		!trim(musicToImagePromptOutput).empty() ||
		!trim(musicToImageDescription).empty() ||
		!trim(musicToImageLyrics).empty();
	ImGui::BeginDisabled(!hasVisualSource);
	if (ImGui::Button("Use in Video Prompt##MusicVideo", ImVec2(180, 0))) {
		applyMusicVideoWorkflowDefaults(true);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!hasVisualSource || generating.load());
	if (ImGui::Button("Plan Music Video##MusicVideo", ImVec2(180, 0))) {
		applyMusicVideoWorkflowDefaults(true);
		runVideoPlanning();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	const bool canPlanEdit =
		!generating.load() &&
		std::strlen(visionVideoPath) > 0 &&
		(!videoEditUseCurrentAnalysis || !trim(visionOutput).empty());
	ImGui::BeginDisabled(!canPlanEdit);
	if (ImGui::Button("Plan Music Video Edit##MusicVideo", ImVec2(210, 0))) {
		applyMusicVideoWorkflowDefaults(false);
		runVideoEditPlanning();
	}
	ImGui::EndDisabled();

	if (!musicToImageStatus.empty()) {
		ImGui::TextWrapped("%s", musicToImageStatus.c_str());
	}
	ImGui::TextDisabled(
		"Workflow defaults: %d beats | %d scenes | %d sections | %.1f s | preset: %s",
		videoPlanBeatCount,
		videoPlanSceneCount,
		musicVideoSectionCount,
		videoPlanDurationSeconds,
		kVideoEditPresets[3].name);
	if (!musicVideoSectionSummary.empty()) {
		ImGui::TextWrapped("%s", musicVideoSectionSummary.c_str());
	}

	ImGui::BeginChild("##MusicVideoConceptOut", ImVec2(0, 90.0f), true);
	if (trim(musicToImagePromptOutput).empty()) {
		ImGui::TextDisabled("Generated music-video visual concept appears here.");
	} else {
		ImGui::TextUnformatted(musicToImagePromptOutput.c_str());
	}
	ImGui::EndChild();
}

void ofApp::runImageToMusicPromptGeneration() {
	if (generating.load()) {
		return;
	}

	const std::string modelPath = getSelectedModelPath();
	if (modelPath.empty()) {
		imageToMusicStatus = "[Error] Select a text model before generating a music prompt.";
		return;
	}

	if (trim(imageToMusicDescription).empty() && trim(imageToMusicSceneNotes).empty()) {
		imageToMusicStatus = "[Error] Enter a visual description or scene notes first.";
		return;
	}

	mediaPromptGenerator.setCompletionExecutable(llmInference.getCompletionExecutable());
	ofxGgmlInferenceSettings settings = buildCurrentTextInferenceSettings(AiMode::MilkDrop);
	settings.maxTokens = std::clamp(settings.maxTokens, 96, 384);
	settings.temperature = std::clamp(settings.temperature, 0.2f, 0.9f);
	settings.stopAtNaturalBoundary = true;

	ofxGgmlImageToMusicRequest request;
	request.imageDescription = trim(imageToMusicDescription);
	request.sceneNotes = trim(imageToMusicSceneNotes);
	request.musicalStyle = trim(imageToMusicStyle);
	request.instrumentation = trim(imageToMusicInstrumentation);
	request.targetDurationSeconds = imageToMusicDurationSeconds;
	request.instrumentalOnly = imageToMusicInstrumentalOnly;

	cancelRequested.store(false);
	generating.store(true);
	activeGenerationMode = AiMode::Vision;
	generationStartTime = ofGetElapsedTimef();
	imageToMusicStatus = "Generating image-inspired music prompt...";
	if (workerThread.joinable()) {
		workerThread.join();
	}

	workerThread = std::thread([this, modelPath, request, settings]() {
		std::string promptText;
		std::string statusText;
		try {
			const ofxGgmlImageToMusicResult result =
				mediaPromptGenerator.generateImageToMusicPrompt(
					modelPath,
					request,
					settings,
					nullptr);
			if (cancelRequested.load()) {
				statusText = "[Cancelled] Music prompt generation cancelled.";
			} else if (result.success) {
				promptText = result.musicPrompt;
				statusText = "Generated image-inspired music prompt.";
			} else {
				statusText = "[Error] " + result.error;
			}
		} catch (const std::exception & e) {
			statusText = std::string("[Error] Music prompt generation failed: ") + e.what();
		} catch (...) {
			statusText = "[Error] Unknown failure during music prompt generation.";
		}

		{
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingImageToMusicPromptOutput = promptText;
			pendingImageToMusicStatus = statusText;
			pendingImageToMusicDirty = true;
		}
		generating.store(false);
	});
}

void ofApp::runImageToMusicNotationGeneration() {
	if (generating.load()) {
		return;
	}

	const std::string modelPath = getSelectedModelPath();
	if (modelPath.empty()) {
		imageToMusicStatus = "[Error] Select a text model before generating ABC notation.";
		return;
	}

	const std::string sourceConcept = !trim(imageToMusicPromptOutput).empty()
		? trim(imageToMusicPromptOutput)
		: trim(imageToMusicDescription);
	if (sourceConcept.empty()) {
		imageToMusicStatus = "[Error] Generate a music prompt first or enter a visual description.";
		return;
	}

	musicGenerator.setCompletionExecutable(llmInference.getCompletionExecutable());
	ofxGgmlInferenceSettings settings = buildCurrentTextInferenceSettings(AiMode::MilkDrop);
	settings.maxTokens = std::clamp(settings.maxTokens, 128, 512);
	settings.temperature = std::clamp(settings.temperature, 0.15f, 0.75f);
	settings.stopAtNaturalBoundary = false;

	ofxGgmlMusicNotationRequest request;
	request.sourceConcept = sourceConcept;
	request.title = trim(imageToMusicAbcTitle).empty()
		? std::string("Generated Theme")
		: trim(imageToMusicAbcTitle);
	request.style = trim(imageToMusicStyle).empty()
		? std::string("cinematic instrumental soundtrack")
		: trim(imageToMusicStyle);
	request.key = trim(imageToMusicAbcKey).empty()
		? std::string("Cm")
		: trim(imageToMusicAbcKey);
	request.bars = imageToMusicAbcBars;
	request.instrumentalOnly = imageToMusicInstrumentalOnly;

	cancelRequested.store(false);
	generating.store(true);
	activeGenerationMode = AiMode::Vision;
	generationStartTime = ofGetElapsedTimef();
	imageToMusicStatus = "Generating ABC music sketch...";
	if (workerThread.joinable()) {
		workerThread.join();
	}

	workerThread = std::thread([this, modelPath, request, settings]() {
		std::string notationText;
		std::string statusText;
		try {
			const ofxGgmlMusicNotationResult result =
				musicGenerator.generateAbcNotation(
					modelPath,
					request,
					settings,
					nullptr);
			if (cancelRequested.load()) {
				statusText = "[Cancelled] ABC notation generation cancelled.";
			} else if (result.success) {
				notationText = result.abcNotation;
				statusText = "Generated ABC music sketch.";
			} else {
				statusText = "[Error] " + result.error;
			}
		} catch (const std::exception & e) {
			statusText = std::string("[Error] ABC notation generation failed: ") + e.what();
		} catch (...) {
			statusText = "[Error] Unknown failure during ABC notation generation.";
		}

		{
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingImageToMusicNotationOutput = notationText;
			pendingImageToMusicStatus = statusText;
			pendingImageToMusicDirty = true;
		}
		generating.store(false);
	});
}

void ofApp::runAceStepMusicGeneration() {
	if (generating.load()) {
		return;
	}

	const std::string prompt = trim(aceStepPrompt);
	if (prompt.empty()) {
		aceStepStatus = "[Error] Enter a music prompt first.";
		return;
	}
	if (!ensureAceStepServerReady(true, true)) {
		aceStepStatus = "[Error] " + aceStepServerStatusMessage;
		return;
	}

	cancelRequested.store(false);
	generating.store(true);
	activeGenerationMode = AiMode::Vision;
	generationStartTime = ofGetElapsedTimef();
	aceStepStatus = "Generating music with AceStep...";
	if (workerThread.joinable()) {
		workerThread.join();
	}

	ofxGgmlAceStepRequest request;
	request.caption = prompt;
	request.lyrics = trim(aceStepLyrics);
	request.bpm = std::max(0, aceStepBpm);
	request.durationSeconds = static_cast<float>(
		std::clamp(aceStepDurationSeconds, 8, 180));
	request.keyscale = trim(aceStepKeyscale);
	request.timesignature = trim(aceStepTimesignature);
	request.seed = aceStepSeed;
	request.instrumentalOnly = aceStepInstrumentalOnly;
	request.wavOutput = aceStepUseWav;
	request.outputDir = trim(aceStepOutputDir);
	request.outputPrefix = trim(aceStepOutputPrefix);
	const std::string serverUrl = effectiveAceStepServerUrl(aceStepServerUrl);

	workerThread = std::thread([this, request, serverUrl]() {
		std::string statusText;
		std::string enrichedRequestJson;
		std::string usedServerUrl;
		std::vector<ofxGgmlGeneratedMusicTrack> tracks;
		try {
			const ofxGgmlAceStepGenerateResult result =
				aceStepBridge.generate(request, serverUrl);
			usedServerUrl = result.usedServerUrl;
			enrichedRequestJson = result.enrichedRequestsJson;
			tracks = result.tracks;
			if (cancelRequested.load()) {
				statusText = "[Cancelled] AceStep music generation cancelled.";
			} else if (result.success) {
				statusText = "Generated " +
					ofToString(static_cast<int>(result.tracks.size())) +
					" AceStep track" +
					(result.tracks.size() == 1 ? "" : "s") + ".";
			} else {
				statusText = "[Error] " + result.error;
			}
		} catch (const std::exception & e) {
			statusText = std::string("[Error] AceStep generation failed: ") + e.what();
		} catch (...) {
			statusText = "[Error] Unknown failure during AceStep generation.";
		}

		{
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingAceStepStatus = statusText;
			pendingAceStepGeneratedRequestJson = enrichedRequestJson;
			pendingAceStepUnderstoodSummary.clear();
			pendingAceStepUnderstoodCaption.clear();
			pendingAceStepUnderstoodLyrics.clear();
			pendingAceStepUsedServerUrl = usedServerUrl;
			pendingAceStepGeneratedTracks = tracks;
			pendingAceStepDirty = true;
		}
		generating.store(false);
	});
}

void ofApp::runAceStepAudioUnderstanding() {
	if (generating.load()) {
		return;
	}

	const std::string audioPath = trim(aceStepAudioPath);
	if (audioPath.empty()) {
		aceStepStatus = "[Error] Select an audio file first.";
		return;
	}
	if (!ensureAceStepServerReady(true, true)) {
		aceStepStatus = "[Error] " + aceStepServerStatusMessage;
		return;
	}

	cancelRequested.store(false);
	generating.store(true);
	activeGenerationMode = AiMode::Vision;
	generationStartTime = ofGetElapsedTimef();
	aceStepStatus = "Understanding audio with AceStep...";
	if (workerThread.joinable()) {
		workerThread.join();
	}

	ofxGgmlAceStepUnderstandRequest request;
	request.audioPath = audioPath;
	request.requestTemplate.caption = trim(aceStepPrompt);
	request.requestTemplate.lyrics = trim(aceStepLyrics);
	request.requestTemplate.bpm = std::max(0, aceStepBpm);
	request.requestTemplate.durationSeconds = static_cast<float>(
		std::clamp(aceStepDurationSeconds, 8, 180));
	request.requestTemplate.keyscale = trim(aceStepKeyscale);
	request.requestTemplate.timesignature = trim(aceStepTimesignature);
	request.requestTemplate.instrumentalOnly = aceStepInstrumentalOnly;
	request.includeRequestTemplate =
		!trim(request.requestTemplate.caption).empty() ||
		!trim(request.requestTemplate.lyrics).empty() ||
		request.requestTemplate.bpm > 0 ||
		!trim(request.requestTemplate.keyscale).empty();
	const std::string serverUrl = effectiveAceStepServerUrl(aceStepServerUrl);

	workerThread = std::thread([this, request, serverUrl]() {
		std::string statusText;
		std::string summaryText;
		std::string captionText;
		std::string lyricsText;
		std::string responseJson;
		std::string usedServerUrl;
		try {
			const ofxGgmlAceStepUnderstandResult result =
				aceStepBridge.understandAudio(request, serverUrl);
			usedServerUrl = result.usedServerUrl;
			responseJson = result.rawJson;
			summaryText = result.summary;
			captionText = result.caption;
			lyricsText = result.lyrics;
			if (cancelRequested.load()) {
				statusText = "[Cancelled] AceStep audio understanding cancelled.";
			} else if (result.success) {
				statusText = "Understood audio with AceStep.";
			} else {
				statusText = "[Error] " + result.error;
			}
		} catch (const std::exception & e) {
			statusText = std::string("[Error] AceStep audio understanding failed: ") + e.what();
		} catch (...) {
			statusText = "[Error] Unknown failure during AceStep audio understanding.";
		}

		{
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingAceStepStatus = statusText;
			pendingAceStepGeneratedRequestJson = responseJson;
			pendingAceStepUnderstoodSummary = summaryText;
			pendingAceStepUnderstoodCaption = captionText;
			pendingAceStepUnderstoodLyrics = lyricsText;
			pendingAceStepUsedServerUrl = usedServerUrl;
			pendingAceStepGeneratedTracks.clear();
			pendingAceStepDirty = true;
		}
		generating.store(false);
	});
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
				streamingOutput = isSmolVlm2VisionProfile(profile)
					? "Contacting llama-server multimodal completion endpoint..."
					: "Contacting " + ofxGgmlVisionInference::normalizeServerUrl(effectiveServerUrl);
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
	const bool musicVideoMode =
		videoEditPresetIndex == 3 &&
		(!trim(musicToImageDescription).empty() ||
		 !trim(musicToImageLyrics).empty() ||
		 !trim(musicToImagePromptOutput).empty());
	const int sectionCount = std::clamp(musicVideoSectionCount, 2, 8);
	const float cutIntensity = std::clamp(musicVideoCutIntensity, 0.0f, 1.0f);
	const int structureIndex = std::clamp(musicVideoStructureIndex, 0, 4);
	static const char * musicVideoStructureHints[] = {
		"intro -> verse -> chorus -> bridge -> outro",
		"verse -> chorus -> verse -> chorus",
		"slow atmospheric build -> energetic drop -> release",
		"setup -> escalation -> payoff",
		"performance coverage with crowd and detail cutaways"
	};
	const std::string sectionStructureHint =
		musicVideoMode
			? musicVideoStructureHints[structureIndex]
			: std::string();

	workerThread = std::thread([this, modelPath, inferenceSettings, sourcePrompt, beatCount, sceneCount, durationSeconds, preferredStyle, negativePrompt, multiScene, musicVideoMode, sectionCount, cutIntensity, sectionStructureHint]() {
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
			request.musicVideoMode = musicVideoMode;
			request.sectionCount = sectionCount;
			request.durationSeconds = durationSeconds;
			request.preferredStyle = preferredStyle;
			request.negativePrompt = negativePrompt;
			request.sectionStructureHint = sectionStructureHint;
			request.cutIntensity = cutIntensity;

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
	const double targetDurationSeconds =
		std::clamp(static_cast<double>(montageTargetDurationSeconds), 1.0, 120.0);
	const double minSpacingSeconds =
		std::clamp(static_cast<double>(montageMinSpacingSeconds), 0.0, 15.0);
	const double preRollSeconds =
		std::clamp(static_cast<double>(montagePreRollSeconds), 0.0, 5.0);
	const double postRollSeconds =
		std::clamp(static_cast<double>(montagePostRollSeconds), 0.0, 5.0);
	const bool preserveChronology = montagePreserveChronology;

	workerThread = std::thread([this, srtPath, goal, reelName, edlTitle, maxClips, fps, minScore, targetDurationSeconds, minSpacingSeconds, preRollSeconds, postRollSeconds, preserveChronology]() {
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
				request.targetDurationSeconds = targetDurationSeconds;
				request.minSpacingSeconds = minSpacingSeconds;
				request.preRollSeconds = preRollSeconds;
				request.postRollSeconds = postRollSeconds;
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
