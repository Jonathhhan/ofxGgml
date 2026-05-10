#include "ofxGgmlRuntime.h"

#include "../compute/ofxGgmlGraph.h"
#include "../compute/ofxGgmlTensor.h"

#if __has_include("ggml.h")
#include "ggml.h"
#include "ggml-alloc.h"
#include "ggml-backend.h"
#include "ggml-cpu.h"
#if defined(OFXGGML_WITH_CUDA) && __has_include("ggml-cuda.h")
#include "ggml-cuda.h"
#define OFXGGML_HAS_CUDA_BACKEND 1
#else
#define OFXGGML_HAS_CUDA_BACKEND 0
#endif
#if defined(OFXGGML_WITH_VULKAN) && __has_include("ggml-vulkan.h")
#include "ggml-vulkan.h"
#define OFXGGML_HAS_VULKAN_BACKEND 1
#else
#define OFXGGML_HAS_VULKAN_BACKEND 0
#endif
#if defined(OFXGGML_WITH_METAL) && __has_include("ggml-metal.h")
#include "ggml-metal.h"
#define OFXGGML_HAS_METAL_BACKEND 1
#else
#define OFXGGML_HAS_METAL_BACKEND 0
#endif
#if defined(OFXGGML_WITH_OPENCL) && __has_include("ggml-opencl.h")
#include "ggml-opencl.h"
#define OFXGGML_HAS_OPENCL_BACKEND 1
#else
#define OFXGGML_HAS_OPENCL_BACKEND 0
#endif
#define OFXGGML_HAS_GGML 1
#else
#define OFXGGML_HAS_GGML 0
#define OFXGGML_HAS_CUDA_BACKEND 0
#define OFXGGML_HAS_VULKAN_BACKEND 0
#define OFXGGML_HAS_METAL_BACKEND 0
#define OFXGGML_HAS_OPENCL_BACKEND 0
#endif

#include <array>
#include <chrono>
#include <utility>

struct ofxGgmlRuntime::Impl {
	~Impl() {
		releaseHandles();
	}

	void releaseHandles() {
#if OFXGGML_HAS_GGML
		if (buffer) {
			ggml_backend_buffer_free(buffer);
			buffer = nullptr;
		}
		if (backend) {
			ggml_backend_free(backend);
			backend = nullptr;
		}
#endif
	}

	ofxGgmlRuntimeState state = ofxGgmlRuntimeState::Uninitialized;
	ofxGgmlRuntimeSettings settings;
#if OFXGGML_HAS_GGML
	ggml_backend_t backend = nullptr;
	ggml_backend_buffer_t buffer = nullptr;
#endif
};

namespace {

#if OFXGGML_HAS_GGML
ggml_backend_t createCpuBackend() {
	return ggml_backend_cpu_init();
}

ggml_backend_t createCudaBackend(int deviceIndex) {
#if OFXGGML_HAS_CUDA_BACKEND
	const int deviceCount = ggml_backend_cuda_get_device_count();
	if (deviceCount <= 0 || deviceIndex < 0 || deviceIndex >= deviceCount) {
		return nullptr;
	}
	return ggml_backend_cuda_init(deviceIndex);
#else
	(void) deviceIndex;
	return nullptr;
#endif
}

ggml_backend_t createVulkanBackend(int deviceIndex) {
#if OFXGGML_HAS_VULKAN_BACKEND
	const int deviceCount = ggml_backend_vk_get_device_count();
	if (deviceCount <= 0 || deviceIndex < 0 || deviceIndex >= deviceCount) {
		return nullptr;
	}
	return ggml_backend_vk_init(static_cast<size_t>(deviceIndex));
#else
	(void) deviceIndex;
	return nullptr;
#endif
}

ggml_backend_t createMetalBackend() {
#if OFXGGML_HAS_METAL_BACKEND
	return ggml_backend_metal_init();
#else
	return nullptr;
#endif
}

ggml_backend_t createOpenCLBackend() {
#if OFXGGML_HAS_OPENCL_BACKEND
	return ggml_backend_opencl_init();
#else
	return nullptr;
#endif
}

#endif

} // namespace

ofxGgmlRuntime::ofxGgmlRuntime()
	: impl(std::make_unique<Impl>()) {}

ofxGgmlRuntime::~ofxGgmlRuntime() {
	close();
}

ofxGgmlRuntime::ofxGgmlRuntime(ofxGgmlRuntime &&) noexcept = default;
ofxGgmlRuntime & ofxGgmlRuntime::operator=(ofxGgmlRuntime &&) noexcept = default;

ofxGgmlResult<void> ofxGgmlRuntime::setup(const ofxGgmlRuntimeSettings & settings) {
	if (!impl) {
		impl = std::make_unique<Impl>();
	}
	close();
	impl->settings = settings;
#if OFXGGML_HAS_GGML
	auto tryBackend = [&](ofxGgmlBackend backend) -> ggml_backend_t {
		switch (backend) {
		case ofxGgmlBackend::Auto:
			return nullptr;
		case ofxGgmlBackend::Cpu:
			return createCpuBackend();
		case ofxGgmlBackend::Cuda:
			return createCudaBackend(settings.deviceIndex);
		case ofxGgmlBackend::Vulkan:
			return createVulkanBackend(settings.deviceIndex);
		case ofxGgmlBackend::Metal:
			return createMetalBackend();
		case ofxGgmlBackend::OpenCL:
			return createOpenCLBackend();
		}
		return nullptr;
	};

	if (settings.preferredBackend == ofxGgmlBackend::Auto) {
		for (const ofxGgmlBackend candidate : std::array<ofxGgmlBackend, 5> {
			ofxGgmlBackend::Cuda,
			ofxGgmlBackend::Vulkan,
			ofxGgmlBackend::Metal,
			ofxGgmlBackend::OpenCL,
			ofxGgmlBackend::Cpu
		}) {
			impl->backend = tryBackend(candidate);
			if (impl->backend) {
				break;
			}
		}
	} else {
		impl->backend = tryBackend(settings.preferredBackend);
		if (!impl->backend && settings.allowCpuFallback && settings.preferredBackend != ofxGgmlBackend::Cpu) {
			impl->backend = createCpuBackend();
		}
	}

	if (!impl->backend) {
		impl->state = ofxGgmlRuntimeState::Error;
		return ofxGgmlResult<void>::failure(
			"failed to initialize ggml " +
			std::string(ofxGgmlGetBackendName(settings.preferredBackend)) +
			" backend");
	}
	impl->state = ofxGgmlRuntimeState::Ready;
	return ofxGgmlResult<void>::success();
#else
	impl->state = ofxGgmlRuntimeState::Error;
	return ofxGgmlResult<void>::failure("ggml headers are not installed; run scripts/setup-ggml.ps1");
#endif
}

void ofxGgmlRuntime::close() {
	if (impl) {
		impl->releaseHandles();
		impl->state = ofxGgmlRuntimeState::Uninitialized;
	}
}

bool ofxGgmlRuntime::isReady() const {
#if OFXGGML_HAS_GGML
	return impl && impl->state == ofxGgmlRuntimeState::Ready && impl->backend;
#else
	return false;
#endif
}

ofxGgmlRuntimeState ofxGgmlRuntime::state() const {
	return impl ? impl->state : ofxGgmlRuntimeState::Uninitialized;
}

std::string ofxGgmlRuntime::getBackendName() const {
#if OFXGGML_HAS_GGML
	if (!impl || !impl->backend) return {};
	const char * name = ggml_backend_name(impl->backend);
	return name ? name : "";
#else
	return {};
#endif
}

std::vector<ofxGgmlDeviceInfo> ofxGgmlRuntime::listDevices() const {
	std::vector<ofxGgmlDeviceInfo> devices;
#if OFXGGML_HAS_GGML
#if OFXGGML_HAS_CUDA_BACKEND
	const int cudaDeviceCount = ggml_backend_cuda_get_device_count();
	for (int i = 0; i < cudaDeviceCount; ++i) {
		char description[256] = {};
		size_t freeMemory = 0;
		size_t totalMemory = 0;
		ggml_backend_cuda_get_device_description(i, description, sizeof(description));
		ggml_backend_cuda_get_device_memory(i, &freeMemory, &totalMemory);
		devices.push_back({
			description[0] ? description : "CUDA",
			ofxGgmlBackend::Cuda,
			totalMemory,
			true
		});
	}
#endif
#if OFXGGML_HAS_VULKAN_BACKEND
	const int vulkanDeviceCount = ggml_backend_vk_get_device_count();
	for (int i = 0; i < vulkanDeviceCount; ++i) {
		char description[256] = {};
		size_t freeMemory = 0;
		size_t totalMemory = 0;
		ggml_backend_vk_get_device_description(i, description, sizeof(description));
		ggml_backend_vk_get_device_memory(i, &freeMemory, &totalMemory);
		devices.push_back({
			description[0] ? description : "Vulkan",
			ofxGgmlBackend::Vulkan,
			totalMemory,
			true
		});
	}
#endif
#endif
	devices.push_back({ "CPU", ofxGgmlBackend::Cpu, 0, true });
	return devices;
}

ofxGgmlResult<void> ofxGgmlRuntime::allocate(ofxGgmlGraph & graph) {
#if OFXGGML_HAS_GGML
	if (!isReady()) return ofxGgmlResult<void>::failure("runtime is not ready");
	if (!graph.isBuilt()) return ofxGgmlResult<void>::failure("graph is not built");
	if (impl->buffer) {
		ggml_backend_buffer_free(impl->buffer);
		impl->buffer = nullptr;
	}
	impl->buffer = ggml_backend_alloc_ctx_tensors(graph.context(), impl->backend);
	if (!impl->buffer) return ofxGgmlResult<void>::failure("failed to allocate graph tensors");
	return ofxGgmlResult<void>::success();
#else
	(void) graph;
	return ofxGgmlResult<void>::failure("ggml is not installed");
#endif
}

ofxGgmlComputeResult ofxGgmlRuntime::compute(ofxGgmlGraph & graph) {
	ofxGgmlComputeResult result;
#if OFXGGML_HAS_GGML
	if (!isReady()) {
		result.error = "runtime is not ready";
		return result;
	}
	if (!graph.isBuilt()) {
		result.error = "graph is not built";
		return result;
	}
	if (!impl->buffer) {
		result.error = "graph tensors are not allocated";
		return result;
	}
	const auto start = std::chrono::steady_clock::now();
	const ggml_status status = ggml_backend_graph_compute(impl->backend, graph.raw());
	const auto stop = std::chrono::steady_clock::now();
	result.elapsedMs = std::chrono::duration<float, std::milli>(stop - start).count();
	result.success = status == GGML_STATUS_SUCCESS;
	if (!result.success) {
		result.error = "ggml compute failed";
	}
#else
	(void) graph;
	result.error = "ggml is not installed";
#endif
	return result;
}

ofxGgmlResult<void> ofxGgmlRuntime::setData(ofxGgmlTensor tensor, const void * data, std::size_t bytes) {
#if OFXGGML_HAS_GGML
	if (!isReady()) return ofxGgmlResult<void>::failure("runtime is not ready");
	if (!impl->buffer) return ofxGgmlResult<void>::failure("graph tensors are not allocated");
	if (!tensor || !data) return ofxGgmlResult<void>::failure("invalid tensor data");
	if (bytes != tensor.bytes()) return ofxGgmlResult<void>::failure("tensor byte count mismatch");
	ggml_backend_tensor_set(tensor.raw(), data, 0, bytes);
	return ofxGgmlResult<void>::success();
#else
	(void) tensor;
	(void) data;
	(void) bytes;
	return ofxGgmlResult<void>::failure("ggml is not installed");
#endif
}

ofxGgmlResult<void> ofxGgmlRuntime::getData(ofxGgmlTensor tensor, void * data, std::size_t bytes) {
#if OFXGGML_HAS_GGML
	if (!isReady()) return ofxGgmlResult<void>::failure("runtime is not ready");
	if (!impl->buffer) return ofxGgmlResult<void>::failure("graph tensors are not allocated");
	if (!tensor || !data) return ofxGgmlResult<void>::failure("invalid tensor data");
	if (bytes != tensor.bytes()) return ofxGgmlResult<void>::failure("tensor byte count mismatch");
	ggml_backend_tensor_get(tensor.raw(), data, 0, bytes);
	return ofxGgmlResult<void>::success();
#else
	(void) tensor;
	(void) data;
	(void) bytes;
	return ofxGgmlResult<void>::failure("ggml is not installed");
#endif
}
