#include "ImGuiHelpers.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdlib>
#endif

// ---------------------------------------------------------------------------
// ImGui UI Helper Functions
// ---------------------------------------------------------------------------

void drawPanelHeader(const char * title, const char * subtitle) {
	ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", title);
	ImGui::SameLine();
	ImGui::TextDisabled("(%s)", subtitle);
	ImGui::Separator();
}

void drawWrappedDisabledText(const std::string & text) {
	if (text.empty()) {
		return;
	}
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextDisabled("%s", text.c_str());
	ImGui::PopTextWrapPos();
}

void drawHelpMarker(const char * helpText) {
	if (!helpText || *helpText == '\0') {
		return;
	}
	ImGui::SameLine();
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered()) {
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
		ImGui::TextUnformatted(helpText);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

void showWrappedTooltip(const std::string & tooltipText) {
	if (tooltipText.empty()) {
		return;
	}
	ImGui::BeginTooltip();
	ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
	ImGui::TextUnformatted(tooltipText.c_str());
	ImGui::PopTextWrapPos();
	ImGui::EndTooltip();
}

void showWrappedTooltipf(const char * fmt, ...) {
	if (!fmt || *fmt == '\0') {
		return;
	}
	va_list args;
	va_start(args, fmt);
	va_list argsCopy;
	va_copy(argsCopy, args);
	const int required = std::vsnprintf(nullptr, 0, fmt, args);
	va_end(args);
	if (required <= 0) {
		va_end(argsCopy);
		return;
	}
	std::string buffer(static_cast<size_t>(required), '\0');
	std::vsnprintf(buffer.data(), static_cast<size_t>(required) + 1, fmt, argsCopy);
	va_end(argsCopy);
	showWrappedTooltip(buffer);
}

void drawSectionSeparator() {
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
}

// ---------------------------------------------------------------------------
// Log Level Support
// ---------------------------------------------------------------------------

const std::array<LogLevelOption, 5> kLogLevelOptions = {{
	{"Silent",   OF_LOG_SILENT},
	{"Errors",   OF_LOG_ERROR},
	{"Warnings", OF_LOG_WARNING},
	{"Notices",  OF_LOG_NOTICE},
	{"Verbose",  OF_LOG_VERBOSE}
}};

const char * logLevelLabel(ofLogLevel level) noexcept {
	switch (level) {
	case OF_LOG_VERBOSE:      return "verbose";
	case OF_LOG_NOTICE:       return "notice";
	case OF_LOG_WARNING:      return "warn";
	case OF_LOG_ERROR:        return "error";
	case OF_LOG_FATAL_ERROR:  return "fatal";
	case OF_LOG_SILENT:       return "silent";
	default:                  return "log";
	}
}

int logLevelIndex(ofLogLevel level) noexcept {
	if (level == OF_LOG_FATAL_ERROR) {
		return 1; // treat fatal as errors for UI selection
	}
	for (size_t i = 0; i < kLogLevelOptions.size(); i++) {
		if (kLogLevelOptions[i].level == level) {
			return static_cast<int>(i);
		}
	}
	// Default to "Notices" when the level isn't found.
	return 3;
}

// ---------------------------------------------------------------------------
// String Utilities
// ---------------------------------------------------------------------------

std::string stripAnsi(const std::string & text) {
	std::string out;
	out.reserve(text.size());
	size_t i = 0;
	while (i < text.size()) {
		if (text[i] == '\x1b') {
			i++; // skip ESC
			if (i < text.size() && text[i] == '[') {
				// CSI sequence: ESC [ <params> <final_byte>
				// Skip parameter bytes (0x30-0x3F) and intermediate
				// bytes (0x20-0x2F), then the final byte (0x40-0x7E).
				i++; // skip '['
				while (i < text.size()
					&& static_cast<unsigned char>(text[i]) < 0x40) {
					i++;
				}
				if (i < text.size()) i++; // skip the final byte (e.g. 'm')
			} else if (i < text.size()) {
				i++; // two-byte escape sequence (e.g. ESC =), skip second byte
			}
		} else {
			out += text[i];
			i++;
		}
	}
	return out;
}

std::string stripLiteralAnsiMarkers(const std::string & text) {
	static const std::regex markerRegex(R"(\[(?:\d{1,3})(?:;\d{1,3})*m)");
	return std::regex_replace(text, markerRegex, "");
}

std::string trim(const std::string & s) {
	size_t start = 0;
	while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
		start++;
	}
	size_t end = s.size();
	while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
		end--;
	}
	return s.substr(start, end - start);
}

std::string trimLogMessage(const std::string & s) {
	if (s.empty()) {
		return {};
	}
	size_t start = 0;
	while (start < s.size() && (s[start] == '\r' || s[start] == '\n')) {
		start++;
	}
	size_t end = s.size();
	while (end > start && (s[end - 1] == '\r' || s[end - 1] == '\n')) {
		end--;
	}
	return s.substr(start, end - start);
}

// ---------------------------------------------------------------------------
// Environment and System Utilities
// ---------------------------------------------------------------------------

std::string getEnvVarString(const char * name) {
	if (!name || *name == '\0') return {};
#ifdef _WIN32
	char * value = nullptr;
	size_t len = 0;
	const errno_t err = _dupenv_s(&value, &len, name);
	if (err != 0 || value == nullptr || len == 0) {
		free(value);
		return {};
	}
	std::string result(value);
	free(value);
	return result;
#else
	const char * value = std::getenv(name);
	return value ? std::string(value) : std::string();
#endif
}

void copyStringToBuffer(char * buffer, size_t bufferSize, const std::string & value) {
	if (!buffer || bufferSize == 0) return;
	const size_t copyLen = std::min(bufferSize - 1, value.size());
	std::memcpy(buffer, value.data(), copyLen);
	buffer[copyLen] = '\0';
}

void setVulkanRuntimeDisabled(bool disabled) {
#ifdef _WIN32
	_putenv_s("GGML_DISABLE_VULKAN", disabled ? "1" : "");
#else
	if (disabled) {
		setenv("GGML_DISABLE_VULKAN", "1", 1);
	} else {
		unsetenv("GGML_DISABLE_VULKAN");
	}
#endif
}

bool shouldDisableVulkanForCurrentSelection(
	const std::vector<std::string> & names,
	int selectedIndex) {
	if (getEnvVarString("OFXGGML_DISABLE_VULKAN") == "1") {
		return true;
	}
	if (selectedIndex >= 0 && selectedIndex < static_cast<int>(names.size())) {
		const std::string & sel = names[static_cast<size_t>(selectedIndex)];
		if (sel.rfind("CUDA", 0) == 0) {
			return true;
		}
	}
	return false;
}
