#pragma once

#include "compute/ofxGgmlTensor.h"
#include "core/ofxGgmlTypes.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ggml_backend_buffer;
struct ggml_context;
struct gguf_context;

/// GGUF model loader and inspection helper.
///
/// The loader keeps GGUF metadata and tensor data in host memory. Use
/// `ofxGgml::loadModelWeights()` when you want those tensors uploaded into a
/// backend buffer for execution.
class ofxGgmlModel {
public:
	ofxGgmlModel();
	~ofxGgmlModel();

	ofxGgmlModel(const ofxGgmlModel &) = delete;
	ofxGgmlModel & operator=(const ofxGgmlModel &) = delete;

	// ---------------------------------------------------------------------------
	// Loading
	// ---------------------------------------------------------------------------

	/// Load a GGUF file into host memory.
	bool load(const std::string & path);

	/// Release all owned resources.
	void close();

	/// True when a model is loaded.
	bool isLoaded() const;

	/// Path of the currently loaded file, or empty when not loaded.
	std::string getPath() const;

	// ---------------------------------------------------------------------------
	// Metadata
	// ---------------------------------------------------------------------------

	/// Number of GGUF metadata key-value entries.
	int64_t getNumMetadataKeys() const;

	/// Metadata key name at the given zero-based index.
	std::string getMetadataKey(int64_t index) const;

	/// Find a metadata key. Returns -1 when missing.
	int64_t findMetadataKey(const std::string & key) const;

	/// Read a string metadata value. Returns an empty string when missing.
	std::string getMetadataString(const std::string & key) const;

	/// Read an integer metadata value with fallback.
	int32_t getMetadataInt32(const std::string & key, int32_t defaultVal = 0) const;

	/// Read an unsigned integer metadata value with fallback.
	uint32_t getMetadataUint32(const std::string & key, uint32_t defaultVal = 0) const;

	/// Read a float metadata value with fallback.
	float getMetadataFloat(const std::string & key, float defaultVal = 0.0f) const;

	// ---------------------------------------------------------------------------
	// Tensor inspection
	// ---------------------------------------------------------------------------

	/// Number of tensors stored in the GGUF file.
	int64_t getNumTensors() const;

	/// Tensor name at the given zero-based index.
	std::string getTensorName(int64_t index) const;

	/// Retrieve a tensor view by name.
	ofxGgmlTensor getTensor(const std::string & name);

	/// List all tensor names.
	std::vector<std::string> getTensorNames() const;

	// ---------------------------------------------------------------------------
	// Low-level access
	// ---------------------------------------------------------------------------

	struct gguf_context * ggufContext();
	const struct gguf_context * ggufContext() const;

	struct ggml_context * ggmlContext();
	const struct ggml_context * ggmlContext() const;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};
