#pragma once

/// @file ofxGgml.h
/// Umbrella header for the ofxGgml addon.
///
/// Include this single file to access all addon classes:
///   - ofxGgml         — backend management, compute scheduling
///   - ofxGgmlGraph    — computation graph builder
///   - ofxGgmlInference — text generation / embeddings helper (llama CLI)
///   - ofxGgmlModel    — GGUF model loading
///   - ofxGgmlProjectMemory — persistent prompt memory helper
///   - ofxGgmlTensor   — non-owning tensor handle
///   - ofxGgmlTypes    — enums, settings, result structs
///   - ofxGgmlHelpers  — utility functions
///   - ofxGgmlVersion  — version macros

#include "ofxGgmlVersion.h"
#include "ofxGgmlTypes.h"
#include "ofxGgmlHelpers.h"
#include "ofxGgmlTensor.h"
#include "ofxGgmlGraph.h"
#include "ofxGgmlInference.h"
#include "ofxGgmlModel.h"
#include "ofxGgmlProjectMemory.h"
#include "ofxGgmlScriptSource.h"
#include "ofxGgmlCore.h"
