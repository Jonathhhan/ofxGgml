#include "PerformancePanel.h"

// TextInferenceBackend enum definition (from ofApp.h)
enum class TextInferenceBackend {
	Cli = 0,
	LlamaServer
};

void PerformancePanel::draw(
	bool & showWindow,
	ofxGgml & ggml,
	const std::vector<ofxGgmlDeviceInfo> & devices,
	float lastComputeMs,
	int lastNodeCount,
	const std::string & lastBackendUsed,
	int selectedBackendIndex,
	const std::vector<std::string> & backendNames,
	int numThreads,
	int contextSize,
	int batchSize,
	TextInferenceBackend textInferenceBackend,
	int detectedModelLayers,
	int gpuLayers,
	int seed,
	int maxTokens,
	float temperature,
	float topP,
	int topK,
	float minP,
	float repeatPenalty,
	std::vector<ofxGgmlDeviceInfo> & devicesOut)
{
	ImGui::SetNextWindowSize(ImVec2(380, 220), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Performance Metrics", &showWindow)) {
		ImGui::Text("Last Computation:");
		ImGui::Separator();
		ImGui::Text("  Elapsed:    %.2f ms", lastComputeMs);
		ImGui::Text("  Nodes:      %d", lastNodeCount);
		ImGui::Text("  Backend:    %s", lastBackendUsed.empty() ? "(none)" : lastBackendUsed.c_str());
		ImGui::Spacing();

		ImGui::Text("Configuration:");
		ImGui::Separator();
		{
			std::string prefLabel = "(none)";
			if (selectedBackendIndex >= 0 &&
				selectedBackendIndex < static_cast<int>(backendNames.size())) {
				prefLabel = backendNames[selectedBackendIndex];
			}
			ImGui::Text("  Preference: %s", prefLabel.c_str());
		}
		ImGui::Text("  Threads:    %d", numThreads);
		ImGui::Text("  Context:    %d", contextSize);
		ImGui::Text("  Batch:      %d", batchSize);
		ImGui::Text("  Text path:  %s",
			textInferenceBackend == TextInferenceBackend::LlamaServer
				? "llama-server"
				: "CLI");
		if (detectedModelLayers > 0) {
			ImGui::Text("  GPU Layers: %d / %d", gpuLayers, detectedModelLayers);
		} else {
			ImGui::Text("  GPU Layers: %d", gpuLayers);
		}
		ImGui::Text("  Seed:       %s", seed < 0 ? "random" : ofToString(seed).c_str());
		ImGui::Spacing();

		ImGui::Text("Sampling:");
		ImGui::Separator();
		ImGui::Text("  Tokens:     %d", maxTokens);
		ImGui::Text("  Temp:       %.2f", temperature);
		ImGui::Text("  Top-P:      %.2f", topP);
		ImGui::Text("  Top-K:      %d", topK);
		ImGui::Text("  Min-P:      %.2f", minP);
		ImGui::Text("  Repeat Pen: %.2f", repeatPenalty);
		ImGui::Spacing();

		// Device memory summary.
		if (!devices.empty()) {
			ImGui::Text("Devices:");
			ImGui::Separator();
			for (const auto & d : devices) {
				ImGui::Text("  %s (%s)", d.name.c_str(),
					ofxGgmlHelpers::backendTypeName(d.type).c_str());
				if (d.memoryTotal > 0) {
					float usedPct = 1.0f - static_cast<float>(d.memoryFree) /
						static_cast<float>(d.memoryTotal);
					ImGui::SameLine();
					ImGui::ProgressBar(usedPct, ImVec2(100, 14),
						ofxGgmlHelpers::formatBytes(d.memoryTotal).c_str());
				}
			}
		}

		if (ImGui::Button("Refresh Devices")) {
			devicesOut = ggml.listDevices();
		}
	}
	ImGui::End();
}
