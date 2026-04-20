#include "ServerHelpers.h"
#include "ImGuiHelpers.h"

// Default server URLs (matching ofApp.cpp constants)
static const char * const kDefaultTextServerUrl = "http://127.0.0.1:8080";
static const char * const kDefaultSpeechServerUrl = "http://127.0.0.1:8081";
static const char * const kDefaultAceStepServerUrl = "http://127.0.0.1:8085";

std::string serverBaseUrlFromConfiguredUrl(const std::string & configuredUrl) {
	std::string value = trim(configuredUrl);
	if (value.empty()) {
		return kDefaultTextServerUrl;
	}
	auto stripSuffix = [&](const std::string & suffix) {
		if (value.size() >= suffix.size() &&
			value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0) {
			value.erase(value.size() - suffix.size());
		}
	};
	stripSuffix("/v1/chat/completions");
	stripSuffix("/chat/completions");
	if (!value.empty() && value.back() == '/') {
		value.pop_back();
	}
	return value.empty() ? std::string(kDefaultTextServerUrl) : value;
}

std::string speechServerBaseUrlFromConfiguredUrl(const std::string & configuredUrl) {
	std::string value = trim(configuredUrl);
	if (value.empty()) {
		return kDefaultSpeechServerUrl;
	}
	auto stripSuffix = [&](const std::string & suffix) {
		if (value.size() >= suffix.size() &&
			value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0) {
			value.erase(value.size() - suffix.size());
		}
	};
	stripSuffix("/v1/audio/transcriptions");
	stripSuffix("/v1/audio/translations");
	stripSuffix("/audio/transcriptions");
	stripSuffix("/audio/translations");
	stripSuffix("/v1");
	if (!value.empty() && value.back() == '/') {
		value.pop_back();
	}
	return value.empty() ? std::string(kDefaultSpeechServerUrl) : value;
}

std::string aceStepServerBaseUrlFromConfiguredUrl(const std::string & configuredUrl) {
	std::string value = trim(configuredUrl);
	if (value.empty()) {
		return kDefaultAceStepServerUrl;
	}
	auto stripSuffix = [&](const std::string & suffix) {
		if (value.size() >= suffix.size() &&
			value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0) {
			value.erase(value.size() - suffix.size());
		}
	};
	stripSuffix("/health");
	stripSuffix("/props");
	stripSuffix("/lm");
	stripSuffix("/synth");
	stripSuffix("/understand");
	if (!value.empty() && value.back() == '/') {
		value.pop_back();
	}
	return value.empty() ? std::string(kDefaultAceStepServerUrl) : value;
}
