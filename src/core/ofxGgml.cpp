#include "ofxGgml.h"

#include "ggml.h"
#include "ggml-alloc.h"
#include "ggml-backend.h"
#include "ggml-cpu.h"

#include <chrono>
#include <cstdio>
#include <cstring>

// --------------------------------------------------------------------------
//  PIMPL
// --------------------------------------------------------------------------

struct ofxGgml::Impl {
	ofxGgmlState state = ofxGgmlState::Uninitialized;
	ofxGgmlSettings settings;

	ggml_backend_t backend = nullptr;
	ggml_backend_t cpuBackend = nullptr;
	ggml_backend_sched_t sched = nullptr;

	ofxGgmlLogCallback logCb;

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
	ggml_backend_load_all();

	// Initialize the preferred backend.
	m_impl->backend = ggml_backend_init_by_type(
		static_cast<enum ggml_backend_dev_type>(settings.preferredBackend), nullptr);

	if (!m_impl->backend) {
		// Fall back to the best available.
		m_impl->backend = ggml_backend_init_best();
	}

	if (!m_impl->backend) {
		m_impl->state = ofxGgmlState::Error;
		m_impl->log(2, "ofxGgml: failed to initialize any backend");
		return false;
	}

	// Ensure we always have a CPU backend for scheduling.
	m_impl->cpuBackend = ggml_backend_init_by_type(GGML_BACKEND_DEVICE_TYPE_CPU, nullptr);
	if (!m_impl->cpuBackend) {
		m_impl->state = ofxGgmlState::Error;
		m_impl->log(2, "ofxGgml: failed to initialize CPU backend");
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
		m_impl->log(2, "ofxGgml: failed to create backend scheduler");
		ggml_backend_free(m_impl->backend);
		ggml_backend_free(m_impl->cpuBackend);
		m_impl->backend = nullptr;
		m_impl->cpuBackend = nullptr;
		return false;
	}

	m_impl->state = ofxGgmlState::Ready;
	m_impl->log(0, std::string("ofxGgml: ready (backend: ") +
		ggml_backend_name(m_impl->backend) + ")");
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

ofxGgmlComputeResult ofxGgml::compute(ofxGgmlGraph & graph) {
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

	ggml_backend_sched_reset(m_impl->sched);

	if (!ggml_backend_sched_alloc_graph(m_impl->sched, graph.raw())) {
		m_impl->state = ofxGgmlState::Ready;
		result.error = "ofxGgml: graph allocation failed";
		return result;
	}

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
