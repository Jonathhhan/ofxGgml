#include "ofxGgmlCore.h"

#include "ggml.h"
#include "ggml-alloc.h"
#include "ggml-backend.h"
#include "ggml-cpu.h"
#if defined(_WIN32)
#include "ggml-cuda.h"
#include "ggml-vulkan.h"
#endif

#include <chrono>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <mutex>
#include <vector>

// --------------------------------------------------------------------------
//  Default console log callback
// --------------------------------------------------------------------------

/// Maps a ggml log level integer to a short label.
static const char * logLevelLabel(int level) noexcept {
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
			const size_t copyLen = std::min(std::strlen(message), sizeof(s_ggmlAbortMsg) - 1);
			std::memcpy(s_ggmlAbortMsg, message, copyLen);
			s_ggmlAbortMsg[copyLen] = '\0';
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

static bool hasPrefixIgnoreCase(const char * value, const char * prefix) noexcept {
	if (!value || !prefix) return false;
	while (*prefix) {
		if (*value == '\0') return false;
		const unsigned char ac = static_cast<unsigned char>(*value);
		const unsigned char bc = static_cast<unsigned char>(*prefix);
		const unsigned char lc_a = (ac >= 'A' && ac <= 'Z') ? (ac | 0x20) : ac;
		const unsigned char lc_b = (bc >= 'A' && bc <= 'Z') ? (bc | 0x20) : bc;
		if (lc_a != lc_b) return false;
		++value;
		++prefix;
	}
	return true;
}

static bool isEnvVarSet(const char * name) {
	if (!name || *name == '\0') return false;
#if defined(_WIN32)
	char * value = nullptr;
	size_t len = 0;
	const errno_t err = _dupenv_s(&value, &len, name);
	const bool isSet = (err == 0 && value != nullptr && len > 0);
	free(value);
	return isSet;
#else
	const char * value = std::getenv(name);
	return value != nullptr && *value != '\0';
#endif
}

static ggml_backend_dev_t findUsableDeviceByNamePrefix(const char * prefix) {
	const size_t devCount = ggml_backend_dev_count();
	for (size_t i = 0; i < devCount; ++i) {
		ggml_backend_dev_t dev = ggml_backend_dev_get(i);
		if (!dev) continue;
		const char * name = ggml_backend_dev_name(dev);
		if (!hasPrefixIgnoreCase(name, prefix)) continue;
		return dev;
	}
	return nullptr;
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

static std::mutex s_logOwnerMutex;
static std::vector<ofxGgml::Impl *> s_logOwners;

static void registerLogCallbackOwner(ofxGgml::Impl * impl) {
	std::lock_guard<std::mutex> lock(s_logOwnerMutex);
	s_logOwners.erase(std::remove(s_logOwners.begin(), s_logOwners.end(), impl), s_logOwners.end());
	s_logOwners.push_back(impl);
	ggml_log_set(ggmlLogCallback, impl);
}

static void unregisterLogCallbackOwner(ofxGgml::Impl * impl) {
	std::lock_guard<std::mutex> lock(s_logOwnerMutex);
	s_logOwners.erase(std::remove(s_logOwners.begin(), s_logOwners.end(), impl), s_logOwners.end());
	if (s_logOwners.empty()) {
		ggml_log_set(nullptr, nullptr);
		return;
	}
	ggml_log_set(ggmlLogCallback, s_logOwners.back());
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
	registerLogCallbackOwner(m_impl.get());

	// With static linking, GPU backends compiled into the library are
	// registered automatically via the ggml_backend_registry constructor
	// (triggered by #ifdef GGML_USE_CUDA / GGML_USE_VULKAN / etc. in
	// ggml-backend-reg.cpp).  We still call ggml_backend_load_all()
	// to pick up any additional runtime-loadable backends, but this is
	// safe — there is only one copy of libggml-base so no duplicate
	// static initializer assertions.
	static std::once_flag backendLoadOnce;
	std::call_once(backendLoadOnce, [&]() {
		guardedGgmlCall([&]() {
			ggml_backend_load_all();
		}, "backend loading");
	});

	// Log discovered devices so the user can see what is available.
	{
		const size_t devCount = ggml_backend_dev_count();
		std::string msg = "ofxGgml: discovered " + std::to_string(devCount) + " backend device(s):\n";
		m_impl->log(GGML_LOG_LEVEL_INFO, msg);
		bool hasGpu = false;
		for (size_t i = 0; i < devCount; i++) {
			ggml_backend_dev_t dev = ggml_backend_dev_get(i);
			const char * devName = ggml_backend_dev_name(dev);
			const char * devDesc = ggml_backend_dev_description(dev);
			const enum ggml_backend_dev_type devType = ggml_backend_dev_type(dev);
			const char * typeLabel = "CPU";
			if (devType == GGML_BACKEND_DEVICE_TYPE_GPU) {
				typeLabel = "GPU";
				hasGpu = true;
			} else if (devType != GGML_BACKEND_DEVICE_TYPE_CPU) {
				typeLabel = "Accelerator";
				hasGpu = true;
			}
			std::string devMsg = "ofxGgml:   [" + std::to_string(i) + "] ";
			devMsg += (devName ? devName : "?");
			devMsg += " - ";
			devMsg += (devDesc ? devDesc : "");
			devMsg += " (";
			devMsg += typeLabel;
			devMsg += ")\n";
			m_impl->log(GGML_LOG_LEVEL_INFO, devMsg);
		}
		if (!hasGpu && settings.preferredBackendName.empty() &&
			settings.preferredBackend != ofxGgmlBackendType::Cpu) {
			m_impl->log(GGML_LOG_LEVEL_WARN,
				"ofxGgml: no usable GPU found - will fall back to CPU\n");
		}
	}

	// Initialize the preferred backend.
	//
	// We attempt backend init directly and rely on guardedGgmlCall() to
	// catch fatal ggml aborts and continue with fallback selection. This
	// avoids false negatives on drivers that report memory as 0/0 before
	// full backend initialization.

	if (!m_impl->backend && !settings.preferredBackendName.empty()) {
		// Try the named device first.
		ggml_backend_dev_t namedDev = ggml_backend_dev_by_name(
			settings.preferredBackendName.c_str());
		if (namedDev) {
			m_impl->backend = tryInitBackendDev(namedDev);
		}
		if (!m_impl->backend) {
			m_impl->log(GGML_LOG_LEVEL_WARN,
				"ofxGgml: backend \"" + settings.preferredBackendName +
				"\" not found or failed to init - trying fallback\n");
		}
	}
	if (!m_impl->backend && settings.preferredBackendName.empty() &&
		settings.preferredBackend == ofxGgmlBackendType::Gpu) {
		// Default GPU path: prefer CUDA first, then Vulkan, then any other
		// usable non-CPU backend.
		const char * preferredPrefixes[] = { "CUDA", "Vulkan" };
		for (const char * prefix : preferredPrefixes) {
			ggml_backend_dev_t dev = findUsableDeviceByNamePrefix(prefix);
			if (!dev) continue;
			m_impl->backend = tryInitBackendDev(dev);
			if (m_impl->backend) break;
		}

		if (!m_impl->backend) {
			const size_t devCount = ggml_backend_dev_count();
			for (size_t i = 0; i < devCount && !m_impl->backend; ++i) {
				ggml_backend_dev_t dev = ggml_backend_dev_get(i);
				if (!dev) continue;
				if (ggml_backend_dev_type(dev) == GGML_BACKEND_DEVICE_TYPE_CPU) continue;
				m_impl->backend = tryInitBackendDev(dev);
			}
		}
	}
	if (!m_impl->backend &&
		settings.preferredBackend != ofxGgmlBackendType::Cpu) {
		// Try the preferred type.
		ggml_backend_dev_t typeDev = ggml_backend_dev_by_type(
			static_cast<enum ggml_backend_dev_type>(settings.preferredBackend));
		if (typeDev) {
			m_impl->backend = tryInitBackendDev(typeDev);
		}
	}

#if defined(_WIN32)
	if (!m_impl->backend &&
		settings.preferredBackend != ofxGgmlBackendType::Cpu) {
		// Fallback when registry/device enumeration only exposes CPU even
		// though CUDA/Vulkan libraries are linked.
		const int cudaCount = ggml_backend_cuda_get_device_count();
		if (cudaCount > 0) {
			m_impl->log(GGML_LOG_LEVEL_WARN,
				"ofxGgml: no CUDA device in registry, trying direct CUDA init\n");
			guardedGgmlCall([&]() {
				m_impl->backend = ggml_backend_cuda_init(0);
			}, "direct CUDA backend init");
		}

		if (!m_impl->backend && !isEnvVarSet("GGML_DISABLE_VULKAN")) {
			const int vkCount = ggml_backend_vk_get_device_count();
			if (vkCount > 0) {
				m_impl->log(GGML_LOG_LEVEL_WARN,
					"ofxGgml: no Vulkan device in registry, trying direct Vulkan init\n");
				guardedGgmlCall([&]() {
					m_impl->backend = ggml_backend_vk_init(0);
				}, "direct Vulkan backend init");
			}
		}
	}
#endif

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
	std::string readyMsg = "ofxGgml: ready (backend: ";
	readyMsg += ggml_backend_name(m_impl->backend);
	readyMsg += ")\n";
	m_impl->log(GGML_LOG_LEVEL_INFO, readyMsg);
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
	// ggml logging is process-global, so only unregister this instance and
	// reactivate the previous live owner if one still exists.
	unregisterLogCallbackOwner(m_impl.get());
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

static size_t clampedTensorTransferSize(const struct ggml_tensor * tensor, size_t bytes) noexcept {
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

static bool allocGraphInternal(ofxGgml::Impl * impl, struct ggml_cgraph * graph, bool validateGraph) {
	if (!impl) return false;
	if (impl->state != ofxGgmlState::Ready) {
		impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: not ready\n");
		return false;
	}

	if (validateGraph) {
		std::string validationError;
		if (!validateGraphForCompute(graph, validationError)) {
			impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: invalid graph: " + validationError + "\n");
			return false;
		}
	}

	if (impl->allocatedGraph == graph) {
		return true;
	}

	auto t0 = std::chrono::steady_clock::now();

	ggml_backend_sched_reset(impl->sched);
	impl->allocatedGraph = nullptr;

	if (impl->reservedGraph != graph) {
		if (!ggml_backend_sched_reserve(impl->sched, graph)) {
			impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: scheduler reserve failed\n");
			return false;
		}
		impl->reservedGraph = graph;
	}

	if (!ggml_backend_sched_alloc_graph(impl->sched, graph)) {
		impl->log(GGML_LOG_LEVEL_ERROR, "ofxGgml: graph allocation failed\n");
		return false;
	}
	impl->allocatedGraph = graph;
	auto t1 = std::chrono::steady_clock::now();
	impl->timings.allocMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
	return true;
}

bool ofxGgml::allocGraph(ofxGgmlGraph & graph) {
	return allocGraphInternal(m_impl.get(), graph.raw(), true);
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
	struct ggml_cgraph * rawGraph = graph.raw();

	if (m_impl->state != ofxGgmlState::Ready) {
		if (m_impl->state == ofxGgmlState::Computing && m_impl->hasPendingAsync) {
			result.error = "ofxGgml: async compute already in flight (call synchronize() first)";
			return result;
		}
		result.error = "ofxGgml: not ready";
		return result;
	}

	// Reused graphs are already validated and allocated, so skip the
	// O(node-count) validation pass on the steady-state compute path.
	if (m_impl->allocatedGraph != rawGraph) {
		std::string validationError;
		if (!validateGraphForCompute(rawGraph, validationError)) {
			result.error = std::string("ofxGgml: invalid graph: ") + validationError;
			return result;
		}

		// Graph was already validated above; skip redundant second validation.
		if (!allocGraphInternal(m_impl.get(), rawGraph, false)) {
			result.error = "ofxGgml: graph allocation failed";
			return result;
		}
	}

	m_impl->state = ofxGgmlState::Computing;

	auto t0 = std::chrono::steady_clock::now();

	enum ggml_status status = ggml_backend_sched_graph_compute_async(m_impl->sched, rawGraph);

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

	std::string loadMsg = "ofxGgml: model weights loaded (";
	loadMsg += std::to_string(snapshots.size());
	loadMsg += " tensors on backend: ";
	loadMsg += ggml_backend_name(m_impl->backend);
	loadMsg += ")\n";
	m_impl->log(GGML_LOG_LEVEL_INFO, loadMsg);
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
