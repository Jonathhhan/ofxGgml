#include "ofApp.h"

#include "ImHelpers.h"
#include "utils/ImGuiHelpers.h"
#include "utils/TextPromptHelpers.h"
#include "utils/ModelHelpers.h"
#include "utils/SpeechHelpers.h"
#include "utils/AudioHelpers.h"
#include "utils/ServerHelpers.h"
#include "utils/VisionHelpers.h"
#include "utils/ProcessHelpers.h"
#include "utils/ConsoleHelpers.h"
#include "utils/PathHelpers.h"
#include "utils/BackendHelpers.h"
#include "utils/ScriptCommandHelpers.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>

namespace {
constexpr float kDotsAnimationSpeed = 3.0f;
const char * const kWaitingLabels[] = {"generating", "generating.", "generating..", "generating..."};
constexpr int kDiffusionImageSizes[] = {128, 256, 384, 512, 640, 768, 896, 1024};
const char * const kDiffusionImageSizeLabels[] = {"128", "256", "384", "512", "640", "768", "896", "1024"};
}

void ofApp::drawDiffusionPanel() {
	drawPanelHeader("Diffusion", "local image generation workflow via ofxStableDiffusion");
	const float compactModeFieldWidth = std::min(320.0f, ImGui::GetContentRegionAvail().x);
	ensureDiffusionPreviewResources();
	const bool diffusionRuntimeAttached = ensureDiffusionBackendConfigured();

	ImGui::TextWrapped(
		"This panel is wired for local image generation through ofxStableDiffusion. "
		"It keeps prompt state, image handoff, CLIP reranking, and result reuse inside the AI Studio example.");

	const auto applyDiffusionProfileDefaults =
		[this](const ofxGgmlImageGenerationModelProfile & profile, bool onlyWhenEmpty) {
			const std::string suggestedPath =
				suggestedModelPath(profile.modelPath, profile.modelFileHint);
			if (!suggestedPath.empty() &&
				(!onlyWhenEmpty || trim(diffusionModelPath).empty())) {
				copyStringToBuffer(
					diffusionModelPath,
					sizeof(diffusionModelPath),
					suggestedPath);
			}
			if (!trim(profile.vaePath).empty() &&
				(!onlyWhenEmpty || trim(diffusionVaePath).empty())) {
				copyStringToBuffer(
					diffusionVaePath,
					sizeof(diffusionVaePath),
					profile.vaePath);
			}
			if (trim(diffusionOutputDir).empty()) {
				copyStringToBuffer(
					diffusionOutputDir,
					sizeof(diffusionOutputDir),
					ofToDataPath("generated", true));
			}
		};

	bool loadedDiffusionProfiles = false;
	if (diffusionProfiles.empty()) {
		diffusionProfiles = ofxGgmlDiffusionInference::defaultProfiles();
		loadedDiffusionProfiles = !diffusionProfiles.empty();
	}
	selectedDiffusionProfileIndex = std::clamp(
		selectedDiffusionProfileIndex,
		0,
		std::max(0, static_cast<int>(diffusionProfiles.size()) - 1));
	if (loadedDiffusionProfiles && !diffusionProfiles.empty()) {
		applyDiffusionProfileDefaults(
			diffusionProfiles[static_cast<size_t>(selectedDiffusionProfileIndex)],
			true);
	}

	const ofxGgmlImageGenerationModelProfile activeProfile =
		diffusionProfiles.empty()
			? ofxGgmlImageGenerationModelProfile{}
			: diffusionProfiles[static_cast<size_t>(selectedDiffusionProfileIndex)];
	const auto profileSupportsTask =
		[](const ofxGgmlImageGenerationModelProfile & profile,
			ofxGgmlImageGenerationTask task) {
			switch (task) {
			case ofxGgmlImageGenerationTask::ImageToImage:
				return profile.supportsImageToImage;
			case ofxGgmlImageGenerationTask::InstructImage:
				return profile.supportsInstructImage;
			case ofxGgmlImageGenerationTask::Variation:
				return profile.supportsVariation;
			case ofxGgmlImageGenerationTask::Restyle:
				return profile.supportsRestyle;
			case ofxGgmlImageGenerationTask::Inpaint:
				return profile.supportsInpaint;
			case ofxGgmlImageGenerationTask::Upscale:
				return profile.supportsUpscale;
			case ofxGgmlImageGenerationTask::TextToImage:
			default:
				return true;
			}
		};
	const auto activeTask =
		static_cast<ofxGgmlImageGenerationTask>(std::clamp(diffusionTaskIndex, 0, 6));
	const bool needsInitImage =
		activeTask == ofxGgmlImageGenerationTask::ImageToImage ||
		activeTask == ofxGgmlImageGenerationTask::InstructImage ||
		activeTask == ofxGgmlImageGenerationTask::Variation ||
		activeTask == ofxGgmlImageGenerationTask::Restyle ||
		activeTask == ofxGgmlImageGenerationTask::Inpaint ||
		activeTask == ofxGgmlImageGenerationTask::Upscale;
	const bool needsMaskImage = activeTask == ofxGgmlImageGenerationTask::Inpaint;

	const auto configuredBridge =
		std::dynamic_pointer_cast<ofxGgmlStableDiffusionBridgeBackend>(
			diffusionInference.getBackend());
	const bool bridgeConfigured =
		!configuredBridge || configuredBridge->isConfigured();
	const std::string diffusionBackendLabel =
		diffusionInference.getBackend()
			? diffusionInference.getBackend()->backendName()
			: std::string("(none)");
	diffusionWidth = clampSupportedDiffusionImageSize(diffusionWidth);
	diffusionHeight = clampSupportedDiffusionImageSize(diffusionHeight);

	ImGui::TextDisabled("Backend: %s", diffusionBackendLabel.c_str());
#if OFXGGML_HAS_OFXSTABLEDIFFUSION
	ImGui::TextDisabled(
		"ofxStableDiffusion integration: %s",
		diffusionRuntimeAttached ? "available in this build" : "failed to attach");
#else
	ImGui::TextDisabled(
		"ofxStableDiffusion integration: add ofxStableDiffusion to the generated project to enable local generation");
#endif
	if (!bridgeConfigured || !diffusionRuntimeAttached) {
		ImGui::TextColored(
			ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
			"Local diffusion runtime is not active yet.");
	}

	if (!diffusionProfiles.empty()) {
		std::vector<const char *> profileNames;
		profileNames.reserve(diffusionProfiles.size());
		for (const auto & profile : diffusionProfiles) {
			profileNames.push_back(profile.name.c_str());
		}
		ImGui::SetNextItemWidth(280);
		if (ImGui::Combo(
			"Diffusion profile",
			&selectedDiffusionProfileIndex,
			profileNames.data(),
			static_cast<int>(profileNames.size()))) {
			const auto & profile =
				diffusionProfiles[static_cast<size_t>(selectedDiffusionProfileIndex)];
			applyDiffusionProfileDefaults(profile, false);
			const auto selectedTask =
				static_cast<ofxGgmlImageGenerationTask>(
					std::clamp(diffusionTaskIndex, 0, 6));
			if (!profileSupportsTask(profile, selectedTask)) {
				diffusionTaskIndex = static_cast<int>(ofxGgmlImageGenerationTask::TextToImage);
			}
		}

		const std::string recommendedModelPath =
			suggestedModelPath(activeProfile.modelPath, activeProfile.modelFileHint);
		const std::string recommendedDownloadUrl =
			suggestedModelDownloadUrl(activeProfile.modelRepoHint, activeProfile.modelFileHint);
		ImGui::TextDisabled("Architecture: %s", activeProfile.architecture.c_str());
		if (!activeProfile.modelRepoHint.empty()) {
			ImGui::TextDisabled("Recommended repo: %s", activeProfile.modelRepoHint.c_str());
		}
		if (!activeProfile.modelFileHint.empty()) {
			ImGui::TextDisabled("Recommended file: %s", activeProfile.modelFileHint.c_str());
		}
		if (!recommendedModelPath.empty()) {
			ImGui::TextDisabled("Recommended local path: %s", recommendedModelPath.c_str());
			ImGui::TextDisabled(
				pathExists(recommendedModelPath)
					? "Recommended model is already present."
					: "Recommended model is not downloaded yet.");
			ImGui::BeginDisabled(trim(diffusionModelPath) == recommendedModelPath);
			if (ImGui::SmallButton("Use recommended path##Diffusion")) {
				copyStringToBuffer(
					diffusionModelPath,
					sizeof(diffusionModelPath),
					recommendedModelPath);
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered()) {
				showWrappedTooltip("Sets the diffusion model path to the profile's recommended file under bin/data/models/.");
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(recommendedDownloadUrl.empty());
			if (ImGui::SmallButton("Download model##Diffusion")) {
				ofLaunchBrowser(recommendedDownloadUrl);
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered()) {
				showWrappedTooltip("Opens the recommended diffusion model in your browser.");
			}
		}
		ImGui::TextDisabled(
			"Img2Img: %s | Instruct: %s | Variation: %s | Restyle: %s | Inpaint: %s | Upscale: %s",
			activeProfile.supportsImageToImage ? "supported" : "not supported",
			activeProfile.supportsInstructImage ? "supported" : "not supported",
			activeProfile.supportsVariation ? "supported" : "not supported",
			activeProfile.supportsRestyle ? "supported" : "not supported",
			activeProfile.supportsInpaint ? "supported" : "not supported",
			activeProfile.supportsUpscale ? "supported" : "not supported");
	}

	if (ImGui::Button("Text to Image", ImVec2(120, 0))) {
		diffusionTaskIndex = static_cast<int>(ofxGgmlImageGenerationTask::TextToImage);
	}
	ImGui::SameLine();
	if (ImGui::Button("Image to Image", ImVec2(120, 0))) {
		diffusionTaskIndex = static_cast<int>(ofxGgmlImageGenerationTask::ImageToImage);
	}
	ImGui::SameLine();
	if (ImGui::Button("Instruct", ImVec2(100, 0))) {
		diffusionTaskIndex = static_cast<int>(ofxGgmlImageGenerationTask::InstructImage);
	}
	ImGui::SameLine();
	if (ImGui::Button("Variation", ImVec2(100, 0))) {
		diffusionTaskIndex = static_cast<int>(ofxGgmlImageGenerationTask::Variation);
	}
	if (ImGui::Button("Restyle", ImVec2(100, 0))) {
		diffusionTaskIndex = static_cast<int>(ofxGgmlImageGenerationTask::Restyle);
	}
	ImGui::SameLine();
	if (ImGui::Button("Inpaint", ImVec2(100, 0))) {
		diffusionTaskIndex = static_cast<int>(ofxGgmlImageGenerationTask::Inpaint);
	}
	ImGui::SameLine();
	if (ImGui::Button("Upscale", ImVec2(100, 0))) {
		diffusionTaskIndex = static_cast<int>(ofxGgmlImageGenerationTask::Upscale);
	}

	static const char * diffusionTaskLabels[] = {
		"Text to Image",
		"Image to Image",
		"Instruct Image",
		"Variation",
		"Restyle",
		"Inpaint",
		"Upscale"
	};
	ImGui::SetNextItemWidth(200);
	ImGui::Combo("Diffusion task", &diffusionTaskIndex, diffusionTaskLabels, 7);

	static const char * diffusionSelectionLabels[] = {
		"Keep Order",
		"Rerank",
		"Best Only"
	};
	diffusionSelectionModeIndex = std::clamp(diffusionSelectionModeIndex, 0, 2);
	ImGui::SetNextItemWidth(200);
	ImGui::Combo(
		"Selection mode",
		&diffusionSelectionModeIndex,
		diffusionSelectionLabels,
		3);
	const bool useClipSelection = diffusionSelectionModeIndex > 0;

	ImGui::InputTextMultiline(
		"Prompt",
		diffusionPrompt,
		sizeof(diffusionPrompt),
		ImVec2(-1, 100));
	if (activeTask == ofxGgmlImageGenerationTask::InstructImage) {
		ImGui::InputTextMultiline(
			"Instruction",
			diffusionInstruction,
			sizeof(diffusionInstruction),
			ImVec2(-1, 80));
	}
	ImGui::InputTextMultiline(
		"Negative prompt",
		diffusionNegativePrompt,
		sizeof(diffusionNegativePrompt),
		ImVec2(-1, 70));
	if (useClipSelection) {
		ImGui::InputTextMultiline(
			"Ranking prompt",
			diffusionRankingPrompt,
			sizeof(diffusionRankingPrompt),
			ImVec2(-1, 70));
		ImGui::Checkbox(
			"Normalize CLIP embeddings",
			&diffusionNormalizeClipEmbeddings);
		ImGui::TextDisabled(
			"Rerank and Best Only need a configured adapter runtime with CLIP attached.");
	}

	const std::string reusableImagePath = getPreferredDiffusionReuseImagePath();
	const bool canReuseVisionImage = !trim(visionImagePath).empty();
	const bool canReuseGeneratedImage = !reusableImagePath.empty();
	ImGui::BeginDisabled(!canReuseVisionImage);
	if (ImGui::Button("Use Vision Image", ImVec2(140, 0))) {
		setDiffusionInitImagePath(trim(visionImagePath), true);
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip("Copies the Vision panel image into Init image and switches Text to Image into Image to Image mode when needed.");
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!canReuseGeneratedImage);
	if (ImGui::Button("Use Last Output", ImVec2(140, 0))) {
		setDiffusionInitImagePath(reusableImagePath, true);
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip("Reuses the selected or latest generated diffusion output as the next init image.");
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(std::strlen(writeInput) == 0);
	if (ImGui::Button("Use Write Prompt", ImVec2(140, 0))) {
		copyStringToBuffer(diffusionPrompt, sizeof(diffusionPrompt), writeInput);
		autoSaveSession();
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip("Copies the current Write panel text into the diffusion prompt.");
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Model path", diffusionModelPath, sizeof(diffusionModelPath));
	ImGui::SameLine();
	if (ImGui::Button("Browse model...##Diffusion", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select diffusion model", false);
		if (result.bSuccess) {
			copyStringToBuffer(diffusionModelPath, sizeof(diffusionModelPath), result.getPath());
		}
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("VAE path", diffusionVaePath, sizeof(diffusionVaePath));
	ImGui::SameLine();
	if (ImGui::Button("Browse VAE...##Diffusion", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select VAE file", false);
		if (result.bSuccess) {
			copyStringToBuffer(diffusionVaePath, sizeof(diffusionVaePath), result.getPath());
		}
	}

	if (needsInitImage) {
		ImGui::SetNextItemWidth(compactModeFieldWidth);
		ImGui::InputText("Init image", diffusionInitImagePath, sizeof(diffusionInitImagePath));
		ImGui::SameLine();
		if (ImGui::Button("Browse init...##Diffusion", ImVec2(110, 0))) {
			ofFileDialogResult result = ofSystemLoadDialog("Select init image", false);
			if (result.bSuccess) {
				copyStringToBuffer(
					diffusionInitImagePath,
					sizeof(diffusionInitImagePath),
					result.getPath());
			}
		}
		drawDiffusionImagePreview(
			"Init preview",
			trim(diffusionInitImagePath),
			diffusionInitPreviewImage,
			diffusionInitPreviewError,
			"##DiffusionInitPreview");
	}

	if (needsMaskImage) {
		ImGui::SetNextItemWidth(compactModeFieldWidth);
		ImGui::InputText("Mask image", diffusionMaskImagePath, sizeof(diffusionMaskImagePath));
		ImGui::SameLine();
		if (ImGui::Button("Browse mask...##Diffusion", ImVec2(110, 0))) {
			ofFileDialogResult result = ofSystemLoadDialog("Select mask image", false);
			if (result.bSuccess) {
				copyStringToBuffer(
					diffusionMaskImagePath,
					sizeof(diffusionMaskImagePath),
					result.getPath());
			}
		}
		drawDiffusionImagePreview(
			"Mask preview",
			trim(diffusionMaskImagePath),
			diffusionMaskPreviewImage,
			diffusionMaskPreviewError,
			"##DiffusionMaskPreview");
	}

	drawImageSearchPanel("Use Diffusion prompt", trim(diffusionPrompt));

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Output dir", diffusionOutputDir, sizeof(diffusionOutputDir));
	ImGui::SameLine();
	if (ImGui::Button("Use data/generated", ImVec2(140, 0))) {
		copyStringToBuffer(
			diffusionOutputDir,
			sizeof(diffusionOutputDir),
			ofToDataPath("generated", true));
	}

	ImGui::SetNextItemWidth(200);
	ImGui::InputText("Output prefix", diffusionOutputPrefix, sizeof(diffusionOutputPrefix));
	ImGui::SetNextItemWidth(180);
	ImGui::InputText("Sampler", diffusionSampler, sizeof(diffusionSampler));

	const auto diffusionImageSizeIndexForValue = [](int value) {
		for (int i = 0; i < static_cast<int>(std::size(kDiffusionImageSizes)); ++i) {
			if (kDiffusionImageSizes[i] == value) {
				return i;
			}
		}
		return 0;
	};
	int diffusionWidthIndex = diffusionImageSizeIndexForValue(diffusionWidth);
	ImGui::SetNextItemWidth(180);
	if (ImGui::Combo(
		"Width",
		&diffusionWidthIndex,
		kDiffusionImageSizeLabels,
		static_cast<int>(std::size(kDiffusionImageSizeLabels)))) {
		diffusionWidth = kDiffusionImageSizes[diffusionWidthIndex];
	}
	int diffusionHeightIndex = diffusionImageSizeIndexForValue(diffusionHeight);
	ImGui::SetNextItemWidth(180);
	if (ImGui::Combo(
		"Height",
		&diffusionHeightIndex,
		kDiffusionImageSizeLabels,
		static_cast<int>(std::size(kDiffusionImageSizeLabels)))) {
		diffusionHeight = kDiffusionImageSizes[diffusionHeightIndex];
	}
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Steps", &diffusionSteps, 1, 80);
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Batch count", &diffusionBatchCount, 1, 8);
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Seed", &diffusionSeed, -1, 999999);
	ImGui::SetNextItemWidth(220);
	ImGui::SliderFloat("CFG scale", &diffusionCfgScale, 0.0f, 20.0f, "%.2f");
	if (needsInitImage) {
		ImGui::SetNextItemWidth(220);
		ImGui::SliderFloat("Strength", &diffusionStrength, 0.0f, 1.0f, "%.2f");
	}
	ImGui::Checkbox("Save metadata", &diffusionSaveMetadata);

	const bool hasPromptText = std::strlen(diffusionPrompt) > 0;
	const bool hasInstructionText = std::strlen(diffusionInstruction) > 0;
	const bool hasClipModel = std::strlen(clipModelPath) > 0;
	const bool taskNeedsText =
		activeTask != ofxGgmlImageGenerationTask::Upscale &&
		activeTask != ofxGgmlImageGenerationTask::Variation;
	const bool hasRunnableText =
		!taskNeedsText ||
		hasPromptText ||
		(activeTask == ofxGgmlImageGenerationTask::InstructImage && hasInstructionText);
	const bool canRunDiffusion =
		!generating.load() &&
		diffusionRuntimeAttached &&
		bridgeConfigured &&
		hasRunnableText &&
		(!needsInitImage || std::strlen(diffusionInitImagePath) > 0) &&
		(!needsMaskImage || std::strlen(diffusionMaskImagePath) > 0) &&
		(!useClipSelection || hasClipModel);
	std::string diffusionRunDisabledReason;
	if (generating.load()) {
		diffusionRunDisabledReason = "A generation is already running.";
	} else if (!diffusionRuntimeAttached) {
		diffusionRunDisabledReason =
			"Local ofxStableDiffusion runtime is not attached yet.";
	} else if (!bridgeConfigured) {
		diffusionRunDisabledReason =
			"Diffusion backend bridge is present but not configured.";
	} else if (!profileSupportsTask(activeProfile, activeTask)) {
		diffusionRunDisabledReason =
			"The selected diffusion profile does not support the chosen task.";
	} else if (!hasRunnableText) {
		diffusionRunDisabledReason =
			activeTask == ofxGgmlImageGenerationTask::InstructImage
				? "Enter a prompt or instruction first."
				: "Enter a prompt first.";
	} else if (needsInitImage && std::strlen(diffusionInitImagePath) == 0) {
		diffusionRunDisabledReason =
			"Select an init image for the current diffusion task.";
	} else if (needsMaskImage && std::strlen(diffusionMaskImagePath) == 0) {
		diffusionRunDisabledReason = "Select a mask image for inpaint mode.";
	} else if (useClipSelection && !hasClipModel) {
		diffusionRunDisabledReason =
			"Set a CLIP model path in the CLIP tab to use rerank or best-only selection.";
	}
	ImGui::BeginDisabled(!canRunDiffusion);
	if (ImGui::Button("Run Diffusion", ImVec2(160, 0))) {
		runDiffusionInference();
	}
	ImGui::EndDisabled();
	if (!diffusionRunDisabledReason.empty()) {
		ImGui::SameLine();
		ImGui::TextDisabled("(%s)", diffusionRunDisabledReason.c_str());
	}

	ImGui::Separator();
	ImGui::Text("Output:");
	if (!diffusionOutput.empty()) {
		ImGui::SameLine();
		if (ImGui::SmallButton("Copy##DiffusionCopy")) copyToClipboard(diffusionOutput);
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear##DiffusionClear")) {
			diffusionOutput.clear();
			diffusionBackendName.clear();
			diffusionElapsedMs = 0.0f;
			diffusionGeneratedImages.clear();
			diffusionMetadata.clear();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(%d chars)", static_cast<int>(diffusionOutput.size()));
	}
	if (!diffusionBackendName.empty()) {
		ImGui::TextDisabled(
			"Last backend: %s%s",
			diffusionBackendName.c_str(),
			diffusionElapsedMs > 0.0f
				? (" in " + ofxGgmlHelpers::formatDurationMs(diffusionElapsedMs)).c_str()
				: "");
	}
	if (!diffusionGeneratedImages.empty()) {
		ImGui::TextDisabled("Generated files:");
		for (const auto & image : diffusionGeneratedImages) {
			std::string label = image.path;
			if (image.selected) {
				label += " [selected]";
			}
			if (!image.scorer.empty()) {
				label += " | " + image.scorer;
				label += ": " + ofToString(image.score, 4);
			}
			ImGui::BulletText("%s", label.c_str());
		}
		const std::string selectedOutputPath = getPreferredDiffusionReuseImagePath();
		ImGui::BeginDisabled(selectedOutputPath.empty());
		if (ImGui::SmallButton("Send To Vision##Diffusion")) {
			copyStringToBuffer(
				visionImagePath,
				sizeof(visionImagePath),
				selectedOutputPath);
			activeMode = AiMode::Vision;
			autoSaveSession();
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered()) {
			showWrappedTooltip("Copies the selected or latest generated image into the Vision panel and switches there.");
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(selectedOutputPath.empty());
		if (ImGui::SmallButton("Reuse As Init##Diffusion")) {
			setDiffusionInitImagePath(selectedOutputPath, true);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered()) {
			showWrappedTooltip("Uses the selected or latest generated image as the next init image.");
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(diffusionGeneratedImages.empty());
		if (ImGui::SmallButton("Send To CLIP##Diffusion")) {
			copyDiffusionOutputsToClipPaths();
			activeMode = AiMode::Clip;
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered()) {
			showWrappedTooltip("Copies all generated image paths into the CLIP panel and switches there.");
		}
		drawDiffusionImagePreview(
			"Generated preview",
			diffusionOutputPreviewLoadedPath,
			diffusionOutputPreviewImage,
			diffusionOutputPreviewError,
			"##DiffusionOutputPreview");
	}

	if (generating.load() && activeGenerationMode == AiMode::Diffusion) {
		ImGui::BeginChild("##DiffusionOut", ImVec2(0, 0), true);
		std::string partial;
		{
			std::lock_guard<std::mutex> lock(streamMutex);
			partial = streamingOutput;
		}
		if (partial.empty()) {
			int dots = static_cast<int>(ImGui::GetTime() * kDotsAnimationSpeed) % 4;
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", kWaitingLabels[dots]);
		} else {
			ImGui::TextWrapped("%s", partial.c_str());
		}
		ImGui::EndChild();
	} else {
		ImGui::BeginChild("##DiffusionOut", ImVec2(0, 0), true);
		if (diffusionOutput.empty()) {
			ImGui::TextDisabled("Diffusion request results appear here.");
		} else {
			ImGui::TextWrapped("%s", diffusionOutput.c_str());
		}
		ImGui::EndChild();
	}
}

void ofApp::drawClipPanel() {
	drawPanelHeader("CLIP", "optional clip.cpp text-image ranking bridge");
	const float compactModeFieldWidth = std::min(320.0f, ImGui::GetContentRegionAvail().x);

	ImGui::TextWrapped(
		"This panel ranks local images against a prompt using the CLIP bridge. "
		"It is designed to pair well with the Diffusion tab, but it also works with any local image set.");

#if OFXGGML_HAS_CLIPCPP
	const bool clipCppAvailable = true;
#else
	const bool clipCppAvailable = false;
#endif

	const std::string clipBackendLabel =
		clipInference.getBackend()
			? clipInference.getBackend()->backendName()
			: std::string("(none)");
	ImGui::TextDisabled("Backend: %s", clipBackendLabel.c_str());
	ImGui::TextDisabled("clip.cpp integration: %s", clipCppAvailable ? "available in this build" : "not compiled in");
	if (!clipCppAvailable) {
		ImGui::TextColored(
			ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
			"This build does not currently see clip.cpp headers. Add clip.cpp to the workspace include path to enable live ranking.");
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Model path", clipModelPath, sizeof(clipModelPath));
	ImGui::SameLine();
	if (ImGui::Button("Browse model...##Clip", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select CLIP model", false);
		if (result.bSuccess) {
			copyStringToBuffer(clipModelPath, sizeof(clipModelPath), result.getPath());
		}
	}

	if (ImGui::Button("Use Diffusion Outputs", ImVec2(160, 0))) {
		copyDiffusionOutputsToClipPaths();
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear Image List", ImVec2(140, 0))) {
		clipImagePaths[0] = '\0';
	}

	ImGui::InputTextMultiline(
		"Prompt",
		clipPrompt,
		sizeof(clipPrompt),
		ImVec2(-1, 100));
	ImGui::InputTextMultiline(
		"Image paths (one per line)",
		clipImagePaths,
		sizeof(clipImagePaths),
		ImVec2(-1, 120));

	const std::vector<std::string> parsedImagePaths =
		extractPathList(clipImagePaths);
	ImGui::TextDisabled("Parsed images: %d", static_cast<int>(parsedImagePaths.size()));

	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Top K", &clipTopK, 0, 16);
	ImGui::Checkbox("Normalize embeddings", &clipNormalizeEmbeddings);
	ImGui::SetNextItemWidth(160);
	ImGui::SliderInt("Verbosity", &clipVerbosity, 0, 2);

	const bool canRunClip =
		!generating.load() &&
		clipCppAvailable &&
		std::strlen(clipPrompt) > 0 &&
		std::strlen(clipModelPath) > 0 &&
		!parsedImagePaths.empty();
	ImGui::BeginDisabled(!canRunClip);
	if (ImGui::Button("Rank Images", ImVec2(160, 0))) {
		runClipInference();
	}
	ImGui::EndDisabled();

	ImGui::Separator();
	ImGui::Text("Output:");
	if (!clipOutput.empty()) {
		ImGui::SameLine();
		if (ImGui::SmallButton("Copy##ClipCopy")) copyToClipboard(clipOutput);
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear##ClipClear")) {
			clipOutput.clear();
			clipBackendName.clear();
			clipElapsedMs = 0.0f;
			clipEmbeddingDimension = 0;
			clipHits.clear();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(%d chars)", static_cast<int>(clipOutput.size()));
	}
	if (!clipBackendName.empty()) {
		ImGui::TextDisabled(
			"Last backend: %s%s%s",
			clipBackendName.c_str(),
			clipElapsedMs > 0.0f
				? (" in " + ofxGgmlHelpers::formatDurationMs(clipElapsedMs)).c_str()
				: "",
			clipEmbeddingDimension > 0
				? (" | dim " + ofToString(clipEmbeddingDimension)).c_str()
				: "");
	}
	if (!clipHits.empty()) {
		ImGui::TextDisabled("Top matches:");
		for (const auto & hit : clipHits) {
			ImGui::BulletText(
				"%.4f  %s",
				hit.score,
				hit.imagePath.c_str());
		}
	}

	if (generating.load() && activeGenerationMode == AiMode::Clip) {
		ImGui::BeginChild("##ClipOut", ImVec2(0, 0), true);
		std::string partial;
		{
			std::lock_guard<std::mutex> lock(streamMutex);
			partial = streamingOutput;
		}
		if (partial.empty()) {
			int dots = static_cast<int>(ImGui::GetTime() * kDotsAnimationSpeed) % 4;
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", kWaitingLabels[dots]);
		} else {
			ImGui::TextWrapped("%s", partial.c_str());
		}
		ImGui::EndChild();
	} else {
		ImGui::BeginChild("##ClipOut", ImVec2(0, 0), true);
		if (clipOutput.empty()) {
			ImGui::TextDisabled("CLIP ranking results appear here.");
		} else {
			ImGui::TextWrapped("%s", clipOutput.c_str());
		}
		ImGui::EndChild();
	}
}

void ofApp::runDiffusionInference() {
	if (generating.load()) return;

	if (diffusionProfiles.empty()) {
		diffusionProfiles = ofxGgmlDiffusionInference::defaultProfiles();
	}
	selectedDiffusionProfileIndex = std::clamp(
		selectedDiffusionProfileIndex,
		0,
		std::max(0, static_cast<int>(diffusionProfiles.size()) - 1));
	const int selectionModeIndex = std::clamp(diffusionSelectionModeIndex, 0, 2);
	if (!ensureDiffusionBackendConfigured()) {
		diffusionOutput =
			"[Error] ofxStableDiffusion is not attached in this build. Add ofxStableDiffusion to the generated project and rebuild the example.";
		diffusionBackendName.clear();
		diffusionElapsedMs = 0.0f;
		diffusionGeneratedImages.clear();
		diffusionMetadata.clear();
		return;
	}
	if (selectionModeIndex > 0 && !ensureDiffusionClipBackendConfigured()) {
		diffusionOutput =
			"[Error] CLIP reranking needs a working clip.cpp model path in the CLIP tab.";
		diffusionBackendName.clear();
		diffusionElapsedMs = 0.0f;
		diffusionGeneratedImages.clear();
		diffusionMetadata.clear();
		return;
	}
	autoSaveSession();

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::Diffusion;
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = "Preparing diffusion request...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	const ofxGgmlImageGenerationModelProfile profileBase =
		diffusionProfiles.empty()
			? ofxGgmlImageGenerationModelProfile{}
			: diffusionProfiles[static_cast<size_t>(selectedDiffusionProfileIndex)];
	const std::string prompt = trim(diffusionPrompt);
	const std::string instruction = trim(diffusionInstruction);
	const std::string negativePrompt = trim(diffusionNegativePrompt);
	const std::string rankingPrompt = trim(diffusionRankingPrompt);
	const std::string modelPath = trim(diffusionModelPath);
	const std::string vaePath = trim(diffusionVaePath);
	const std::string initImagePath = trim(diffusionInitImagePath);
	const std::string maskImagePath = trim(diffusionMaskImagePath);
	const std::string outputDir = trim(diffusionOutputDir);
	const std::string outputPrefix = trim(diffusionOutputPrefix);
	const std::string sampler = trim(diffusionSampler);
	const int taskIndex = std::clamp(diffusionTaskIndex, 0, 6);
	const int width = clampSupportedDiffusionImageSize(diffusionWidth);
	const int height = clampSupportedDiffusionImageSize(diffusionHeight);
	const int steps = std::clamp(diffusionSteps, 1, 200);
	const int batchCount = std::clamp(diffusionBatchCount, 1, 16);
	const int requestSeed = diffusionSeed;
	const float cfgScale = std::isfinite(diffusionCfgScale)
		? std::clamp(diffusionCfgScale, 0.0f, 30.0f)
		: 7.0f;
	const float strength = std::isfinite(diffusionStrength)
		? std::clamp(diffusionStrength, 0.0f, 1.0f)
		: 0.75f;
	const bool normalizeClipEmbeddings = diffusionNormalizeClipEmbeddings;
	const bool saveMetadata = diffusionSaveMetadata;

	workerThread = std::thread([this, profileBase, prompt, instruction, negativePrompt, rankingPrompt, modelPath, vaePath, initImagePath, maskImagePath, outputDir, outputPrefix, sampler, taskIndex, selectionModeIndex, width, height, steps, batchCount, requestSeed, cfgScale, strength, normalizeClipEmbeddings, saveMetadata]() {
		auto setPending = [this](const std::string & text) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingOutput = text;
			pendingRole = "assistant";
			pendingMode = AiMode::Diffusion;
		};

		auto clearPendingDiffusionArtifacts = [this]() {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingDiffusionBackendName.clear();
			pendingDiffusionElapsedMs = 0.0f;
			pendingDiffusionImages.clear();
			pendingDiffusionMetadata.clear();
		};

		try {
			const auto task = static_cast<ofxGgmlImageGenerationTask>(taskIndex);
			const auto selectionMode =
				static_cast<ofxGgmlImageSelectionMode>(selectionModeIndex);
			const auto supportsTask =
				[](const ofxGgmlImageGenerationModelProfile & profile,
					ofxGgmlImageGenerationTask taskValue) {
					switch (taskValue) {
					case ofxGgmlImageGenerationTask::ImageToImage:
						return profile.supportsImageToImage;
					case ofxGgmlImageGenerationTask::InstructImage:
						return profile.supportsInstructImage;
					case ofxGgmlImageGenerationTask::Variation:
						return profile.supportsVariation;
					case ofxGgmlImageGenerationTask::Restyle:
						return profile.supportsRestyle;
					case ofxGgmlImageGenerationTask::Inpaint:
						return profile.supportsInpaint;
					case ofxGgmlImageGenerationTask::Upscale:
						return profile.supportsUpscale;
					case ofxGgmlImageGenerationTask::TextToImage:
					default:
						return true;
					}
				};
			const bool needsPromptText =
				task != ofxGgmlImageGenerationTask::Upscale &&
				task != ofxGgmlImageGenerationTask::Variation;
			const bool hasRunnableText =
				!needsPromptText ||
				!prompt.empty() ||
				(task == ofxGgmlImageGenerationTask::InstructImage &&
					!instruction.empty());
			if (!hasRunnableText) {
				clearPendingDiffusionArtifacts();
				setPending(
					task == ofxGgmlImageGenerationTask::InstructImage
						? "[Error] Enter a prompt or instruction first."
						: "[Error] Enter a prompt first.");
				generating.store(false);
				return;
			}
			const bool needsInitImage =
				task == ofxGgmlImageGenerationTask::ImageToImage ||
				task == ofxGgmlImageGenerationTask::InstructImage ||
				task == ofxGgmlImageGenerationTask::Variation ||
				task == ofxGgmlImageGenerationTask::Restyle ||
				task == ofxGgmlImageGenerationTask::Inpaint ||
				task == ofxGgmlImageGenerationTask::Upscale;
			const bool needsMaskImage =
				task == ofxGgmlImageGenerationTask::Inpaint;
			if (needsInitImage && initImagePath.empty()) {
				clearPendingDiffusionArtifacts();
				setPending("[Error] Select an init image for the current diffusion task.");
				generating.store(false);
				return;
			}
			if (needsMaskImage && maskImagePath.empty()) {
				clearPendingDiffusionArtifacts();
				setPending("[Error] Select a mask image for inpaint mode.");
				generating.store(false);
				return;
			}

			if (!supportsTask(profileBase, task)) {
				clearPendingDiffusionArtifacts();
				setPending("[Error] The selected diffusion profile does not support the chosen task.");
				generating.store(false);
				return;
			}

			std::string effectiveModelPath = modelPath.empty()
				? suggestedModelPath(profileBase.modelPath, profileBase.modelFileHint)
				: modelPath;
			std::string effectiveOutputDir = outputDir.empty()
				? ofToDataPath("generated", true)
				: outputDir;

			std::error_code dirEc;
			std::filesystem::create_directories(effectiveOutputDir, dirEc);
			if (dirEc) {
				clearPendingDiffusionArtifacts();
				setPending("[Error] Failed to create output directory: " + effectiveOutputDir);
				generating.store(false);
				return;
			}

			ofxGgmlImageGenerationRequest request;
			request.task = task;
			request.selectionMode = selectionMode;
			request.prompt = prompt;
			request.instruction = instruction;
			request.negativePrompt = negativePrompt;
			request.rankingPrompt = rankingPrompt;
			request.modelPath = effectiveModelPath;
			request.vaePath = vaePath;
			request.initImagePath = initImagePath;
			request.maskImagePath = maskImagePath;
			request.outputDir = effectiveOutputDir;
			request.outputPrefix = outputPrefix;
			request.sampler = sampler;
			request.width = width;
			request.height = height;
			request.steps = steps;
			request.batchCount = batchCount;
			request.seed = requestSeed;
			request.cfgScale = cfgScale;
			request.strength = strength;
			request.normalizeClipEmbeddings = normalizeClipEmbeddings;
			request.saveMetadata = saveMetadata;

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput =
					"Calling " + diffusionInference.getBackend()->backendName() + "...";
			}

			const ofxGgmlImageGenerationResult result =
				diffusionInference.generate(request);
			if (cancelRequested.load()) {
				clearPendingDiffusionArtifacts();
				setPending("[Cancelled] Diffusion request cancelled.");
			} else if (result.success) {
				std::ostringstream summary;
				summary << "Generated " << result.images.size() << " image";
				if (result.images.size() != 1) {
					summary << "s";
				}
				if (!result.backendName.empty()) {
					summary << " via " << result.backendName;
				}
				if (result.elapsedMs > 0.0f) {
					summary << " in " << ofxGgmlHelpers::formatDurationMs(result.elapsedMs);
				}
				summary << ".";
				if (!result.images.empty()) {
					summary << "\n\nFiles:";
					for (const auto & image : result.images) {
						summary << "\n- " << image.path;
						if (image.width > 0 && image.height > 0) {
							summary << " (" << image.width << "x" << image.height << ")";
						}
						if (image.selected) {
							summary << " [selected]";
						}
						if (!image.scorer.empty()) {
							summary << " | " << image.scorer;
						}
						if (!image.scorer.empty() || !image.scoreSummary.empty()) {
							summary << " score=" << ofToString(image.score, 4);
						}
						if (!image.scoreSummary.empty()) {
							summary << " | " << image.scoreSummary;
						}
					}
				}
				if (!result.metadata.empty()) {
					summary << "\n\nMetadata:";
					for (const auto & entry : result.metadata) {
						summary << "\n- " << entry.first << ": " << entry.second;
					}
				}
				if (!result.rawOutput.empty()) {
					summary << "\n\nBackend output:\n" << result.rawOutput;
				}
				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingDiffusionBackendName = result.backendName;
					pendingDiffusionElapsedMs = result.elapsedMs;
					pendingDiffusionImages = result.images;
					pendingDiffusionMetadata = result.metadata;
				}
				setPending(summary.str());
				logWithLevel(
					OF_LOG_NOTICE,
					"Diffusion request completed in " +
						ofxGgmlHelpers::formatDurationMs(result.elapsedMs) +
						" via " + result.backendName);
			} else {
				clearPendingDiffusionArtifacts();
				std::string errorText = result.error.empty()
					? std::string("Diffusion backend returned no result.")
					: result.error;
				if (!result.rawOutput.empty()) {
					errorText += "\n\nBackend output:\n" + result.rawOutput;
				}
				setPending("[Error] " + errorText);
			}
		} catch (const std::exception & e) {
			clearPendingDiffusionArtifacts();
			setPending(std::string("[Error] Diffusion inference failed: ") + e.what());
		} catch (...) {
			clearPendingDiffusionArtifacts();
			setPending("[Error] Unknown failure during diffusion inference.");
		}

		{
			std::lock_guard<std::mutex> lock(streamMutex);
			streamingOutput.clear();
		}
		generating.store(false);
	});
}

void ofApp::runClipInference() {
	if (generating.load()) return;

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::Clip;
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = "Preparing CLIP ranking request...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	const std::string prompt = trim(clipPrompt);
	const std::string modelPath = trim(clipModelPath);
	const std::vector<std::string> imagePaths = extractPathList(clipImagePaths);
	const size_t topK = clipTopK > 0 ? static_cast<size_t>(clipTopK) : 0;
	const bool normalizeEmbeddings = clipNormalizeEmbeddings;
	const int verbosity = std::clamp(clipVerbosity, 0, 2);
#if OFXGGML_HAS_CLIPCPP
	if (!prompt.empty() && !modelPath.empty()) {
		ensureClipBackendConfigured(modelPath, verbosity, normalizeEmbeddings);
	}
#endif
	autoSaveSession();

	workerThread = std::thread([this, prompt, modelPath, imagePaths, topK, normalizeEmbeddings, verbosity]() {
		auto setPending = [this](const std::string & text) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingOutput = text;
			pendingRole = "assistant";
			pendingMode = AiMode::Clip;
		};

		auto clearPendingClipArtifacts = [this]() {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingClipBackendName.clear();
			pendingClipElapsedMs = 0.0f;
			pendingClipEmbeddingDimension = 0;
			pendingClipHits.clear();
		};

		try {
			if (prompt.empty()) {
				clearPendingClipArtifacts();
				setPending("[Error] Enter a prompt first.");
				generating.store(false);
				return;
			}
			if (modelPath.empty()) {
				clearPendingClipArtifacts();
				setPending("[Error] Select a clip.cpp model first.");
				generating.store(false);
				return;
			}
			if (imagePaths.empty()) {
				clearPendingClipArtifacts();
				setPending("[Error] Add at least one image path.");
				generating.store(false);
				return;
			}

#if OFXGGML_HAS_CLIPCPP
			ofxGgmlClipImageRankingRequest request;
			request.prompt = prompt;
			request.promptId = "prompt";
			request.promptLabel = "Prompt";
			request.imagePaths = imagePaths;
			request.topK = topK;
			request.normalizeEmbeddings = normalizeEmbeddings;

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput =
					"Ranking " + ofToString(imagePaths.size()) + " image(s) with clip.cpp...";
			}

			const ofxGgmlClipImageRankingResult result =
				clipInference.rankImagesForText(request);
			if (cancelRequested.load()) {
				clearPendingClipArtifacts();
				setPending("[Cancelled] CLIP ranking cancelled.");
			} else if (result.success) {
				const int embeddingDimension =
					static_cast<int>(result.queryEmbedding.embedding.size());
				std::ostringstream summary;
				summary << "Ranked " << imagePaths.size() << " image";
				if (imagePaths.size() != 1) {
					summary << "s";
				}
				if (!result.backendName.empty()) {
					summary << " via " << result.backendName;
				}
				if (result.elapsedMs > 0.0f) {
					summary << " in " << ofxGgmlHelpers::formatDurationMs(result.elapsedMs);
				}
				if (embeddingDimension > 0) {
					summary << " (dim " << embeddingDimension << ")";
				}
				summary << ".";
				if (!result.hits.empty()) {
					summary << "\n\nTop matches:";
					for (size_t i = 0; i < result.hits.size(); ++i) {
						const auto & hit = result.hits[i];
						summary << "\n" << (i + 1) << ". "
							<< hit.imagePath
							<< " | score " << ofToString(hit.score, 4);
					}
				}
				if (!result.rawOutput.empty()) {
					summary << "\n\nBackend output:\n" << result.rawOutput;
				}
				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingClipBackendName = result.backendName;
					pendingClipElapsedMs = result.elapsedMs;
					pendingClipEmbeddingDimension = embeddingDimension;
					pendingClipHits = result.hits;
				}
				setPending(summary.str());
			} else {
				clearPendingClipArtifacts();
				std::string errorText = result.error.empty()
					? std::string("CLIP backend returned no result.")
					: result.error;
				if (!result.rawOutput.empty()) {
					errorText += "\n\nBackend output:\n" + result.rawOutput;
				}
				setPending("[Error] " + errorText);
			}
#else
			clearPendingClipArtifacts();
			setPending("[Error] clip.cpp support is not available in this build yet.");
#endif
		} catch (const std::exception & e) {
			clearPendingClipArtifacts();
			setPending(std::string("[Error] CLIP ranking failed: ") + e.what());
		} catch (...) {
			clearPendingClipArtifacts();
			setPending("[Error] Unknown failure during CLIP ranking.");
		}

		{
			std::lock_guard<std::mutex> lock(streamMutex);
			streamingOutput.clear();
		}
		generating.store(false);
	});
}
