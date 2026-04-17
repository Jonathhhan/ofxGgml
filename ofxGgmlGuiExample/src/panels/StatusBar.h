#pragma once

#include "ofMain.h"
#include "ofxImGui.h"
#include "config/ModelPresets.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// Forward declarations
enum class LiveContextMode;
struct ofxGgmlChatLanguageOption;
struct ofxGgmlCodeLanguagePreset;

// ---------------------------------------------------------------------------
// StatusBar — displays application status at the bottom of the window
// ---------------------------------------------------------------------------

class StatusBar {
public:
	void draw(
		const std::string & engineStatus,
		const std::vector<ModelPreset> & modelPresets,
		int selectedModelIndex,
		AiMode activeMode,
		const char * const * modeLabels,
		int chatLanguageIndex,
		const std::vector<ofxGgmlChatLanguageOption> & chatLanguages,
		int selectedLanguageIndex,
		const std::vector<ofxGgmlCodeLanguagePreset> & scriptLanguages,
		int maxTokens,
		float temperature,
		float topP,
		int topK,
		float minP,
		LiveContextMode liveContextMode,
		int gpuLayers,
		int detectedModelLayers,
		const std::atomic<bool> & generating,
		float generationStartTime,
		const std::string & streamingOutput,
		std::mutex & streamMutex,
		float lastComputeMs);

private:
	static constexpr float kSpinnerInterval = 0.15f;  // seconds per spinner frame
};
