#pragma once

#include <cstddef>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Audio Utilities
// ---------------------------------------------------------------------------

struct LoadedWavAudio {
	std::vector<float> samples;
	int sampleRate = 0;
	int channels = 0;

	bool empty() const {
		return samples.empty() || sampleRate <= 0 || channels <= 0;
	}

	size_t frameCount() const {
		return channels > 0 ? (samples.size() / static_cast<size_t>(channels)) : 0;
	}
};

// Generate a unique temporary path for microphone recording
std::string makeTempMicRecordingPath();

// Write a mono WAV file from float samples
bool writeMonoWavFile(
	const std::string & path,
	const std::vector<float> & samples,
	int sampleRate);

// Load a PCM/float WAV file into normalized floating-point samples
bool loadWavFile(
	const std::string & path,
	LoadedWavAudio & audio,
	std::string * errorMessage = nullptr);
