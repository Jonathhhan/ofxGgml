#pragma once

#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <string>
#endif

// Normalize executable path for comparison (Windows-specific)
#ifdef _WIN32
std::string normalizeExecutablePathForCompare(const std::string & path);

// Convert wide string to UTF-8 (Windows-specific)
std::string utf8FromWide(const std::wstring & text);

// Terminate addon llama-server processes (Windows-specific)
bool terminateAddonLlamaServerProcesses(const std::string & serverExePath);
#else
// Stub for non-Windows platforms
bool terminateAddonLlamaServerProcesses(const std::string & serverExePath);
#endif

// Probe for server executable from candidate paths
std::string probeServerExecutable(
	const std::vector<std::filesystem::path> & candidates);
