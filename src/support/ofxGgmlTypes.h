#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// Backend device type — mirrors ggml_backend_dev_type.
enum class ofxGgmlBackendType {
	Cpu = 0,
	Gpu,
	IntegratedGpu,
	Accelerator
};

/// Tensor element type — mirrors ggml_type for the most common formats.
enum class ofxGgmlType {
	F32 = 0,
	F16 = 1,
	Q4_0 = 2,
	Q4_1 = 3,
	Q5_0 = 6,
	Q5_1 = 7,
	Q8_0 = 8,
	Q8_1 = 9,
	I8 = 24,
	I16 = 25,
	I32 = 26,
	I64 = 27,
	F64 = 28,
	BF16 = 30
};

/// Unary activation functions — mirrors ggml_unary_op.
enum class ofxGgmlUnaryOp {
	Abs,
	Sgn,
	Neg,
	Step,
	Tanh,
	Elu,
	Relu,
	SiLU,
	Gelu,
	GeluQuick,
	Sigmoid,
	HardSwish,
	HardSigmoid,
	Exp
};

/// Lifecycle state for the main ofxGgml instance.
enum class ofxGgmlState {
	Uninitialized,
	Ready,
	Computing,
	Error
};

/// Configuration for ofxGgml::setup().
struct ofxGgmlSettings {
	/// Number of CPU threads for computation (0 = auto-detect).
	int threads = 0;

	/// Preferred backend type.  The addon will fall back to CPU when the
	/// requested device is unavailable.
	ofxGgmlBackendType preferredBackend = ofxGgmlBackendType::Cpu;

	/// Size of the default computation-graph arena (number of nodes).
	size_t graphSize = 2048;
};

/// Information about a backend device discovered at runtime.
struct ofxGgmlDeviceInfo {
	std::string name;
	std::string description;
	ofxGgmlBackendType type = ofxGgmlBackendType::Cpu;
	size_t memoryFree = 0;
	size_t memoryTotal = 0;
};

/// Result of a graph computation.
struct ofxGgmlComputeResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string error;
};

/// Progress / log callback signature.
using ofxGgmlLogCallback = std::function<void(int level, const std::string & message)>;
