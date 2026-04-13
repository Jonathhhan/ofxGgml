#include "ofxGgmlCore.h"

#include "ggml.h"
#include "ggml-alloc.h"
#include "ggml-backend.h"
#include "ggml-cpu.h"

#include <chrono>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <mutex>

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

	/// Backend buffer for uploaded model weights.
	ggml_backend_buffer_t modelWeightBuf = nullptr;

	/// Last graph reserved/allocated for scheduler reuse.
	struct ggml_cgraph * reservedGraph = nullptr;
	struct ggml_cgraph * allocatedGraph = nullptr;

	/// Async compute tracking.
	bool hasPendingAsync = false;
	std::chrono::steady_clock::time_point asyncStart;

	/// Last timings.
	ofxGgmlTimings timings;

	/// Log callback — initialised to the built-in console logger so that
	/// messages are visible by default.
	ofxGgmlLogCallback logCb = defaultLogCallback;

	void log(int level, const std::string & msg) {
		if (logCb) logCb(level, msg);
	}
};

// --------------------------------------------------------------------------
//  Device validation helper
// --------------------------------------------------------------------------

/// Returns true when the device reports usable memory (total > 0 and
/// free > 0).  CPU devices always pass — this check is only meaningful
/// for GPU / accelerator devices where the driver may enumerate the
/// device even though it cannot allocate memory.
static bool isDeviceMemoryAvailable(ggml_backend_dev_t dev) {
	if (!dev) return false;
	enum ggml_backend_dev_type dt = ggml_backend_dev_type(dev);
	if (dt == GGML_BACKEND_DEVICE_TYPE_CPU) return true;
	size_t free = 0, total = 0;
	ggml_backend_dev_memory(dev, &free, &total);
	return total > 0 && free > 0;
}

// --------------------------------------------------------------------------
//  Abort recovery for ggml calls
// --------------------------------------------------------------------------
//
// ggml calls GGML_ABORT("fatal error") → ggml_abort() → abort() when an
// internal allocation fails (e.g. inside ggml_malloc / ggml_calloc).
//
// Since ggml is compiled directly into the addon as a static library,
// there is only a single copy of libggml-base.  The abort callback set
// via ggml_set_abort_callback() is always effective — no SIGABRT handler
// or environment-variable workarounds are needed.

static thread_local std::jmp_buf s_ggmlAbortJmpBuf;
static thread_local bool s_ggmlAbortGuardActive = false;
static thread_local char s_ggmlAbortMsg[256] = {};

/// ggml abort callback — intercepts ggml_abort() and longjmp's back to
/// the recovery point set by guardedGgmlCall().
static void ggmlAbortHandler(const char * message) {
	if (s_ggmlAbortGuardActive) {
		if (message) {
			std::strncpy(s_ggmlAbortMsg, message,
				sizeof(s_ggmlAbortMsg) - 1);
			s_ggmlAbortMsg[sizeof(s_ggmlAbortMsg) - 1] = '\0';
		} else {
			s_ggmlAbortMsg[0] = '\0';
		}
		s_ggmlAbortGuardActive = false;
		std::longjmp(s_ggmlAbortJmpBuf, 1);
	}
}

/// Execute a callable with abort recovery.  If the callable triggers a
/// fatal ggml abort (GGML_ABORT / ggml_abort()), the longjmp fires and
/// this function returns false.  On success it returns true.
///
/// @param fn       Callable to execute.
/// @param context  Short label for log messages (e.g. "backend init").
///
/// A mutex serialises access because ggml_set_abort_callback() operates
/// at process-wide scope.
template<typename F>
static bool guardedGgmlCall(F && fn, const char * context = nullptr) {
	static std::mutex mtx;
	std::lock_guard<std::mutex> lock(mtx);

	ggml_abort_callback_t prevAbortCb = ggml_set_abort_callback(ggmlAbortHandler);

	s_ggmlAbortGuardActive = true;
	s_ggmlAbortMsg[0] = '\0';

	bool ok = false;
	if (setjmp(s_ggmlAbortJmpBuf) == 0) {
		fn();
		ok = true;
	} else {
		fprintf(stderr, "ofxGgml: ggml abort caught%s%s: %s\n",
			context ? " during " : "",
			context ? context : "",
			s_ggmlAbortMsg[0] ? s_ggmlAbortMsg : "unknown error");
	}

	s_ggmlAbortGuardActive = false;
	ggml_set_abort_callback(prevAbortCb);

	return ok;
}

/// Try to initialise a backend device, catching any fatal ggml abort
/// so the process can continue with a CPU fallback.
static ggml_backend_t tryInitBackendDev(ggml_backend_dev_t dev) {
	ggml_backend_t result = nullptr;
	guardedGgmlCall([&]() {
		result = ggml_backend_dev_init(dev, nullptr);
	}, "GPU backend init");
	return result;
}

// --------------------------------------------------------------------------
//  Static log callback for ggml
// --------------------------------------------------------------------------

static void ggmlLogCallback(ggml_log_level level, const char * text, void * user_data) {
	auto * impl = static_cast<ofxGgml::Impl *>(user_data);
	if (impl && impl->logCb) {
		impl->logCb(static_cast<int>(level), text ? text : "");
	}
}

static bool validateGraphForCompute(struct ggml_cgraph * graph, std::string & error) {
	if (!graph) {
		error = "graph not built (call graph.build() first)";
		return false;
	}
	const int n = ggml_graph_n_nodes(graph);
	if (n <= 0) {
		error = "graph has no compute nodes";
		return false;
	}
	for (int i = 0; i < n; ++i) {
		struct ggml_tensor * node = ggml_graph_node(graph, i);
		if (!node) {
			error = "graph node " + std::to_string(i) + " is null";
			return false;
		}
		const int nd = ggml_n_dims(node);
		if (nd <= 0 || nd > GGML_MAX_DIMS) {
			error = "graph node " + std::to_string(i) + " has invalid rank " + std::to_string(nd);
			return false;
		}
		for (int d = 0; d < nd; ++d) {
			if (node->ne[d] <= 0) {
				error = "graph node " + std::to_string(i) + " has non-positive shape at dim " + std::to_string(d);
				return false;
			}
		}
		if (node->op < 0 || node->op >= GGML_OP_COUNT) {
			error = "graph node " + std::to_string(i) + " has invalid op code";
			return false;
		}
	}
	return true;
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
	auto tSetup0 = std::chrono::steady_clock::now();

	if (m_impl->state != ofxGgmlState::Uninitialized) {
		close();
	}

	m_impl->settings = settings;
	ggml_log_set(ggmlLogCallback, m_impl.get());

	// With static linking, GPU backends compiled into the library are
	// registered automatically via the ggml_backend_registry constructor
	// (triggered by #ifdef GGML_USE_CUDA / GGML_USE_VULKAN / etc. in
	// ggml-backend-reg.cpp).  We still call ggml_backend_load_all()
	// to pick up any additional runtime-loadable backends, but this is
	// safe — there is only one copy of libggml-base so no duplicate
	// static initializer assertions.
	static bool backendsLoaded = false;
	if (!backendsLoaded) {
		guardedGgmlCall([&]() {
			ggml_backend_load_all();
		}, "backend loading");
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
		if (!hasGpu && settings.preferredBackendName.empty() &&
			settings.preferredBackend != ofxGgmlBackendType::Cpu) {
			m_impl->log(GGML_LOG_LEVEL_WARN,
				"ofxGgml: no usable GPU found — will fall back to CPU\n");
		}
	}

	// Initialize the preferred backend.
	//
	// When a GPU backend is requested we first validate that the device
	// actually reports usable memory.  Some systems enumerate a GPU
	// device (e.g. via ggml_backend_load_all()) even though the
	// underlying driver cannot create a working context – attempting to
	// initialise such a device can trigger a fatal GGML_ABORT inside
	// ggml_malloc/ggml_calloc when an internal allocation fails.
	//
	// The validation mirrors the approach used by stable-diffusion.cpp:
	// only attempt GPU init when the device is genuinely available, and
	// fall back to CPU immediately otherwise.

	if (!m_impl->backend && !settings.preferredBackendName.empty()) {
		// Validate the named device before attempting init.
		ggml_backend_dev_t namedDev = ggml_backend_dev_by_name(
			settings.preferredBackendName.c_str());
		if (namedDev) {
			if (isDeviceMemoryAvailable(namedDev)) {
				m_impl->backend = tryInitBackendDev(namedDev);
			} else {
				m_impl->log(GGML_LOG_LEVEL_WARN,
					"ofxGgml: device \"" + settings.preferredBackendName +
					"\" reports no usable memory — skipping\n");
			}
		}
		if (!m_impl->backend) {
			m_impl->log(GGML_LOG_LEVEL_WARN,
				"ofxGgml: backend \"" + settings.preferredBackendName +
				"\" not found or failed to init — trying fallback\n");
		}
	}
	if (!m_impl->backend &&
		settings.preferredBackend != ofxGgmlBackendType::Cpu) {
		// Try the preferred type, but validate the device first.
		ggml_backend_dev_t typeDev = ggml_backend_dev_by_type(
			static_cast<enum ggml_backend_dev_type>(settings.preferredBackend));
		if (typeDev && isDeviceMemoryAvailable(typeDev)) {
			m_impl->backend = tryInitBackendDev(typeDev);
		} else if (typeDev) {
			m_impl->log(GGML_LOG_LEVEL_WARN,
				"ofxGgml: preferred device type reports no usable memory"
				" — falling back\n");
		}
	}
	if (!m_impl->backend) {
		// Explicit CPU init as a fallback — avoids ggml_backend_init_best()
		// which would attempt GPU init again and could crash.
		guardedGgmlCall([&]() {
			m_impl->backend = ggml_backend_init_by_type(
				GGML_BACKEND_DEVICE_TYPE_CPU, nullptr);
		}, "CPU backend init");
	}

	if (!m_impl->backend) {
		m_impl->state = ofxGgmlState::Error;
		m_impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: failed to initialize any backend\n");
		return false;
	}

	// Ensure we always have a CPU backend for scheduling.
	guardedGgmlCall([&]() {
		m_impl->cpuBackend = ggml_backend_init_by_type(
			GGML_BACKEND_DEVICE_TYPE_CPU, nullptr);
	}, "CPU scheduling backend init");
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
	guardedGgmlCall([&]() {
		m_impl->sched = ggml_backend_sched_new(
			backends, nullptr, nBackends,
			static_cast<size_t>(settings.graphSize), false, true);
	}, "scheduler creation");

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
	auto tSetup1 = std::chrono::steady_clock::now();
	m_impl->timings.setupMs = std::chrono::duration<float, std::milli>(tSetup1 - tSetup0).count();
	m_impl->log(GGML_LOG_LEVEL_INFO, std::string("ofxGgml: ready (backend: ") +
		ggml_backend_name(m_impl->backend) + ")\n");
	return true;
}

void ofxGgml::close() {
	if (m_impl->sched) {
		ggml_backend_sched_free(m_impl->sched);
		m_impl->sched = nullptr;
	}
	// Graph tracking is tied to scheduler lifetime; after close(), any
	// previously tracked graph allocations/reservations are invalid.
	m_impl->reservedGraph = nullptr;
	m_impl->allocatedGraph = nullptr;
	m_impl->hasPendingAsync = false;
	// Free model weight buffer before backends.
	if (m_impl->modelWeightBuf) {
		ggml_backend_buffer_free(m_impl->modelWeightBuf);
		m_impl->modelWeightBuf = nullptr;
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

static size_t clampedTensorTransferSize(const struct ggml_tensor * tensor, size_t bytes) {
	if (!tensor) return 0;
	const size_t tensorBytes = ggml_nbytes(tensor);
	return bytes < tensorBytes ? bytes : tensorBytes;
}

void ofxGgml::setTensorData(ofxGgmlTensor tensor, const void * data, size_t bytes) {
	if (!tensor.raw() || !data) return;
	const size_t tensorBytes = ggml_nbytes(tensor.raw());
	const size_t safeBytes = clampedTensorTransferSize(tensor.raw(), bytes);
	if (safeBytes == 0) return;
	if (safeBytes != bytes) {
		m_impl->log(GGML_LOG_LEVEL_WARN,
			"ofxGgml: setTensorData clamped " + std::to_string(bytes) +
			" bytes to tensor size " + std::to_string(tensorBytes) + " bytes\n");
	}
	ggml_backend_tensor_set(tensor.raw(), data, 0, safeBytes);
}

void ofxGgml::getTensorData(ofxGgmlTensor tensor, void * data, size_t bytes) const {
	if (!tensor.raw() || !data) return;
	const size_t tensorBytes = ggml_nbytes(tensor.raw());
	const size_t safeBytes = clampedTensorTransferSize(tensor.raw(), bytes);
	if (safeBytes == 0) return;
	if (safeBytes != bytes) {
		m_impl->log(GGML_LOG_LEVEL_WARN,
			"ofxGgml: getTensorData clamped " + std::to_string(bytes) +
			" bytes to tensor size " + std::to_string(tensorBytes) + " bytes\n");
	}
	ggml_backend_tensor_get(tensor.raw(), data, 0, safeBytes);
}

// --------------------------------------------------------------------------
//  Computation
// --------------------------------------------------------------------------

bool ofxGgml::allocGraph(ofxGgmlGraph & graph) {
	if (m_impl->state != ofxGgmlState::Ready) {
		m_impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: not ready\n");
		return false;
	}
	std::string validationError;
	if (!validateGraphForCompute(graph.raw(), validationError)) {
		m_impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: invalid graph: " + validationError + "\n");
		return false;
	}

	if (m_impl->allocatedGraph == graph.raw()) {
		return true;
	}

	auto t0 = std::chrono::steady_clock::now();

	ggml_backend_sched_reset(m_impl->sched);
	m_impl->allocatedGraph = nullptr;

	if (m_impl->reservedGraph != graph.raw()) {
		if (!ggml_backend_sched_reserve(m_impl->sched, graph.raw())) {
			m_impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: scheduler reserve failed\n");
			return false;
		}
		m_impl->reservedGraph = graph.raw();
	}

	if (!ggml_backend_sched_alloc_graph(m_impl->sched, graph.raw())) {
		m_impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: graph allocation failed\n");
		return false;
	}
	m_impl->allocatedGraph = graph.raw();
	auto t1 = std::chrono::steady_clock::now();
	m_impl->timings.allocMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
	return true;
}

ofxGgmlComputeResult ofxGgml::computeGraph(ofxGgmlGraph & graph) {
	ofxGgmlComputeResult submit = computeGraphAsync(graph);
	if (!submit.success) {
		return submit;
	}
	return synchronize();
}

ofxGgmlComputeResult ofxGgml::computeGraphAsync(ofxGgmlGraph & graph) {
	ofxGgmlComputeResult result;

	if (m_impl->state != ofxGgmlState::Ready) {
		if (m_impl->state == ofxGgmlState::Computing && m_impl->hasPendingAsync) {
			result.error = "ofxGgml: async compute already in flight (call synchronize() first)";
			return result;
		}
		result.error = "ofxGgml: not ready";
		return result;
	}
	std::string validationError;
	if (!validateGraphForCompute(graph.raw(), validationError)) {
		result.error = std::string("ofxGgml: invalid graph: ") + validationError;
		return result;
	}

	if (m_impl->allocatedGraph != graph.raw()) {
		if (!allocGraph(graph)) {
			result.error = "ofxGgml: graph allocation failed";
			return result;
		}
	}

	m_impl->state = ofxGgmlState::Computing;

	auto t0 = std::chrono::steady_clock::now();

	enum ggml_status status = ggml_backend_sched_graph_compute_async(m_impl->sched, graph.raw());

	auto t1 = std::chrono::steady_clock::now();
	result.elapsedMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
	m_impl->timings.computeSubmitMs = result.elapsedMs;

	if (status == GGML_STATUS_SUCCESS) {
		result.success = true;
		m_impl->hasPendingAsync = true;
		m_impl->asyncStart = t0;
	} else {
		result.error = std::string("ofxGgml: compute failed (status ") +
			ggml_status_to_string(status) + ")";
		m_impl->state = ofxGgmlState::Ready;
	}

	return result;
}

ofxGgmlComputeResult ofxGgml::synchronize() {
	ofxGgmlComputeResult result;
	if (m_impl->state != ofxGgmlState::Computing || !m_impl->hasPendingAsync) {
		result.success = true;
		return result;
	}

	ggml_backend_sched_synchronize(m_impl->sched);
	auto t1 = std::chrono::steady_clock::now();
	m_impl->timings.computeTotalMs =
		std::chrono::duration<float, std::milli>(t1 - m_impl->asyncStart).count();

	result.success = true;
	result.elapsedMs = m_impl->timings.computeTotalMs;
	m_impl->hasPendingAsync = false;
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

	auto t0 = std::chrono::steady_clock::now();

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
	const int64_t expectedTensors = model.getNumTensors();
	if (expectedTensors > 0) {
		snapshots.reserve(static_cast<size_t>(expectedTensors));
	}

	for (struct ggml_tensor * cur = ggml_get_first_tensor(modelCtx);
		cur != nullptr;
		cur = ggml_get_next_tensor(modelCtx, cur))
	{
		if (cur->data) {
			snapshots.push_back({cur, cur->data, ggml_nbytes(cur)});
		}
	}

	// Step 2 — allocate a backend buffer for all context tensors.
	// Free any previously allocated model weight buffer.
	if (m_impl->modelWeightBuf) {
		ggml_backend_buffer_free(m_impl->modelWeightBuf);
		m_impl->modelWeightBuf = nullptr;
	}
	ggml_backend_buffer_t buf = ggml_backend_alloc_ctx_tensors_from_buft(
		modelCtx, ggml_backend_get_default_buffer_type(m_impl->backend));
	if (!buf) {
		m_impl->log(GGML_LOG_LEVEL_WARN,
			"ofxGgml: alloc_ctx_tensors_from_buft failed, falling back to alloc_ctx_tensors\n");
		buf = ggml_backend_alloc_ctx_tensors(modelCtx, m_impl->backend);
	}
	if (!buf) {
		m_impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: failed to allocate backend buffer for model weights\n");
		return false;
	}
	ggml_backend_buffer_set_usage(buf, GGML_BACKEND_BUFFER_USAGE_WEIGHTS);
	m_impl->modelWeightBuf = buf;

	// Step 3 — copy host data into the (possibly GPU-resident) buffer.
	ggml_backend_t uploadBackend = m_impl->backend ? m_impl->backend : m_impl->cpuBackend;
	for (const auto & snap : snapshots) {
		ggml_backend_tensor_set_async(uploadBackend, snap.tensor, snap.hostData, 0, snap.bytes);
	}
	ggml_backend_synchronize(uploadBackend);

	auto t1 = std::chrono::steady_clock::now();
	m_impl->timings.weightUploadMs = std::chrono::duration<float, std::milli>(t1 - t0).count();

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

ofxGgmlTimings ofxGgml::getLastTimings() const {
	return m_impl->timings;
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
