#pragma once

#include "ofxGgmlTypes.h"

#include <string>

namespace ofxGgmlHelpers {

/// Human-readable label for an element type.
inline std::string typeName(ofxGgmlType t) {
	switch (t) {
		case ofxGgmlType::F32:  return "f32";
		case ofxGgmlType::F16:  return "f16";
		case ofxGgmlType::Q4_0: return "q4_0";
		case ofxGgmlType::Q4_1: return "q4_1";
		case ofxGgmlType::Q5_0: return "q5_0";
		case ofxGgmlType::Q5_1: return "q5_1";
		case ofxGgmlType::Q8_0: return "q8_0";
		case ofxGgmlType::Q8_1: return "q8_1";
		case ofxGgmlType::I8:   return "i8";
		case ofxGgmlType::I16:  return "i16";
		case ofxGgmlType::I32:  return "i32";
		case ofxGgmlType::I64:  return "i64";
		case ofxGgmlType::F64:  return "f64";
		case ofxGgmlType::BF16: return "bf16";
		default:                return "unknown";
	}
}

/// Human-readable label for a backend device type.
inline std::string backendTypeName(ofxGgmlBackendType t) {
	switch (t) {
		case ofxGgmlBackendType::Cpu:            return "CPU";
		case ofxGgmlBackendType::Gpu:            return "GPU";
		case ofxGgmlBackendType::IntegratedGpu:  return "Integrated GPU";
		case ofxGgmlBackendType::Accelerator:    return "Accelerator";
		default:                                 return "Unknown";
	}
}

/// Element size in bytes for the common unquantized types.
/// Returns 0 for quantized types (their block size varies).
inline size_t elementSize(ofxGgmlType t) {
	switch (t) {
		case ofxGgmlType::F32:  return 4;
		case ofxGgmlType::F16:  return 2;
		case ofxGgmlType::BF16: return 2;
		case ofxGgmlType::F64:  return 8;
		case ofxGgmlType::I8:   return 1;
		case ofxGgmlType::I16:  return 2;
		case ofxGgmlType::I32:  return 4;
		case ofxGgmlType::I64:  return 8;
		default:                return 0;
	}
}

/// Human-readable label for the addon state.
inline std::string stateName(ofxGgmlState s) {
	switch (s) {
		case ofxGgmlState::Uninitialized: return "Uninitialized";
		case ofxGgmlState::Ready:         return "Ready";
		case ofxGgmlState::Computing:     return "Computing";
		case ofxGgmlState::Error:         return "Error";
		default:                          return "Unknown";
	}
}

/// Format a byte count as a human-readable string (e.g. "1.23 GB").
inline std::string formatBytes(size_t bytes) {
	const char * units[] = { "B", "KB", "MB", "GB", "TB" };
	int idx = 0;
	double size = static_cast<double>(bytes);
	while (size >= 1024.0 && idx < 4) {
		size /= 1024.0;
		idx++;
	}
	char buf[64];
	snprintf(buf, sizeof(buf), "%.2f %s", size, units[idx]);
	return buf;
}

} // namespace ofxGgmlHelpers
