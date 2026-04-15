#pragma once

#include <string>
#include <sstream>

// Minimal openFrameworks stub used for headless unit tests.
// Full addon functionality that depends on OF runtime pieces is tested in
// OFXGGML_WITH_OPENFRAMEWORKS mode.

enum ofLogLevel {
    OF_LOG_VERBOSE = 0,
    OF_LOG_NOTICE,
    OF_LOG_WARNING,
    OF_LOG_ERROR,
    OF_LOG_FATAL_ERROR,
    OF_LOG_SILENT
};

inline void ofLog(ofLogLevel, const std::string &) {}

template <typename T>
inline std::string ofToString(const T & v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}
