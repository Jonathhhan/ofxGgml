#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

enum class ofxGgmlCreativeCapabilityKind {
	Text,
	Vision,
	SpeechToText,
	TextToSpeech,
	ImageGeneration,
	Segmentation,
	VideoPlanning,
	Research,
	Unknown
};

struct ofxGgmlCreativeCapability {
	std::string id;
	std::string name;
	ofxGgmlCreativeCapabilityKind kind = ofxGgmlCreativeCapabilityKind::Unknown;
	bool configured = false;
	std::string note;
};

class ofxGgmlCreativeCapabilityRegistry {
public:
	void clear() {
		capabilities_.clear();
	}

	bool empty() const {
		return capabilities_.empty();
	}

	std::size_t size() const {
		return capabilities_.size();
	}

	void add(ofxGgmlCreativeCapability capability) {
		if (capability.id.empty()) {
			capability.id = capability.name;
		}
		const auto existing = findIndex(capability.id);
		if (existing) {
			capabilities_[*existing] = std::move(capability);
			return;
		}
		capabilities_.push_back(std::move(capability));
	}

	std::optional<ofxGgmlCreativeCapability> find(const std::string & id) const {
		const auto index = findIndex(id);
		if (!index) {
			return std::nullopt;
		}
		return capabilities_[*index];
	}

	std::vector<ofxGgmlCreativeCapability> findByKind(ofxGgmlCreativeCapabilityKind kind) const {
		std::vector<ofxGgmlCreativeCapability> matches;
		for (const auto & capability : capabilities_) {
			if (capability.kind == kind) {
				matches.push_back(capability);
			}
		}
		return matches;
	}

	const std::vector<ofxGgmlCreativeCapability> & list() const {
		return capabilities_;
	}

private:
	std::optional<std::size_t> findIndex(const std::string & id) const {
		const auto it = std::find_if(capabilities_.begin(), capabilities_.end(), [&](const auto & capability) {
			return capability.id == id;
		});
		if (it == capabilities_.end()) {
			return std::nullopt;
		}
		return static_cast<std::size_t>(std::distance(capabilities_.begin(), it));
	}

	std::vector<ofxGgmlCreativeCapability> capabilities_;
};
