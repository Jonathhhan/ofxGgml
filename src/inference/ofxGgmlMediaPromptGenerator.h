#pragma once

#include "inference/ofxGgmlInference.h"

#include <functional>
#include <string>

struct ofxGgmlMusicToImageRequest {
	std::string musicDescription;
	std::string lyrics;
	std::string visualStyle = "cinematic still, richly lit, highly detailed";
	bool includeLyrics = true;
};

struct ofxGgmlMusicToImagePreparedPrompt {
	std::string prompt;
	std::string label;
};

struct ofxGgmlImageToMusicRequest {
	std::string imageDescription;
	std::string sceneNotes;
	std::string musicalStyle = "cinematic instrumental soundtrack, expressive, high fidelity";
	std::string instrumentation;
	int targetDurationSeconds = 30;
	bool instrumentalOnly = true;
};

struct ofxGgmlImageToMusicPreparedPrompt {
	std::string prompt;
	std::string label;
};

struct ofxGgmlMusicToImageResult {
	bool success = false;
	ofxGgmlMusicToImagePreparedPrompt prepared;
	ofxGgmlInferenceResult inference;
	std::string visualPrompt;
	std::string error;
};

struct ofxGgmlImageToMusicResult {
	bool success = false;
	ofxGgmlImageToMusicPreparedPrompt prepared;
	ofxGgmlInferenceResult inference;
	std::string musicPrompt;
	std::string error;
};

class ofxGgmlMediaPromptGenerator {
public:
	void setCompletionExecutable(const std::string & path);
	ofxGgmlInference & getInference();
	const ofxGgmlInference & getInference() const;

	ofxGgmlMusicToImagePreparedPrompt prepareMusicToImagePrompt(
		const ofxGgmlMusicToImageRequest & request) const;
	ofxGgmlImageToMusicPreparedPrompt prepareImageToMusicPrompt(
		const ofxGgmlImageToMusicRequest & request) const;

	ofxGgmlMusicToImageResult generateMusicToImagePrompt(
		const std::string & modelPath,
		const ofxGgmlMusicToImageRequest & request,
		const ofxGgmlInferenceSettings & settings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const;
	ofxGgmlImageToMusicResult generateImageToMusicPrompt(
		const std::string & modelPath,
		const ofxGgmlImageToMusicRequest & request,
		const ofxGgmlInferenceSettings & settings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const;

	static std::string sanitizeVisualPrompt(const std::string & rawText);
	static std::string sanitizeMusicPrompt(const std::string & rawText);

private:
	ofxGgmlInference m_inference;
};
