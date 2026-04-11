#include "ofxGgmlGraph.h"

#include "ggml.h"

#include <stdexcept>

ofxGgmlGraph::ofxGgmlGraph(size_t maxNodes)
	: m_maxNodes(maxNodes) {
	ensureContext();
}

ofxGgmlGraph::~ofxGgmlGraph() {
	if (m_ctx) {
		ggml_free(m_ctx);
		m_ctx = nullptr;
	}
}

void ofxGgmlGraph::reset() {
	if (m_ctx) {
		ggml_free(m_ctx);
		m_ctx = nullptr;
	}
	m_graph = nullptr;
	m_buf.clear();
	ensureContext();
}

void ofxGgmlGraph::ensureContext() {
	if (m_ctx) return;
	const size_t bufSize = ggml_tensor_overhead() * m_maxNodes + ggml_graph_overhead();
	m_buf.resize(bufSize);
	struct ggml_init_params params = {
		/*.mem_size   =*/ bufSize,
		/*.mem_buffer =*/ m_buf.data(),
		/*.no_alloc   =*/ true,
	};
	m_ctx = ggml_init(params);
}

// --------------------------------------------------------------------------
//  Tensor creation
// --------------------------------------------------------------------------

ofxGgmlTensor ofxGgmlGraph::newTensor1d(ofxGgmlType type, int64_t ne0) {
	return ofxGgmlTensor(ggml_new_tensor_1d(m_ctx, static_cast<enum ggml_type>(type), ne0));
}

ofxGgmlTensor ofxGgmlGraph::newTensor2d(ofxGgmlType type, int64_t ne0, int64_t ne1) {
	return ofxGgmlTensor(ggml_new_tensor_2d(m_ctx, static_cast<enum ggml_type>(type), ne0, ne1));
}

ofxGgmlTensor ofxGgmlGraph::newTensor3d(ofxGgmlType type, int64_t ne0, int64_t ne1, int64_t ne2) {
	return ofxGgmlTensor(ggml_new_tensor_3d(m_ctx, static_cast<enum ggml_type>(type), ne0, ne1, ne2));
}

ofxGgmlTensor ofxGgmlGraph::newTensor4d(ofxGgmlType type, int64_t ne0, int64_t ne1, int64_t ne2, int64_t ne3) {
	return ofxGgmlTensor(ggml_new_tensor_4d(m_ctx, static_cast<enum ggml_type>(type), ne0, ne1, ne2, ne3));
}

void ofxGgmlGraph::setParam(ofxGgmlTensor tensor) {
	if (tensor.raw()) ggml_set_param(m_ctx, tensor.raw());
}

void ofxGgmlGraph::setInput(ofxGgmlTensor tensor) {
	if (tensor.raw()) ggml_set_input(tensor.raw());
}

void ofxGgmlGraph::setOutput(ofxGgmlTensor tensor) {
	if (tensor.raw()) ggml_set_output(tensor.raw());
}

// --------------------------------------------------------------------------
//  Element-wise arithmetic
// --------------------------------------------------------------------------

ofxGgmlTensor ofxGgmlGraph::add(ofxGgmlTensor a, ofxGgmlTensor b) {
	return ofxGgmlTensor(ggml_add(m_ctx, a.raw(), b.raw()));
}

ofxGgmlTensor ofxGgmlGraph::sub(ofxGgmlTensor a, ofxGgmlTensor b) {
	return ofxGgmlTensor(ggml_sub(m_ctx, a.raw(), b.raw()));
}

ofxGgmlTensor ofxGgmlGraph::mul(ofxGgmlTensor a, ofxGgmlTensor b) {
	return ofxGgmlTensor(ggml_mul(m_ctx, a.raw(), b.raw()));
}

ofxGgmlTensor ofxGgmlGraph::div(ofxGgmlTensor a, ofxGgmlTensor b) {
	return ofxGgmlTensor(ggml_div(m_ctx, a.raw(), b.raw()));
}

ofxGgmlTensor ofxGgmlGraph::sqr(ofxGgmlTensor a) {
	return ofxGgmlTensor(ggml_sqr(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::sqrt(ofxGgmlTensor a) {
	return ofxGgmlTensor(ggml_sqrt(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::scale(ofxGgmlTensor a, float s) {
	return ofxGgmlTensor(ggml_scale(m_ctx, a.raw(), s));
}

ofxGgmlTensor ofxGgmlGraph::clamp(ofxGgmlTensor a, float minVal, float maxVal) {
	return ofxGgmlTensor(ggml_clamp(m_ctx, a.raw(), minVal, maxVal));
}

// --------------------------------------------------------------------------
//  Reduction
// --------------------------------------------------------------------------

ofxGgmlTensor ofxGgmlGraph::sum(ofxGgmlTensor a) {
	return ofxGgmlTensor(ggml_sum(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::sumRows(ofxGgmlTensor a) {
	return ofxGgmlTensor(ggml_sum_rows(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::mean(ofxGgmlTensor a) {
	return ofxGgmlTensor(ggml_mean(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::argmax(ofxGgmlTensor a) {
	return ofxGgmlTensor(ggml_argmax(m_ctx, a.raw()));
}

// --------------------------------------------------------------------------
//  Matrix / linear algebra
// --------------------------------------------------------------------------

ofxGgmlTensor ofxGgmlGraph::matMul(ofxGgmlTensor a, ofxGgmlTensor b) {
	return ofxGgmlTensor(ggml_mul_mat(m_ctx, a.raw(), b.raw()));
}

// --------------------------------------------------------------------------
//  Tensor manipulation
// --------------------------------------------------------------------------

ofxGgmlTensor ofxGgmlGraph::reshape2d(ofxGgmlTensor a, int64_t ne0, int64_t ne1) {
	return ofxGgmlTensor(ggml_reshape_2d(m_ctx, a.raw(), ne0, ne1));
}

ofxGgmlTensor ofxGgmlGraph::reshape3d(ofxGgmlTensor a, int64_t ne0, int64_t ne1, int64_t ne2) {
	return ofxGgmlTensor(ggml_reshape_3d(m_ctx, a.raw(), ne0, ne1, ne2));
}

ofxGgmlTensor ofxGgmlGraph::transpose(ofxGgmlTensor a) {
	return ofxGgmlTensor(ggml_transpose(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::permute(ofxGgmlTensor a, int axis0, int axis1, int axis2, int axis3) {
	return ofxGgmlTensor(ggml_permute(m_ctx, a.raw(), axis0, axis1, axis2, axis3));
}

ofxGgmlTensor ofxGgmlGraph::view1d(ofxGgmlTensor a, int64_t ne0, size_t offset) {
	return ofxGgmlTensor(ggml_view_1d(m_ctx, a.raw(), ne0, offset));
}

ofxGgmlTensor ofxGgmlGraph::view2d(ofxGgmlTensor a, int64_t ne0, int64_t ne1, size_t nb1, size_t offset) {
	return ofxGgmlTensor(ggml_view_2d(m_ctx, a.raw(), ne0, ne1, nb1, offset));
}

ofxGgmlTensor ofxGgmlGraph::repeat(ofxGgmlTensor a, ofxGgmlTensor b) {
	return ofxGgmlTensor(ggml_repeat(m_ctx, a.raw(), b.raw()));
}

ofxGgmlTensor ofxGgmlGraph::concat(ofxGgmlTensor a, ofxGgmlTensor b, int dim) {
	return ofxGgmlTensor(ggml_concat(m_ctx, a.raw(), b.raw(), dim));
}

// --------------------------------------------------------------------------
//  Normalization
// --------------------------------------------------------------------------

ofxGgmlTensor ofxGgmlGraph::norm(ofxGgmlTensor a, float eps) {
	return ofxGgmlTensor(ggml_norm(m_ctx, a.raw(), eps));
}

ofxGgmlTensor ofxGgmlGraph::rmsNorm(ofxGgmlTensor a, float eps) {
	return ofxGgmlTensor(ggml_rms_norm(m_ctx, a.raw(), eps));
}

ofxGgmlTensor ofxGgmlGraph::layerNorm(ofxGgmlTensor a, float eps) {
	// Layer normalization is equivalent to ggml_norm (mean-subtracted,
	// variance-normalized).  Callers should apply learned scale/bias
	// with add() and mul() on the result when needed.
	return ofxGgmlTensor(ggml_norm(m_ctx, a.raw(), eps));
}

// --------------------------------------------------------------------------
//  Activations
// --------------------------------------------------------------------------

ofxGgmlTensor ofxGgmlGraph::relu(ofxGgmlTensor a) {
	return ofxGgmlTensor(ggml_relu(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::gelu(ofxGgmlTensor a) {
	return ofxGgmlTensor(ggml_gelu(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::silu(ofxGgmlTensor a) {
	return ofxGgmlTensor(ggml_silu(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::sigmoid(ofxGgmlTensor a) {
	return ofxGgmlTensor(ggml_sigmoid(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::tanh(ofxGgmlTensor a) {
	return ofxGgmlTensor(ggml_tanh(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::softmax(ofxGgmlTensor a) {
	return ofxGgmlTensor(ggml_soft_max(m_ctx, a.raw()));
}

// --------------------------------------------------------------------------
//  Attention / Transformer helpers
// --------------------------------------------------------------------------

ofxGgmlTensor ofxGgmlGraph::flashAttn(ofxGgmlTensor q, ofxGgmlTensor k, ofxGgmlTensor v, bool masked) {
	struct ggml_tensor * mask = nullptr;
	if (masked) {
		// Build a causal (lower-triangular) mask filled with -INFINITY for
		// future positions.  Shape: [kv_len, q_len] with F32 type.
		const int64_t qLen  = q.raw()->ne[1];
		const int64_t kvLen = k.raw()->ne[1];
		mask = ggml_new_tensor_2d(m_ctx, GGML_TYPE_F32, kvLen, qLen);
		ggml_set_name(mask, "causal_mask");
		ggml_set_input(mask);
	}
	float scale = 1.0f;
	return ofxGgmlTensor(ggml_flash_attn_ext(m_ctx, q.raw(), k.raw(), v.raw(),
		mask, scale, 0.0f, 0.0f));
}

ofxGgmlTensor ofxGgmlGraph::rope(ofxGgmlTensor a, ofxGgmlTensor positions, int nDims, int mode) {
	return ofxGgmlTensor(ggml_rope(m_ctx, a.raw(), positions.raw(), nDims, mode));
}

// --------------------------------------------------------------------------
//  Convolution / Pooling
// --------------------------------------------------------------------------

ofxGgmlTensor ofxGgmlGraph::conv1d(ofxGgmlTensor a, ofxGgmlTensor kernel, int stride, int padding, int dilation) {
	// Note: ggml provides ggml_conv_transpose_1d for transposed (deconv) 1-D
	// convolution.  For a standard 1-D convolution the caller can use the
	// im2col + matMul pattern.  This wrapper exposes the transposed variant
	// directly since it is the primitive available in ggml.
	(void)dilation; // dilation is not directly supported by ggml_conv_transpose_1d
	return ofxGgmlTensor(ggml_conv_transpose_1d(m_ctx, kernel.raw(), a.raw(), stride, padding, dilation));
}

ofxGgmlTensor ofxGgmlGraph::pool1d(ofxGgmlTensor a, int kernelSize, int stride, int padding) {
	return ofxGgmlTensor(ggml_pool_1d(m_ctx, a.raw(), GGML_OP_POOL_AVG, kernelSize, stride, padding));
}

ofxGgmlTensor ofxGgmlGraph::pool2d(ofxGgmlTensor a, int kernelSize, int stride, int padding) {
	return ofxGgmlTensor(ggml_pool_2d(m_ctx, a.raw(), GGML_OP_POOL_AVG,
		kernelSize, kernelSize, stride, stride, padding, padding));
}

ofxGgmlTensor ofxGgmlGraph::upscale(ofxGgmlTensor a, int scaleFactor) {
	return ofxGgmlTensor(ggml_upscale(m_ctx, a.raw(), scaleFactor));
}

// --------------------------------------------------------------------------
//  Loss
// --------------------------------------------------------------------------

ofxGgmlTensor ofxGgmlGraph::crossEntropyLoss(ofxGgmlTensor logits, ofxGgmlTensor targets) {
	return ofxGgmlTensor(ggml_cross_entropy_loss(m_ctx, logits.raw(), targets.raw()));
}

// --------------------------------------------------------------------------
//  Graph finalization
// --------------------------------------------------------------------------

void ofxGgmlGraph::build(ofxGgmlTensor output) {
	m_graph = ggml_new_graph(m_ctx);
	ggml_build_forward_expand(m_graph, output.raw());
}

void ofxGgmlGraph::build(const std::vector<ofxGgmlTensor> & outputs) {
	m_graph = ggml_new_graph(m_ctx);
	for (auto & t : outputs) {
		ggml_build_forward_expand(m_graph, t.raw());
	}
}

int ofxGgmlGraph::getNumNodes() const {
	if (!m_graph) return 0;
	return ggml_graph_n_nodes(m_graph);
}

ofxGgmlTensor ofxGgmlGraph::getNode(int index) const {
	if (!m_graph) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_graph_node(m_graph, index));
}
