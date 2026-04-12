#include "ofxGgmlModel.h"

#include "ggml.h"
#include "gguf.h"

#include <cstdio>

// --------------------------------------------------------------------------
//  PIMPL
// --------------------------------------------------------------------------

struct ofxGgmlModel::Impl {
	struct gguf_context * ggufCtx = nullptr;
	struct ggml_context * ggmlCtx = nullptr;
	std::string path;
};

// --------------------------------------------------------------------------
//  Lifecycle
// --------------------------------------------------------------------------

ofxGgmlModel::ofxGgmlModel()
	: m_impl(std::make_unique<Impl>()) {}

ofxGgmlModel::~ofxGgmlModel() {
	close();
}

bool ofxGgmlModel::load(const std::string & path) {
	// Close any previously loaded model.
	close();

	// Prepare a ggml_context pointer that gguf_init_from_file will
	// populate with the tensor metadata and data.
	struct ggml_context * dataCtx = nullptr;

	struct gguf_init_params params;
	params.no_alloc = false;   // allocate and read tensor data into memory
	params.ctx      = &dataCtx;

	struct gguf_context * guf = gguf_init_from_file(path.c_str(), params);
	if (!guf) {
		return false;
	}

	m_impl->ggufCtx = guf;
	m_impl->ggmlCtx = dataCtx;
	m_impl->path    = path;
	return true;
}

void ofxGgmlModel::close() {
	if (m_impl->ggufCtx) {
		gguf_free(m_impl->ggufCtx);
		m_impl->ggufCtx = nullptr;
	}
	if (m_impl->ggmlCtx) {
		ggml_free(m_impl->ggmlCtx);
		m_impl->ggmlCtx = nullptr;
	}
	m_impl->path.clear();
}

bool ofxGgmlModel::isLoaded() const {
	return m_impl->ggufCtx != nullptr;
}

std::string ofxGgmlModel::getPath() const {
	return m_impl->path;
}

// --------------------------------------------------------------------------
//  Metadata
// --------------------------------------------------------------------------

int64_t ofxGgmlModel::getNumMetadataKeys() const {
	if (!m_impl->ggufCtx) return 0;
	return gguf_get_n_kv(m_impl->ggufCtx);
}

std::string ofxGgmlModel::getMetadataKey(int64_t index) const {
	if (!m_impl->ggufCtx) return {};
	const int64_t n = gguf_get_n_kv(m_impl->ggufCtx);
	if (index < 0 || index >= n) return {};
	const char * key = gguf_get_key(m_impl->ggufCtx, index);
	return key ? key : "";
}

int64_t ofxGgmlModel::findMetadataKey(const std::string & key) const {
	if (!m_impl->ggufCtx) return -1;
	return gguf_find_key(m_impl->ggufCtx, key.c_str());
}

std::string ofxGgmlModel::getMetadataString(const std::string & key) const {
	if (!m_impl->ggufCtx) return {};
	const int64_t id = gguf_find_key(m_impl->ggufCtx, key.c_str());
	if (id < 0) return {};
	if (gguf_get_kv_type(m_impl->ggufCtx, id) != GGUF_TYPE_STRING) return {};
	const char * val = gguf_get_val_str(m_impl->ggufCtx, id);
	return val ? val : "";
}

int32_t ofxGgmlModel::getMetadataInt32(const std::string & key, int32_t defaultVal) const {
	if (!m_impl->ggufCtx) return defaultVal;
	const int64_t id = gguf_find_key(m_impl->ggufCtx, key.c_str());
	if (id < 0) return defaultVal;
	const enum gguf_type t = gguf_get_kv_type(m_impl->ggufCtx, id);
	if (t == GGUF_TYPE_INT32)  return gguf_get_val_i32(m_impl->ggufCtx, id);
	if (t == GGUF_TYPE_UINT32) return static_cast<int32_t>(gguf_get_val_u32(m_impl->ggufCtx, id));
	if (t == GGUF_TYPE_INT8)   return static_cast<int32_t>(gguf_get_val_i8(m_impl->ggufCtx, id));
	if (t == GGUF_TYPE_UINT8)  return static_cast<int32_t>(gguf_get_val_u8(m_impl->ggufCtx, id));
	if (t == GGUF_TYPE_INT16)  return static_cast<int32_t>(gguf_get_val_i16(m_impl->ggufCtx, id));
	if (t == GGUF_TYPE_UINT16) return static_cast<int32_t>(gguf_get_val_u16(m_impl->ggufCtx, id));
	return defaultVal;
}

uint32_t ofxGgmlModel::getMetadataUint32(const std::string & key, uint32_t defaultVal) const {
	if (!m_impl->ggufCtx) return defaultVal;
	const int64_t id = gguf_find_key(m_impl->ggufCtx, key.c_str());
	if (id < 0) return defaultVal;
	const enum gguf_type t = gguf_get_kv_type(m_impl->ggufCtx, id);
	if (t == GGUF_TYPE_UINT32) return gguf_get_val_u32(m_impl->ggufCtx, id);
	if (t == GGUF_TYPE_INT32)  return static_cast<uint32_t>(gguf_get_val_i32(m_impl->ggufCtx, id));
	return defaultVal;
}

float ofxGgmlModel::getMetadataFloat(const std::string & key, float defaultVal) const {
	if (!m_impl->ggufCtx) return defaultVal;
	const int64_t id = gguf_find_key(m_impl->ggufCtx, key.c_str());
	if (id < 0) return defaultVal;
	if (gguf_get_kv_type(m_impl->ggufCtx, id) != GGUF_TYPE_FLOAT32) return defaultVal;
	return gguf_get_val_f32(m_impl->ggufCtx, id);
}

// --------------------------------------------------------------------------
//  Tensor information
// --------------------------------------------------------------------------

int64_t ofxGgmlModel::getNumTensors() const {
	if (!m_impl->ggufCtx) return 0;
	return gguf_get_n_tensors(m_impl->ggufCtx);
}

std::string ofxGgmlModel::getTensorName(int64_t index) const {
	if (!m_impl->ggufCtx) return {};
	const int64_t n = gguf_get_n_tensors(m_impl->ggufCtx);
	if (index < 0 || index >= n) return {};
	const char * name = gguf_get_tensor_name(m_impl->ggufCtx, index);
	return name ? name : "";
}

ofxGgmlTensor ofxGgmlModel::getTensor(const std::string & name) {
	if (!m_impl->ggmlCtx) return ofxGgmlTensor();
	struct ggml_tensor * t = ggml_get_tensor(m_impl->ggmlCtx, name.c_str());
	return ofxGgmlTensor(t);
}

std::vector<std::string> ofxGgmlModel::getTensorNames() const {
	std::vector<std::string> names;
	if (!m_impl->ggufCtx) return names;
	const int64_t n = gguf_get_n_tensors(m_impl->ggufCtx);
	names.reserve(static_cast<size_t>(n));
	for (int64_t i = 0; i < n; i++) {
		const char * name = gguf_get_tensor_name(m_impl->ggufCtx, i);
		names.emplace_back(name ? name : "");
	}
	return names;
}

// --------------------------------------------------------------------------
//  Low-level access
// --------------------------------------------------------------------------

struct gguf_context * ofxGgmlModel::ggufContext() {
	return m_impl->ggufCtx;
}

const struct gguf_context * ofxGgmlModel::ggufContext() const {
	return m_impl->ggufCtx;
}

struct ggml_context * ofxGgmlModel::ggmlContext() {
	return m_impl->ggmlCtx;
}

const struct ggml_context * ofxGgmlModel::ggmlContext() const {
	return m_impl->ggmlCtx;
}
