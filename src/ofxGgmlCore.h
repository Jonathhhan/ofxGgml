#pragma once

/// Core-only header for ofxGgml addon.
///
/// This header provides access to the fundamental components:
/// - Runtime and backend management (CPU, CUDA, Vulkan, Metal)
/// - Tensor operations and graph building
/// - GGUF model loading and inspection
/// - Version information and basic types
///
/// Use this header when you only need low-level tensor operations
/// and model loading without the higher-level inference helpers.
///
/// For basic text inference, see ofxGgmlBasic.h
/// For complete functionality, see ofxGgml.h

// Version and types
#include "core/ofxGgmlVersion.h"
#include "core/ofxGgmlTypes.h"
#include "core/ofxGgmlResult.h"
#include "core/ofxGgmlHelpers.h"
#include "core/ofxGgmlLogger.h"
#include "core/ofxGgmlMetrics.h"
#include "core/ofxGgmlCore.h"

// Tensor and graph operations
#include "compute/ofxGgmlTensor.h"
#include "compute/ofxGgmlGraph.h"

// Model loading
#include "model/ofxGgmlModel.h"
#include "model/ofxGgmlModelRegistry.h"
