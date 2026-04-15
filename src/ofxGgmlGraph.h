#pragma once

#include "ofxGgmlTensor.h"
#include "ofxGgmlTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct ggml_cgraph;
struct ggml_context;

/// Fluent builder for ggml computation graphs.
///
/// The builder owns the ggml context, tensor metadata arena, and finalized
/// graph object. Create tensors, chain operations, call build(), then pass the
/// graph to `ofxGgml` for allocation and execution.
class ofxGgmlGraph {
public:
	/// Construct a graph builder with the requested node arena size.
	explicit ofxGgmlGraph(size_t maxNodes = 2048);
	~ofxGgmlGraph();

	ofxGgmlGraph(const ofxGgmlGraph &) = delete;
	ofxGgmlGraph & operator=(const ofxGgmlGraph &) = delete;

	/// Discard the current graph and recreate the builder context.
	void reset();

	// ---------------------------------------------------------------------------
	// Tensor creation
	// ---------------------------------------------------------------------------

	ofxGgmlTensor newTensor1d(ofxGgmlType type, int64_t ne0);
	ofxGgmlTensor newTensor2d(ofxGgmlType type, int64_t ne0, int64_t ne1);
	ofxGgmlTensor newTensor3d(ofxGgmlType type, int64_t ne0, int64_t ne1, int64_t ne2);
	ofxGgmlTensor newTensor4d(ofxGgmlType type, int64_t ne0, int64_t ne1, int64_t ne2, int64_t ne3);

	/// Mark a tensor as a differentiable parameter.
	void setParam(ofxGgmlTensor tensor);

	/// Mark a tensor as a graph input.
	void setInput(ofxGgmlTensor tensor);

	/// Mark a tensor as a graph output.
	void setOutput(ofxGgmlTensor tensor);

	// ---------------------------------------------------------------------------
	// Element-wise arithmetic
	// ---------------------------------------------------------------------------

	ofxGgmlTensor add(ofxGgmlTensor a, ofxGgmlTensor b);
	ofxGgmlTensor sub(ofxGgmlTensor a, ofxGgmlTensor b);
	ofxGgmlTensor mul(ofxGgmlTensor a, ofxGgmlTensor b);
	ofxGgmlTensor div(ofxGgmlTensor a, ofxGgmlTensor b);
	ofxGgmlTensor sqr(ofxGgmlTensor a);
	ofxGgmlTensor sqrt(ofxGgmlTensor a);
	ofxGgmlTensor scale(ofxGgmlTensor a, float s);
	ofxGgmlTensor clamp(ofxGgmlTensor a, float minVal, float maxVal);

	// ---------------------------------------------------------------------------
	// Reduction
	// ---------------------------------------------------------------------------

	ofxGgmlTensor sum(ofxGgmlTensor a);
	ofxGgmlTensor sumRows(ofxGgmlTensor a);
	ofxGgmlTensor mean(ofxGgmlTensor a);
	ofxGgmlTensor argmax(ofxGgmlTensor a);

	// ---------------------------------------------------------------------------
	// Matrix and tensor transforms
	// ---------------------------------------------------------------------------

	/// Matrix multiplication using ggml's `mul_mat` semantics.
	ofxGgmlTensor matMul(ofxGgmlTensor a, ofxGgmlTensor b);

	ofxGgmlTensor reshape2d(ofxGgmlTensor a, int64_t ne0, int64_t ne1);
	ofxGgmlTensor reshape3d(ofxGgmlTensor a, int64_t ne0, int64_t ne1, int64_t ne2);
	ofxGgmlTensor transpose(ofxGgmlTensor a);
	ofxGgmlTensor permute(ofxGgmlTensor a, int axis0, int axis1, int axis2, int axis3);
	ofxGgmlTensor view1d(ofxGgmlTensor a, int64_t ne0, size_t offset);
	ofxGgmlTensor view2d(ofxGgmlTensor a, int64_t ne0, int64_t ne1, size_t nb1, size_t offset);
	ofxGgmlTensor repeat(ofxGgmlTensor a, ofxGgmlTensor b);
	ofxGgmlTensor concat(ofxGgmlTensor a, ofxGgmlTensor b, int dim = 0);

	// ---------------------------------------------------------------------------
	// Normalization and activations
	// ---------------------------------------------------------------------------

	ofxGgmlTensor norm(ofxGgmlTensor a, float eps = 1e-5f);
	ofxGgmlTensor rmsNorm(ofxGgmlTensor a, float eps = 1e-5f);
	ofxGgmlTensor relu(ofxGgmlTensor a);
	ofxGgmlTensor gelu(ofxGgmlTensor a);
	ofxGgmlTensor silu(ofxGgmlTensor a);
	ofxGgmlTensor sigmoid(ofxGgmlTensor a);
	ofxGgmlTensor tanh(ofxGgmlTensor a);
	ofxGgmlTensor softmax(ofxGgmlTensor a);

	// ---------------------------------------------------------------------------
	// Attention and positional helpers
	// ---------------------------------------------------------------------------

	/// Attention helper. Pass an invalid tensor for unmasked attention.
	ofxGgmlTensor flashAttn(
		ofxGgmlTensor q,
		ofxGgmlTensor k,
		ofxGgmlTensor v,
		ofxGgmlTensor mask = ofxGgmlTensor());

	ofxGgmlTensor rope(ofxGgmlTensor a, ofxGgmlTensor positions, int nDims, int mode = 0);

	// ---------------------------------------------------------------------------
	// Convolution and pooling
	// ---------------------------------------------------------------------------

	ofxGgmlTensor conv1d(ofxGgmlTensor a, ofxGgmlTensor kernel, int stride = 1, int padding = 0, int dilation = 1);
	ofxGgmlTensor convTranspose1d(ofxGgmlTensor a, ofxGgmlTensor kernel, int stride = 1, int padding = 0, int dilation = 1);
	ofxGgmlTensor pool1d(
		ofxGgmlTensor a,
		int kernelSize,
		int stride = 1,
		int padding = 0,
		ofxGgmlPoolType type = ofxGgmlPoolType::Avg);
	ofxGgmlTensor pool2d(
		ofxGgmlTensor a,
		int kernelSize,
		int stride = 1,
		int padding = 0,
		ofxGgmlPoolType type = ofxGgmlPoolType::Avg);
	ofxGgmlTensor upscale(ofxGgmlTensor a, int scaleFactor);

	// ---------------------------------------------------------------------------
	// Loss
	// ---------------------------------------------------------------------------

	ofxGgmlTensor crossEntropyLoss(ofxGgmlTensor logits, ofxGgmlTensor targets);

	// ---------------------------------------------------------------------------
	// Graph finalization
	// ---------------------------------------------------------------------------

	/// Finalize the graph by expanding forward from one output tensor.
	void build(ofxGgmlTensor output);

	/// Finalize the graph from multiple outputs.
	void build(const std::vector<ofxGgmlTensor> & outputs);

	/// Underlying ggml graph handle. Valid after build().
	struct ggml_cgraph * raw() { return m_graph; }
	const struct ggml_cgraph * raw() const { return m_graph; }

	/// Underlying ggml context.
	struct ggml_context * context() { return m_ctx; }
	const struct ggml_context * context() const { return m_ctx; }

	/// Number of nodes in the finalized graph.
	int getNumNodes() const;

	/// Fetch a graph node by index. Negative indices count from the end.
	ofxGgmlTensor getNode(int index) const;

private:
	void ensureContext();

	struct ggml_context * m_ctx = nullptr;
	struct ggml_cgraph * m_graph = nullptr;
	std::vector<uint8_t> m_buf;
	size_t m_maxNodes;
};
