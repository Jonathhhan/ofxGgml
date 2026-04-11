#pragma once

#include "../tensor/ofxGgmlTensor.h"
#include "../support/ofxGgmlTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct ggml_context;
struct ggml_cgraph;

/// Builder for ggml computation graphs.
///
/// Usage:
///   1. Call newTensor*() to define input/weight tensors.
///   2. Chain tensor operations (add, mul, matMul, …) to build the graph.
///   3. Call build() to finalize the graph, then pass it to ofxGgml::compute().
///
/// The graph and all tensors it references are owned by this object
/// and freed on destruction or reset().
class ofxGgmlGraph {
public:
	/// Construct a graph builder with the given arena size.
	/// @param maxNodes  Maximum number of nodes in the computation graph.
	explicit ofxGgmlGraph(size_t maxNodes = 2048);
	~ofxGgmlGraph();

	ofxGgmlGraph(const ofxGgmlGraph &) = delete;
	ofxGgmlGraph & operator=(const ofxGgmlGraph &) = delete;

	/// Discard the current graph and context so the builder can be reused.
	void reset();

	// ------------------------------------------------------------------
	//  Tensor creation
	// ------------------------------------------------------------------

	ofxGgmlTensor newTensor1d(ofxGgmlType type, int64_t ne0);
	ofxGgmlTensor newTensor2d(ofxGgmlType type, int64_t ne0, int64_t ne1);
	ofxGgmlTensor newTensor3d(ofxGgmlType type, int64_t ne0, int64_t ne1, int64_t ne2);
	ofxGgmlTensor newTensor4d(ofxGgmlType type, int64_t ne0, int64_t ne1, int64_t ne2, int64_t ne3);

	/// Mark a tensor as a differentiable parameter (for automatic
	/// differentiation / optimisation).
	void setParam(ofxGgmlTensor tensor);

	/// Mark a tensor as a graph input (never overwritten during alloc).
	void setInput(ofxGgmlTensor tensor);

	/// Mark a tensor as a graph output (never freed during alloc).
	void setOutput(ofxGgmlTensor tensor);

	// ------------------------------------------------------------------
	//  Element-wise arithmetic
	// ------------------------------------------------------------------

	ofxGgmlTensor add(ofxGgmlTensor a, ofxGgmlTensor b);
	ofxGgmlTensor sub(ofxGgmlTensor a, ofxGgmlTensor b);
	ofxGgmlTensor mul(ofxGgmlTensor a, ofxGgmlTensor b);
	ofxGgmlTensor div(ofxGgmlTensor a, ofxGgmlTensor b);
	ofxGgmlTensor sqr(ofxGgmlTensor a);
	ofxGgmlTensor sqrt(ofxGgmlTensor a);
	ofxGgmlTensor scale(ofxGgmlTensor a, float s);
	ofxGgmlTensor clamp(ofxGgmlTensor a, float minVal, float maxVal);

	// ------------------------------------------------------------------
	//  Reduction
	// ------------------------------------------------------------------

	ofxGgmlTensor sum(ofxGgmlTensor a);
	ofxGgmlTensor sumRows(ofxGgmlTensor a);
	ofxGgmlTensor mean(ofxGgmlTensor a);
	ofxGgmlTensor argmax(ofxGgmlTensor a);

	// ------------------------------------------------------------------
	//  Matrix / linear algebra
	// ------------------------------------------------------------------

	/// Matrix multiplication:  result = a * b^T.
	ofxGgmlTensor matMul(ofxGgmlTensor a, ofxGgmlTensor b);

	// ------------------------------------------------------------------
	//  Tensor manipulation
	// ------------------------------------------------------------------

	ofxGgmlTensor reshape2d(ofxGgmlTensor a, int64_t ne0, int64_t ne1);
	ofxGgmlTensor reshape3d(ofxGgmlTensor a, int64_t ne0, int64_t ne1, int64_t ne2);
	ofxGgmlTensor transpose(ofxGgmlTensor a);
	ofxGgmlTensor permute(ofxGgmlTensor a, int axis0, int axis1, int axis2, int axis3);
	ofxGgmlTensor view1d(ofxGgmlTensor a, int64_t ne0, size_t offset);
	ofxGgmlTensor view2d(ofxGgmlTensor a, int64_t ne0, int64_t ne1, size_t nb1, size_t offset);
	ofxGgmlTensor repeat(ofxGgmlTensor a, ofxGgmlTensor b);
	ofxGgmlTensor concat(ofxGgmlTensor a, ofxGgmlTensor b, int dim = 0);

	// ------------------------------------------------------------------
	//  Normalization
	// ------------------------------------------------------------------

	ofxGgmlTensor norm(ofxGgmlTensor a, float eps = 1e-5f);
	ofxGgmlTensor rmsNorm(ofxGgmlTensor a, float eps = 1e-5f);
	ofxGgmlTensor layerNorm(ofxGgmlTensor a, float eps = 1e-5f);

	// ------------------------------------------------------------------
	//  Activations
	// ------------------------------------------------------------------

	ofxGgmlTensor relu(ofxGgmlTensor a);
	ofxGgmlTensor gelu(ofxGgmlTensor a);
	ofxGgmlTensor silu(ofxGgmlTensor a);
	ofxGgmlTensor sigmoid(ofxGgmlTensor a);
	ofxGgmlTensor tanh(ofxGgmlTensor a);
	ofxGgmlTensor softmax(ofxGgmlTensor a);

	// ------------------------------------------------------------------
	//  Attention / Transformer helpers
	// ------------------------------------------------------------------

	/// Scaled-dot-product attention (flash attention when available).
	ofxGgmlTensor flashAttn(ofxGgmlTensor q, ofxGgmlTensor k, ofxGgmlTensor v, bool masked = false);

	/// Rotary position embedding.
	ofxGgmlTensor rope(ofxGgmlTensor a, ofxGgmlTensor positions, int nDims, int mode = 0);

	// ------------------------------------------------------------------
	//  Convolution / Pooling
	// ------------------------------------------------------------------

	ofxGgmlTensor conv1d(ofxGgmlTensor a, ofxGgmlTensor kernel, int stride = 1, int padding = 0, int dilation = 1);
	ofxGgmlTensor pool1d(ofxGgmlTensor a, int kernelSize, int stride = 1, int padding = 0);
	ofxGgmlTensor pool2d(ofxGgmlTensor a, int kernelSize, int stride = 1, int padding = 0);
	ofxGgmlTensor upscale(ofxGgmlTensor a, int scaleFactor);

	// ------------------------------------------------------------------
	//  Loss
	// ------------------------------------------------------------------

	/// Cross-entropy loss between logits and one-hot targets.
	ofxGgmlTensor crossEntropyLoss(ofxGgmlTensor logits, ofxGgmlTensor targets);

	// ------------------------------------------------------------------
	//  Graph finalization
	// ------------------------------------------------------------------

	/// Finalize the graph by expanding forward from the given output tensor.
	/// Must be called before passing to ofxGgml::compute().
	void build(ofxGgmlTensor output);

	/// Finalize with multiple outputs.
	void build(const std::vector<ofxGgmlTensor> & outputs);

	/// Access the underlying ggml_cgraph (valid after build()).
	struct ggml_cgraph * raw() { return m_graph; }
	const struct ggml_cgraph * raw() const { return m_graph; }

	/// Access the underlying ggml_context.
	struct ggml_context * context() { return m_ctx; }
	const struct ggml_context * context() const { return m_ctx; }

	/// Number of nodes in the finalized graph.
	int getNumNodes() const;

	/// Get the i-th node tensor (negative indices count from the end).
	ofxGgmlTensor getNode(int index) const;

private:
	void ensureContext();

	struct ggml_context * m_ctx = nullptr;
	struct ggml_cgraph * m_graph = nullptr;
	std::vector<uint8_t> m_buf;
	size_t m_maxNodes;
};
