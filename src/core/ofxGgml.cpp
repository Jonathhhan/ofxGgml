#include "ofxGgml.h"

#include "ggml.h"
#include "ggml-alloc.h"
#include "ggml-backend.h"
#include "ggml-cpu.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// --------------------------------------------------------------------------
//  Early environment setup
// --------------------------------------------------------------------------
//
// ggml's static initializer (ggml.cpp:22) installs a custom
// std::terminate handler.  When a second copy of libggml-base is loaded
// at runtime (e.g. via dlopen when ggml_backend_load_all() loads GPU
// backend plugins), its static initializer runs again and asserts that
// the handler was not already replaced.  Setting GGML_NO_BACKTRACE
// tells the initializer to skip the handler, avoiding the assertion.
//
// We set the variable as early as possible — via a high-priority
// constructor on GCC/Clang, or via an init_seg(lib) on MSVC — so it is
// in place before any ggml static initializer runs.

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
static void ofxGgml_earlyEnvSetup() {
	setenv("GGML_NO_BACKTRACE", "1", 0); // don't overwrite if already set
}
#elif defined(_MSC_VER)
#pragma init_seg(lib)
static struct OfxGgmlEarlyEnv {
	OfxGgmlEarlyEnv() {
		size_t len = 0;
		// Only set if not already present, matching the setenv() behavior.
		if (getenv_s(&len, nullptr, 0, "GGML_NO_BACKTRACE") != 0 || len == 0) {
			_putenv_s("GGML_NO_BACKTRACE", "1");
		}
	}
} s_ofxGgmlEarlyEnv;
#endif

// --------------------------------------------------------------------------
//  Default console log callback
// --------------------------------------------------------------------------

/// Maps a ggml log level integer to a short label.
static const char * logLevelLabel(int level) {
	switch (level) {
		case 1:  return "[DEBUG] ";
		case 2:  return "[INFO]  ";
		case 3:  return "[WARN]  ";
		case 4:  return "[ERROR] ";
		default: return "";        // NONE (0) and CONT (5)
	}
}

/// Default callback that prints to stderr so that messages are always
/// visible even when the caller does not install a custom callback.
static void defaultLogCallback(int level, const std::string & message) {
	if (message.empty()) return;
	fprintf(stderr, "ofxGgml %s%s", logLevelLabel(level), message.c_str());
	// GGML messages usually include a trailing newline; add one only
	// when the message does not already end with one.
	if (message.back() != '\n') {
		fputc('\n', stderr);
	}
}

// --------------------------------------------------------------------------
//  PIMPL
// --------------------------------------------------------------------------

struct ofxGgml::Impl {
	ofxGgmlState state = ofxGgmlState::Uninitialized;
	ofxGgmlSettings settings;

	ggml_backend_t backend = nullptr;
	ggml_backend_t cpuBackend = nullptr;
	ggml_backend_sched_t sched = nullptr;

	/// Log callback — initialised to the built-in console logger so that
	/// messages are visible by default.
	ofxGgmlLogCallback logCb = defaultLogCallback;

	void log(int level, const std::string & msg) {
		if (logCb) logCb(level, msg);
	}
};

// --------------------------------------------------------------------------
//  Static log callback for ggml
// --------------------------------------------------------------------------

static void ggmlLogCallback(ggml_log_level level, const char * text, void * user_data) {
	auto * impl = static_cast<ofxGgml::Impl *>(user_data);
	if (impl && impl->logCb) {
		impl->logCb(static_cast<int>(level), text ? text : "");
	}
}

// --------------------------------------------------------------------------
//  Lifecycle
// --------------------------------------------------------------------------

ofxGgml::ofxGgml()
	: m_impl(std::make_unique<Impl>()) {}

ofxGgml::~ofxGgml() {
	close();
}

bool ofxGgml::setup(const ofxGgmlSettings & settings) {
	if (m_impl->state != ofxGgmlState::Uninitialized) {
		close();
	}

	m_impl->settings = settings;
	ggml_log_set(ggmlLogCallback, m_impl.get());

	// Load all available backend libraries (CUDA, Metal, Vulkan, …).
	// Guard: only load once per process to avoid triggering the
	// GGML_ASSERT(prev != ggml_uncaught_exception) in ggml.cpp:22
	// when backend shared libraries are loaded via dlopen(RTLD_LOCAL)
	// and pull in a second copy of libggml-base.
	static bool backendsLoaded = false;
	if (!backendsLoaded) {
		// Set GGML_NO_BACKTRACE so that any additional copies of
		// libggml-base loaded via dlopen() skip the terminate-handler
		// static initializer that triggers the assertion.
#ifdef _WIN32
		{
			size_t len = 0;
			if (getenv_s(&len, nullptr, 0, "GGML_NO_BACKTRACE") != 0 || len == 0) {
				_putenv_s("GGML_NO_BACKTRACE", "1");
			}
		}
#else
		setenv("GGML_NO_BACKTRACE", "1", 0); // don't overwrite if already set
#endif
		ggml_backend_load_all();
		backendsLoaded = true;
	}

	// Log discovered devices so the user can see what is available.
	{
		const size_t devCount = ggml_backend_dev_count();
		m_impl->log(GGML_LOG_LEVEL_INFO,
			"ofxGgml: discovered " + std::to_string(devCount) + " backend device(s):\n");
		bool hasGpu = false;
		for (size_t i = 0; i < devCount; i++) {
			ggml_backend_dev_t dev = ggml_backend_dev_get(i);
			const char * devName = ggml_backend_dev_name(dev);
			const char * devDesc = ggml_backend_dev_description(dev);
			enum ggml_backend_dev_type devType = ggml_backend_dev_type(dev);
			const char * typeLabel = "CPU";
			if (devType == GGML_BACKEND_DEVICE_TYPE_GPU) {
				typeLabel = "GPU";
				hasGpu = true;
			} else if (devType != GGML_BACKEND_DEVICE_TYPE_CPU) {
				typeLabel = "Accelerator";
				hasGpu = true;
			}
			m_impl->log(GGML_LOG_LEVEL_INFO,
				"ofxGgml:   [" + std::to_string(i) + "] " +
				(devName ? devName : "?") + " — " +
				(devDesc ? devDesc : "") + " (" + typeLabel + ")\n");
		}
		if (!hasGpu && settings.preferredBackend != ofxGgmlBackendType::Cpu) {
			m_impl->log(GGML_LOG_LEVEL_WARN,
				"ofxGgml: no usable GPU found — will fall back to CPU\n");
		}
	}

	// Initialize the preferred backend.
	if (settings.deviceIndex >= 0) {
		// A specific device index was requested — try to initialise it.
		const size_t devCount = ggml_backend_dev_count();
		if (static_cast<size_t>(settings.deviceIndex) < devCount) {
			ggml_backend_dev_t dev = ggml_backend_dev_get(
				static_cast<size_t>(settings.deviceIndex));
			m_impl->backend = ggml_backend_dev_init(dev, nullptr);
			if (m_impl->backend) {
				m_impl->log(GGML_LOG_LEVEL_INFO,
					"ofxGgml: initialised requested device index " +
					std::to_string(settings.deviceIndex) + "\n");
			} else {
				m_impl->log(GGML_LOG_LEVEL_WARN,
					"ofxGgml: failed to initialise device index " +
					std::to_string(settings.deviceIndex) + ", trying alternatives\n");
			}
		} else {
			m_impl->log(GGML_LOG_LEVEL_WARN,
				"ofxGgml: requested device index " +
				std::to_string(settings.deviceIndex) + " out of range (" +
				std::to_string(ggml_backend_dev_count()) + " available)\n");
		}
	}

	if (!m_impl->backend) {
		m_impl->backend = ggml_backend_init_by_type(
			static_cast<enum ggml_backend_dev_type>(settings.preferredBackend), nullptr);
	}

	if (!m_impl->backend) {
		// Fall back to the best available.
		m_impl->backend = ggml_backend_init_best();
	}

	if (!m_impl->backend) {
		m_impl->state = ofxGgmlState::Error;
		m_impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: failed to initialize any backend\n");
		return false;
	}

	// Ensure we always have a CPU backend for scheduling.
	m_impl->cpuBackend = ggml_backend_init_by_type(GGML_BACKEND_DEVICE_TYPE_CPU, nullptr);
	if (!m_impl->cpuBackend) {
		m_impl->state = ofxGgmlState::Error;
		m_impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: failed to initialize CPU backend\n");
		ggml_backend_free(m_impl->backend);
		m_impl->backend = nullptr;
		return false;
	}

	// Set thread count.
	if (settings.threads > 0) {
		ggml_backend_cpu_set_n_threads(m_impl->cpuBackend, settings.threads);
	}

	// Build scheduler with up to 2 backends.
	ggml_backend_t backends[2] = { m_impl->backend, m_impl->cpuBackend };
	int nBackends = (m_impl->backend == m_impl->cpuBackend) ? 1 : 2;
	m_impl->sched = ggml_backend_sched_new(
		backends, nullptr, nBackends,
		static_cast<size_t>(settings.graphSize), false, true);

	if (!m_impl->sched) {
		m_impl->state = ofxGgmlState::Error;
		m_impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: failed to create backend scheduler\n");
		ggml_backend_free(m_impl->backend);
		ggml_backend_free(m_impl->cpuBackend);
		m_impl->backend = nullptr;
		m_impl->cpuBackend = nullptr;
		return false;
	}

	m_impl->state = ofxGgmlState::Ready;
	m_impl->log(GGML_LOG_LEVEL_INFO, std::string("ofxGgml: ready (backend: ") +
		ggml_backend_name(m_impl->backend) + ")\n");
	return true;
}

void ofxGgml::close() {
	if (m_impl->sched) {
		ggml_backend_sched_free(m_impl->sched);
		m_impl->sched = nullptr;
	}
	// Guard: if backend and cpuBackend point to the same allocation,
	// only free once to avoid double-free.
	const bool sameBackend = (m_impl->backend && m_impl->backend == m_impl->cpuBackend);
	if (m_impl->backend) {
		ggml_backend_free(m_impl->backend);
		m_impl->backend = nullptr;
	}
	if (m_impl->cpuBackend && !sameBackend) {
		ggml_backend_free(m_impl->cpuBackend);
	}
	m_impl->cpuBackend = nullptr;
	// Clear the global log callback to prevent use-after-free.
	ggml_log_set(nullptr, nullptr);
	m_impl->state = ofxGgmlState::Uninitialized;
}

ofxGgmlState ofxGgml::getState() const {
	return m_impl->state;
}

bool ofxGgml::isReady() const {
	return m_impl->state == ofxGgmlState::Ready;
}

// --------------------------------------------------------------------------
//  Device enumeration
// --------------------------------------------------------------------------

std::vector<ofxGgmlDeviceInfo> ofxGgml::listDevices() const {
	std::vector<ofxGgmlDeviceInfo> devices;
	const size_t n = ggml_backend_dev_count();
	devices.reserve(n);
	for (size_t i = 0; i < n; i++) {
		ggml_backend_dev_t dev = ggml_backend_dev_get(i);
		ofxGgmlDeviceInfo info;
		info.name = ggml_backend_dev_name(dev);
		info.description = ggml_backend_dev_description(dev);
		info.type = static_cast<ofxGgmlBackendType>(ggml_backend_dev_type(dev));
		size_t free = 0, total = 0;
		ggml_backend_dev_memory(dev, &free, &total);
		info.memoryFree = free;
		info.memoryTotal = total;
		devices.push_back(std::move(info));
	}
	return devices;
}

std::string ofxGgml::getBackendName() const {
	if (!m_impl->backend) return "none";
	return ggml_backend_name(m_impl->backend);
}

// --------------------------------------------------------------------------
//  Tensor data helpers
// --------------------------------------------------------------------------

void ofxGgml::setTensorData(ofxGgmlTensor tensor, const void * data, size_t bytes) {
	if (!tensor.raw() || !data) return;
	ggml_backend_tensor_set(tensor.raw(), data, 0, bytes);
}

void ofxGgml::getTensorData(ofxGgmlTensor tensor, void * data, size_t bytes) const {
	if (!tensor.raw() || !data) return;
	ggml_backend_tensor_get(tensor.raw(), data, 0, bytes);
}

// --------------------------------------------------------------------------
//  Computation
// --------------------------------------------------------------------------

bool ofxGgml::allocGraph(ofxGgmlGraph & graph) {
	if (m_impl->state != ofxGgmlState::Ready) {
		m_impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: not ready\n");
		return false;
	}
	if (!graph.raw()) {
		m_impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: graph not built (call graph.build() first)\n");
		return false;
	}

	ggml_backend_sched_reset(m_impl->sched);

	if (!ggml_backend_sched_alloc_graph(m_impl->sched, graph.raw())) {
		m_impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: graph allocation failed\n");
		return false;
	}
	return true;
}

ofxGgmlComputeResult ofxGgml::computeGraph(ofxGgmlGraph & graph) {
	ofxGgmlComputeResult result;

	if (m_impl->state != ofxGgmlState::Ready) {
		result.error = "ofxGgml: not ready";
		return result;
	}
	if (!graph.raw()) {
		result.error = "ofxGgml: graph not built (call graph.build() first)";
		return result;
	}

	m_impl->state = ofxGgmlState::Computing;

	auto t0 = std::chrono::steady_clock::now();

	enum ggml_status status = ggml_backend_sched_graph_compute(m_impl->sched, graph.raw());

	auto t1 = std::chrono::steady_clock::now();
	result.elapsedMs = std::chrono::duration<float, std::milli>(t1 - t0).count();

	if (status == GGML_STATUS_SUCCESS) {
		result.success = true;
	} else {
		result.error = std::string("ofxGgml: compute failed (status ") +
			ggml_status_to_string(status) + ")";
	}

	m_impl->state = ofxGgmlState::Ready;
	return result;
}

ofxGgmlComputeResult ofxGgml::compute(ofxGgmlGraph & graph) {
	ofxGgmlComputeResult result;

	if (!allocGraph(graph)) {
		result.error = "ofxGgml: graph allocation failed";
		return result;
	}

	return computeGraph(graph);
}

// --------------------------------------------------------------------------
//  Model weight loading
// --------------------------------------------------------------------------

bool ofxGgml::loadModelWeights(ofxGgmlModel & model) {
	if (m_impl->state != ofxGgmlState::Ready) {
		m_impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: not ready\n");
		return false;
	}
	if (!model.isLoaded()) {
		m_impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: model not loaded\n");
		return false;
	}

	struct ggml_context * modelCtx = model.ggmlContext();
	if (!modelCtx) {
		m_impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: model has no ggml context\n");
		return false;
	}

	// The model was loaded with no_alloc=false, so every tensor's
	// data pointer currently points into the ggml_context's host
	// memory buffer.  We need to:
	//   1. Save each tensor's current (host) data pointer.
	//   2. Allocate a backend buffer (which reassigns data pointers).
	//   3. Copy the saved host data into the backend buffer.

	// Step 1 — snapshot host pointers for every tensor.
	struct TensorSnapshot {
		struct ggml_tensor * tensor;
		const void * hostData;
		size_t      bytes;
	};
	std::vector<TensorSnapshot> snapshots;

	for (struct ggml_tensor * cur = ggml_get_first_tensor(modelCtx);
		cur != nullptr;
		cur = ggml_get_next_tensor(modelCtx, cur))
	{
		if (cur->data) {
			snapshots.push_back({cur, cur->data, ggml_nbytes(cur)});
		}
	}

	// Step 2 — allocate a backend buffer for all context tensors.
	ggml_backend_buffer_t buf = ggml_backend_alloc_ctx_tensors(modelCtx, m_impl->backend);
	if (!buf) {
		m_impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: failed to allocate backend buffer for model weights\n");
		return false;
	}

	// Step 3 — copy host data into the (possibly GPU-resident) buffer.
	for (const auto & snap : snapshots) {
		ggml_backend_tensor_set(snap.tensor, snap.hostData, 0, snap.bytes);
	}

	m_impl->log(GGML_LOG_LEVEL_INFO, std::string("ofxGgml: model weights loaded (") +
		std::to_string(snapshots.size()) + " tensors on backend: " +
		ggml_backend_name(m_impl->backend) + ")\n");
	return true;
}

// --------------------------------------------------------------------------
//  Logging
// --------------------------------------------------------------------------

void ofxGgml::setLogCallback(ofxGgmlLogCallback cb) {
	m_impl->logCb = std::move(cb);
}

// --------------------------------------------------------------------------
//  Low-level access
// --------------------------------------------------------------------------

struct ggml_backend * ofxGgml::getBackend() {
	return m_impl->backend;
}

struct ggml_backend * ofxGgml::getCpuBackend() {
	return m_impl->cpuBackend;
}

struct ggml_backend_sched * ofxGgml::getScheduler() {
	return m_impl->sched;
}
