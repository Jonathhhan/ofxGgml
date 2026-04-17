#pragma once

#include <string>

// Enable/disable ANSI formatting (set by setup)
extern bool gConsoleAnsiEnabled;

// Format console log text (remove ANSI, flatten whitespace)
std::string formatConsoleLogText(const std::string & text, bool chatLike = false);

// Enable console ANSI formatting support
bool enableConsoleAnsiFormatting();

// Style text with ANSI code (if enabled)
std::string styleConsoleText(const std::string & text, const char * ansiCode);

// Get ANSI code for role
const char * consoleRoleAnsiCode(const std::string & role);

// Get ANSI code for channel
const char * consoleChannelAnsiCode(const std::string & channel);

// Get ANSI code for body text
const char * consoleBodyAnsiCode(const std::string & text);

// Format complete console log line with ANSI styling
std::string formatConsoleLogLine(
	const std::string & channel,
	const std::string & roleLabel,
	const std::string & text,
	bool chatLike = false);
