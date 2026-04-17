#pragma once

#include "ofxGgmlSpeechInference.h"
#include "ofMain.h"

#include <atomic>
#include <cstdint>
#include <future>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

struct ofxGgmlLiveSpeechSettings {
	ofxGgmlSpeechModelProfile profile;
	std::string executable;
	std::string modelPath;
	std::string serverUrl;
	std::string serverModel;
	std::string prompt;
	std::string languageHint;
	ofxGgmlSpeechTask task = ofxGgmlSpeechTask::Transcribe;
	int sampleRate = 16000;
	float intervalSeconds = 1.25f;
	float windowSeconds = 8.0f;
	float overlapSeconds = 0.75f;
	bool enabled = false;
	bool returnTimestamps = false;
};

struct ofxGgmlLiveSpeechSnapshot {
	bool enabled = false;
	bool capturing = false;
	bool busy = false;
	double bufferedSeconds = 0.0;
	std::string transcript;
	std::string status;
	std::string detectedLanguage;
};

class ofxGgmlLiveSpeechTranscriber {
public:
	ofxGgmlLiveSpeechTranscriber();
	~ofxGgmlLiveSpeechTranscriber();

	void setSettings(const ofxGgmlLiveSpeechSettings & settings);
	ofxGgmlLiveSpeechSettings getSettings() const;

	void setLogCallback(
		std::function<void(ofLogLevel, const std::string &)> callback);

	void beginCapture(bool clearTranscript = true);
	void stopCapture(bool keepBufferedAudio = true);
	void reset(bool clearTranscript = true);
	void clearTranscript();

	void appendMonoSamples(const std::vector<float> & samples);
	void appendMonoSamples(const float * samples, size_t sampleCount);
	void update();

	ofxGgmlLiveSpeechSnapshot getSnapshot() const;

	bool isBusy() const;
	bool isCapturing() const;

private:
	void runChunkTask(
		uint64_t generationId,
		ofxGgmlLiveSpeechSettings settings,
		std::string chunkPath);
	void drainFinishedTask();
	void updateStatus(const std::string & status);

	mutable std::mutex m_settingsMutex;
	mutable std::mutex m_stateMutex;
	mutable std::mutex m_audioMutex;
	ofxGgmlLiveSpeechSettings m_settings;
	std::vector<float> m_recordedSamples;
	std::future<void> m_future;
	std::function<void(ofLogLevel, const std::string &)> m_logCallback;
	std::string m_transcript;
	std::string m_status;
	std::string m_detectedLanguage;
	bool m_capturing = false;
	size_t m_processedSamples = 0;
	float m_nextRunTimeSeconds = 0.0f;
	std::atomic<bool> m_busy{false};
	std::atomic<uint64_t> m_generation{0};
};
