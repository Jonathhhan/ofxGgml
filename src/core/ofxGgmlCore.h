#pragma once

#include "compute/ofxGgmlGraph.h"
#include "compute/ofxGgmlTensor.h"
#include "core/ofxGgmlTypes.h"
#include "model/ofxGgmlModel.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

struct ggml_backend;
struct ggml_backend_buffer;
struct ggml_backend_sched;

/// Main runtime entry point for the ofxGgml addon.
///
/// The class owns backend setup, device discovery, scheduler allocation,
/// tensor upload and download helpers, graph execution, and model weight
/// transfer into backend memory.
class ofxGgml {
public:
	static ofxGgmlAddonVersionInfo getAddonVersionInfo();

	struct Impl;

	ofxGgml();
	~ofxGgml();

	ofxGgml(const ofxGgml &) = delete;
	ofxGgml & operator=(const ofxGgml &) = delete;

	// ---------------------------------------------------------------------------
	// Lifecycle
	// ---------------------------------------------------------------------------

	/// Initialize backends according to the provided settings.
	bool setup(const ofxGgmlSettings & settings = {});

	/// Shut down backends and release owned resources.
	void close();

	/// Current addon state.
	ofxGgmlState getState() const;
	bool isReady() const;

	// ---------------------------------------------------------------------------
	// Device discovery
	// ---------------------------------------------------------------------------

	/// List backend devices discovered during setup.
	std::vector<ofxGgmlDeviceInfo> listDevices() const;

	/// Name of the active primary backend, for example "CPU" or "CUDA0".
	std::string getBackendName() const;

	// ---------------------------------------------------------------------------
	// Tensor transfer
	// ---------------------------------------------------------------------------

	/// Copy host data into a tensor that may live in CPU or accelerator memory.
	void setTensorData(ofxGgmlTensor tensor, const void * data, size_t bytes);

	/// Copy tensor data back into host memory.
	void getTensorData(ofxGgmlTensor tensor, void * data, size_t bytes) const;

	// ---------------------------------------------------------------------------
	// Graph execution
	// ---------------------------------------------------------------------------

	/// Allocate backend buffers for all tensors in the graph.
	bool allocGraph(ofxGgmlGraph & graph);

	/// Execute an already allocated graph synchronously.
	ofxGgmlComputeResult computeGraph(ofxGgmlGraph & graph);

	/// Execute a graph asynchronously. Allocates first when needed.
	ofxGgmlComputeResult computeGraphAsync(ofxGgmlGraph & graph);

	/// Wait for an in-flight async execution to finish.
	ofxGgmlComputeResult synchronize();

	/// Convenience helper that allocates and computes in one call.
	ofxGgmlComputeResult compute(ofxGgmlGraph & graph);

	/// Last recorded setup, allocation, upload, and compute timings.
	ofxGgmlTimings getLastTimings() const;

	// ---------------------------------------------------------------------------
	// Model upload
	// ---------------------------------------------------------------------------

	/// Upload a loaded model's tensors into backend memory.
	bool loadModelWeights(ofxGgmlModel & model);

	// ---------------------------------------------------------------------------
	// Logging
	// ---------------------------------------------------------------------------

	/// Install a custom log callback for addon and ggml runtime messages.
	void setLogCallback(ofxGgmlLogCallback cb);

	// ---------------------------------------------------------------------------
	// Low-level handles
	// ---------------------------------------------------------------------------

	/// Primary compute backend handle.
	struct ggml_backend * getBackend();

	/// CPU fallback backend handle.
	struct ggml_backend * getCpuBackend();

	/// Backend scheduler handle.
	struct ggml_backend_sched * getScheduler();

private:
	std::unique_ptr<Impl> m_impl;
};
