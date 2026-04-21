#include "PerformancePanel.h"
#include "core/ofxGgmlMetrics.h"

#include <cinttypes>

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
	ImGui::SetNextWindowSize(ImVec2(520, 380), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Performance & Telemetry", &showWindow)) {
		const char * backendLabel = lastBackendUsed.empty() ? "(none)" : lastBackendUsed.c_str();
		auto labelValueRow = [](const char * label, const std::string & value) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", label);
			ImGui::TableNextColumn();
			ImGui::Text("%s", value.c_str());
		};

		const ImGuiTableFlags kvFlags = ImGuiTableFlags_SizingStretchProp |
			ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;

		// Quick summary
		if (ImGui::BeginTable("Summary", 3, ImGuiTableFlags_SizingStretchProp |
				ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
				ImGuiTableFlags_NoSavedSettings)) {
			ImGui::TableSetupColumn("Elapsed");
			ImGui::TableSetupColumn("Nodes");
			ImGui::TableSetupColumn("Backend");
			ImGui::TableHeadersRow();

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("%.2f ms", lastComputeMs);
			ImGui::TableNextColumn();
			ImGui::Text("%d", lastNodeCount);
			ImGui::TableNextColumn();
			ImGui::Text("%s", backendLabel);

			ImGui::EndTable();
		}

		ImGui::Spacing();

		// Config + sampling side by side.
		ImGui::Columns(2, "PerfColumns", false);
		ImGui::SetColumnWidth(0, ImGui::GetContentRegionAvail().x * 0.55f);

		ImGui::Text("Configuration");
		ImGui::Separator();
		if (ImGui::BeginTable("ConfigTable", 2, kvFlags)) {
			std::string prefLabel = "(none)";
			if (selectedBackendIndex >= 0 &&
				selectedBackendIndex < static_cast<int>(backendNames.size())) {
				prefLabel = backendNames[static_cast<size_t>(selectedBackendIndex)];
			}
			labelValueRow("Preference", prefLabel);
			labelValueRow("Threads", ofToString(numThreads));
			labelValueRow("Context", ofToString(contextSize));
			labelValueRow("Batch", ofToString(batchSize));
			labelValueRow("Text path",
				textInferenceBackend == TextInferenceBackend::LlamaServer
					? "llama-server"
					: "CLI");
			if (detectedModelLayers > 0) {
				labelValueRow("GPU layers",
					ofToString(gpuLayers) + " / " + ofToString(detectedModelLayers));
			} else {
				labelValueRow("GPU layers", ofToString(gpuLayers));
			}
			labelValueRow("Seed", seed < 0 ? "random" : ofToString(seed));
			ImGui::EndTable();
		}

		ImGui::NextColumn();

		ImGui::Text("Sampling");
		ImGui::Separator();
		if (ImGui::BeginTable("SamplingTable", 2, kvFlags)) {
			labelValueRow("Tokens", ofToString(maxTokens));
			labelValueRow("Temperature", ofToString(temperature, 2));
			labelValueRow("Top-P", ofToString(topP, 2));
			labelValueRow("Top-K", ofToString(topK));
			labelValueRow("Min-P", ofToString(minP, 2));
			labelValueRow("Repeat penalty", ofToString(repeatPenalty, 2));
			ImGui::EndTable();
		}

		ImGui::Columns(1);

		ImGui::Spacing();
		ImGui::Separator();

		// Device memory summary.
		ImGui::Text("Devices");
		ImGui::SameLine();
		if (ImGui::SmallButton("Refresh")) {
			devicesOut = ggml.listDevices();
		}
		if (devices.empty()) {
			ImGui::TextDisabled("No devices discovered.");
		} else if (ImGui::BeginTable("DevicesTable", 3,
			ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Borders | ImGuiTableFlags_NoSavedSettings)) {
			ImGui::TableSetupColumn("Name");
			ImGui::TableSetupColumn("Type");
			ImGui::TableSetupColumn("Memory");
			ImGui::TableHeadersRow();

			for (const auto & d : devices) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%s", d.name.c_str());
				ImGui::TableNextColumn();
				ImGui::Text("%s", ofxGgmlHelpers::backendTypeName(d.type).c_str());
				ImGui::TableNextColumn();
				if (d.memoryTotal > 0) {
					const float usedPct = 1.0f - static_cast<float>(d.memoryFree) /
						static_cast<float>(d.memoryTotal);
					const std::string memoryLabel =
						ofxGgmlHelpers::formatBytes(d.memoryFree) + " / " +
						ofxGgmlHelpers::formatBytes(d.memoryTotal);
					ImGui::ProgressBar(usedPct, ImVec2(-FLT_MIN, 0.0f), memoryLabel.c_str());
				} else {
					ImGui::TextDisabled("n/a");
				}
			}
			ImGui::EndTable();
		}

		// Streaming telemetry
		const auto streamStats = ofxGgmlMetrics::getInstance().getStreamStats();
		if (!streamStats.empty()) {
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Text("Streaming Telemetry");
			if (ImGui::BeginTable("StreamingTable", 4,
				ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
					ImGuiTableFlags_Borders | ImGuiTableFlags_NoSavedSettings)) {
				ImGui::TableSetupColumn("Transport");
				ImGui::TableSetupColumn("Chunks", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("Data");
				ImGui::TableSetupColumn("Cancelled", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableHeadersRow();

				for (const auto & entry : streamStats) {
					const auto & name = entry.first;
					const auto & agg = entry.second;

					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text("%s", name.c_str());
					ImGui::TableNextColumn();
					ImGui::Text("%" PRIu64, static_cast<uint64_t>(agg.chunks));
					ImGui::TableNextColumn();
					ImGui::Text("%s", ofxGgmlHelpers::formatBytes(agg.bytes).c_str());
					ImGui::TableNextColumn();
					ImGui::Text("%" PRIu64, static_cast<uint64_t>(agg.cancelled));
				}
				ImGui::EndTable();
			}
		}
	}
	ImGui::End();
}
