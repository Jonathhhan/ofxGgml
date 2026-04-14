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
///   - ofxGgmlScriptSource — local-folder / GitHub script source browser helper
///   - ofxGgmlTensor   — non-owning tensor handle
///   - ofxGgmlTypes    — enums, settings, result structs
///   - ofxGgmlResult   — Result<T> type for error handling
///   - ofxGgmlHelpers  — utility functions
///   - ofxGgmlVersion  — version macros

#include "ofxGgmlVersion.h"
#include "ofxGgmlTypes.h"
#include "ofxGgmlResult.h"
#include "ofxGgmlHelpers.h"
#include "ofxGgmlTensor.h"
#include "ofxGgmlGraph.h"
#include "ofxGgmlInference.h"
#include "ofxGgmlModel.h"
#include "ofxGgmlProjectMemory.h"
#include "ofxGgmlScriptSource.h"
#include "ofxGgmlCore.h"
