#include "ofApp.h"

#include "ImHelpers.h"
#include "utils/ImGuiHelpers.h"
#include "utils/PathHelpers.h"
#include "utils/ConsoleHelpers.h"

#include <algorithm>
#include <filesystem>
#include <sstream>

namespace {

constexpr float kMilkDropPreviewHeight = 260.0f;
constexpr float kMilkDropWaitingDotsAnimationSpeed = 3.0f;
const char * const kMilkDropWaitingLabels[] = {
	"generating",
	"generating.",
	"generating..",
	"generating..."
};

std::string summarizeValidation(const ofxGgmlMilkDropValidation & validation) {
	if (validation.sanitizedPresetText.empty() && validation.issues.empty()) {
		return {};
	}
	if (validation.valid) {
		return "Validation passed.";
	}
	const auto firstError = std::find_if(
		validation.issues.begin(),
		validation.issues.end(),
		[](const ofxGgmlMilkDropValidationIssue & issue) {
			return issue.severity == "error";
		});
	if (firstError != validation.issues.end()) {
		return firstError->message;
	}
	return "Validation reported warnings.";
}

std::string makeMilkDropGenerationSummary(
	const std::string & actionLabel,
	const std::string & pathLabel,
	size_t count) {
	std::ostringstream out;
	out << actionLabel;
	if (count > 0) {
		out << " (" << count << ")";
	}
	if (!pathLabel.empty()) {
		out << "\n\n" << pathLabel;
	}
	return out.str();
}

std::string makeFileUrl(const std::string & path) {
	std::string normalized = ofFilePath::getAbsolutePath(path, true);
	std::replace(normalized.begin(), normalized.end(), '\\', '/');
	if (!normalized.empty() && normalized.front() != '/') {
		normalized.insert(normalized.begin(), '/');
	}
	return "file://" + normalized;
}

std::vector<std::string> defaultProjectMTextureSearchPaths() {
	std::vector<std::string> searchPaths;
	const auto addIfExists = [&searchPaths](const std::string & rawPath) {
		if (rawPath.empty() || !ofDirectory::doesDirectoryExist(rawPath, true)) {
			return;
		}
		const std::string absolutePath = ofFilePath::getAbsolutePath(rawPath, true);
		if (std::find(searchPaths.begin(), searchPaths.end(), absolutePath) == searchPaths.end()) {
			searchPaths.push_back(absolutePath);
		}
	};

	addIfExists(ofToDataPath("textures/textures", true));
	addIfExists(ofToDataPath("textures", true));
	addIfExists(ofToDataPath("presets/textures", true));
	return searchPaths;
}

void drawMilkDropOutputChild(
	const std::string & output,
	const bool isGenerating,
	std::mutex & streamMutex,
	std::string & streamingOutput) {
	ImGui::BeginChild("##MilkDropOutput", ImVec2(0, 220.0f), true);
	if (isGenerating) {
		std::string partial;
		{
			std::lock_guard<std::mutex> lock(streamMutex);
			partial = streamingOutput;
		}
		if (partial.empty()) {
			const int dots = static_cast<int>(ImGui::GetTime() * kMilkDropWaitingDotsAnimationSpeed) % 4;
			ImGui::TextDisabled("%s", kMilkDropWaitingLabels[dots]);
		} else {
			ImGui::TextUnformatted(partial.c_str());
		}
	} else if (output.empty()) {
		ImGui::TextDisabled("Generated preset text will appear here.");
	} else {
		ImGui::TextUnformatted(output.c_str());
	}
	ImGui::EndChild();
}

} // namespace

std::string ofApp::defaultMilkDropPresetDir() const {
	const auto root = addonRootPath();
	if (!root.empty()) {
		return (root / "generated" / "milkdrop").lexically_normal().string();
	}
	return ofToDataPath("generated/milkdrop", true);
}

void ofApp::validateCurrentMilkDropPreset() {
	milkdropValidation = ofxGgmlMilkDropGenerator::validatePreset(milkdropOutput);
	milkdropPreviewStatus = summarizeValidation(milkdropValidation);
}

bool ofApp::saveMilkDropPresetToConfiguredPath() {
	const std::string outputPath = trim(milkdropPresetPath);
	if (outputPath.empty()) {
		milkdropPreviewStatus = "[Error] Choose an output .milk path first.";
		return false;
	}
	const std::string savedPath = milkdropGenerator.savePreset(milkdropOutput, outputPath);
	if (savedPath.empty()) {
		milkdropPreviewStatus = "[Error] Failed to save the MilkDrop preset.";
		return false;
	}
	milkdropSavedPresetPath = savedPath;
	copyStringToBuffer(milkdropPresetPath, sizeof(milkdropPresetPath), savedPath);
	milkdropPreviewStatus = "Saved preset to " + savedPath;
	return true;
}

#if OFXGGML_HAS_OFXPROJECTM
bool ofApp::ensureMilkDropPreviewReady() {
	if (milkdropPreviewInitialized) {
		return true;
	}

	try {
		milkdropPreviewPlayer.init();
		milkdropPreviewPlayer.useInternalTextureOnly();
		milkdropPreviewPlayer.setWindowSize(512, 512);
		milkdropPreviewPlayer.setTextureSearchPaths(defaultProjectMTextureSearchPaths());
		milkdropPreviewPlayer.setPresetDuration(milkdropPreviewPresetDuration);
		milkdropPreviewPlayer.setBeatSensitivity(milkdropPreviewBeatSensitivity);
		milkdropPreviewInitialized = milkdropPreviewPlayer.isInitialized();
		if (!milkdropPreviewInitialized) {
			milkdropPreviewError = "projectM preview could not be initialized.";
			return false;
		}
		milkdropPreviewError.clear();
		return true;
	} catch (const std::exception & e) {
		milkdropPreviewError = e.what();
		return false;
	} catch (...) {
		milkdropPreviewError = "projectM preview failed with an unknown error.";
		return false;
	}
}

bool ofApp::loadMilkDropPresetIntoPreview(const std::string & presetText) {
	if (!ensureMilkDropPreviewReady()) {
		return false;
	}

	const std::string sanitized = ofxGgmlMilkDropGenerator::sanitizePresetText(presetText);
	if (sanitized.empty()) {
		milkdropPreviewError = "Preset text is empty after sanitization.";
		return false;
	}
	if (!milkdropPreviewPlayer.loadPresetData(sanitized, true)) {
		milkdropPreviewError = milkdropPreviewPlayer.getLastErrorMessage().empty()
			? std::string("projectM rejected the generated preset.")
			: milkdropPreviewPlayer.getLastErrorMessage();
		return false;
	}
	milkdropPreviewError.clear();
	milkdropPreviewStatus = "projectM preview loaded the generated preset.";
	return true;
}
#endif

void ofApp::runMilkDropGeneration(
	bool editExisting,
	bool generateVariants,
	bool repairExisting) {
	if (generating.load()) {
		return;
	}

	const std::string modelPath = getSelectedModelPath();
	if (modelPath.empty()) {
		milkdropOutput = "[Error] Select a text model before generating a MilkDrop preset.";
		return;
	}

	if (milkdropCategories.empty()) {
		milkdropCategories = ofxGgmlMilkDropGenerator::defaultCategories();
	}

	const std::string prompt = trim(milkdropPrompt);
	if (prompt.empty()) {
		milkdropOutput = "[Error] Enter a creative direction for the MilkDrop preset.";
		return;
	}

	if ((editExisting || repairExisting) && trim(milkdropOutput).empty()) {
		milkdropOutput = "[Error] Generate or load a preset before editing it.";
		return;
	}

	const int categoryIndex = std::clamp(
		milkdropCategoryIndex,
		0,
		std::max(0, static_cast<int>(milkdropCategories.size()) - 1));
	const std::string category = milkdropCategories.empty()
		? std::string("General")
		: milkdropCategories[static_cast<size_t>(categoryIndex)].name;

	milkdropGenerator.setCompletionExecutable(llmInference.getCompletionExecutable());

	ofxGgmlInferenceSettings settings = buildCurrentTextInferenceSettings(AiMode::MilkDrop);
	settings.temperature = std::clamp(milkdropRandomness, 0.0f, 1.2f);
	settings.maxTokens = std::max(settings.maxTokens, 512);
	settings.stopAtNaturalBoundary = false;

	ofxGgmlMilkDropRequest request;
	request.prompt = prompt;
	request.category = category;
	request.randomness = milkdropRandomness;
	request.audioReactive = true;
	request.seamlessLoop = true;
	request.existingPresetText = (editExisting || repairExisting) ? milkdropOutput : std::string();
	request.presetNameHint = ofxGgmlMilkDropGenerator::makeSuggestedFileName(prompt, category);
	const int variantCount = milkdropVariantCount;

	cancelRequested.store(false);
	generating.store(true);
	activeGenerationMode = AiMode::MilkDrop;
	streamingOutput.clear();
	generationStartTime = ofGetElapsedTimef();
	generatingStatus = generateVariants
		? "Generating MilkDrop preset variants..."
		: repairExisting
			? "Repairing MilkDrop preset..."
			: editExisting
				? "Editing MilkDrop preset..."
				: "Generating MilkDrop preset...";
	if (workerThread.joinable()) {
		workerThread.join();
	}

	workerThread = std::thread([this, modelPath, request, settings, generateVariants, repairExisting, variantCount]() mutable {
		auto streamCallback = [this](const std::string & delta) -> bool {
			std::lock_guard<std::mutex> lock(streamMutex);
			streamingOutput += delta;
			return !cancelRequested.load();
		};

		std::string outputText;
		{
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingMode = AiMode::MilkDrop;
			pendingMilkDropValidation = {};
			pendingMilkDropVariants.clear();
		}

		if (generateVariants) {
			const ofxGgmlMilkDropVariantResult variantResult =
				milkdropGenerator.generateVariants(
					modelPath,
					request,
					variantCount,
					settings);
			if (variantResult.success && !variantResult.variants.empty()) {
				outputText = variantResult.variants.front().presetText;
				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingMilkDropValidation =
						variantResult.variants.front().validation;
					pendingMilkDropVariants = variantResult.variants;
				}
			} else {
				outputText = "[Error] " + (variantResult.error.empty()
					? std::string("MilkDrop variant generation failed.")
					: variantResult.error);
			}
		} else {
			const ofxGgmlMilkDropResult result = repairExisting
				? milkdropGenerator.repairPreset(
					modelPath,
					request.existingPresetText,
					request.category,
					std::min(request.randomness, 0.35f),
					"",
					settings,
					streamCallback)
				: milkdropGenerator.generatePreset(
					modelPath,
					request,
					settings,
					streamCallback);

			if (result.success) {
				outputText = result.presetText;
				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingMilkDropValidation = result.validation;
				}
			} else {
				outputText = "[Error] " + (!result.error.empty()
					? result.error
					: result.inference.error.empty()
						? std::string("MilkDrop generation failed.")
						: result.inference.error);
			}
		}

		{
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingOutput = outputText;
		}

		generating.store(false);
	});
}

void ofApp::drawMilkDropPanel() {
	drawPanelHeader("MilkDrop", "generate projectM / MilkDrop presets with the text backend");

	if (milkdropCategories.empty()) {
		milkdropCategories = ofxGgmlMilkDropGenerator::defaultCategories();
	}
	milkdropCategoryIndex = std::clamp(
		milkdropCategoryIndex,
		0,
		std::max(0, static_cast<int>(milkdropCategories.size()) - 1));

	if (trim(milkdropPresetPath).empty()) {
		const std::string suggestedPath = ofFilePath::join(
			defaultMilkDropPresetDir(),
			ofxGgmlMilkDropGenerator::makeSuggestedFileName(
				trim(milkdropPrompt),
				milkdropCategories.empty()
					? std::string("General")
					: milkdropCategories[static_cast<size_t>(milkdropCategoryIndex)].name));
		copyStringToBuffer(milkdropPresetPath, sizeof(milkdropPresetPath), suggestedPath);
	}

	ImGui::TextWrapped(
		"Generate MilkDrop / projectM preset text with the current text model. "
		"This is useful for audio-reactive visualizer presets, not for rendered video generation.");
	const std::string selectedModelPath = getSelectedModelPath();
	if (!selectedModelPath.empty()) {
		ImGui::TextDisabled("Text model: %s", ofFilePath::getFileName(selectedModelPath).c_str());
	}
	ImGui::TextDisabled(
		"Backend: %s",
		textInferenceBackend == TextInferenceBackend::LlamaServer ? "llama-server" : "CLI fallback");

	if (ImGui::Button("Use Write Prompt", ImVec2(130, 0))) {
		copyStringToBuffer(milkdropPrompt, sizeof(milkdropPrompt), trim(writeInput));
	}
	ImGui::SameLine();
	if (ImGui::Button("Use Vision Prompt", ImVec2(130, 0))) {
		copyStringToBuffer(milkdropPrompt, sizeof(milkdropPrompt), trim(visionPrompt));
	}
	ImGui::SameLine();
	if (ImGui::Button("Use Custom Input", ImVec2(130, 0))) {
		copyStringToBuffer(milkdropPrompt, sizeof(milkdropPrompt), trim(customInput));
	}

	ImGui::InputTextMultiline(
		"Creative direction",
		milkdropPrompt,
		sizeof(milkdropPrompt),
		ImVec2(-1, 110));

	std::vector<const char *> categoryNames;
	categoryNames.reserve(milkdropCategories.size());
	for (const auto & category : milkdropCategories) {
		categoryNames.push_back(category.name.c_str());
	}
	ImGui::SetNextItemWidth(240.0f);
	if (!categoryNames.empty()) {
		ImGui::Combo(
			"Category",
			&milkdropCategoryIndex,
			categoryNames.data(),
			static_cast<int>(categoryNames.size()));
		ImGui::TextDisabled(
			"%s",
			milkdropCategories[static_cast<size_t>(milkdropCategoryIndex)].description.c_str());
	}

	ImGui::SliderFloat("Randomness", &milkdropRandomness, 0.0f, 1.0f, "%.2f");
	ImGui::SetNextItemWidth(180.0f);
	ImGui::SliderInt("Variant count", &milkdropVariantCount, 1, 6);
	ImGui::Checkbox("Auto preview after generation", &milkdropAutoPreview);
	ImGui::SameLine();
	ImGui::Checkbox("Feed mic while recording", &milkdropPreviewFeedMicWhileRecording);

	ImGui::InputText("Preset output path", milkdropPresetPath, sizeof(milkdropPresetPath));
	ImGui::SameLine();
	if (ImGui::Button("Preset folder", ImVec2(110, 0))) {
		ofLaunchBrowser(makeFileUrl(defaultMilkDropPresetDir()));
	}

	const bool canGenerate = !generating.load() && !trim(milkdropPrompt).empty();
	ImGui::BeginDisabled(!canGenerate);
	if (ImGui::Button("Generate Preset", ImVec2(150, 0))) {
		runMilkDropGeneration(false);
	}
	ImGui::SameLine();
	if (ImGui::Button("Generate Variants", ImVec2(150, 0))) {
		runMilkDropGeneration(false, true, false);
	}
	ImGui::SameLine();
	if (ImGui::Button("Edit Current", ImVec2(110, 0))) {
		runMilkDropGeneration(true, false, false);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	const bool hasPresetText = !trim(milkdropOutput).empty();
	ImGui::BeginDisabled(!hasPresetText);
	if (ImGui::Button("Repair Current", ImVec2(120, 0))) {
		runMilkDropGeneration(false, false, true);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(!hasPresetText);
	if (ImGui::Button("Validate", ImVec2(90, 0))) {
		validateCurrentMilkDropPreset();
	}
	ImGui::SameLine();
	if (ImGui::Button("Save .milk", ImVec2(100, 0))) {
		saveMilkDropPresetToConfiguredPath();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!hasPresetText);
	if (ImGui::Button("Copy", ImVec2(80, 0))) {
		copyToClipboard(milkdropOutput);
	}
	ImGui::EndDisabled();

	if (!milkdropSavedPresetPath.empty()) {
		ImGui::TextDisabled("Last saved preset: %s", milkdropSavedPresetPath.c_str());
	}
	if (!milkdropPreviewStatus.empty()) {
		ImGui::TextWrapped("%s", milkdropPreviewStatus.c_str());
	}

	if (!milkdropValidation.sanitizedPresetText.empty() || !milkdropValidation.issues.empty()) {
		const ImVec4 statusColor = milkdropValidation.valid
			? ImVec4(0.45f, 0.85f, 0.55f, 1.0f)
			: ImVec4(0.95f, 0.65f, 0.30f, 1.0f);
		ImGui::TextColored(
			statusColor,
			"Validation: %s (%d issues, %d assignments)",
			milkdropValidation.valid ? "passed" : "needs review",
			static_cast<int>(milkdropValidation.issues.size()),
			milkdropValidation.assignmentCount);
		if (ImGui::TreeNode("Validation details")) {
			if (milkdropValidation.issues.empty()) {
				ImGui::TextDisabled("No issues detected.");
			} else {
				for (const auto & issue : milkdropValidation.issues) {
					std::string issueLine;
					if (issue.line > 0) {
						issueLine = "line " + ofToString(issue.line) + ": ";
					}
					std::string detail = issueLine + issue.message;
					if (!issue.suggestion.empty()) {
						detail += " Suggestion: " + issue.suggestion;
					}
					ImGui::BulletText("%s", detail.c_str());
				}
			}
			ImGui::TreePop();
		}
	}

	ImGui::Separator();
	ImGui::Text("Generated preset:");
	drawMilkDropOutputChild(
		milkdropOutput,
		generating.load() && activeGenerationMode == AiMode::MilkDrop,
		streamMutex,
		streamingOutput);

	if (!milkdropVariants.empty()) {
		ImGui::Separator();
		ImGui::Text("Preset variants:");
		for (size_t i = 0; i < milkdropVariants.size(); ++i) {
			const auto & variant = milkdropVariants[i];
			ImGui::PushID(static_cast<int>(i));
			const std::string label =
				variant.label.empty() ? ("Variant " + ofToString(i + 1)) : variant.label;
			if (ImGui::Selectable(
					label.c_str(),
					milkdropSelectedVariantIndex == static_cast<int>(i),
					ImGuiSelectableFlags_AllowDoubleClick)) {
				milkdropSelectedVariantIndex = static_cast<int>(i);
			}
			ImGui::SameLine();
			ImGui::TextDisabled(
				"%s",
				variant.success
					? (variant.validation.valid ? "valid" : "warning")
					: "error");
			if (variant.success) {
				ImGui::SameLine();
				if (ImGui::SmallButton("Use")) {
					milkdropOutput = variant.presetText;
					milkdropValidation = variant.validation;
					milkdropSelectedVariantIndex = static_cast<int>(i);
#if OFXGGML_HAS_OFXPROJECTM
					if (milkdropAutoPreview) {
						loadMilkDropPresetIntoPreview(milkdropOutput);
					}
#endif
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Copy")) {
					copyToClipboard(variant.presetText);
				}
			}
			ImGui::PopID();
		}
	}

	ImGui::Separator();
	ImGui::Text("projectM preview:");
#if OFXGGML_HAS_OFXPROJECTM
	if (!milkdropPreviewError.empty()) {
		ImGui::TextColored(
			ImVec4(0.95f, 0.45f, 0.35f, 1.0f),
			"%s",
			milkdropPreviewError.c_str());
	}
	ImGui::SetNextItemWidth(180.0f);
	ImGui::SliderFloat("Beat sensitivity", &milkdropPreviewBeatSensitivity, 0.2f, 3.0f, "%.2f");
	ImGui::SetNextItemWidth(180.0f);
	ImGui::SliderFloat("Preset duration", &milkdropPreviewPresetDuration, 4.0f, 60.0f, "%.1f s");
	if (milkdropPreviewFeedMicWhileRecording) {
		ImGui::TextDisabled(
			"Mic audio drives preview while Speech recording is active.");
	}
	ImGui::BeginDisabled(trim(milkdropOutput).empty());
	if (ImGui::Button("Preview in projectM", ImVec2(150, 0))) {
		loadMilkDropPresetIntoPreview(milkdropOutput);
	}
	ImGui::EndDisabled();
	if (milkdropPreviewInitialized && milkdropPreviewPlayer.getTexture().isAllocated()) {
		ImGui::BeginChild("##MilkDropPreview", ImVec2(0, kMilkDropPreviewHeight + 20.0f), true);
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float availableWidth = std::max(200.0f, ImGui::GetContentRegionAvail().x);
		const float previewSize = std::min(availableWidth, kMilkDropPreviewHeight);
		milkdropPreviewPlayer.draw(
			static_cast<int>(origin.x),
			static_cast<int>(origin.y),
			static_cast<int>(previewSize),
			static_cast<int>(previewSize));
		ImGui::Dummy(ImVec2(previewSize, previewSize));
		ImGui::EndChild();
	} else {
		ImGui::TextDisabled("Generate or preview a preset to see the live projectM output.");
	}
#else
	ImGui::TextDisabled(
		"Direct projectM preview becomes available after regenerating the example with ofxProjectM on the include path.");
#endif
}
