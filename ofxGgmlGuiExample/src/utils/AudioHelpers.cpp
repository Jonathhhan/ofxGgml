#include "AudioHelpers.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
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
	return writeWavFile(path, samples, sampleRate, 1);
}

bool writeWavFile(
	const std::string & path,
	const std::vector<float> & samples,
	int sampleRate,
	int channels) {
	if (path.empty() || samples.empty() || sampleRate <= 0 || channels <= 0) {
		return false;
	}
	if ((samples.size() % static_cast<size_t>(channels)) != 0) {
		return false;
	}
	std::ofstream out(path, std::ios::binary);
	if (!out.is_open()) {
		return false;
	}

	const uint16_t wavChannels = static_cast<uint16_t>(channels);
	const uint16_t bitsPerSample = 16;
	const uint32_t byteRate = static_cast<uint32_t>(sampleRate) * wavChannels * (bitsPerSample / 8);
	const uint16_t blockAlign = wavChannels * (bitsPerSample / 8);
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
	writeU16(wavChannels);
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

namespace {

uint16_t readU16Le(const unsigned char * data) {
	return static_cast<uint16_t>(data[0]) |
		(static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readU32Le(const unsigned char * data) {
	return static_cast<uint32_t>(data[0]) |
		(static_cast<uint32_t>(data[1]) << 8) |
		(static_cast<uint32_t>(data[2]) << 16) |
		(static_cast<uint32_t>(data[3]) << 24);
}

int32_t readS24Le(const unsigned char * data) {
	int32_t value =
		static_cast<int32_t>(data[0]) |
		(static_cast<int32_t>(data[1]) << 8) |
		(static_cast<int32_t>(data[2]) << 16);
	if ((value & 0x00800000) != 0) {
		value |= ~0x00FFFFFF;
	}
	return value;
}

} // namespace

bool loadWavFile(
	const std::string & path,
	LoadedWavAudio & audio,
	std::string * errorMessage) {
	audio = {};
	if (errorMessage != nullptr) {
		errorMessage->clear();
	}
	if (path.empty()) {
		if (errorMessage != nullptr) {
			*errorMessage = "Audio path is empty.";
		}
		return false;
	}

	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) {
		if (errorMessage != nullptr) {
			*errorMessage = "Failed to open WAV file: " + path;
		}
		return false;
	}

	in.seekg(0, std::ios::end);
	const std::streamoff fileSize = in.tellg();
	if (fileSize < 44) {
		if (errorMessage != nullptr) {
			*errorMessage = "WAV file is too small to contain a valid header.";
		}
		return false;
	}
	in.seekg(0, std::ios::beg);

	std::vector<unsigned char> bytes(static_cast<size_t>(fileSize));
	in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if (!in.good() && !in.eof()) {
		if (errorMessage != nullptr) {
			*errorMessage = "Failed to read WAV file bytes.";
		}
		return false;
	}

	if (std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
		std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
		if (errorMessage != nullptr) {
			*errorMessage = "Unsupported WAV container. Expected RIFF/WAVE.";
		}
		return false;
	}

	uint16_t audioFormat = 0;
	uint16_t channels = 0;
	uint32_t sampleRate = 0;
	uint16_t bitsPerSample = 0;
	size_t dataOffset = 0;
	size_t dataSize = 0;
	bool foundFmt = false;
	bool foundData = false;

	size_t offset = 12;
	while (offset + 8 <= bytes.size()) {
		const char * chunkId = reinterpret_cast<const char *>(bytes.data() + offset);
		const uint32_t chunkSize = readU32Le(bytes.data() + offset + 4);
		const size_t chunkDataOffset = offset + 8;
		const size_t chunkDataEnd = chunkDataOffset + static_cast<size_t>(chunkSize);
		if (chunkDataEnd > bytes.size()) {
			if (errorMessage != nullptr) {
				*errorMessage = "WAV chunk overruns the file boundary.";
			}
			return false;
		}

		if (std::memcmp(chunkId, "fmt ", 4) == 0) {
			if (chunkSize < 16) {
				if (errorMessage != nullptr) {
					*errorMessage = "WAV fmt chunk is incomplete.";
				}
				return false;
			}
			audioFormat = readU16Le(bytes.data() + chunkDataOffset);
			channels = readU16Le(bytes.data() + chunkDataOffset + 2);
			sampleRate = readU32Le(bytes.data() + chunkDataOffset + 4);
			bitsPerSample = readU16Le(bytes.data() + chunkDataOffset + 14);
			foundFmt = true;
		} else if (std::memcmp(chunkId, "data", 4) == 0) {
			dataOffset = chunkDataOffset;
			dataSize = static_cast<size_t>(chunkSize);
			foundData = true;
		}

		offset = chunkDataEnd + (chunkSize % 2u);
	}

	if (!foundFmt || !foundData) {
		if (errorMessage != nullptr) {
			*errorMessage = "WAV file is missing fmt or data chunks.";
		}
		return false;
	}
	if (channels == 0 || sampleRate == 0 || bitsPerSample == 0) {
		if (errorMessage != nullptr) {
			*errorMessage = "WAV metadata is incomplete.";
		}
		return false;
	}

	const size_t bytesPerSample = static_cast<size_t>(bitsPerSample / 8);
	if (bytesPerSample == 0) {
		if (errorMessage != nullptr) {
			*errorMessage = "Unsupported WAV sample depth.";
		}
		return false;
	}
	const size_t frameStride = static_cast<size_t>(channels) * bytesPerSample;
	if (frameStride == 0 || dataSize < frameStride) {
		if (errorMessage != nullptr) {
			*errorMessage = "WAV data chunk is empty.";
		}
		return false;
	}
	const size_t frameCount = dataSize / frameStride;
	if (frameCount == 0) {
		if (errorMessage != nullptr) {
			*errorMessage = "WAV data chunk contains no audio frames.";
		}
		return false;
	}

	audio.samples.resize(frameCount * static_cast<size_t>(channels));
	audio.sampleRate = static_cast<int>(sampleRate);
	audio.channels = static_cast<int>(channels);

	for (size_t frame = 0; frame < frameCount; ++frame) {
		for (size_t channel = 0; channel < static_cast<size_t>(channels); ++channel) {
			const size_t sampleOffset = dataOffset +
				frame * frameStride +
				channel * bytesPerSample;
			if (sampleOffset + bytesPerSample > bytes.size()) {
				if (errorMessage != nullptr) {
					*errorMessage = "WAV sample data overruns the file boundary.";
				}
				audio = {};
				return false;
			}

			float sample = 0.0f;
			const unsigned char * sampleBytes = bytes.data() + sampleOffset;
			if (audioFormat == 1) {
				switch (bitsPerSample) {
				case 8:
					sample =
						(static_cast<float>(sampleBytes[0]) - 128.0f) /
						128.0f;
					break;
				case 16: {
					const int16_t value = static_cast<int16_t>(readU16Le(sampleBytes));
					sample = static_cast<float>(value) / 32768.0f;
					break;
				}
				case 24:
					sample = static_cast<float>(readS24Le(sampleBytes)) / 8388608.0f;
					break;
				case 32: {
					int32_t value = 0;
					std::memcpy(&value, sampleBytes, sizeof(value));
					sample = static_cast<float>(value / 2147483648.0);
					break;
				}
				default:
					if (errorMessage != nullptr) {
						*errorMessage =
							"Unsupported PCM WAV bit depth: " + std::to_string(bitsPerSample);
					}
					audio = {};
					return false;
				}
			} else if (audioFormat == 3 && bitsPerSample == 32) {
				float value = 0.0f;
				std::memcpy(&value, sampleBytes, sizeof(value));
				sample = value;
			} else {
				if (errorMessage != nullptr) {
					*errorMessage =
						"Unsupported WAV encoding. Only PCM and 32-bit float WAV files are supported.";
				}
				audio = {};
				return false;
			}

			audio.samples[frame * static_cast<size_t>(channels) + channel] =
				std::clamp(sample, -1.0f, 1.0f);
		}
	}

	return true;
}
