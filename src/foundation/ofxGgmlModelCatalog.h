#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

enum class ofxGgmlModelRole {
	Text,
	Embedding,
	Vision,
	Speech,
	Tts,
	Image,
	Unknown
};

struct ofxGgmlModelSpec {
	std::string id;
	std::string name;
	std::string path;
	ofxGgmlModelRole role = ofxGgmlModelRole::Unknown;
	int contextTokens = 4096;
	std::string description;
};

class ofxGgmlModelCatalog {
public:
	void clear() {
		models_.clear();
	}

	bool empty() const {
		return models_.empty();
	}

	std::size_t size() const {
		return models_.size();
	}

	void add(ofxGgmlModelSpec model) {
		if (model.id.empty()) {
			model.id = model.path.empty() ? model.name : model.path;
		}

		const auto existing = findIndex(model.id);
		if (existing) {
			models_[*existing] = std::move(model);
			return;
		}
		models_.push_back(std::move(model));
	}

	std::optional<ofxGgmlModelSpec> find(const std::string & id) const {
		const auto index = findIndex(id);
		if (!index) {
			return std::nullopt;
		}
		return models_[*index];
	}

	std::vector<ofxGgmlModelSpec> findByRole(ofxGgmlModelRole role) const {
		std::vector<ofxGgmlModelSpec> matches;
		for (const auto & model : models_) {
			if (model.role == role) {
				matches.push_back(model);
			}
		}
		return matches;
	}

	const std::vector<ofxGgmlModelSpec> & list() const {
		return models_;
	}

private:
	std::optional<std::size_t> findIndex(const std::string & id) const {
		const auto it = std::find_if(models_.begin(), models_.end(), [&](const ofxGgmlModelSpec & model) {
			return model.id == id;
		});
		if (it == models_.end()) {
			return std::nullopt;
		}
		return static_cast<std::size_t>(std::distance(models_.begin(), it));
	}

	std::vector<ofxGgmlModelSpec> models_;
};
