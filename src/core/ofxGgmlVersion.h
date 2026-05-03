#pragma once

/// @file ofxGgmlVersion.h
/// Version information for the ofxGgml addon.

#define OFX_GGML_VERSION_MAJOR 1
#define OFX_GGML_VERSION_MINOR 0
#define OFX_GGML_VERSION_PATCH 4

/// Public compatibility aliases. New code should prefer the OFXGGML_* names;
/// the historical OFX_GGML_* macros remain supported for 1.x.
#define OFXGGML_VERSION_MAJOR OFX_GGML_VERSION_MAJOR
#define OFXGGML_VERSION_MINOR OFX_GGML_VERSION_MINOR
#define OFXGGML_VERSION_PATCH OFX_GGML_VERSION_PATCH

/// Semantic version string, e.g. "1.0.4".
#define OFX_GGML_VERSION_STRING "1.0.4"
#define OFXGGML_VERSION_STRING OFX_GGML_VERSION_STRING

/// Numeric version helper suitable for preprocessor comparisons.
#define OFXGGML_VERSION_ENCODE(major, minor, patch) (((major) * 10000) + ((minor) * 100) + (patch))
#define OFXGGML_VERSION_CODE OFXGGML_VERSION_ENCODE(OFXGGML_VERSION_MAJOR, OFXGGML_VERSION_MINOR, OFXGGML_VERSION_PATCH)
#define OFXGGML_VERSION_AT_LEAST(major, minor, patch) \
	(OFXGGML_VERSION_CODE >= OFXGGML_VERSION_ENCODE((major), (minor), (patch)))
