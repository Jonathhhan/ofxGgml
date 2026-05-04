#pragma once

/// Basic inference header for ofxGgml addon.
///
/// This header provides access to:
/// - All core functionality (runtime, tensors, models)
/// - Basic LLM text inference (llama-server and CLI backends)
/// - Streaming context with backpressure control
/// - Prompt templates and lightweight conversation/source helpers
///
/// This is the recommended starting point for text-only AI workflows.
/// For speech, vision, or specialized workflows, see other headers.
///
/// Example usage:
///   #include "ofxGgmlBasic.h"
///
///   ofxGgmlInference inference;
///   ofxGgmlInferenceSettings settings;
///   auto result = inference.generate("model.gguf", "Hello!", settings);

// Include core functionality
#include "ofxGgmlCore.h"

// Basic inference
#include "inference/ofxGgmlInference.h"
#include "inference/ofxGgmlStreamingContext.h"

// Text utilities
#include "support/ofxGgmlPromptTemplates.h"
#include "support/ofxGgmlConversationManager.h"
#include "support/ofxGgmlScriptSource.h"
#include "support/ofxGgmlProjectMemory.h"

// Chat assistant
#include "assistants/ofxGgmlChatAssistant.h"
#include "assistants/ofxGgmlTextAssistant.h"
