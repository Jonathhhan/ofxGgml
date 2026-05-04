#pragma once

/// Umbrella header for the supported ofxGgml core addon tier.
///
/// Include this for the default local inference boundary:
/// - Core runtime, tensors, graphs, and GGUF models
/// - Text inference, streaming, batching, metrics, prompt utilities, and model registry
/// - Chat/text/code assistant helpers that stay close to local inference
///
/// Speech, TTS, vision, video, diffusion, and CLIP/image ranking adapters are
/// intentionally split behind ofxGgmlModalities.h. Planning/research helpers
/// are behind ofxGgmlWorkflows.h. Creative product workflows such as montage,
/// video essay, music/AceStep, MilkDrop, and Holoscan remain companion/example
/// surfaces behind ofxGgmlCompanionWorkflows.h.
///
/// For additional layers, include the explicit layered headers:
/// - ofxGgmlBasic.h (core + text)
/// - ofxGgmlModalities.h (basic + speech/vision/TTS/images/CLIP adapters)
/// - ofxGgmlWorkflows.h (basic + source-grounded planning/research helpers)
/// - ofxGgmlCompanionWorkflows.h (example/companion-tier workflows)
#include "ofxGgmlBasic.h"
#include "ofxGgmlAssistants.h"
