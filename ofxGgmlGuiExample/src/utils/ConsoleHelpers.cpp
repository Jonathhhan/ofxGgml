#include "ConsoleHelpers.h"
#include "ImGuiHelpers.h"
#include "ofxGgmlInference.h"

#include <cctype>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <cstdio>
#endif

bool gConsoleAnsiEnabled = false;

std::string formatConsoleLogText(const std::string & text, bool chatLike) {
	std::string out = stripLiteralAnsiMarkers(stripAnsi(text));
	if (chatLike) {
		out = ofxGgmlInference::sanitizeGeneratedText(out);
	}

	std::string flattened;
	flattened.reserve(out.size());
	bool lastWasSpace = false;
	for (unsigned char ch : out) {
		if (ch == '\r' || ch == '\n' || ch == '\t') {
			if (!flattened.empty() && !lastWasSpace) {
				flattened.push_back(' ');
				lastWasSpace = true;
			}
			continue;
		}
		if (std::iscntrl(ch)) {
			continue;
		}
		flattened.push_back(static_cast<char>(ch));
		lastWasSpace = (ch == ' ');
	}

	return trim(flattened);
}

bool enableConsoleAnsiFormatting() {
#ifdef _WIN32
	HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
	if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
		return false;
	}
	DWORD mode = 0;
	if (!GetConsoleMode(handle, &mode)) {
		return false;
	}
	if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) {
		return true;
	}
	return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
	return ::isatty(fileno(stderr)) != 0;
#endif
}

std::string styleConsoleText(const std::string & text, const char * ansiCode) {
	if (!gConsoleAnsiEnabled || text.empty() || ansiCode == nullptr || *ansiCode == '\0') {
		return text;
	}
	return std::string("\x1b[") + ansiCode + "m" + text + "\x1b[0m";
}

const char * consoleRoleAnsiCode(const std::string & role) {
	if (role == "user" || role == "You") return "1;32";
	if (role == "assistant" || role == "AI") return "1;36";
	if (role == "system" || role == "System") return "1;90";
	return "1;37";
}

const char * consoleChannelAnsiCode(const std::string & channel) {
	if (channel == "ChatWindow") return "90";
	if (channel == "Script") return "95";
	if (channel == "Summarize") return "94";
	if (channel == "Write") return "35";
	if (channel == "Translate") return "96";
	if (channel == "Custom") return "37";
	if (channel == "Vision") return "93";
	if (channel == "Speech") return "92";
	return "90";
}

const char * consoleBodyAnsiCode(const std::string & text) {
	const std::string trimmed = trim(text);
	if (trimmed.rfind("[Error]", 0) == 0) return "1;31";
	if (trimmed.rfind("[warning]", 0) == 0 || trimmed.rfind("[warn]", 0) == 0) return "33";
	return "";
}

std::string formatConsoleLogLine(
	const std::string & channel,
	const std::string & roleLabel,
	const std::string & text,
	bool chatLike) {
	const std::string formattedText = formatConsoleLogText(text, chatLike);
	const std::string prefix = "[" + channel + "]";
	const std::string role = roleLabel + ":";
	const std::string styledPrefix = styleConsoleText(prefix, consoleChannelAnsiCode(channel));
	const std::string styledRole = styleConsoleText(role, consoleRoleAnsiCode(roleLabel));
	const std::string styledText = styleConsoleText(formattedText, consoleBodyAnsiCode(formattedText));
	return styledPrefix + " " + styledRole + " " + styledText;
}
