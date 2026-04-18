#include "StatusBar.h"
#include "ofxGgmlChatAssistant.h"
#include "ofxGgmlCodeAssistant.h"

// Enums from ofApp.h
enum class LiveContextMode {
	Offline = 0,
	LoadedSourcesOnly,
	LiveContext,
	LiveContextStrictCitations
};

void StatusBar::draw(
	const std::string & engineStatus,
	const std::vector<ModelPreset> & modelPresets,
	int selectedModelIndex,
	AiMode activeMode,
	const char * const * modeLabels,
	int chatLanguageIndex,
	const std::vector<ofxGgmlChatLanguageOption> & chatLanguages,
	int selectedLanguageIndex,
	const std::vector<ofxGgmlCodeLanguagePreset> & scriptLanguages,
	int maxTokens,
	float temperature,
	float topP,
	int topK,
	float minP,
	LiveContextMode liveContextMode,
	int gpuLayers,
	int detectedModelLayers,
	const std::atomic<bool> & generating,
	float generationStartTime,
	const std::string & streamingOutput,
	std::mutex & streamMutex,
	float lastComputeMs)
{
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoScrollbar;

	if (ImGui::Begin("##StatusBar", nullptr, flags)) {
		ImGui::Text("Engine: %s", engineStatus.c_str());
		ImGui::SameLine();
		if (!modelPresets.empty()) {
			ImGui::Text(" | Model: %s", modelPresets[static_cast<size_t>(selectedModelIndex)].name.c_str());
			ImGui::SameLine();
		}
		ImGui::Text(" | Mode: %s", modeLabels[static_cast<int>(activeMode)]);
		if (activeMode == AiMode::Chat &&
			chatLanguageIndex > 0 &&
			chatLanguageIndex < static_cast<int>(chatLanguages.size())) {
			ImGui::SameLine();
			ImGui::Text(" | Chat Lang: %s",
				chatLanguages[static_cast<size_t>(chatLanguageIndex)].name.c_str());
		}
		if (activeMode == AiMode::Script && !scriptLanguages.empty()) {
			ImGui::SameLine();
			ImGui::Text(" | Lang: %s", scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].name.c_str());
		}
		ImGui::SameLine();
		ImGui::Text(" | Tokens: %d  Temp: %.2f  Top-P: %.2f  Top-K: %d  Min-P: %.2f",
			maxTokens, temperature, topP, topK, minP);
		if (liveContextMode == LiveContextMode::Offline) {
			ImGui::SameLine();
			ImGui::TextDisabled(" | Offline");
		} else if (liveContextMode == LiveContextMode::LoadedSourcesOnly) {
			ImGui::SameLine();
			ImGui::TextDisabled(" | LoadedSourcesOnly");
		} else if (liveContextMode == LiveContextMode::LiveContextStrictCitations) {
			ImGui::SameLine();
			ImGui::TextDisabled(" | LiveContextStrictCitations");
		} else {
			ImGui::SameLine();
			ImGui::TextDisabled(" | LiveContext");
		}
		if (gpuLayers > 0) {
			ImGui::SameLine();
			if (detectedModelLayers > 0) {
				ImGui::Text(" | GPU: %d/%d layers", gpuLayers, detectedModelLayers);
			} else {
				ImGui::Text(" | GPU: %d layers", gpuLayers);
			}
		}
		if (generating.load()) {
			ImGui::SameLine();
			const char * spinner = "|/-\\";
			int spinIdx = static_cast<int>(ImGui::GetTime() / kSpinnerInterval) % 4;
			float elapsed = ofGetElapsedTimef() - generationStartTime;
			char statusLabel[64];
			snprintf(statusLabel, sizeof(statusLabel), " | %c Generating... (%.1fs)", spinner[spinIdx], elapsed);
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", statusLabel);
			std::string partial;
			{
				std::lock_guard<std::mutex> lock(streamMutex);
				partial = streamingOutput;
			}
			if (elapsed > 0.2f && !partial.empty()) {
				const float cps = static_cast<float>(partial.size()) / elapsed;
				ImGui::SameLine();
				ImGui::TextDisabled(" | %.0f chars/s", cps);
			}
		} else if (lastComputeMs > 0.0f) {
			ImGui::SameLine();
			ImGui::TextDisabled(" | Last: %.1f ms", lastComputeMs);
		}
		ImGui::SameLine();
		ImGui::Text(" | FPS: %.0f", ofGetFrameRate());
	}
	ImGui::End();
}
