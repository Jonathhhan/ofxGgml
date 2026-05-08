#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class ofxGgmlRuntimeBackend {
	Cpu,
	Cuda,
	Vulkan,
	Metal,
	Remote,
	Auto
};

struct ofxGgmlRuntimeSettings {
	ofxGgmlRuntimeBackend preferredBackend = ofxGgmlRuntimeBackend::Auto;
	int threads = 0;
	bool allowGpu = true;
	std::string modelDirectory = "models";
};

struct ofxGgmlRuntimeDevice {
	std::string name;
	ofxGgmlRuntimeBackend backend = ofxGgmlRuntimeBackend::Cpu;
	uint64_t memoryBytes = 0;
	bool available = true;
};

class ofxGgmlRuntime {
public:
	bool setup(const ofxGgmlRuntimeSettings & settings = {}) {
		settings_ = settings;
		ready_ = true;
		devices_.clear();
		devices_.push_back({"CPU", ofxGgmlRuntimeBackend::Cpu, 0, true});
		return true;
	}

	void close() {
		ready_ = false;
		devices_.clear();
	}

	bool isReady() const {
		return ready_;
	}

	const ofxGgmlRuntimeSettings & getSettings() const {
		return settings_;
	}

	const std::vector<ofxGgmlRuntimeDevice> & listDevices() const {
		return devices_;
	}

	std::string getBackendName() const {
		if (!ready_ || devices_.empty()) {
			return "Unavailable";
		}
		return toString(devices_.front().backend);
	}

	static std::string toString(ofxGgmlRuntimeBackend backend) {
		switch (backend) {
		case ofxGgmlRuntimeBackend::Cpu:
			return "CPU";
		case ofxGgmlRuntimeBackend::Cuda:
			return "CUDA";
		case ofxGgmlRuntimeBackend::Vulkan:
			return "Vulkan";
		case ofxGgmlRuntimeBackend::Metal:
			return "Metal";
		case ofxGgmlRuntimeBackend::Remote:
			return "Remote";
		case ofxGgmlRuntimeBackend::Auto:
			return "Auto";
		}
		return "Unknown";
	}

private:
	bool ready_ = false;
	ofxGgmlRuntimeSettings settings_;
	std::vector<ofxGgmlRuntimeDevice> devices_;
};
