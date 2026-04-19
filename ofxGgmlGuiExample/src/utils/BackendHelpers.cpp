#include "BackendHelpers.h"
#include "ImGuiHelpers.h"
#include "ServerHelpers.h"
#include "../config/ModelPresets.h"
#include "../ofApp.h"

#include <algorithm>
#include <regex>
#include <string>

std::pair<std::string, int> parseServerHostPort(const std::string & configuredUrl) {
	const std::string baseUrl = serverBaseUrlFromConfiguredUrl(configuredUrl);
	static const std::regex hostPortRe(R"(^https?://([^/:]+)(?::(\d+))?.*$)", std::regex::icase);
	std::smatch match;
	if (std::regex_match(baseUrl, match, hostPortRe)) {
		const std::string host = match[1].str();
		const int port = (match.size() >= 3 && match[2].matched)
			? std::max(1, std::stoi(match[2].str()))
			: 8080;
		return {host, port};
	}
	return {"127.0.0.1", 8080};
}

std::pair<std::string, int> parseSpeechServerHostPort(const std::string & configuredUrl) {
	const std::string baseUrl = speechServerBaseUrlFromConfiguredUrl(configuredUrl);
	static const std::regex hostPortRe(R"(^https?://([^/:]+)(?::(\d+))?.*$)", std::regex::icase);
	std::smatch match;
	if (std::regex_match(baseUrl, match, hostPortRe)) {
		const std::string host = match[1].str();
		const int port = (match.size() >= 3 && match[2].matched)
			? std::max(1, std::stoi(match[2].str()))
			: 8081;
		return {host, port};
	}
	return {"127.0.0.1", 8081};
}

bool shouldManageLocalTextServer(const std::string & configuredUrl) {
	const auto [host, port] = parseServerHostPort(configuredUrl);
	(void)port;
	std::string normalizedHost = trim(host);
	std::transform(
		normalizedHost.begin(),
		normalizedHost.end(),
		normalizedHost.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return normalizedHost.empty() ||
		normalizedHost == "127.0.0.1" ||
		normalizedHost == "localhost";
}

bool shouldManageLocalSpeechServer(const std::string & configuredUrl) {
	if (trim(configuredUrl).empty()) {
		return false;
	}
	const auto [host, port] = parseSpeechServerHostPort(configuredUrl);
	(void)port;
	std::string normalizedHost = trim(host);
	std::transform(
		normalizedHost.begin(),
		normalizedHost.end(),
		normalizedHost.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return normalizedHost.empty() ||
		normalizedHost == "127.0.0.1" ||
		normalizedHost == "localhost";
}

bool aiModeSupportsTextBackend(AiMode mode) {
	switch (mode) {
	case AiMode::Chat:
	case AiMode::Script:
	case AiMode::Summarize:
	case AiMode::Write:
	case AiMode::Translate:
	case AiMode::Custom:
	case AiMode::VideoEssay:
	case AiMode::MilkDrop:
		return true;
	case AiMode::Vision:
	case AiMode::Speech:
	case AiMode::Diffusion:
	case AiMode::Clip:
		return false;
	}
	return false;
}

TextInferenceBackend clampTextInferenceBackend(int value) {
	return value == static_cast<int>(TextInferenceBackend::LlamaServer)
		? TextInferenceBackend::LlamaServer
		: TextInferenceBackend::Cli;
}

std::string describeExitCode(int code) {
#ifdef _WIN32
	switch (static_cast<unsigned int>(code)) {
	case 0xC0000409: return "stack buffer overrun (0xC0000409)";
	case 0xC0000005: return "access violation (0xC0000005)";
	case 0xC00000FD: return "stack overflow (0xC00000FD)";
	case 0xC0000135: return "DLL not found (0xC0000135)";
	case 0xC000001D: return "illegal instruction (0xC000001D) — CPU may not support required features";
	case 0xC0000374: return "heap corruption (0xC0000374)";
	default: break;
	}
	// DWORD exit codes are stored as a signed int; match negative
	// representations of the same NTSTATUS values.
	switch (code) {
	case -1073740791: return "stack buffer overrun (0xC0000409)";
	case -1073741819: return "access violation (0xC0000005)";
	case -1073741571: return "stack overflow (0xC00000FD)";
	case -1073741515: return "DLL not found (0xC0000135)";
	case -1073741795: return "illegal instruction (0xC000001D) — CPU may not support required features";
	case -1073741676: return "heap corruption (0xC0000374)";
	default: break;
	}
#endif
	if (code >= 129 && code <= 159) {
		int sig = code - 128;
		return "killed by signal " + std::to_string(sig);
	}
	return "";
}
