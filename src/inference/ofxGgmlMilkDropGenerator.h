#pragma once

#include "inference/ofxGgmlInference.h"

#include <functional>
#include <string>
#include <vector>

struct ofxGgmlMilkDropCategoryOption {
	std::string name;
	std::string description;
};

struct ofxGgmlMilkDropRequest {
	std::string prompt;
	std::string category = "General";
	float randomness = 0.55f;
	bool audioReactive = true;
	bool seamlessLoop = true;
	std::string existingPresetText;
	std::string presetNameHint;
};

struct ofxGgmlMilkDropPreparedPrompt {
	std::string prompt;
	std::string label;
};

struct ofxGgmlMilkDropResult {
	bool success = false;
	ofxGgmlMilkDropPreparedPrompt prepared;
	ofxGgmlInferenceResult inference;
	std::string presetText;
	std::string savedPath;
	std::string error;
};

class ofxGgmlMilkDropGenerator {
public:
	void setCompletionExecutable(const std::string & path);
	ofxGgmlInference & getInference();
	const ofxGgmlInference & getInference() const;

	ofxGgmlMilkDropPreparedPrompt preparePrompt(
		const ofxGgmlMilkDropRequest & request) const;

	ofxGgmlMilkDropResult generatePreset(
		const std::string & modelPath,
		const ofxGgmlMilkDropRequest & request,
		const ofxGgmlInferenceSettings & settings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const;

	ofxGgmlMilkDropResult editPreset(
		const std::string & modelPath,
		const std::string & existingPresetText,
		const std::string & editInstruction,
		const std::string & category = "General",
		float randomness = 0.45f,
		const ofxGgmlInferenceSettings & settings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const;

	std::string savePreset(
		const std::string & presetText,
		const std::string & outputPath) const;

	static std::vector<ofxGgmlMilkDropCategoryOption> defaultCategories();
	static std::string sanitizePresetText(const std::string & rawText);
	static std::string makeSuggestedFileName(
		const std::string & prompt,
		const std::string & category);

private:
	ofxGgmlInference m_inference;
};
