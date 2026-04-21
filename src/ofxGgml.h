#pragma once

/// Complete feature set header for ofxGgml addon.
///
/// ⚠️  This header includes ALL features and may slow down compilation.
///
/// For most projects, use a specific layered header instead:
/// - ofxGgmlBasic.h      → Text inference only (recommended start)
/// - ofxGgmlModalities.h → Speech, vision, TTS, image generation
/// - ofxGgmlWorkflows.h  → Video planning, montage, research
/// - ofxGgmlAssistants.h → Code and chat assistants
///
/// See docs/getting-started/CHOOSING_FEATURES.md for guidance.

// Include all layers
#include "ofxGgmlCore.h"
#include "ofxGgmlBasic.h"
#include "ofxGgmlModalities.h"
#include "ofxGgmlWorkflows.h"
#include "ofxGgmlAssistants.h"
