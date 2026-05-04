#pragma once

/// Specialized workflows header for ofxGgml addon.
///
/// This header adds opt-in addon-tier research and planning helpers on top of
/// basic text inference, without pulling speech/TTS/vision/diffusion adapters:
/// - Video planning and editing manifests (text/metadata planning only)
/// - Citation search (source-grounded research)
/// - Web crawling and RAG pipelines
/// - Media prompt translation
/// - Image/reference search and CLIP-style ranking primitives
///
/// Montage planning, video essay, music generation, MilkDrop, AceStep, and
/// Holoscan bridge surfaces are companion/example-tier features. Include
/// ofxGgmlCompanionWorkflows.h, or define
/// OFXGGML_ENABLE_COMPANION_WORKFLOWS=1 before including ofxGgmlEasy.h, when
/// you intentionally opt into those boundaries.
///
/// Example usage:
///   #include "ofxGgmlWorkflows.h"
///   #include "support/ofxGgmlEasy.h"
///
///   ofxGgmlEasy ai;
///   ai.configureText(textConfig);
///
///   // Use video planning workflow
///   auto plan = ai.planVideoEdit(
///     "City footage",
///     "Create a fast-paced social recap",
///     "Skyline, transit, crowds");

// Include basic text/local inference without pulling optional modality adapters.
#include "ofxGgmlBasic.h"
#include "inference/ofxGgmlClipInference.h"
#include "inference/ofxGgmlClipCppAdapters.h"

// Video workflows
#include "inference/ofxGgmlVideoPlanner.h"
#include "inference/ofxGgmlLongVideoPlanner.h"

// Research and content workflows
#include "inference/ofxGgmlCitationSearch.h"
#include "inference/ofxGgmlWebCrawler.h"
#include "inference/ofxGgmlImageSearch.h"
#include "inference/ofxGgmlRAGPipeline.h"

// Music and creative workflows
#include "inference/ofxGgmlMediaPromptGenerator.h"
