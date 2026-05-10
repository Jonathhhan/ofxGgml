#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

enum class ofxGgmlBackend {
	Auto,
	CPU,
	CUDA,
	Vulkan,
	Metal,
	OpenCL
};

inline const char * ofxGgmlGetBackendName(ofxGgmlBackend backend) {
	switch (backend) {
	case ofxGgmlBackend::Auto: return "Auto";
	case ofxGgmlBackend::CPU: return "CPU";
	case ofxGgmlBackend::CUDA: return "CUDA";
	case ofxGgmlBackend::Vulkan: return "Vulkan";
	case ofxGgmlBackend::Metal: return "Metal";
	case ofxGgmlBackend::OpenCL: return "OpenCL";
	}
	return "unknown";
}

enum class ofxGgmlType {
	F32,
	F16,
	I32,
	I16,
	I8
};

enum class ofxGgmlRuntimeState {
	Uninitialized,
	Ready,
	Error
};

struct ofxGgmlRuntimeSettings {
	ofxGgmlBackend preferredBackend = ofxGgmlBackend::Auto;
	bool allowCpuFallback = true;
	int deviceIndex = 0;
	int threads = 0;
};

struct ofxGgmlDeviceInfo {
	std::string name;
	ofxGgmlBackend backend = ofxGgmlBackend::CPU;
	std::size_t memoryBytes = 0;
	bool available = false;
};

struct ofxGgmlComputeResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string error;

	explicit operator bool() const {
		return isOk();
	}

	bool isOk() const {
		return success;
	}

	bool isError() const {
		return !isOk();
	}
};
