#pragma once

/// Companion/example-tier workflows for ofxGgml.
///
/// These surfaces intentionally live outside the default Option A addon tier
/// (ggml tensors + basic LLM inference). Include this header only when an app
/// explicitly opts into workflow prototypes that should eventually become
/// standalone companion addons or example-level code.

#ifndef OFXGGML_ENABLE_COMPANION_WORKFLOWS
#define OFXGGML_ENABLE_COMPANION_WORKFLOWS 1
#endif

#include "ofxGgmlWorkflows.h"

#include "bridges/ofxGgmlHoloscanBridge.h"
#include "inference/ofxGgmlAceStepBridge.h"
#include "inference/ofxGgmlMilkDropGenerator.h"
#include "inference/ofxGgmlMusicGenerator.h"
#include "inference/ofxGgmlVideoEssayWorkflow.h"
