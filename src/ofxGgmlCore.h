#pragma once

#include "ofxGgmlTypes.h"
#include "ofxGgmlTensor.h"
#include "ofxGgmlGraph.h"
#include "ofxGgmlModel.h"

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
///   ggml.allocGraph(graph);              // allocate tensor buffers
///   ggml.setTensorData(a, dataA, size);  // set input data
///   ggml.setTensorData(b, dataB, size);
///   auto r = ggml.computeGraph(graph);   // execute on chosen backend
/// @endcode
class ofxGgml {
public:
	struct Impl;

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

	/// Allocate backend buffers for all tensors in the graph.
	/// Must be called before setTensorData() / computeGraph().
	/// Returns true on success.
	bool allocGraph(ofxGgmlGraph & graph);

	/// Compute an already-allocated graph synchronously.
	/// Call allocGraph() first, then setTensorData() for inputs,
	/// then this method.
	ofxGgmlComputeResult computeGraph(ofxGgmlGraph & graph);

	/// Convenience: allocate buffers and compute in one call.
	/// Only suitable for graphs whose input tensors do not need
	/// external data set via setTensorData() between allocation
	/// and computation.
	ofxGgmlComputeResult compute(ofxGgmlGraph & graph);

	// ------------------------------------------------------------------
	//  Model weight loading
	// ------------------------------------------------------------------

	/// Allocate a backend buffer for all tensors in the loaded model and
	/// upload the tensor data from host memory.  This makes the model's
	/// weight tensors ready for use in computation graphs.
	///
	/// Returns true on success.  On failure the model weights remain in
	/// host memory and must be transferred manually.
	bool loadModelWeights(ofxGgmlModel & model);

	// ------------------------------------------------------------------
	//  Logging
	// ------------------------------------------------------------------

	/// Install a custom log callback.  By default, all log messages
	/// (from ggml internals and from the addon itself) are printed to
	/// stderr with a level prefix.  Call this method with a custom
	/// callback to redirect them (e.g. to a GUI log panel).
	///
	/// The callback signature is:
	///   void(int level, const std::string & message)
	/// where level follows the ggml_log_level enum:
	///   0 = NONE, 1 = DEBUG, 2 = INFO, 3 = WARN, 4 = ERROR, 5 = CONT.
	///
	/// Pass a no-op lambda to silence all output:
	///   ggml.setLogCallback([](int, const std::string &) {});
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
	std::unique_ptr<Impl> m_impl;
};
