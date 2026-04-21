#pragma once

/// Assistants header for ofxGgml addon.
///
/// This header adds AI assistant capabilities for specialized tasks:
/// - Chat assistant (conversation management, multi-turn dialogue)
/// - Code assistant (semantic retrieval, inline completion, task planning)
/// - Workspace assistant (patch validation, unified diff application)
/// - Coding agent (orchestration layer with approval callbacks)
/// - Code review (hierarchical analysis, embedding-based ranking)
/// - Text assistant (translation, summarization, rewriting)
///
/// These assistants provide higher-level task-oriented APIs
/// on top of the basic inference layer.
///
/// Example usage:
///   #include "ofxGgmlAssistants.h"
///
///   ofxGgmlCodeAssistant codeAssistant;
///   codeAssistant.setInference(&inference);
///
///   auto result = codeAssistant.generateCode(
///     "Add error handling to parseConfig",
///     workspace);

// Include basic functionality
#include "ofxGgmlBasic.h"

// Assistant implementations
#include "assistants/ofxGgmlChatAssistant.h"
#include "assistants/ofxGgmlTextAssistant.h"
#include "assistants/ofxGgmlCodeAssistant.h"
#include "assistants/ofxGgmlWorkspaceAssistant.h"
#include "assistants/ofxGgmlCodingAgent.h"
#include "assistants/ofxGgmlCodeReview.h"
