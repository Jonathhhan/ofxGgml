#include "ofApp.h"

#include "ImHelpers.h"
#include "utils/ImGuiHelpers.h"
#include "utils/PathHelpers.h"
#include "utils/ConsoleHelpers.h"

#include <algorithm>
#include <filesystem>

namespace {

constexpr float kMilkDropPreviewHeight = 260.0f;
constexpr float kMilkDropWaitingDotsAnimationSpeed = 3.0f;
const char * const kMilkDropWaitingLabels[] = {
	"generating",
	"generating.",
	"generating..",
	"generating..."
};

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
		milkdropPreviewPlayer.setPresetDuration(24.0);
		milkdropPreviewPlayer.setBeatSensitivity(1.25f);
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

void ofApp::runMilkDropGeneration(bool editExisting) {
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

	if (editExisting && trim(milkdropOutput).empty()) {
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
	request.existingPresetText = editExisting ? milkdropOutput : std::string();
	request.presetNameHint = ofxGgmlMilkDropGenerator::makeSuggestedFileName(prompt, category);

	cancelRequested.store(false);
	generating.store(true);
	activeGenerationMode = AiMode::MilkDrop;
	streamingOutput.clear();
	generationStartTime = ofGetElapsedTimef();
	generatingStatus = editExisting
		? "Editing MilkDrop preset..."
		: "Generating MilkDrop preset...";
	if (workerThread.joinable()) {
		workerThread.join();
	}

	workerThread = std::thread([this, modelPath, request, settings]() mutable {
		auto streamCallback = [this](const std::string & delta) -> bool {
			std::lock_guard<std::mutex> lock(streamMutex);
			streamingOutput += delta;
			return !cancelRequested.load();
		};

		const ofxGgmlMilkDropResult result = milkdropGenerator.generatePreset(
			modelPath,
			request,
			settings,
			streamCallback);

		std::string outputText;
		if (result.success) {
			outputText = result.presetText;
		} else {
			outputText = "[Error] " + (!result.error.empty()
				? result.error
				: result.inference.error.empty()
					? std::string("MilkDrop generation failed.")
					: result.inference.error);
		}

		{
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingMode = AiMode::MilkDrop;
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
	ImGui::Checkbox("Auto preview after generation", &milkdropAutoPreview);

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
	if (ImGui::Button("Edit Current", ImVec2(120, 0))) {
		runMilkDropGeneration(true);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(trim(milkdropOutput).empty());
	if (ImGui::Button("Save .milk", ImVec2(100, 0))) {
		saveMilkDropPresetToConfiguredPath();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(milkdropOutput).empty());
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

	ImGui::Separator();
	ImGui::Text("Generated preset:");
	drawMilkDropOutputChild(
		milkdropOutput,
		generating.load() && activeGenerationMode == AiMode::MilkDrop,
		streamMutex,
		streamingOutput);

	ImGui::Separator();
	ImGui::Text("projectM preview:");
#if OFXGGML_HAS_OFXPROJECTM
	if (!milkdropPreviewError.empty()) {
		ImGui::TextColored(
			ImVec4(0.95f, 0.45f, 0.35f, 1.0f),
			"%s",
			milkdropPreviewError.c_str());
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
