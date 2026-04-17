#include "AudioHelpers.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

std::string makeTempMicRecordingPath() {
	std::error_code ec;
	std::filesystem::path base = std::filesystem::temp_directory_path(ec);
	if (ec) {
		base = std::filesystem::current_path();
	}
	const auto now = std::chrono::system_clock::now();
	const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
		now.time_since_epoch()).count();
	const uint64_t nonceHi = static_cast<uint64_t>(std::random_device{}());
	const uint64_t nonceLo = static_cast<uint64_t>(std::random_device{}());
	const uint64_t nonce = (nonceHi << 32) | nonceLo;
	std::ostringstream name;
	name << "ofxggml_mic_" << millis << "_" << std::hex << nonce << ".wav";
	return (base / name.str()).string();
}

bool writeMonoWavFile(
	const std::string & path,
	const std::vector<float> & samples,
	int sampleRate) {
	if (path.empty() || samples.empty() || sampleRate <= 0) {
		return false;
	}
	std::ofstream out(path, std::ios::binary);
	if (!out.is_open()) {
		return false;
	}

	const uint16_t channels = 1;
	const uint16_t bitsPerSample = 16;
	const uint32_t byteRate = static_cast<uint32_t>(sampleRate) * channels * (bitsPerSample / 8);
	const uint16_t blockAlign = channels * (bitsPerSample / 8);
	const uint32_t dataSize = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
	const uint32_t riffSize = 36u + dataSize;

	auto writeU16 = [&](uint16_t value) {
		out.write(reinterpret_cast<const char *>(&value), sizeof(value));
	};
	auto writeU32 = [&](uint32_t value) {
		out.write(reinterpret_cast<const char *>(&value), sizeof(value));
	};

	out.write("RIFF", 4);
	writeU32(riffSize);
	out.write("WAVE", 4);
	out.write("fmt ", 4);
	writeU32(16);
	writeU16(1);
	writeU16(channels);
	writeU32(static_cast<uint32_t>(sampleRate));
	writeU32(byteRate);
	writeU16(blockAlign);
	writeU16(bitsPerSample);
	out.write("data", 4);
	writeU32(dataSize);

	for (float sample : samples) {
		const float clamped = std::clamp(sample, -1.0f, 1.0f);
		const int16_t pcm = static_cast<int16_t>(std::lrint(clamped * 32767.0f));
		out.write(reinterpret_cast<const char *>(&pcm), sizeof(pcm));
	}

	return out.good();
}
