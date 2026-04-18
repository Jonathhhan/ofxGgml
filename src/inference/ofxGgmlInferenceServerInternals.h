#pragma once

#include "ofxGgmlInference.h"

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ofxGgmlInferenceServerInternals {

inline std::string trimCopy(const std::string & s) {
	size_t b = 0;
	while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
		++b;
	}
	size_t e = s.size();
	while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
		--e;
	}
	return s.substr(b, e - b);
}

inline std::string normalizeServerUrl(const std::string & serverUrl) {
	std::string normalized = trimCopy(serverUrl);
	if (normalized.empty()) {
		return "http://127.0.0.1:8080/v1/chat/completions";
	}
	if (normalized.find("/v1/chat/completions") != std::string::npos) {
		return normalized;
	}
	if (!normalized.empty() && normalized.back() == '/') {
		normalized.pop_back();
	}
	if (normalized.find("/v1") == std::string::npos) {
		normalized += "/v1/chat/completions";
	} else {
		normalized += "/chat/completions";
	}
	return normalized;
}

inline std::string normalizeServerEmbeddingsUrl(const std::string & serverUrl) {
	std::string normalized = trimCopy(serverUrl);
	if (normalized.empty()) {
		return "http://127.0.0.1:8080/v1/embeddings";
	}
	if (normalized.find("/v1/embeddings") != std::string::npos) {
		return normalized;
	}
	if (normalized.find("/v1/chat/completions") != std::string::npos) {
		normalized.replace(
			normalized.find("/v1/chat/completions"),
			std::string("/v1/chat/completions").size(),
			"/v1/embeddings");
		return normalized;
	}
	if (normalized.find("/chat/completions") != std::string::npos) {
		normalized.replace(
			normalized.find("/chat/completions"),
			std::string("/chat/completions").size(),
			"/v1/embeddings");
		return normalized;
	}
	if (normalized.find("/embeddings") != std::string::npos) {
		return normalized;
	}
	if (!normalized.empty() && normalized.back() == '/') {
		normalized.pop_back();
	}
	if (normalized.find("/v1") == std::string::npos) {
		normalized += "/v1/embeddings";
	} else {
		normalized += "/embeddings";
	}
	return normalized;
}

inline std::string serverBaseUrlFromConfiguredUrl(const std::string & configuredUrl) {
	std::string value = trimCopy(configuredUrl);
	if (value.empty()) {
		return "http://127.0.0.1:8080";
	}
	auto stripSuffix = [&](const std::string & suffix) {
		if (value.size() >= suffix.size() &&
			value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0) {
			value.erase(value.size() - suffix.size());
		}
	};
	stripSuffix("/v1/chat/completions");
	stripSuffix("/chat/completions");
	stripSuffix("/v1/embeddings");
	stripSuffix("/embeddings");
	stripSuffix("/v1/models");
	stripSuffix("/models");
	if (!value.empty() && value.back() == '/') {
		value.pop_back();
	}
	return value.empty() ? std::string("http://127.0.0.1:8080") : value;
}

inline std::string normalizeServerModelsUrl(const std::string & serverUrl) {
	std::string baseUrl = serverBaseUrlFromConfiguredUrl(serverUrl);
	if (!baseUrl.empty() && baseUrl.back() == '/') {
		baseUrl.pop_back();
	}
	return baseUrl + "/v1/models";
}

inline std::vector<std::string> extractModelIdsFromServerResponse(
	const std::string & responseJson) {
	std::vector<std::string> ids;
	if (trimCopy(responseJson).empty()) {
		return ids;
	}
#ifdef OFXGGML_HEADLESS_STUBS
	return ids;
#else
	try {
		const ofJson parsed = ofJson::parse(responseJson);
		if (!parsed.contains("data") || !parsed["data"].is_array()) {
			return ids;
		}
		for (const auto & item : parsed["data"]) {
			if (!item.is_object()) {
				continue;
			}
			const std::string id = trimCopy(item.value("id", std::string()));
			if (!id.empty()) {
				ids.push_back(id);
			}
		}
	} catch (...) {
	}
	return ids;
#endif
}

inline bool detectVisionCapabilityFromServerResponse(const std::string & responseJson) {
	if (trimCopy(responseJson).empty()) {
		return false;
	}
#ifdef OFXGGML_HEADLESS_STUBS
	return false;
#else
	try {
		const ofJson parsed = ofJson::parse(responseJson);
		auto itemHasVisionCapability = [](const ofJson & item) {
			if (!item.is_object()) {
				return false;
			}
			if (item.contains("modalities") && item["modalities"].is_object()) {
				const auto & modalities = item["modalities"];
				if (modalities.value("vision", false) ||
					modalities.value("image", false) ||
					modalities.value("multimodal", false)) {
					return true;
				}
			}
			if (item.value("multimodal", false)) {
				return true;
			}
			if (item.contains("capabilities") && item["capabilities"].is_array()) {
				for (const auto & capability : item["capabilities"]) {
					if (!capability.is_string()) {
						continue;
					}
					const std::string name = capability.get<std::string>();
					if (name == "vision" || name == "image" || name == "multimodal") {
						return true;
					}
				}
			}
			return false;
		};

		if (parsed.contains("data") && parsed["data"].is_array()) {
			for (const auto & item : parsed["data"]) {
				if (itemHasVisionCapability(item)) {
					return true;
				}
			}
		}
		if (parsed.contains("models") && parsed["models"].is_array()) {
			for (const auto & item : parsed["models"]) {
				if (itemHasVisionCapability(item)) {
					return true;
				}
			}
		}
	} catch (...) {
	}
	return false;
#endif
}

inline std::string buildServerCapabilitySummary(const ofxGgmlServerProbeResult & probe) {
	if (!probe.reachable) {
		return {};
	}
	std::vector<std::string> parts;
	parts.emplace_back("text");
	if (probe.embeddingsRouteLikely) {
		parts.emplace_back("embeddings route");
	}
	if (probe.visionCapable) {
		parts.emplace_back("vision");
	}
	if (probe.routerLikely) {
		parts.emplace_back(
			"router (" + std::to_string(std::max<size_t>(probe.modelIds.size(), 1)) +
			" models)");
	} else if (probe.modelsOk) {
		parts.emplace_back("single-model");
	}

	std::ostringstream joined;
	for (size_t i = 0; i < parts.size(); ++i) {
		if (i > 0) {
			joined << " + ";
		}
		joined << parts[i];
	}
	return joined.str();
}

struct ServerModelCacheEntry {
	std::chrono::steady_clock::time_point checkedAt;
	std::string activeModel;
	bool reachable = false;
};

inline std::string resolveCachedActiveServerModel(const std::string & serverUrl) {
#ifdef OFXGGML_HEADLESS_STUBS
	(void) serverUrl;
	return {};
#else
	static std::mutex cacheMutex;
	static std::unordered_map<std::string, ServerModelCacheEntry> cache;
	const std::string baseUrl = serverBaseUrlFromConfiguredUrl(serverUrl);
	const auto now = std::chrono::steady_clock::now();
	{
		std::lock_guard<std::mutex> lock(cacheMutex);
		auto it = cache.find(baseUrl);
		if (it != cache.end() &&
			std::chrono::duration_cast<std::chrono::seconds>(now - it->second.checkedAt).count() <
				10) {
			return it->second.reachable ? it->second.activeModel : std::string();
		}
	}

	const ofxGgmlServerProbeResult probe = ofxGgmlInference::probeServer(serverUrl, true);
	{
		std::lock_guard<std::mutex> lock(cacheMutex);
		cache[baseUrl] = { now, probe.activeModel, probe.reachable };
	}
	return probe.reachable ? probe.activeModel : std::string();
#endif
}

inline std::string extractTextContentValue(const ofJson & value) {
	if (value.is_string()) {
		return value.get<std::string>();
	}
	if (!value.is_array()) {
		return {};
	}

	std::ostringstream joined;
	for (const auto & item : value) {
		if (item.is_string()) {
			if (joined.tellp() > 0) {
				joined << "\n";
			}
			joined << item.get<std::string>();
		} else if (item.is_object()) {
			const std::string type = item.value("type", std::string());
			const std::string text = item.value("text", std::string());
			if (!text.empty() &&
				(type.empty() || type == "text" || type == "output_text" || type == "content")) {
				if (joined.tellp() > 0) {
					joined << "\n";
				}
				joined << text;
			}
		}
	}
	return joined.str();
}

inline std::string extractTextFromOpenAiResponse(const std::string & responseJson) {
	if (trimCopy(responseJson).empty()) {
		return {};
	}

#ifdef OFXGGML_HEADLESS_STUBS
	return responseJson;
#else
	try {
		const ofJson parsed = ofJson::parse(responseJson);
		if (!parsed.contains("choices") || !parsed["choices"].is_array() ||
			parsed["choices"].empty()) {
			return {};
		}
		const auto & choice = parsed["choices"][0];
		if (choice.contains("message")) {
			const auto & message = choice["message"];
			if (message.contains("content")) {
				const std::string content = extractTextContentValue(message["content"]);
				if (!content.empty()) {
					return content;
				}
			}
		}
		if (choice.contains("text")) {
			const std::string text = extractTextContentValue(choice["text"]);
			if (!text.empty()) {
				return text;
			}
		}
		if (choice.contains("content")) {
			const std::string content = extractTextContentValue(choice["content"]);
			if (!content.empty()) {
				return content;
			}
		}
		if (parsed.contains("content")) {
			const std::string content = extractTextContentValue(parsed["content"]);
			if (!content.empty()) {
				return content;
			}
		}
	} catch (...) {
	}
	return {};
#endif
}

inline std::string extractDeltaTextFromOpenAiStreamEvent(const std::string & eventJson) {
	if (trimCopy(eventJson).empty()) {
		return {};
	}

#ifdef OFXGGML_HEADLESS_STUBS
	return eventJson;
#else
	try {
		const ofJson parsed = ofJson::parse(eventJson);
		if (!parsed.contains("choices") || !parsed["choices"].is_array() ||
			parsed["choices"].empty()) {
			return {};
		}
		const auto & choice = parsed["choices"][0];
		if (choice.contains("delta")) {
			const auto & delta = choice["delta"];
			if (delta.contains("content")) {
				const std::string content = extractTextContentValue(delta["content"]);
				if (!content.empty()) {
					return content;
				}
			}
		}
		if (choice.contains("text") && choice["text"].is_string()) {
			return choice["text"].get<std::string>();
		}
		if (choice.contains("content")) {
			const std::string content = extractTextContentValue(choice["content"]);
			if (!content.empty()) {
				return content;
			}
		}
		if (parsed.contains("content")) {
			const std::string content = extractTextContentValue(parsed["content"]);
			if (!content.empty()) {
				return content;
			}
		}
	} catch (...) {
	}
	return {};
#endif
}

} // namespace ofxGgmlInferenceServerInternals
