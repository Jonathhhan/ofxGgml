#pragma once

#include "../tensor/ofxGgmlTensor.h"
#include "../support/ofxGgmlTypes.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct gguf_context;
struct ggml_context;
struct ggml_backend_buffer;

/// Load and inspect a GGUF model file.
///
/// Typical usage:
///
/// @code
///   ofxGgmlModel model;
///   if (!model.load("path/to/model.gguf")) { /* handle error */ }
///
///   // Query metadata.
///   std::string arch = model.getMetadataString("general.architecture");
///
///   // List tensors.
///   for (int64_t i = 0; i < model.getNumTensors(); i++) {
///       auto name = model.getTensorName(i);
///       auto t    = model.getTensor(name);
///       // ... use tensor shape / type information ...
///   }
///
///   // After loading, upload weights to a backend buffer:
///   ofxGgml ggml;
///   ggml.setup();
///   ggml.loadModelWeights(model);
///   // model tensors now reside in backend memory and can be used
///   // in computation graphs.
/// @endcode
class ofxGgmlModel {
public:
	ofxGgmlModel();
	~ofxGgmlModel();

	ofxGgmlModel(const ofxGgmlModel &) = delete;
	ofxGgmlModel & operator=(const ofxGgmlModel &) = delete;

	// ------------------------------------------------------------------
	//  Loading
	// ------------------------------------------------------------------

	/// Load a GGUF file.  Reads all tensor data into host (CPU) memory.
	/// Returns true on success.
	bool load(const std::string & path);

	/// Release all resources.
	void close();

	/// True when a model has been loaded successfully.
	bool isLoaded() const;

	/// File path that was loaded (empty when no model is loaded).
	std::string getPath() const;

	// ------------------------------------------------------------------
	//  Metadata  (key-value pairs stored in the GGUF header)
	// ------------------------------------------------------------------

	/// Total number of metadata key-value pairs.
	int64_t getNumMetadataKeys() const;

	/// Key name at the given index (0-based).
	std::string getMetadataKey(int64_t index) const;

	/// Find a metadata key.  Returns the key index, or -1 if not found.
	int64_t findMetadataKey(const std::string & key) const;

	/// Read a string-valued metadata entry.  Returns "" if missing.
	std::string getMetadataString(const std::string & key) const;

	/// Read an integer metadata entry.  Returns defaultVal if missing
	/// or if the type does not match.
	int32_t getMetadataInt32(const std::string & key, int32_t defaultVal = 0) const;

	/// Read a uint32 metadata entry.
	uint32_t getMetadataUint32(const std::string & key, uint32_t defaultVal = 0) const;

	/// Read a float metadata entry.
	float getMetadataFloat(const std::string & key, float defaultVal = 0.0f) const;

	// ------------------------------------------------------------------
	//  Tensor information
	// ------------------------------------------------------------------

	/// Number of tensors stored in the GGUF file.
	int64_t getNumTensors() const;

	/// Name of the tensor at the given index (0-based).
	std::string getTensorName(int64_t index) const;

	/// Retrieve a tensor by name from the loaded model.
	/// The returned wrapper points into host memory allocated during
	/// load().  Returns an invalid (null) tensor if not found.
	ofxGgmlTensor getTensor(const std::string & name);

	/// List all tensor names.
	std::vector<std::string> getTensorNames() const;

	// ------------------------------------------------------------------
	//  Low-level access
	// ------------------------------------------------------------------

	/// Underlying GGUF context (valid after load()).
	struct gguf_context * ggufContext();
	const struct gguf_context * ggufContext() const;

	/// Underlying ggml context holding the loaded tensor data.
	struct ggml_context * ggmlContext();
	const struct ggml_context * ggmlContext() const;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};
