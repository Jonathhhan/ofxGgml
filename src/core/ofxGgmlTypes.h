#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

enum class ofxGgmlBackend {
	Auto,
	Cpu,
	Cuda,
	Vulkan,
	Metal,
	OpenCL
};

inline const char * ofxGgmlBackendName(ofxGgmlBackend backend) {
	switch (backend) {
	case ofxGgmlBackend::Auto: return "Auto";
	case ofxGgmlBackend::Cpu: return "CPU";
	case ofxGgmlBackend::Cuda: return "CUDA";
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
	ofxGgmlBackend backend = ofxGgmlBackend::Cpu;
	std::size_t memoryBytes = 0;
	bool available = false;
};

struct ofxGgmlComputeResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string error;
};
