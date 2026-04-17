#pragma once

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Audio Utilities
// ---------------------------------------------------------------------------

// Generate a unique temporary path for microphone recording
std::string makeTempMicRecordingPath();

// Write a mono WAV file from float samples
bool writeMonoWavFile(
	const std::string & path,
	const std::vector<float> & samples,
	int sampleRate);
