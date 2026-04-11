#pragma once

#include "../support/ofxGgmlTypes.h"
#include "../tensor/ofxGgmlTensor.h"
#include "../graph/ofxGgmlGraph.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

struct ggml_backend;
struct ggml_backend_sched;
struct ggml_backend_buffer;

/// Main entry point for the ofxGgml addon.
///
/// Manages ggml backend lifetime, device enumeration, buffer
/// allocation, and graph execution.  Typical usage:
///
/// @code
///   ofxGgml ggml;
///   ggml.setup();                        // init CPU backend
///
///   ofxGgmlGraph graph;
///   auto a = graph.newTensor2d(ofxGgmlType::F32, 2, 4);
///   auto b = graph.newTensor2d(ofxGgmlType::F32, 2, 3);
///   graph.setInput(a);
///   graph.setInput(b);
///   auto result = graph.matMul(a, b);
///   graph.setOutput(result);
///   graph.build(result);
///
///   auto r = ggml.compute(graph);        // execute on chosen backend
/// @endcode
class ofxGgml {
public:
	ofxGgml();
	~ofxGgml();

	ofxGgml(const ofxGgml &) = delete;
	ofxGgml & operator=(const ofxGgml &) = delete;

	// ------------------------------------------------------------------
	//  Lifecycle
	// ------------------------------------------------------------------

	/// Initialize backends according to settings.
	/// Returns true on success.
	bool setup(const ofxGgmlSettings & settings = {});

	/// Shut down backends and release resources.
	void close();

	/// Current addon state.
	ofxGgmlState getState() const;
	bool isReady() const;

	// ------------------------------------------------------------------
	//  Device enumeration
	// ------------------------------------------------------------------

	/// List all backend devices discovered at startup.
	std::vector<ofxGgmlDeviceInfo> listDevices() const;

	/// Name of the primary compute backend (e.g. "CPU", "CUDA", "Metal").
	std::string getBackendName() const;

	// ------------------------------------------------------------------
	//  Tensor data helpers  –  set / get data through the backend API
	//  so that they work for both CPU and accelerator buffers.
	// ------------------------------------------------------------------

	/// Copy host floats into a tensor that lives in any backend buffer.
	void setTensorData(ofxGgmlTensor tensor, const void * data, size_t bytes);

	/// Copy tensor data back to host memory.
	void getTensorData(ofxGgmlTensor tensor, void * data, size_t bytes) const;

	// ------------------------------------------------------------------
	//  Computation
	// ------------------------------------------------------------------

	/// Allocate backend buffers and compute the graph synchronously.
	ofxGgmlComputeResult compute(ofxGgmlGraph & graph);

	// ------------------------------------------------------------------
	//  Logging
	// ------------------------------------------------------------------

	void setLogCallback(ofxGgmlLogCallback cb);

	// ------------------------------------------------------------------
	//  Low-level access
	// ------------------------------------------------------------------

	/// Primary compute backend handle (may be CPU, CUDA, Metal, …).
	struct ggml_backend * getBackend();

	/// CPU fallback backend handle.
	struct ggml_backend * getCpuBackend();

	/// Backend scheduler handle.
	struct ggml_backend_sched * getScheduler();

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};
