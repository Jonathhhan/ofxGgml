#pragma once

#include "ofxGgmlResult.h"
#include <map>
#include <vector>
#include <chrono>
#include <optional>

#if __cplusplus >= 202002L
#include <source_location>
#endif

/// Enhanced error with structured context information.
///
/// This extends ofxGgmlError with:
/// - Nested error chains (cause tracking)
/// - Structured context (key-value pairs)
/// - Source location information (C++20)
/// - Timestamp
/// - Stack trace capability
///
/// Example usage:
///     return EnhancedError(ofxGgmlErrorCode::ModelLoadFailed)
///         .withMessage("Failed to load GGUF file")
///         .withContext("path", modelPath)
///         .withContext("file_size", std::to_string(fileSize))
///         .withCause(innerError)
///         .toResult<ModelInfo>();
///
struct EnhancedError : public ofxGgmlError {
	/// Structured context information (key-value pairs)
	std::map<std::string, std::string> context;

	/// Chain of underlying causes (innermost error last)
	std::vector<ofxGgmlError> causes;

	/// Timestamp when error was created
	std::chrono::system_clock::time_point timestamp;

#if __cplusplus >= 202002L
	/// Source location where error was created (C++20)
	std::source_location location;
#else
	/// Fallback source location info
	struct {
		const char* file = "";
		int line = 0;
		const char* function = "";
	} location;
#endif

	/// Stack trace (optional, expensive to capture)
	std::vector<std::string> stackTrace;

	// Constructors
	EnhancedError() : ofxGgmlError() {
		timestamp = std::chrono::system_clock::now();
	}

#if __cplusplus >= 202002L
	explicit EnhancedError(
		ofxGgmlErrorCode c,
		const std::string& msg = "",
		std::source_location loc = std::source_location::current()
	) : ofxGgmlError(c, msg), location(loc) {
		timestamp = std::chrono::system_clock::now();
	}
#else
	explicit EnhancedError(
		ofxGgmlErrorCode c,
		const std::string& msg = "",
		const char* file = __builtin_FILE(),
		int line = __builtin_LINE()
	) : ofxGgmlError(c, msg) {
		timestamp = std::chrono::system_clock::now();
		location.file = file;
		location.line = line;
	}
#endif

	// Fluent API for building error context
	EnhancedError& withMessage(const std::string& msg) {
		message = msg;
		return *this;
	}

	EnhancedError& withContext(const std::string& key, const std::string& value) {
		context[key] = value;
		return *this;
	}

	template<typename T>
	EnhancedError& withContext(const std::string& key, const T& value) {
		context[key] = std::to_string(value);
		return *this;
	}

	EnhancedError& withCause(const ofxGgmlError& cause) {
		if (cause.hasError()) {
			causes.push_back(cause);
		}
		return *this;
	}

	EnhancedError& withCause(const EnhancedError& cause) {
		if (cause.hasError()) {
			causes.push_back(cause);
			// Flatten nested causes
			causes.insert(causes.end(), cause.causes.begin(), cause.causes.end());
		}
		return *this;
	}

	EnhancedError& captureStackTrace(size_t maxDepth = 20) {
		// Note: Stack trace capture is platform-specific and expensive
		// This is a placeholder - real implementation would use backtrace() on Unix
		// or CaptureStackBackTrace() on Windows
		stackTrace.push_back("[Stack trace capture not yet implemented]");
		return *this;
	}

	/// Get source location as string
	std::string locationString() const {
#if __cplusplus >= 202002L
		return std::string(location.file_name()) + ":" +
		       std::to_string(location.line()) + " in " +
		       location.function_name();
#else
		return std::string(location.file) + ":" +
		       std::to_string(location.line);
#endif
	}

	/// Get timestamp as ISO 8601 string
	std::string timestampString() const {
		auto time = std::chrono::system_clock::to_time_t(timestamp);
		char buffer[100];
		std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", std::gmtime(&time));
		return buffer;
	}

	/// Returns a detailed error report with all context
	std::string toDetailedString() const {
		if (!hasError()) return "No error";

		std::string result;
		result += "[" + timestampString() + "] ";
		result += codeString();
		result += ": " + message + "\n";

		// Source location
		result += "  Location: " + locationString() + "\n";

		// Context
		if (!context.empty()) {
			result += "  Context:\n";
			for (const auto& [key, value] : context) {
				result += "    " + key + ": " + value + "\n";
			}
		}

		// Causes
		if (!causes.empty()) {
			result += "  Caused by:\n";
			for (size_t i = 0; i < causes.size(); ++i) {
				result += "    " + std::to_string(i + 1) + ". " +
				          causes[i].toString() + "\n";
			}
		}

		// Stack trace
		if (!stackTrace.empty()) {
			result += "  Stack trace:\n";
			for (const auto& frame : stackTrace) {
				result += "    " + frame + "\n";
			}
		}

		return result;
	}

	/// Convert to Result<T> for returning from functions
	template<typename T>
	Result<T> toResult() const {
		return Result<T>::error(*this);
	}

	/// Convert to JSON for structured logging
	std::string toJson() const {
		std::string json = "{";
		json += "\"code\":\"" + std::string(codeString()) + "\",";
		json += "\"message\":\"" + escapeJson(message) + "\",";
		json += "\"timestamp\":\"" + timestampString() + "\",";
		json += "\"location\":\"" + escapeJson(locationString()) + "\"";

		if (!context.empty()) {
			json += ",\"context\":{";
			bool first = true;
			for (const auto& [key, value] : context) {
				if (!first) json += ",";
				json += "\"" + escapeJson(key) + "\":\"" + escapeJson(value) + "\"";
				first = false;
			}
			json += "}";
		}

		if (!causes.empty()) {
			json += ",\"causes\":[";
			for (size_t i = 0; i < causes.size(); ++i) {
				if (i > 0) json += ",";
				json += "\"" + escapeJson(causes[i].toString()) + "\"";
			}
			json += "]";
		}

		json += "}";
		return json;
	}

private:
	static std::string escapeJson(const std::string& str) {
		std::string result;
		for (char c : str) {
			switch (c) {
				case '"': result += "\\\""; break;
				case '\\': result += "\\\\"; break;
				case '\n': result += "\\n"; break;
				case '\r': result += "\\r"; break;
				case '\t': result += "\\t"; break;
				default: result += c;
			}
		}
		return result;
	}
};


/// Helper macro for creating enhanced errors with automatic source location
#if __cplusplus >= 202002L
#define OFXGGML_ERROR(code, msg) \
	EnhancedError(code, msg)
#else
#define OFXGGML_ERROR(code, msg) \
	EnhancedError(code, msg, __FILE__, __LINE__)
#endif


/// Helper macro for wrapping operations with automatic error context
#define OFXGGML_TRY(expr, errorCode, errorMsg) \
	({ \
		auto _result = (expr); \
		if (!_result) { \
			return OFXGGML_ERROR(errorCode, errorMsg) \
				.withContext("expression", #expr) \
				.withCause(_result.error()) \
				.toResult<decltype(_result.value())>(); \
		} \
		_result.value(); \
	})
