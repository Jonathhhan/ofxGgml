#pragma once

#include <memory>
#include <string>
#include <vector>

enum class ofxGgmlSpeechTask {
	Transcribe = 0,
	Translate
};

struct ofxGgmlSpeechModelProfile {
	std::string name;
	std::string modelRepoHint;
	std::string modelFileHint;
	std::string modelPath;
	std::string executable = "whisper-cli";
	bool supportsTranslate = true;
	bool supportsTimestamps = false;
};

struct ofxGgmlSpeechSegment {
	double startSeconds = 0.0;
	double endSeconds = 0.0;
	std::string text;
};

struct ofxGgmlSpeechRequest {
	ofxGgmlSpeechTask task = ofxGgmlSpeechTask::Transcribe;
	std::string audioPath;
	std::string modelPath;
	std::string serverUrl;
	std::string serverModel;
	std::string languageHint;
	std::string prompt;
	bool returnTimestamps = false;
};

struct ofxGgmlSpeechResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string text;
	std::string error;
	std::string backendName;
	std::string rawOutput;
	std::string transcriptPath;
	std::string srtPath;
	std::string vttPath;
	std::string detectedLanguage;
	std::string usedServerUrl;
	std::vector<ofxGgmlSpeechSegment> segments;
};

class ofxGgmlSpeechBackend {
public:
	virtual ~ofxGgmlSpeechBackend() = default;
	virtual std::string backendName() const = 0;
	virtual ofxGgmlSpeechResult transcribe(
		const ofxGgmlSpeechRequest & request) const = 0;
};

class ofxGgmlWhisperCliSpeechBackend : public ofxGgmlSpeechBackend {
public:
	explicit ofxGgmlWhisperCliSpeechBackend(
		std::string executable = "whisper-cli");

	void setExecutable(const std::string & executable);
	const std::string & getExecutable() const;

	std::string backendName() const override;
	std::vector<std::string> buildCommandArguments(
		const ofxGgmlSpeechRequest & request,
		const std::string & outputBase) const;
	std::string expectedTranscriptPath(const std::string & outputBase) const;
	std::string expectedSrtPath(const std::string & outputBase) const;
	std::string expectedVttPath(const std::string & outputBase) const;
	static std::vector<ofxGgmlSpeechSegment> parseSrtSegments(
		const std::string & srtText);
	ofxGgmlSpeechResult transcribe(
		const ofxGgmlSpeechRequest & request) const override;

private:
	std::string m_executable;
};

class ofxGgmlWhisperServerSpeechBackend : public ofxGgmlSpeechBackend {
public:
	ofxGgmlWhisperServerSpeechBackend(
		std::string serverUrl = "http://127.0.0.1:8081",
		std::string serverModel = "");

	void setServerUrl(const std::string & serverUrl);
	void setServerModel(const std::string & serverModel);
	const std::string & getServerUrl() const;
	const std::string & getServerModel() const;

	std::string backendName() const override;
	static std::string normalizeServerUrl(
		const std::string & serverUrl,
		ofxGgmlSpeechTask task);
	ofxGgmlSpeechResult transcribe(
		const ofxGgmlSpeechRequest & request) const override;

private:
	std::string m_serverUrl;
	std::string m_serverModel;
};

class ofxGgmlSpeechInference {
public:
	ofxGgmlSpeechInference();

	static std::vector<ofxGgmlSpeechModelProfile> defaultProfiles();
	static const char * taskLabel(ofxGgmlSpeechTask task);
	static std::shared_ptr<ofxGgmlSpeechBackend> createWhisperCliBackend(
		const std::string & executable = "whisper-cli");
	static std::shared_ptr<ofxGgmlSpeechBackend> createWhisperServerBackend(
		const std::string & serverUrl = "http://127.0.0.1:8081",
		const std::string & serverModel = "");

	void setBackend(std::shared_ptr<ofxGgmlSpeechBackend> backend);
	std::shared_ptr<ofxGgmlSpeechBackend> getBackend() const;

	ofxGgmlSpeechResult transcribe(
		const ofxGgmlSpeechRequest & request) const;

private:
	std::shared_ptr<ofxGgmlSpeechBackend> m_backend;
};
