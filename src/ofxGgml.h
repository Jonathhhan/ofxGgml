#pragma once

/// Umbrella header for ofxGgml.
///
/// Include this when you want full addon functionality:
/// - Core runtime, tensors, models
/// - Text/chat inference and assistants
/// - Speech, TTS, vision, diffusion, CLIP
/// - Video, montage, and citation workflows
///
/// For lighter builds, prefer the layered headers:
/// - ofxGgmlBasic.h (core + text)
/// - ofxGgmlModalities.h (basic + speech/vision/TTS/images)
/// - ofxGgmlWorkflows.h (modalities + video/montage/research)
#include "ofxGgmlWorkflows.h"
#include "ofxGgmlAssistants.h"
