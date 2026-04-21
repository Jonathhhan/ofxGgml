#pragma once

/// Modalities header for ofxGgml addon.
///
/// This header adds multimodal AI capabilities on top of basic inference:
/// - Speech-to-text (Whisper integration)
/// - Text-to-speech (Piper, OuteTTS support)
/// - Vision and video understanding (LLaVA-style models)
/// - Image generation (Stable Diffusion integration)
/// - CLIP embeddings (text/image similarity)
///
/// Include this when you need audio or visual AI workflows.
/// Requires basic inference functionality from ofxGgmlBasic.h
///
/// Example usage:
///   #include "ofxGgmlModalities.h"
///
///   ofxGgmlEasy ai;
///   // Configure text first
///   ai.configureText(textConfig);
///
///   // Add vision capability
///   ofxGgmlEasyVisionConfig visionConfig;
///   visionConfig.modelPath = "vision-model.gguf";
///   ai.configureVision(visionConfig);
///   auto result = ai.describeImage("photo.jpg");

// Include basic functionality
#include "ofxGgmlBasic.h"

// Speech modalities
#include "inference/ofxGgmlSpeechInference.h"
#include "inference/ofxGgmlLiveSpeechTranscriber.h"

// TTS modalities
#include "inference/ofxGgmlTtsInference.h"
#include "inference/ofxGgmlPiperTtsAdapters.h"
#include "inference/ofxGgmlChatLlmTtsAdapters.h"

// Vision modalities
#include "inference/ofxGgmlVisionInference.h"
#include "inference/ofxGgmlVideoInference.h"

// Image generation
#include "inference/ofxGgmlDiffusionInference.h"
#include "inference/ofxGgmlStableDiffusionAdapters.h"

// CLIP embeddings
#include "inference/ofxGgmlClipInference.h"
#include "inference/ofxGgmlClipCppAdapters.h"
