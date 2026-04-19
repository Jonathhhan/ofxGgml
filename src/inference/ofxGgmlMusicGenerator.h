#pragma once

#include "inference/ofxGgmlInference.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct ofxGgmlMusicPromptRequest {
	std::string sourceConcept;
	std::string style = "cinematic instrumental soundtrack, expressive, high fidelity";
	std::string instrumentation;
	std::string mood;
	std::string referenceLyrics;
	int targetDurationSeconds = 30;
	bool instrumentalOnly = true;
};

struct ofxGgmlMusicPromptPreparedPrompt {
	std::string prompt;
	std::string label;
};

struct ofxGgmlMusicPromptResult {
	bool success = false;
	ofxGgmlMusicPromptPreparedPrompt prepared;
	ofxGgmlInferenceResult inference;
	std::string musicPrompt;
	std::string error;
};

struct ofxGgmlMusicNotationRequest {
	std::string sourceConcept;
	std::string title = "Generated Theme";
	std::string style = "cinematic instrumental soundtrack";
	std::string meter = "4/4";
	std::string key = "Cm";
	std::string formHint = "AABA";
	int bars = 16;
	bool instrumentalOnly = true;
};

struct ofxGgmlMusicNotationPreparedPrompt {
	std::string prompt;
	std::string label;
};

struct ofxGgmlMusicNotationValidationIssue {
	std::string severity;
	int line = 0;
	std::string message;
	std::string suggestion;
};

struct ofxGgmlMusicNotationValidation {
	bool valid = false;
	std::string sanitizedNotation;
	std::vector<ofxGgmlMusicNotationValidationIssue> issues;
};

struct ofxGgmlMusicNotationResult {
	bool success = false;
	ofxGgmlMusicNotationPreparedPrompt prepared;
	ofxGgmlInferenceResult inference;
	std::string abcNotation;
	std::string error;
	ofxGgmlMusicNotationValidation validation;
};

struct ofxGgmlGeneratedMusicTrack {
	std::string path;
	std::string label;
	double durationSeconds = 0.0;
	int sampleRate = 0;
};

struct ofxGgmlMusicGenerationRequest {
	std::string prompt;
	std::string negativePrompt;
	int durationSeconds = 30;
	std::string outputDir;
	std::string outputPrefix = "music";
	int seed = -1;
	bool instrumentalOnly = true;
	std::string backendModel;
};

struct ofxGgmlMusicGenerationResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string backendName;
	std::string generatedPrompt;
	std::string normalizedCommand;
	std::string commandOutput;
	std::string error;
	int exitCode = -1;
	std::vector<ofxGgmlGeneratedMusicTrack> tracks;
};

class ofxGgmlMusicGenerationBackend {
public:
	virtual ~ofxGgmlMusicGenerationBackend() = default;
	virtual std::string backendName() const = 0;
	virtual ofxGgmlMusicGenerationResult generate(
		const ofxGgmlMusicGenerationRequest & request) const = 0;
};

class ofxGgmlMusicGenerationBridgeBackend : public ofxGgmlMusicGenerationBackend {
public:
	using GenerateCallback =
		std::function<ofxGgmlMusicGenerationResult(
			const ofxGgmlMusicGenerationRequest &)>;

	explicit ofxGgmlMusicGenerationBridgeBackend(GenerateCallback callback);

	std::string backendName() const override;
	ofxGgmlMusicGenerationResult generate(
		const ofxGgmlMusicGenerationRequest & request) const override;

private:
	GenerateCallback m_callback;
};

std::shared_ptr<ofxGgmlMusicGenerationBackend>
createMusicGenerationBridgeBackend(
	ofxGgmlMusicGenerationBridgeBackend::GenerateCallback callback);

class ofxGgmlMusicGenerator {
public:
	void setCompletionExecutable(const std::string & path);
	ofxGgmlInference & getInference();
	const ofxGgmlInference & getInference() const;

	void setBackend(std::shared_ptr<ofxGgmlMusicGenerationBackend> backend);
	std::shared_ptr<ofxGgmlMusicGenerationBackend> getBackend() const;

	ofxGgmlMusicPromptPreparedPrompt prepareMusicPrompt(
		const ofxGgmlMusicPromptRequest & request) const;
	ofxGgmlMusicPromptResult generateMusicPrompt(
		const std::string & modelPath,
		const ofxGgmlMusicPromptRequest & request,
		const ofxGgmlInferenceSettings & settings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const;

	ofxGgmlMusicNotationPreparedPrompt prepareAbcNotationPrompt(
		const ofxGgmlMusicNotationRequest & request) const;
	ofxGgmlMusicNotationResult generateAbcNotation(
		const std::string & modelPath,
		const ofxGgmlMusicNotationRequest & request,
		const ofxGgmlInferenceSettings & settings = {},
		std::function<bool(const std::string &)> onChunk = nullptr) const;

	ofxGgmlMusicGenerationResult generateAudio(
		const ofxGgmlMusicGenerationRequest & request) const;

	static std::string sanitizeMusicPrompt(const std::string & rawText);
	static std::string sanitizeAbcNotation(const std::string & rawText);
	static ofxGgmlMusicNotationValidation validateAbcNotation(
		const std::string & rawText);
	static std::string saveAbcNotation(
		const std::string & abcNotation,
		const std::string & outputPath);
	static std::string makeSuggestedFileName(const std::string & sourceConcept);

private:
	ofxGgmlInference m_inference;
	std::shared_ptr<ofxGgmlMusicGenerationBackend> m_backend;
};
