#pragma once

#include "ofMain.h"
#include "ofxImGui.h"

#include <string>

// ---------------------------------------------------------------------------
// ImGui UI Helper Functions
// ---------------------------------------------------------------------------

void drawPanelHeader(const char * title, const char * subtitle);
void drawWrappedDisabledText(const std::string & text);
void drawHelpMarker(const char * helpText);
void showWrappedTooltip(const std::string & tooltipText);
void showWrappedTooltipf(const char * fmt, ...);
void drawSectionSeparator();
bool drawDisabledButton(const char * label, bool disabled, const ImVec2 & size = ImVec2(0, 0));

// ---------------------------------------------------------------------------
// Log Level Support
// ---------------------------------------------------------------------------

struct LogLevelOption {
	const char * label;
	ofLogLevel level;
};

extern const std::array<LogLevelOption, 5> kLogLevelOptions;

const char * logLevelLabel(ofLogLevel level) noexcept;
int logLevelIndex(ofLogLevel level) noexcept;

// ---------------------------------------------------------------------------
// String Utilities
// ---------------------------------------------------------------------------

std::string stripAnsi(const std::string & text);
std::string stripLiteralAnsiMarkers(const std::string & text);
std::string trim(const std::string & s);
std::string trimLogMessage(const std::string & s);

// ---------------------------------------------------------------------------
// Environment and System Utilities
// ---------------------------------------------------------------------------

std::string getEnvVarString(const char * name);
void copyStringToBuffer(char * buffer, size_t bufferSize, const std::string & value);
void setVulkanRuntimeDisabled(bool disabled);
bool shouldDisableVulkanForCurrentSelection(
	const std::vector<std::string> & names,
	int selectedIndex);
