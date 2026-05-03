#pragma once

/// Umbrella header for ofxGgml.
///
/// Include this for supported addon functionality:
/// - Core runtime, tensors, models
/// - Text/chat inference and assistants
/// - Optional modalities and helper workflows that remain inside the addon tier
///
/// Companion/example-tier surfaces such as video essay, music/AceStep,
/// MilkDrop, and Holoscan are intentionally excluded. Include
/// ofxGgmlCompanionWorkflows.h for those prototypes.
///
/// For additional layers, include the explicit layered headers:
/// - ofxGgmlBasic.h (core + text)
/// - ofxGgmlModalities.h (basic + speech/vision/TTS/images)
/// - ofxGgmlWorkflows.h (modalities + planning/research helpers)
/// - ofxGgmlCompanionWorkflows.h (example/companion-tier workflows)
#include "ofxGgmlWorkflows.h"
#include "ofxGgmlAssistants.h"
