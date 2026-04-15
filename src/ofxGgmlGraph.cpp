#include "ofxGgmlGraph.h"

#include "ggml.h"

#include <atomic>
#include <cstdio>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

static uint64_t nextGraphCacheToken() {
	static std::atomic<uint64_t> counter { 1 };
	return counter.fetch_add(1, std::memory_order_relaxed);
}

ofxGgmlGraph::ofxGgmlGraph(size_t maxNodes)
	: m_maxNodes(maxNodes)
	, m_cacheToken(nextGraphCacheToken()) {
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
	m_cacheToken = nextGraphCacheToken();
	ensureContext();
}

void ofxGgmlGraph::ensureContext() {
	if (m_ctx) {
		return;
	}

	const size_t bufSize = ggml_tensor_overhead() * m_maxNodes
		+ ggml_graph_overhead_custom(m_maxNodes, false);
	m_buf.resize(bufSize);

	struct ggml_init_params params = {
		/*.mem_size   =*/ bufSize,
		/*.mem_buffer =*/ m_buf.data(),
		/*.no_alloc   =*/ true,
	};
	m_ctx = ggml_init(params);
	if (!m_ctx) {
		throw std::runtime_error(
			"ofxGgmlGraph: ggml_init failed - buffer too small or ggml not linked correctly");
	}
}

// ---------------------------------------------------------------------------
// Tensor creation
// ---------------------------------------------------------------------------

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
	if (tensor.raw()) {
		ggml_set_param(tensor.raw());
	}
}

void ofxGgmlGraph::setInput(ofxGgmlTensor tensor) {
	if (tensor.raw()) {
		ggml_set_input(tensor.raw());
	}
}

void ofxGgmlGraph::setOutput(ofxGgmlTensor tensor) {
	if (tensor.raw()) {
		ggml_set_output(tensor.raw());
	}
}

// ---------------------------------------------------------------------------
// Element-wise arithmetic
// ---------------------------------------------------------------------------

ofxGgmlTensor ofxGgmlGraph::add(ofxGgmlTensor a, ofxGgmlTensor b) {
	if (!a.raw() || !b.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_add(m_ctx, a.raw(), b.raw()));
}

ofxGgmlTensor ofxGgmlGraph::sub(ofxGgmlTensor a, ofxGgmlTensor b) {
	if (!a.raw() || !b.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_sub(m_ctx, a.raw(), b.raw()));
}

ofxGgmlTensor ofxGgmlGraph::mul(ofxGgmlTensor a, ofxGgmlTensor b) {
	if (!a.raw() || !b.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_mul(m_ctx, a.raw(), b.raw()));
}

ofxGgmlTensor ofxGgmlGraph::div(ofxGgmlTensor a, ofxGgmlTensor b) {
	if (!a.raw() || !b.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_div(m_ctx, a.raw(), b.raw()));
}

ofxGgmlTensor ofxGgmlGraph::sqr(ofxGgmlTensor a) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_sqr(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::sqrt(ofxGgmlTensor a) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_sqrt(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::scale(ofxGgmlTensor a, float s) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_scale(m_ctx, a.raw(), s));
}

ofxGgmlTensor ofxGgmlGraph::clamp(ofxGgmlTensor a, float minVal, float maxVal) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_clamp(m_ctx, a.raw(), minVal, maxVal));
}

// ---------------------------------------------------------------------------
// Reduction
// ---------------------------------------------------------------------------

ofxGgmlTensor ofxGgmlGraph::sum(ofxGgmlTensor a) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_sum(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::sumRows(ofxGgmlTensor a) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_sum_rows(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::mean(ofxGgmlTensor a) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_mean(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::argmax(ofxGgmlTensor a) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_argmax(m_ctx, a.raw()));
}

// ---------------------------------------------------------------------------
// Matrix and tensor transforms
// ---------------------------------------------------------------------------

ofxGgmlTensor ofxGgmlGraph::matMul(ofxGgmlTensor a, ofxGgmlTensor b) {
	if (!a.raw() || !b.raw()) return ofxGgmlTensor();
	if (a.raw()->ne[0] != b.raw()->ne[0]) {
		fprintf(
			stderr,
			"ofxGgmlGraph: matMul shape mismatch: a.ne[0]=%lld, b.ne[0]=%lld\n",
			static_cast<long long>(a.raw()->ne[0]),
			static_cast<long long>(b.raw()->ne[0]));
		return ofxGgmlTensor();
	}
	return ofxGgmlTensor(ggml_mul_mat(m_ctx, a.raw(), b.raw()));
}

ofxGgmlTensor ofxGgmlGraph::reshape2d(ofxGgmlTensor a, int64_t ne0, int64_t ne1) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_reshape_2d(m_ctx, a.raw(), ne0, ne1));
}

ofxGgmlTensor ofxGgmlGraph::reshape3d(ofxGgmlTensor a, int64_t ne0, int64_t ne1, int64_t ne2) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_reshape_3d(m_ctx, a.raw(), ne0, ne1, ne2));
}

ofxGgmlTensor ofxGgmlGraph::transpose(ofxGgmlTensor a) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_transpose(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::permute(ofxGgmlTensor a, int axis0, int axis1, int axis2, int axis3) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_permute(m_ctx, a.raw(), axis0, axis1, axis2, axis3));
}

ofxGgmlTensor ofxGgmlGraph::view1d(ofxGgmlTensor a, int64_t ne0, size_t offset) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_view_1d(m_ctx, a.raw(), ne0, offset));
}

ofxGgmlTensor ofxGgmlGraph::view2d(ofxGgmlTensor a, int64_t ne0, int64_t ne1, size_t nb1, size_t offset) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_view_2d(m_ctx, a.raw(), ne0, ne1, nb1, offset));
}

ofxGgmlTensor ofxGgmlGraph::repeat(ofxGgmlTensor a, ofxGgmlTensor b) {
	if (!a.raw() || !b.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_repeat(m_ctx, a.raw(), b.raw()));
}

ofxGgmlTensor ofxGgmlGraph::concat(ofxGgmlTensor a, ofxGgmlTensor b, int dim) {
	if (!a.raw() || !b.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_concat(m_ctx, a.raw(), b.raw(), dim));
}

// ---------------------------------------------------------------------------
// Normalization and activations
// ---------------------------------------------------------------------------

ofxGgmlTensor ofxGgmlGraph::norm(ofxGgmlTensor a, float eps) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_norm(m_ctx, a.raw(), eps));
}

ofxGgmlTensor ofxGgmlGraph::rmsNorm(ofxGgmlTensor a, float eps) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_rms_norm(m_ctx, a.raw(), eps));
}

ofxGgmlTensor ofxGgmlGraph::relu(ofxGgmlTensor a) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_relu(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::gelu(ofxGgmlTensor a) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_gelu(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::silu(ofxGgmlTensor a) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_silu(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::sigmoid(ofxGgmlTensor a) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_sigmoid(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::tanh(ofxGgmlTensor a) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_tanh(m_ctx, a.raw()));
}

ofxGgmlTensor ofxGgmlGraph::softmax(ofxGgmlTensor a) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_soft_max(m_ctx, a.raw()));
}

// ---------------------------------------------------------------------------
// Attention and positional helpers
// ---------------------------------------------------------------------------

ofxGgmlTensor ofxGgmlGraph::flashAttn(
	ofxGgmlTensor q,
	ofxGgmlTensor k,
	ofxGgmlTensor v,
	ofxGgmlTensor mask) {
	if (!q.raw() || !k.raw() || !v.raw()) return ofxGgmlTensor();
	constexpr float scale = 1.0f;
	return ofxGgmlTensor(ggml_flash_attn_ext(
		m_ctx,
		q.raw(),
		k.raw(),
		v.raw(),
		mask.raw(),
		scale,
		0.0f,
		0.0f));
}

ofxGgmlTensor ofxGgmlGraph::rope(ofxGgmlTensor a, ofxGgmlTensor positions, int nDims, int mode) {
	if (!a.raw() || !positions.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_rope(m_ctx, a.raw(), positions.raw(), nDims, mode));
}

// ---------------------------------------------------------------------------
// Convolution and pooling
// ---------------------------------------------------------------------------

ofxGgmlTensor ofxGgmlGraph::conv1d(ofxGgmlTensor a, ofxGgmlTensor kernel, int stride, int padding, int dilation) {
	if (!a.raw() || !kernel.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_conv_1d(m_ctx, kernel.raw(), a.raw(), stride, padding, dilation));
}

ofxGgmlTensor ofxGgmlGraph::convTranspose1d(ofxGgmlTensor a, ofxGgmlTensor kernel, int stride, int padding, int dilation) {
	if (!a.raw() || !kernel.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_conv_transpose_1d(m_ctx, kernel.raw(), a.raw(), stride, padding, dilation));
}

ofxGgmlTensor ofxGgmlGraph::pool1d(
	ofxGgmlTensor a,
	int kernelSize,
	int stride,
	int padding,
	ofxGgmlPoolType type) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_pool_1d(
		m_ctx,
		a.raw(),
		static_cast<enum ggml_op_pool>(type),
		kernelSize,
		stride,
		padding));
}

ofxGgmlTensor ofxGgmlGraph::pool2d(
	ofxGgmlTensor a,
	int kernelSize,
	int stride,
	int padding,
	ofxGgmlPoolType type) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_pool_2d(
		m_ctx,
		a.raw(),
		static_cast<enum ggml_op_pool>(type),
		kernelSize,
		kernelSize,
		stride,
		stride,
		static_cast<float>(padding),
		static_cast<float>(padding)));
}

ofxGgmlTensor ofxGgmlGraph::upscale(ofxGgmlTensor a, int scaleFactor) {
	if (!a.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_upscale(m_ctx, a.raw(), scaleFactor, GGML_SCALE_MODE_NEAREST));
}

// ---------------------------------------------------------------------------
// Loss
// ---------------------------------------------------------------------------

ofxGgmlTensor ofxGgmlGraph::crossEntropyLoss(ofxGgmlTensor logits, ofxGgmlTensor targets) {
	if (!logits.raw() || !targets.raw()) return ofxGgmlTensor();
	return ofxGgmlTensor(ggml_cross_entropy_loss(m_ctx, logits.raw(), targets.raw()));
}

// ---------------------------------------------------------------------------
// Graph finalization
// ---------------------------------------------------------------------------

void ofxGgmlGraph::build(ofxGgmlTensor output) {
	if (!output.raw()) {
		m_graph = nullptr;
		return;
	}
	if (!m_graph) {
		m_graph = ggml_new_graph_custom(m_ctx, m_maxNodes, /*grads=*/false);
	} else {
		ggml_graph_clear(m_graph);
	}
	ggml_build_forward_expand(m_graph, output.raw());
}

void ofxGgmlGraph::build(const std::vector<ofxGgmlTensor> & outputs) {
	if (outputs.empty()) {
		m_graph = nullptr;
		return;
	}
	if (!m_graph) {
		m_graph = ggml_new_graph_custom(m_ctx, m_maxNodes, /*grads=*/false);
	} else {
		ggml_graph_clear(m_graph);
	}

	int validCount = 0;
	for (auto t : outputs) {
		if (t.raw()) {
			ggml_build_forward_expand(m_graph, t.raw());
			++validCount;
		}
	}

	if (validCount == 0) {
		fprintf(stderr, "ofxGgmlGraph: build(outputs) failed: all output tensors were invalid\n");
		m_graph = nullptr;
	} else if (validCount < static_cast<int>(outputs.size())) {
		fprintf(stderr, "ofxGgmlGraph: build(outputs) skipped one or more invalid tensors\n");
	}
}

int ofxGgmlGraph::getNumNodes() const {
	if (!m_graph) return 0;
	return ggml_graph_n_nodes(m_graph);
}

ofxGgmlTensor ofxGgmlGraph::getNode(int index) const {
	if (!m_graph) return ofxGgmlTensor();

	const int nodeCount = ggml_graph_n_nodes(m_graph);
	if (nodeCount <= 0) {
		return ofxGgmlTensor();
	}

	int resolvedIndex = index;
	if (resolvedIndex < 0) {
		resolvedIndex = nodeCount + resolvedIndex;
	}
	if (resolvedIndex < 0 || resolvedIndex >= nodeCount) {
		return ofxGgmlTensor();
	}

	return ofxGgmlTensor(ggml_graph_node(m_graph, resolvedIndex));
}
