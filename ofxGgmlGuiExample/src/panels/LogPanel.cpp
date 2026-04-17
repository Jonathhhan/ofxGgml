#include "LogPanel.h"

void LogPanel::draw(
	bool & showWindow,
	std::deque<std::string> & logMessages,
	std::mutex & logMutex)
{
	ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Engine Log", &showWindow)) {
		if (ImGui::Button("Clear")) {
			std::lock_guard<std::mutex> lock(logMutex);
			logMessages.clear();
		}
		ImGui::Separator();
		ImGui::BeginChild("##LogScroll", ImVec2(0, 0), false);
		std::lock_guard<std::mutex> lock(logMutex);
		for (const auto & line : logMessages) {
			ImGui::TextWrapped("%s", line.c_str());
		}
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f) {
			ImGui::SetScrollHereY(1.0f);
		}
		ImGui::EndChild();
	}
	ImGui::End();
}
