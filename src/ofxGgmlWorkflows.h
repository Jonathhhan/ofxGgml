#pragma once

/// Specialized workflows header for ofxGgml addon.
///
/// This header adds high-level creative and research workflows:
/// - Video planning and editing (beat planning, multi-scene scripts)
/// - Montage planning (subtitle-driven clip selection, EDL export)
/// - Citation search (source-grounded research)
/// - Video essay workflow (topic → script → narration → planning)
/// - Music generation (prompt generation, ABC notation, AceStep integration)
/// - MilkDrop preset generation (visualization presets)
/// - Web crawling and RAG pipelines
/// - Media prompt translation (music → image, image → music)
/// - Image search (reference gathering)
///
/// These workflows combine multiple modalities and assistants
/// into domain-specific pipelines.
///
/// Example usage:
///   #include "ofxGgmlWorkflows.h"
///
///   ofxGgmlEasy ai;
///   ai.configureText(textConfig);
///
///   // Use video planning workflow
///   auto plan = ai.planVideoEdit(
///     "City footage",
///     "Create a fast-paced social recap",
///     "Skyline, transit, crowds");

// Include modalities (which includes basic)
#include "ofxGgmlModalities.h"

// Video workflows
#include "inference/ofxGgmlVideoPlanner.h"
#include "inference/ofxGgmlLongVideoPlanner.h"
#include "inference/ofxGgmlMontagePlanner.h"
#include "inference/ofxGgmlMontagePreviewBridge.h"
#include "inference/ofxGgmlVideoEssayWorkflow.h"

// Research and content workflows
#include "inference/ofxGgmlCitationSearch.h"
#include "inference/ofxGgmlWebCrawler.h"
#include "inference/ofxGgmlImageSearch.h"
#include "inference/ofxGgmlRAGPipeline.h"

// Music and creative workflows
#include "inference/ofxGgmlMediaPromptGenerator.h"
#include "inference/ofxGgmlMusicGenerator.h"
#include "inference/ofxGgmlMilkDropGenerator.h"
#include "inference/ofxGgmlAceStepBridge.h"
