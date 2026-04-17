#pragma once

#include "ofMain.h"
#include "ofxGgml.h"
#include "ofxImGui.h"

#include <string>
#include <vector>

// Forward declaration
enum class TextInferenceBackend;

// ---------------------------------------------------------------------------
// PerformancePanel — displays performance metrics and configuration
// ---------------------------------------------------------------------------

class PerformancePanel {
public:
	void draw(
		bool & showWindow,
		ofxGgml & ggml,
		const std::vector<ofxGgmlDeviceInfo> & devices,
		float lastComputeMs,
		int lastNodeCount,
		const std::string & lastBackendUsed,
		int selectedBackendIndex,
		const std::vector<std::string> & backendNames,
		int numThreads,
		int contextSize,
		int batchSize,
		TextInferenceBackend textInferenceBackend,
		int detectedModelLayers,
		int gpuLayers,
		int seed,
		int maxTokens,
		float temperature,
		float topP,
		int topK,
		float minP,
		float repeatPenalty,
		std::vector<ofxGgmlDeviceInfo> & devicesOut);
};
