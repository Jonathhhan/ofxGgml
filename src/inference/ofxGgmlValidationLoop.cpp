#include "ofxGgmlValidationLoop.h"
#include "ofxGgmlDiffusionInference.h"
#include "ofxGgmlVisionInference.h"
#include "ofxGgmlVideoPlanner.h"
#include "ofxGgmlClipInference.h"
#include "ofxGgmlInference.h"

#include <chrono>

// Concrete validation loop implementations for common workflows

namespace ofxGgmlValidationLoops {

/// Diffusion → Vision validation loop helper
ofxGgmlValidationLoopResult<ofxGgmlImageGenerationResult, ofxGgmlDiffusionInference::ImageValidationResult>
validateDiffusionWithVision(
	const ofxGgmlImageGenerationRequest& request,
	ofxGgmlDiffusionInference* diffusion,
	ofxGgmlVisionInference* vision,
	const ofxGgmlVisionModelProfile& visionProfile,
	ofxGgmlInference* textInference,
	const ofxGgmlValidationLoopConfig& config) {

	ofxGgmlValidationLoop<ofxGgmlImageGenerationResult,
		ofxGgmlDiffusionInference::ImageValidationResult> loop(config);

	// Generator: Generate images with diffusion
	loop.setGenerator([&](int attempt) {
		auto genRequest = request;
		// Could modify seed or other params based on attempt number
		if (attempt > 1) {
			genRequest.seed = -1; // Random seed for retry
		}
		return diffusion->generate(genRequest);
	});

	// Validator: Analyze with vision
	loop.setValidator([&](const ofxGgmlImageGenerationResult& genResult) {
		return ofxGgmlDiffusionInference::validateWithVision(
			genResult, request.prompt, vision, visionProfile, textInference);
	});

	// Scorer: Use average alignment score
	loop.setScorer([](const ofxGgmlImageGenerationResult&,
		const ofxGgmlDiffusionInference::ImageValidationResult& validation) {
		return validation.averageScore;
	});

	return loop.run();
}

/// Video Planning → CLIP validation loop helper
ofxGgmlValidationLoopResult<ofxGgmlVideoPlan, ofxGgmlVideoPlanner::SceneCoherenceResult>
validateVideoPlanWithCLIP(
	const ofxGgmlVideoPlannerRequest& request,
	const std::string& modelPath,
	const ofxGgmlInferenceSettings& settings,
	const ofxGgmlInference& inference,
	const ofxGgmlVideoPlanner& planner,
	ofxGgmlClipInference* clip,
	const ofxGgmlValidationLoopConfig& config) {

	ofxGgmlValidationLoop<ofxGgmlVideoPlan,
		ofxGgmlVideoPlanner::SceneCoherenceResult> loop(config);

	// Generator: Create video plan
	loop.setGenerator([&](int attempt) {
		auto planRequest = request;
		// Could adjust parameters based on previous feedback
		auto result = planner.plan(modelPath, planRequest, settings, inference);
		if (!result.success) {
			throw std::runtime_error("Video planning failed: " + result.error);
		}
		return result.plan;
	});

	// Validator: Check scene coherence with CLIP
	loop.setValidator([&](const ofxGgmlVideoPlan& plan) {
		return ofxGgmlVideoPlanner::validateSceneCoherence(plan, clip);
	});

	// Scorer: Use average coherence score
	loop.setScorer([](const ofxGgmlVideoPlan&,
		const ofxGgmlVideoPlanner::SceneCoherenceResult& coherence) {
		return coherence.averageScore;
	});

	return loop.run();
}

} // namespace ofxGgmlValidationLoops
