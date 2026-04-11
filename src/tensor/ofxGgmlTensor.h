#pragma once

#include "../support/ofxGgmlTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct ggml_tensor;

/// Lightweight, non-owning wrapper around a ggml_tensor pointer.
///
/// Tensors are always owned by a ggml_context (inside ofxGgml or
/// ofxGgmlGraph).  This class provides convenient, OF-friendly
/// accessors without taking ownership.
class ofxGgmlTensor {
public:
	ofxGgmlTensor() = default;
	explicit ofxGgmlTensor(struct ggml_tensor * raw);

	/// True when the wrapper points to a valid tensor.
	bool isValid() const;
	explicit operator bool() const { return isValid(); }

	// ------------------------------------------------------------------
	//  Metadata
	// ------------------------------------------------------------------

	/// Tensor name (set via setName or ggml_set_name).
	std::string getName() const;
	void setName(const std::string & name);

	/// Element type.
	ofxGgmlType getType() const;

	/// Number of dimensions (1–4).
	int getNumDimensions() const;

	/// Size along each dimension (ne[0] … ne[3]).
	int64_t getDimSize(int dim) const;

	/// Total number of elements.
	int64_t getNumElements() const;

	/// Total size in bytes (including padding/quantisation blocks).
	size_t getByteSize() const;

	// ------------------------------------------------------------------
	//  Data access  –  only valid for tensors whose backing buffer
	//  resides in host (CPU) memory.
	// ------------------------------------------------------------------

	/// Raw pointer to the first element.
	void * getData();
	const void * getData() const;

	/// Copy the entire tensor into a float vector (de-quantises if
	/// necessary via ggml helpers).  Returns an empty vector when the
	/// tensor is not host-accessible.
	std::vector<float> toFloatVector() const;

	/// Set the full tensor contents from a float buffer.
	/// The caller must supply exactly getNumElements() floats.
	void setFromFloats(const float * data, size_t count);

	/// Convenience: set all elements to a single scalar.
	void fill(float value);

	// ------------------------------------------------------------------
	//  Underlying handle
	// ------------------------------------------------------------------

	struct ggml_tensor * raw() { return m_tensor; }
	const struct ggml_tensor * raw() const { return m_tensor; }

private:
	struct ggml_tensor * m_tensor = nullptr;
};
