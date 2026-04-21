#pragma once

/// Basic inference header for ofxGgml addon.
///
/// This header provides access to:
/// - All core functionality (runtime, tensors, models)
/// - Basic LLM text inference (llama-server and CLI backends)
/// - Streaming context with backpressure control
/// - Simple facade for common tasks (ofxGgmlEasy)
/// - Prompt templates
///
/// This is the recommended starting point for text-only AI workflows.
/// For speech, vision, or specialized workflows, see other headers.
///
/// Example usage:
///   #include "ofxGgmlBasic.h"
///
///   ofxGgmlEasy ai;
///   ofxGgmlEasyTextConfig config;
///   config.modelPath = "model.gguf";
///   ai.configureText(config);
///   auto result = ai.chat("Hello!", "English");

// Include core functionality
#include "ofxGgmlCore.h"

// Basic inference
#include "inference/ofxGgmlInference.h"
#include "inference/ofxGgmlStreamingContext.h"

// Simple facade and utilities
#include "support/ofxGgmlEasy.h"
#include "support/ofxGgmlPromptTemplates.h"
#include "support/ofxGgmlConversationManager.h"
#include "support/ofxGgmlScriptSource.h"
#include "support/ofxGgmlProjectMemory.h"

// Chat assistant
#include "assistants/ofxGgmlChatAssistant.h"
#include "assistants/ofxGgmlTextAssistant.h"
