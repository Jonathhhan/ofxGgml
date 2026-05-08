#pragma once

/// Opt-in creative layer for the rebuilt ofxGgml API.
///
/// This header intentionally starts as capability metadata, not heavy modality
/// code. Real vision, speech, diffusion, segmentation, and planning backends
/// can register here without changing the foundation or inference layers.

#include "ofxGgmlInference.h"
#include "foundation/ofxGgmlCreativeCapabilities.h"

class ofxGgmlCreativeSession {
public:
	bool setup(const ofxGgmlSessionSettings & settings = {}) {
		return inference_.setup(settings);
	}

	void close() {
		capabilities_.clear();
		inference_.close();
	}

	bool isReady() const {
		return inference_.isReady();
	}

	ofxGgmlSession & inference() {
		return inference_;
	}

	const ofxGgmlSession & inference() const {
		return inference_;
	}

	void addCapability(const ofxGgmlCreativeCapability & capability) {
		capabilities_.add(capability);
	}

	std::optional<ofxGgmlCreativeCapability> findCapability(const std::string & id) const {
		return capabilities_.find(id);
	}

	std::vector<ofxGgmlCreativeCapability> findCapabilities(ofxGgmlCreativeCapabilityKind kind) const {
		return capabilities_.findByKind(kind);
	}

	const ofxGgmlCreativeCapabilityRegistry & capabilities() const {
		return capabilities_;
	}

private:
	ofxGgmlSession inference_;
	ofxGgmlCreativeCapabilityRegistry capabilities_;
};
