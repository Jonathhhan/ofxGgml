#pragma once

/// @file ofxGgml.h
/// Umbrella header for the ofxGgml addon.
///
/// Include this single file to access all addon classes:
///   - ofxGgml         — backend management, compute scheduling
///   - ofxGgmlGraph    — computation graph builder
///   - ofxGgmlModel    — GGUF model loading
///   - ofxGgmlTensor   — non-owning tensor handle
///   - ofxGgmlTypes    — enums, settings, result structs
///   - ofxGgmlHelpers  — utility functions
///   - ofxGgmlVersion  — version macros

#include "ofxGgmlVersion.h"
#include "ofxGgmlTypes.h"
#include "ofxGgmlHelpers.h"
#include "ofxGgmlTensor.h"
#include "ofxGgmlGraph.h"
#include "ofxGgmlModel.h"
#include "ofxGgmlCore.h"
