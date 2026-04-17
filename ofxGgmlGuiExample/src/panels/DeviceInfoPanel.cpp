#include "DeviceInfoPanel.h"

void DeviceInfoPanel::draw(
	bool & showWindow,
	ofxGgml & ggml,
	const std::vector<ofxGgmlDeviceInfo> & devices)
{
	ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Device Info", &showWindow)) {
		ImGui::Text("Backend: %s", ggml.getBackendName().c_str());
		ImGui::Text("State: %s", ofxGgmlHelpers::stateName(ggml.getState()).c_str());
		ImGui::Separator();

		if (devices.empty()) {
			ImGui::TextDisabled("No devices discovered.");
		} else {
			for (size_t i = 0; i < devices.size(); i++) {
				const auto & d = devices[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::Text("%s", d.name.c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("(%s)", d.description.c_str());
				ImGui::Text("  Type: %s  Memory: %s / %s",
					ofxGgmlHelpers::backendTypeName(d.type).c_str(),
					ofxGgmlHelpers::formatBytes(d.memoryFree).c_str(),
					ofxGgmlHelpers::formatBytes(d.memoryTotal).c_str());
				ImGui::Separator();
				ImGui::PopID();
			}
		}
	}
	ImGui::End();
}
