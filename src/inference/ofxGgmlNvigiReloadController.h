#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#ifndef OFXGGML_ENABLE_NVIGI
#define OFXGGML_ENABLE_NVIGI 0
#endif

enum class ofxGgmlNvigiReloadAction {
	Load = 0,
	Unload,
	Reload,
	Refresh
};

struct ofxGgmlNvigiReloadRequest {
	ofxGgmlNvigiReloadAction action = ofxGgmlNvigiReloadAction::Reload;
	std::string componentId;
	std::string pluginId;
	std::string modelId;
	std::string modelPath;
	std::string device;
	bool preserveSession = true;
};

struct ofxGgmlNvigiReloadResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string error;
	ofxGgmlNvigiReloadAction action = ofxGgmlNvigiReloadAction::Reload;
	std::string componentId;
	std::string pluginId;
	std::string modelId;
	std::vector<std::pair<std::string, std::string>> metadata;
};

class ofxGgmlNvigiReloadController {
public:
	using ReloadFunction = std::function<ofxGgmlNvigiReloadResult(
		const ofxGgmlNvigiReloadRequest &)>;

	explicit ofxGgmlNvigiReloadController(
		ReloadFunction reloadFunction = {},
		std::string displayName = "NVIGI Reload")
		: m_reloadFunction(std::move(reloadFunction))
		, m_displayName(std::move(displayName)) {
	}

	void setReloadFunction(ReloadFunction reloadFunction) {
		m_reloadFunction = std::move(reloadFunction);
	}

	bool isConfigured() const {
		return static_cast<bool>(m_reloadFunction);
	}

	static bool isSdkEnabled() {
		return OFXGGML_ENABLE_NVIGI != 0;
	}

	std::string controllerName() const {
		return m_displayName.empty() ? "NVIGI Reload" : m_displayName;
	}

	static const char * actionLabel(ofxGgmlNvigiReloadAction action) {
		switch (action) {
		case ofxGgmlNvigiReloadAction::Load: return "Load";
		case ofxGgmlNvigiReloadAction::Unload: return "Unload";
		case ofxGgmlNvigiReloadAction::Reload: return "Reload";
		case ofxGgmlNvigiReloadAction::Refresh: return "Refresh";
		}
		return "Reload";
	}

	ofxGgmlNvigiReloadResult execute(
		const ofxGgmlNvigiReloadRequest & request) const {
		ofxGgmlNvigiReloadResult result;
		result.action = request.action;
		result.componentId = request.componentId;
		result.pluginId = request.pluginId;
		result.modelId = request.modelId;
		if (!isSdkEnabled()) {
			result.error =
				"NVIGI reload is disabled. Define OFXGGML_ENABLE_NVIGI=1 and "
				"attach an NVIGI SDK reload callback before calling execute().";
			return result;
		}
		if (!m_reloadFunction) {
			result.error =
				"NVIGI reload controller is not configured yet.";
			return result;
		}

		const auto started = std::chrono::steady_clock::now();
		result = m_reloadFunction(request);
		if (result.elapsedMs <= 0.0f) {
			result.elapsedMs = std::chrono::duration<float, std::milli>(
				std::chrono::steady_clock::now() - started).count();
		}
		if (result.componentId.empty()) {
			result.componentId = request.componentId;
		}
		if (result.pluginId.empty()) {
			result.pluginId = request.pluginId;
		}
		if (result.modelId.empty()) {
			result.modelId = request.modelId;
		}
		result.action = request.action;
		return result;
	}

	ofxGgmlNvigiReloadResult reload(
		const std::string & componentId,
		const std::string & modelPath = "") const {
		ofxGgmlNvigiReloadRequest request;
		request.action = ofxGgmlNvigiReloadAction::Reload;
		request.componentId = componentId;
		request.modelPath = modelPath;
		return execute(request);
	}

	ofxGgmlNvigiReloadResult load(
		const std::string & componentId,
		const std::string & modelPath = "") const {
		ofxGgmlNvigiReloadRequest request;
		request.action = ofxGgmlNvigiReloadAction::Load;
		request.componentId = componentId;
		request.modelPath = modelPath;
		return execute(request);
	}

	ofxGgmlNvigiReloadResult unload(const std::string & componentId) const {
		ofxGgmlNvigiReloadRequest request;
		request.action = ofxGgmlNvigiReloadAction::Unload;
		request.componentId = componentId;
		return execute(request);
	}

private:
	ReloadFunction m_reloadFunction;
	std::string m_displayName;
};
