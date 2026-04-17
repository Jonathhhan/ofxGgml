#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

enum class ofxGgmlTtsTask {
	Synthesize = 0,
	CloneVoice,
	ContinueSpeech
};

struct ofxGgmlTtsModelProfile {
	std::string name;
	std::string architecture;
	std::string modelRepoHint;
	std::string modelFileHint;
	std::string modelPath;
	std::string speakerRepoHint;
	std::string speakerFileHint;
	std::string speakerPath;
	bool supportsVoiceCloning = true;
	bool supportsStreaming = false;
	bool requiresSpeakerProfile = false;
};

struct ofxGgmlTtsAudioArtifact {
	std::string path;
	int sampleRate = 0;
	int channels = 0;
	float durationSeconds = 0.0f;
};

struct ofxGgmlTtsRequest {
	ofxGgmlTtsTask task = ofxGgmlTtsTask::Synthesize;
	std::string text;
	std::string modelPath;
	std::string tokenizerPath;
	std::string speakerPath;
	std::string speakerReferencePath;
	std::string defaultSpeakerName;
	std::string language;
	std::string outputPath;
	std::string promptAudioPath;
	int seed = -1;
	int maxTokens = 0;
	float temperature = 0.4f;
	float repetitionPenalty = 1.1f;
	int repetitionRange = 64;
	int topK = 40;
	float topP = 0.9f;
	float minP = 0.05f;
	bool streamAudio = false;
	bool normalizeText = true;
};

struct ofxGgmlTtsResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string error;
	std::string backendName;
	std::string rawOutput;
	std::string speakerPath;
	std::vector<ofxGgmlTtsAudioArtifact> audioFiles;
	std::vector<std::pair<std::string, std::string>> metadata;
};

class ofxGgmlTtsBackend {
public:
	virtual ~ofxGgmlTtsBackend() = default;
	virtual std::string backendName() const = 0;
	virtual ofxGgmlTtsResult synthesize(
		const ofxGgmlTtsRequest & request) const = 0;
};

class ofxGgmlTtsBridgeBackend : public ofxGgmlTtsBackend {
public:
	using SynthesizeFunction = std::function<ofxGgmlTtsResult(
		const ofxGgmlTtsRequest &)>;

	explicit ofxGgmlTtsBridgeBackend(
		SynthesizeFunction synthesizeFunction = {},
		std::string displayName = "TtsBridge");

	void setSynthesizeFunction(SynthesizeFunction synthesizeFunction);
	bool isConfigured() const;

	std::string backendName() const override;
	ofxGgmlTtsResult synthesize(
		const ofxGgmlTtsRequest & request) const override;

private:
	SynthesizeFunction m_synthesizeFunction;
	std::string m_displayName;
};

class ofxGgmlTtsInference {
public:
	ofxGgmlTtsInference();

	static std::vector<ofxGgmlTtsModelProfile> defaultProfiles();
	static const char * taskLabel(ofxGgmlTtsTask task);
	static std::shared_ptr<ofxGgmlTtsBackend>
		createTtsBridgeBackend(
			ofxGgmlTtsBridgeBackend::SynthesizeFunction synthesizeFunction = {},
			const std::string & displayName = "TtsBridge");
	static std::shared_ptr<ofxGgmlTtsBackend>
		createChatLlmTtsBridgeBackend(
			ofxGgmlTtsBridgeBackend::SynthesizeFunction synthesizeFunction = {},
			const std::string & displayName = "ChatLLM TTS");
	static std::shared_ptr<ofxGgmlTtsBackend>
		createOuteTtsBridgeBackend(
			ofxGgmlTtsBridgeBackend::SynthesizeFunction synthesizeFunction = {},
			const std::string & displayName = "OuteTTS");

	void setBackend(std::shared_ptr<ofxGgmlTtsBackend> backend);
	std::shared_ptr<ofxGgmlTtsBackend> getBackend() const;

	ofxGgmlTtsResult synthesize(
		const ofxGgmlTtsRequest & request) const;

private:
	std::shared_ptr<ofxGgmlTtsBackend> m_backend;
};
