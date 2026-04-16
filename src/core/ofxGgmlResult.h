#pragma once

#include <string>
#include <stdexcept>
#include <utility>
#include <type_traits>

/// Error codes for ofxGgml operations.
enum class ofxGgmlErrorCode {
	None = 0,

	// Initialization errors
	BackendInitFailed,
	DeviceNotFound,
	OutOfMemory,

	// Graph errors
	GraphNotBuilt,
	GraphAllocFailed,
	InvalidTensor,
	TensorShapeMismatch,

	// Computation errors
	ComputeFailed,
	SynchronizationFailed,
	AsyncOperationPending,

	// Model errors
	ModelLoadFailed,
	ModelFormatInvalid,
	ModelWeightUploadFailed,

	// Inference errors
	InferenceExecutableMissing,
	InferenceProcessFailed,
	InferenceOutputInvalid,

	// General errors
	InvalidArgument,
	NotImplemented,
	UnknownError
};

/// Detailed error information.
struct ofxGgmlError {
	ofxGgmlErrorCode code = ofxGgmlErrorCode::None;
	std::string message;

	ofxGgmlError() = default;

	explicit ofxGgmlError(ofxGgmlErrorCode c, const std::string & msg = "")
		: code(c), message(msg) {}

	/// Returns true if this represents an error (not None).
	constexpr bool hasError() const noexcept {
		return code != ofxGgmlErrorCode::None;
	}

	/// Returns a human-readable description of the error code.
	const char * codeString() const noexcept {
		switch (code) {
			case ofxGgmlErrorCode::None: return "None";
			case ofxGgmlErrorCode::BackendInitFailed: return "BackendInitFailed";
			case ofxGgmlErrorCode::DeviceNotFound: return "DeviceNotFound";
			case ofxGgmlErrorCode::OutOfMemory: return "OutOfMemory";
			case ofxGgmlErrorCode::GraphNotBuilt: return "GraphNotBuilt";
			case ofxGgmlErrorCode::GraphAllocFailed: return "GraphAllocFailed";
			case ofxGgmlErrorCode::InvalidTensor: return "InvalidTensor";
			case ofxGgmlErrorCode::TensorShapeMismatch: return "TensorShapeMismatch";
			case ofxGgmlErrorCode::ComputeFailed: return "ComputeFailed";
			case ofxGgmlErrorCode::SynchronizationFailed: return "SynchronizationFailed";
			case ofxGgmlErrorCode::AsyncOperationPending: return "AsyncOperationPending";
			case ofxGgmlErrorCode::ModelLoadFailed: return "ModelLoadFailed";
			case ofxGgmlErrorCode::ModelFormatInvalid: return "ModelFormatInvalid";
			case ofxGgmlErrorCode::ModelWeightUploadFailed: return "ModelWeightUploadFailed";
			case ofxGgmlErrorCode::InferenceExecutableMissing: return "InferenceExecutableMissing";
			case ofxGgmlErrorCode::InferenceProcessFailed: return "InferenceProcessFailed";
			case ofxGgmlErrorCode::InferenceOutputInvalid: return "InferenceOutputInvalid";
			case ofxGgmlErrorCode::InvalidArgument: return "InvalidArgument";
			case ofxGgmlErrorCode::NotImplemented: return "NotImplemented";
			case ofxGgmlErrorCode::UnknownError: return "UnknownError";
			default: return "Unknown";
		}
	}

	/// Returns a full error description combining code and message.
	std::string toString() const {
		if (!hasError()) return "No error";
		std::string result = codeString();
		if (!message.empty()) {
			result += ": " + message;
		}
		return result;
	}
};

/// Result<T> — A type that contains either a success value or an error.
///
/// This is similar to std::expected (C++23) or Rust's Result<T, E>.
/// Use it for operations that can fail with detailed error information.
///
/// Example usage:
///   Result<int> divide(int a, int b) {
///       if (b == 0) return ofxGgmlError(ofxGgmlErrorCode::InvalidArgument, "division by zero");
///       return a / b;
///   }
///
///   auto r = divide(10, 2);
///   if (r.isOk()) {
///       std::cout << "Result: " << r.value() << std::endl;
///   } else {
///       std::cout << "Error: " << r.error().toString() << std::endl;
///   }
template<typename T>
class Result {
public:
	/// Construct a successful result.
	Result(T && val) : m_hasValue(true) {
		new (&m_storage.value) T(std::move(val));
	}

	Result(const T & val) : m_hasValue(true) {
		new (&m_storage.value) T(val);
	}

	/// Construct an error result.
	Result(const ofxGgmlError & err) : m_hasValue(false) {
		new (&m_storage.error) ofxGgmlError(err);
	}

	Result(ofxGgmlErrorCode code, const std::string & msg = "")
		: m_hasValue(false) {
		new (&m_storage.error) ofxGgmlError(code, msg);
	}

	/// Copy constructor
	Result(const Result & other) : m_hasValue(other.m_hasValue) {
		if (m_hasValue) {
			new (&m_storage.value) T(other.m_storage.value);
		} else {
			new (&m_storage.error) ofxGgmlError(other.m_storage.error);
		}
	}

	/// Move constructor
	Result(Result && other) noexcept : m_hasValue(other.m_hasValue) {
		if (m_hasValue) {
			new (&m_storage.value) T(std::move(other.m_storage.value));
		} else {
			new (&m_storage.error) ofxGgmlError(std::move(other.m_storage.error));
		}
	}

	/// Destructor
	~Result() {
		if (m_hasValue) {
			m_storage.value.~T();
		} else {
			m_storage.error.~ofxGgmlError();
		}
	}

	/// Copy assignment
	Result & operator=(const Result & other) {
		if (this != &other) {
			this->~Result();
			new (this) Result(other);
		}
		return *this;
	}

	/// Move assignment
	Result & operator=(Result && other) noexcept {
		if (this != &other) {
			this->~Result();
			new (this) Result(std::move(other));
		}
		return *this;
	}

	/// Returns true if this contains a value (success).
	constexpr bool isOk() const noexcept { return m_hasValue; }

	/// Returns true if this contains an error.
	constexpr bool isError() const noexcept { return !m_hasValue; }

	/// Returns the success value. Throws if this contains an error.
	T & value() {
		if (!m_hasValue) {
			throw std::runtime_error("Result::value() called on error: " + m_storage.error.toString());
		}
		return m_storage.value;
	}

	const T & value() const {
		if (!m_hasValue) {
			throw std::runtime_error("Result::value() called on error: " + m_storage.error.toString());
		}
		return m_storage.value;
	}

	/// Returns the error. Throws if this contains a value.
	ofxGgmlError & error() {
		if (m_hasValue) {
			throw std::runtime_error("Result::error() called on success value");
		}
		return m_storage.error;
	}

	const ofxGgmlError & error() const {
		if (m_hasValue) {
			throw std::runtime_error("Result::error() called on success value");
		}
		return m_storage.error;
	}

	/// Returns the value if present, otherwise returns the default value.
	T valueOr(const T & defaultValue) const {
		return m_hasValue ? m_storage.value : defaultValue;
	}

	T valueOr(T && defaultValue) const {
		if (m_hasValue) {
			return m_storage.value;
		}
		return std::move(defaultValue);
	}

	/// Explicit bool conversion — returns true if success.
	explicit operator bool() const { return isOk(); }

private:
	bool m_hasValue;
	// Use aligned storage to ensure proper alignment for both T and ofxGgmlError
	union Storage {
		alignas(T) alignas(ofxGgmlError) T value;
		alignas(T) alignas(ofxGgmlError) ofxGgmlError error;

		Storage() {} // Uninitialized
		~Storage() {} // Handled by Result destructor
	} m_storage;
	static_assert(alignof(Storage) >= alignof(T), "Storage alignment must satisfy T alignment");
	static_assert(alignof(Storage) >= alignof(ofxGgmlError), "Storage alignment must satisfy ofxGgmlError alignment");
};

/// Specialization for void (operations that don't return a value).
template<>
class Result<void> {
public:
	/// Construct a successful result.
	Result() : m_error(ofxGgmlErrorCode::None, "") {}

	/// Construct an error result.
	Result(const ofxGgmlError & err) : m_error(err) {}

	Result(ofxGgmlErrorCode code, const std::string & msg = "")
		: m_error(code, msg) {}

	/// Returns true if this represents success.
	constexpr bool isOk() const noexcept { return !m_error.hasError(); }

	/// Returns true if this contains an error.
	constexpr bool isError() const noexcept { return m_error.hasError(); }

	/// Returns the error.
	const ofxGgmlError & error() const { return m_error; }

	/// Explicit bool conversion — returns true if success.
	explicit operator bool() const { return isOk(); }

private:
	ofxGgmlError m_error;
};
