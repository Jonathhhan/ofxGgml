#include "ofxGgmlTensor.h"

#include "ggml.h"
#include "ggml-backend.h"
#include "ggml-cpu.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace {

bool isHostAccessible(const struct ggml_tensor * tensor) {
	if (!tensor) return false;
	return tensor->buffer == nullptr || ggml_backend_buffer_is_host(tensor->buffer);
}

}

ofxGgmlTensor::ofxGgmlTensor(struct ggml_tensor * raw)
	: m_tensor(raw) {}

bool ofxGgmlTensor::isValid() const {
	return m_tensor != nullptr;
}

std::string ofxGgmlTensor::getName() const {
	if (!m_tensor) return {};
	return m_tensor->name;
}

void ofxGgmlTensor::setName(const std::string & name) {
	if (m_tensor) {
		ggml_set_name(m_tensor, name.c_str());
	}
}

ofxGgmlType ofxGgmlTensor::getType() const {
	if (!m_tensor) return ofxGgmlType::F32;
	return static_cast<ofxGgmlType>(m_tensor->type);
}

int ofxGgmlTensor::getNumDimensions() const {
	if (!m_tensor) return 0;
	return ggml_n_dims(m_tensor);
}

int64_t ofxGgmlTensor::getDimSize(int dim) const {
	if (!m_tensor || dim < 0 || dim > 3) return 0;
	return m_tensor->ne[dim];
}

int64_t ofxGgmlTensor::getNumElements() const {
	if (!m_tensor) return 0;
	return ggml_nelements(m_tensor);
}

size_t ofxGgmlTensor::getByteSize() const {
	if (!m_tensor) return 0;
	return ggml_nbytes(m_tensor);
}

void * ofxGgmlTensor::getData() {
	if (!m_tensor) return nullptr;
	if (!isHostAccessible(m_tensor)) return nullptr;
	return m_tensor->data;
}

const void * ofxGgmlTensor::getData() const {
	if (!m_tensor) return nullptr;
	if (!isHostAccessible(m_tensor)) return nullptr;
	return m_tensor->data;
}

std::vector<float> ofxGgmlTensor::toFloatVector() const {
	if (!m_tensor) return {};
	const int64_t n = ggml_nelements(m_tensor);
	if (n <= 0) return {};

	// Validate bounds for both F32 and non-F32 types to prevent integer overflow
	if (n > std::numeric_limits<int>::max()) return {};

	std::vector<float> out(static_cast<size_t>(n));
	if (m_tensor->type == GGML_TYPE_F32) {
		ggml_backend_tensor_get(m_tensor, out.data(), 0, out.size() * sizeof(float));
	} else {
		if (!isHostAccessible(m_tensor) || !m_tensor->data) return {};
		// Safe to cast since we validated n <= INT_MAX
		for (int64_t i = 0; i < n; ++i) {
			out[static_cast<size_t>(i)] = ggml_get_f32_1d(m_tensor, static_cast<int>(i));
		}
	}
	return out;
}

void ofxGgmlTensor::setFromFloats(const float * data, size_t count) {
	if (!m_tensor || !data) return;
	const int64_t n = ggml_nelements(m_tensor);
	const size_t actual = std::min(count, static_cast<size_t>(n));
	if (m_tensor->type == GGML_TYPE_F32) {
		ggml_backend_tensor_set(m_tensor, data, 0, actual * sizeof(float));
	} else {
		if (!isHostAccessible(m_tensor) || !m_tensor->data) return;
		if (actual > static_cast<size_t>(std::numeric_limits<int>::max())) return;
		for (size_t i = 0; i < actual; ++i) {
			ggml_set_f32_1d(m_tensor, static_cast<int>(i), data[i]);
		}
	}
}

void ofxGgmlTensor::fill(float value) {
	if (!m_tensor) return;
	if (m_tensor->type == GGML_TYPE_F32) {
		const int64_t n = ggml_nelements(m_tensor);
		if (n <= 0) return;
		const size_t total = static_cast<size_t>(n);
		const size_t chunkElems = 8192;
		std::vector<float> chunk(std::min(total, chunkElems), value);
		size_t offsetElems = 0;
		while (offsetElems < total) {
			const size_t writeElems = std::min(total - offsetElems, chunk.size());
			ggml_backend_tensor_set(m_tensor, chunk.data(),
				offsetElems * sizeof(float), writeElems * sizeof(float));
			offsetElems += writeElems;
		}
		return;
	}
	if (!isHostAccessible(m_tensor) || !m_tensor->data) return;
	ggml_set_f32(m_tensor, value);
}
