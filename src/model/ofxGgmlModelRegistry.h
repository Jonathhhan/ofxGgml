#pragma once

#include "ofMain.h"
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/// Model metadata for version tracking and management.
struct ofxGgmlModelMetadata {
	std::string modelId;           // Unique identifier
	std::string version;           // Version string (e.g., "1.0.0", "v2-q4")
	std::string name;              // Human-readable name
	std::string description;       // Model description
	std::string path;              // File path to model
	std::string sha256;            // Checksum for integrity verification
	std::string modelType;         // Type: "llm", "embedding", "vision", "speech"
	std::string architecture;      // Architecture: "llama", "mistral", "phi", etc.
	size_t parameterCount = 0;     // Number of parameters (in millions)
	size_t contextSize = 0;        // Default context window size
	std::string quantization;      // Quantization: "Q4_0", "Q5_1", "F16", etc.
	uint64_t loadedTimestamp = 0;  // When this model was loaded
	bool isActive = false;         // Currently active/loaded
	std::map<std::string, std::string> customFields; // Extensible metadata
};

/// Registry for managing multiple model versions and enabling hot-swapping.
///
/// Provides version tracking, metadata storage, and runtime model switching
/// without application restart.
///
/// Example usage:
/// ```cpp
/// auto& registry = ofxGgmlModelRegistry::getInstance();
///
/// // Register multiple versions
/// ofxGgmlModelMetadata v1;
/// v1.modelId = "llama-7b";
/// v1.version = "v1-q4";
/// v1.path = "models/llama-7b-q4.gguf";
/// registry.registerModel(v1);
///
/// ofxGgmlModelMetadata v2;
/// v2.modelId = "llama-7b";
/// v2.version = "v2-q5";
/// v2.path = "models/llama-7b-v2-q5.gguf";
/// registry.registerModel(v2);
///
/// // Set active version
/// registry.setActiveVersion("llama-7b", "v2-q5");
///
/// // Get active model path
/// auto path = registry.getActiveModelPath("llama-7b");
/// ```
class ofxGgmlModelRegistry {
public:
	/// Get singleton instance.
	static ofxGgmlModelRegistry& getInstance() {
		static ofxGgmlModelRegistry instance;
		return instance;
	}

	/// Register a model with metadata.
	bool registerModel(const ofxGgmlModelMetadata& metadata) {
		std::lock_guard<std::mutex> lock(m_mutex);
		std::string key = makeKey(metadata.modelId, metadata.version);
		m_models[key] = metadata;
		m_modelsByType[metadata.modelType].push_back(key);
		return true;
	}

	/// Unregister a model version.
	bool unregisterModel(const std::string& modelId, const std::string& version) {
		std::lock_guard<std::mutex> lock(m_mutex);
		std::string key = makeKey(modelId, version);
		auto it = m_models.find(key);
		if (it != m_models.end()) {
			// Remove from type index
			auto& typeList = m_modelsByType[it->second.modelType];
			typeList.erase(std::remove(typeList.begin(), typeList.end(), key), typeList.end());
			// Remove main entry
			m_models.erase(it);
			// Clear active if this was active
			if (m_activeModels[modelId] == version) {
				m_activeModels.erase(modelId);
			}
			return true;
		}
		return false;
	}

	/// Set the active version for a model ID.
	bool setActiveVersion(const std::string& modelId, const std::string& version) {
		std::lock_guard<std::mutex> lock(m_mutex);
		std::string key = makeKey(modelId, version);
		if (m_models.find(key) != m_models.end()) {
			// Deactivate previous version
			auto prevIt = m_activeModels.find(modelId);
			if (prevIt != m_activeModels.end()) {
				std::string prevKey = makeKey(modelId, prevIt->second);
				auto prevModel = m_models.find(prevKey);
				if (prevModel != m_models.end()) {
					prevModel->second.isActive = false;
				}
			}
			// Activate new version
			m_activeModels[modelId] = version;
			auto& model = m_models[key];
			model.isActive = true;
			model.loadedTimestamp = getCurrentTimestamp();
			return true;
		}
		return false;
	}

	/// Get active version for a model ID.
	std::string getActiveVersion(const std::string& modelId) const {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_activeModels.find(modelId);
		return (it != m_activeModels.end()) ? it->second : "";
	}

	/// Get metadata for a specific model version.
	ofxGgmlModelMetadata getMetadata(const std::string& modelId, const std::string& version) const {
		std::lock_guard<std::mutex> lock(m_mutex);
		std::string key = makeKey(modelId, version);
		auto it = m_models.find(key);
		return (it != m_models.end()) ? it->second : ofxGgmlModelMetadata{};
	}

	/// Get metadata for the active version of a model.
	ofxGgmlModelMetadata getActiveMetadata(const std::string& modelId) const {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto versionIt = m_activeModels.find(modelId);
		if (versionIt != m_activeModels.end()) {
			std::string key = makeKey(modelId, versionIt->second);
			auto it = m_models.find(key);
			if (it != m_models.end()) {
				return it->second;
			}
		}
		return ofxGgmlModelMetadata{};
	}

	/// Get file path for the active version of a model.
	std::string getActiveModelPath(const std::string& modelId) const {
		auto metadata = getActiveMetadata(modelId);
		return metadata.path;
	}

	/// List all versions of a model ID.
	std::vector<std::string> listVersions(const std::string& modelId) const {
		std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<std::string> versions;
		for (const auto& [key, metadata] : m_models) {
			if (metadata.modelId == modelId) {
				versions.push_back(metadata.version);
			}
		}
		return versions;
	}

	/// List all registered model IDs.
	std::vector<std::string> listModelIds() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<std::string> ids;
		for (const auto& [modelId, _] : m_activeModels) {
			ids.push_back(modelId);
		}
		// Also include non-active models
		for (const auto& [key, metadata] : m_models) {
			if (std::find(ids.begin(), ids.end(), metadata.modelId) == ids.end()) {
				ids.push_back(metadata.modelId);
			}
		}
		return ids;
	}

	/// List models by type.
	std::vector<ofxGgmlModelMetadata> listModelsByType(const std::string& modelType) const {
		std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<ofxGgmlModelMetadata> result;
		auto typeIt = m_modelsByType.find(modelType);
		if (typeIt != m_modelsByType.end()) {
			for (const auto& key : typeIt->second) {
				auto it = m_models.find(key);
				if (it != m_models.end()) {
					result.push_back(it->second);
				}
			}
		}
		return result;
	}

	/// Check if a model version exists.
	bool hasModel(const std::string& modelId, const std::string& version) const {
		std::lock_guard<std::mutex> lock(m_mutex);
		std::string key = makeKey(modelId, version);
		return m_models.find(key) != m_models.end();
	}

	/// Check if a model ID has an active version.
	bool hasActiveVersion(const std::string& modelId) const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_activeModels.find(modelId) != m_activeModels.end();
	}

	/// Update metadata for an existing model.
	bool updateMetadata(const std::string& modelId, const std::string& version,
	                    const std::function<void(ofxGgmlModelMetadata&)>& updater) {
		std::lock_guard<std::mutex> lock(m_mutex);
		std::string key = makeKey(modelId, version);
		auto it = m_models.find(key);
		if (it != m_models.end()) {
			updater(it->second);
			return true;
		}
		return false;
	}

	/// Clear all registered models.
	void clear() {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_models.clear();
		m_activeModels.clear();
		m_modelsByType.clear();
	}

	/// Get count of registered models.
	size_t getModelCount() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_models.size();
	}

private:
	ofxGgmlModelRegistry() = default;
	~ofxGgmlModelRegistry() = default;
	ofxGgmlModelRegistry(const ofxGgmlModelRegistry&) = delete;
	ofxGgmlModelRegistry& operator=(const ofxGgmlModelRegistry&) = delete;

	static std::string makeKey(const std::string& modelId, const std::string& version) {
		return modelId + "::" + version;
	}

	static uint64_t getCurrentTimestamp() {
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
	}

	mutable std::mutex m_mutex;
	std::map<std::string, ofxGgmlModelMetadata> m_models; // key: modelId::version
	std::map<std::string, std::string> m_activeModels;    // modelId -> active version
	std::map<std::string, std::vector<std::string>> m_modelsByType; // type -> keys
};
