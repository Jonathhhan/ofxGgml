#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Version metadata.
// ---------------------------------------------------------------------------

struct ofxGgmlAddonVersionInfo {
	int major = 0;
	int minor = 0;
	int patch = 0;
	std::string versionString;
};

// ---------------------------------------------------------------------------
// Core runtime types.
// ---------------------------------------------------------------------------

/// Backend device type. Mirrors ggml_backend_dev_type.
enum class ofxGgmlBackendType {
	Cpu = 0,
	Gpu,
	IntegratedGpu,
	Accelerator
};

/// Tensor element type. Mirrors ggml_type for the most common formats.
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

/// Pooling operation type. Mirrors ggml_op_pool.
enum class ofxGgmlPoolType {
	Max = 0,
	Avg = 1
};

/// Unary activation functions. Mirrors ggml_unary_op.
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

/// Log message severity levels.
enum class ofxGgmlLogLevel {
	None = 0,
	Debug = 1,
	Info = 2,
	Warn = 3,
	Error = 4,
	Cont = 5
};

/// Configuration for ofxGgml::setup().
struct ofxGgmlSettings {
	/// Number of CPU threads for computation (0 = auto-detect).
	int threads = 0;

	/// Preferred backend device name using the raw ggml name (for example
	/// "CPU", "CUDA0", "Metal", or "Vulkan0"). When non-empty this takes
	/// priority over preferredBackend.
	std::string preferredBackendName;

	/// Preferred backend type used when preferredBackendName is empty.
	/// The addon falls back to CPU when the requested backend is unavailable.
	ofxGgmlBackendType preferredBackend = ofxGgmlBackendType::Gpu;

	/// Size of the default computation-graph arena in nodes.
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

/// Last recorded timings for major runtime stages.
struct ofxGgmlTimings {
	float setupMs = 0.0f;
	float allocMs = 0.0f;
	float weightUploadMs = 0.0f;
	float computeSubmitMs = 0.0f;
	float computeTotalMs = 0.0f;
};

/// Progress and log callback signature.
using ofxGgmlLogCallback = std::function<void(int level, const std::string & message)>;
