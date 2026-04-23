#include "catch2.hpp"
#include "../src/inference/ofxGgmlVideoPlanner.h"
#include "../src/inference/ofxGgmlDiffusionInference.h"
#include "../src/inference/ofxGgmlClipInference.h"
#include "../src/inference/ofxGgmlVisionInference.h"
#include "../src/inference/ofxGgmlVideoInference.h"
#include "../src/inference/ofxGgmlInference.h"

TEST_CASE("CLIP scene coherence validation - basic structure", "[feature_synergies][video_planner][clip]") {
	ofxGgmlVideoPlan plan;
	plan.originalPrompt = "A cyberpunk city at night";
	plan.overallScene = "neon-lit streets with rain";

	plan.scenes = {
		{1, "Street scene", "Protagonist walks through wet streets",
			"cyberpunk street with neon lights and rain", "dark alley", "dolly", "fade", 5.0, {}},
		{2, "Rooftop", "Looking over the city",
			"rooftop view of neon cityscape", "high building", "crane", "cut", 4.0, {}}
	};

	SECTION("Validation with null CLIP inference fails gracefully") {
		auto result = ofxGgmlVideoPlanner::validateSceneCoherence(plan, nullptr);
		REQUIRE_FALSE(result.success);
		REQUIRE_FALSE(result.error.empty());
		REQUIRE(result.error.find("CLIP") != std::string::npos);
	}

	SECTION("Empty scene plan validates successfully") {
		ofxGgmlVideoPlan emptyPlan;
		emptyPlan.originalPrompt = "test";
		emptyPlan.overallScene = "test scene";

		// Even with null CLIP, empty plans should succeed with score 1.0
		ofxGgmlClipInference clipInference;
		auto result = ofxGgmlVideoPlanner::validateSceneCoherence(emptyPlan, &clipInference);
		REQUIRE(result.success);
		REQUIRE(result.averageScore == 1.0f);
		REQUIRE(result.sceneScores.empty());
		REQUIRE(result.warnings.empty());
	}

	SECTION("SceneCoherenceResult structure") {
		ofxGgmlVideoPlanner::SceneCoherenceResult result;
		REQUIRE_FALSE(result.success);
		REQUIRE(result.averageScore == 0.0f);
		REQUIRE(result.sceneScores.empty());
		REQUIRE(result.warnings.empty());
		REQUIRE(result.error.empty());
	}
}

TEST_CASE("CLIP scene coherence validation - mock backend", "[feature_synergies][video_planner][clip]") {
	ofxGgmlVideoPlan plan;
	plan.originalPrompt = "A cyberpunk city at night";
	plan.overallScene = "neon-lit streets with rain";

	plan.scenes = {
		{1, "Street scene", "Protagonist walks through wet streets",
			"cyberpunk street with neon lights and rain", "dark alley", "dolly", "fade", 5.0, {}},
		{2, "Beach scene", "Sunny beach day",
			"tropical beach with palm trees", "sandy beach", "handheld", "dissolve", 4.0, {}}
	};

	SECTION("Mock CLIP backend for coherence testing") {
		ofxGgmlClipInference clipInference;
		auto backend = std::dynamic_pointer_cast<ofxGgmlClipBridgeBackend>(
			ofxGgmlClipInference::createClipBridgeBackend());

		// Mock CLIP embeddings
		backend->setEmbedFunction([](const ofxGgmlClipEmbeddingRequest & req) {
			ofxGgmlClipEmbeddingResult result;
			result.success = true;
			result.text = req.text;

			// Create mock embeddings based on keywords
			std::string text = req.text;
			std::vector<float> embedding(512, 0.0f);

			// High values for "cyberpunk" related terms
			if (text.find("cyberpunk") != std::string::npos ||
				text.find("neon") != std::string::npos ||
				text.find("rain") != std::string::npos) {
				embedding[0] = 1.0f;
				embedding[1] = 0.8f;
			}
			// Different values for beach terms
			else if (text.find("beach") != std::string::npos ||
					 text.find("tropical") != std::string::npos) {
				embedding[0] = -0.5f;
				embedding[1] = 0.3f;
			}

			result.embedding = embedding;
			return result;
		});

		clipInference.setBackend(backend);

		auto result = ofxGgmlVideoPlanner::validateSceneCoherence(plan, &clipInference);
		REQUIRE(result.success);
		REQUIRE(result.sceneScores.size() == 2);

		// First scene should have high coherence (similar to overall)
		REQUIRE(result.sceneScores[0] > 0.5f);

		// Second scene (beach) should have low coherence with cyberpunk
		// and generate a warning
		REQUIRE_FALSE(result.warnings.empty());
	}
}

TEST_CASE("Vision-based diffusion validation - basic structure", "[feature_synergies][diffusion][vision]") {
	ofxGgmlImageGenerationResult genResult;
	genResult.success = true;
	genResult.images = {
		{"output1.png", 512, 512, 12345, 0, 0, false, 0.0f, "", ""},
		{"output2.png", 512, 512, 67890, 1, 0, false, 0.0f, "", ""}
	};

	SECTION("Validation with null vision inference fails gracefully") {
		ofxGgmlVisionModelProfile profile;
		auto result = ofxGgmlDiffusionInference::validateWithVision(
			genResult, "test prompt", nullptr, profile, nullptr);

		REQUIRE_FALSE(result.success);
		REQUIRE_FALSE(result.error.empty());
		REQUIRE(result.error.find("Vision") != std::string::npos);
	}

	SECTION("Validation with empty generation result") {
		ofxGgmlImageGenerationResult emptyResult;
		emptyResult.success = false;

		ofxGgmlVisionInference visionInference;
		ofxGgmlVisionModelProfile profile;

		auto result = ofxGgmlDiffusionInference::validateWithVision(
			emptyResult, "test prompt", &visionInference, profile, nullptr);

		REQUIRE_FALSE(result.success);
		REQUIRE_FALSE(result.error.empty());
		REQUIRE(result.error.find("images") != std::string::npos);
	}

	SECTION("ImageValidationResult structure") {
		ofxGgmlDiffusionInference::ImageValidationResult result;
		REQUIRE_FALSE(result.success);
		REQUIRE(result.averageScore == 0.0f);
		REQUIRE(result.imageScores.empty());
		REQUIRE(result.descriptions.empty());
		REQUIRE(result.error.empty());
	}
}

TEST_CASE("Video analysis hints extraction", "[feature_synergies][video_planner][video_analysis]") {
	SECTION("Extract hints from action analysis") {
		ofxGgmlVideoStructuredAnalysis analysis;
		analysis.analysisType = "Action";
		analysis.primaryLabel = "Running";
		analysis.confidence = 0.85f;
		analysis.secondaryLabels = {"jumping", "dodging"};
		analysis.timeline = {"0-5s: running", "5-10s: jumping"};

		auto hints = ofxGgmlVideoPlanner::extractHintsFromVideoAnalysis(analysis);

		REQUIRE(hints.primaryEmotion == "Running");
		REQUIRE(hints.emotionConfidence == 0.85f);
		REQUIRE(hints.actionLabels.size() == 2);
		REQUIRE(hints.actionLabels[0] == "jumping");
		REQUIRE(hints.suggestedPacing == "dynamic and fast-paced");
		REQUIRE(hints.timeline.size() == 2);
	}

	SECTION("Extract hints from emotion analysis - high arousal") {
		ofxGgmlVideoStructuredAnalysis analysis;
		analysis.analysisType = "Emotion";
		analysis.primaryLabel = "Excitement";
		analysis.confidence = 0.92f;
		analysis.valence = "positive";
		analysis.arousal = "high";
		analysis.secondaryLabels = {"joy", "anticipation"};

		auto hints = ofxGgmlVideoPlanner::extractHintsFromVideoAnalysis(analysis);

		REQUIRE(hints.primaryEmotion == "Excitement");
		REQUIRE(hints.emotionConfidence == 0.92f);
		REQUIRE(hints.suggestedPacing == "energetic");
		REQUIRE(hints.suggestedTone == "intense");
	}

	SECTION("Extract hints from emotion analysis - low arousal") {
		ofxGgmlVideoStructuredAnalysis analysis;
		analysis.analysisType = "Emotion";
		analysis.primaryLabel = "Sadness";
		analysis.confidence = 0.78f;
		analysis.valence = "negative";
		analysis.arousal = "low";

		auto hints = ofxGgmlVideoPlanner::extractHintsFromVideoAnalysis(analysis);

		REQUIRE(hints.primaryEmotion == "Sadness");
		REQUIRE(hints.suggestedPacing == "slow and contemplative");
		REQUIRE(hints.suggestedTone == "reflective");
	}

	SECTION("VideoAnalysisHints structure") {
		ofxGgmlVideoPlanner::VideoAnalysisHints hints;
		REQUIRE(hints.primaryEmotion.empty());
		REQUIRE(hints.emotionConfidence == 0.0f);
		REQUIRE(hints.actionLabels.empty());
		REQUIRE(hints.suggestedPacing.empty());
		REQUIRE(hints.suggestedTone.empty());
		REQUIRE(hints.timeline.empty());
	}
}

TEST_CASE("Enrich planning prompt with video analysis hints", "[feature_synergies][video_planner]") {
	SECTION("Enrich with emotion hints") {
		ofxGgmlVideoPlanner::VideoAnalysisHints hints;
		hints.primaryEmotion = "Excitement";
		hints.emotionConfidence = 0.85f;
		hints.actionLabels = {"dancing", "celebrating"};
		hints.suggestedPacing = "energetic";
		hints.suggestedTone = "joyful";
		hints.timeline = {"0-3s: buildup", "3-6s: peak celebration"};

		std::string basePrompt = "Create a party scene video plan.";
		std::string enriched = ofxGgmlVideoPlanner::enrichPlanningPromptWithHints(
			basePrompt, hints);

		REQUIRE(enriched.find("Create a party scene video plan.") != std::string::npos);
		REQUIRE(enriched.find("Excitement") != std::string::npos);
		REQUIRE(enriched.find("0.85") != std::string::npos);
		REQUIRE(enriched.find("dancing") != std::string::npos);
		REQUIRE(enriched.find("celebrating") != std::string::npos);
		REQUIRE(enriched.find("energetic") != std::string::npos);
		REQUIRE(enriched.find("joyful") != std::string::npos);
		REQUIRE(enriched.find("buildup") != std::string::npos);
	}

	SECTION("Enrich with minimal hints") {
		ofxGgmlVideoPlanner::VideoAnalysisHints hints;
		hints.primaryEmotion = "Calm";

		std::string basePrompt = "Plan a meditation video.";
		std::string enriched = ofxGgmlVideoPlanner::enrichPlanningPromptWithHints(
			basePrompt, hints);

		REQUIRE(enriched.find("Plan a meditation video.") != std::string::npos);
		REQUIRE(enriched.find("Calm") != std::string::npos);
	}

	SECTION("Enrich with empty hints returns original prompt") {
		ofxGgmlVideoPlanner::VideoAnalysisHints hints;

		std::string basePrompt = "Standard video plan.";
		std::string enriched = ofxGgmlVideoPlanner::enrichPlanningPromptWithHints(
			basePrompt, hints);

		// Should just return the base prompt when hints are empty
		REQUIRE(enriched == basePrompt);
	}
}

TEST_CASE("Feature synergies integration scenarios", "[feature_synergies][integration]") {
	SECTION("Video analysis to planning workflow") {
		// 1. Simulate video analysis
		ofxGgmlVideoStructuredAnalysis analysis;
		analysis.analysisType = "Action";
		analysis.primaryLabel = "Chase sequence";
		analysis.confidence = 0.88f;
		analysis.secondaryLabels = {"running", "jumping", "combat"};
		analysis.timeline = {"0-5s: pursuit begins", "5-10s: parkour sequence"};

		// 2. Extract hints
		auto hints = ofxGgmlVideoPlanner::extractHintsFromVideoAnalysis(analysis);
		REQUIRE(hints.suggestedPacing == "dynamic and fast-paced");

		// 3. Enrich planning request
		ofxGgmlVideoPlannerRequest request;
		request.prompt = "Action chase sequence";
		request.durationSeconds = 10.0;

		std::string basePrompt = ofxGgmlVideoPlanner::buildPlanningPrompt(request);
		std::string enrichedPrompt = ofxGgmlVideoPlanner::enrichPlanningPromptWithHints(
			basePrompt, hints);

		REQUIRE(enrichedPrompt.length() > basePrompt.length());
		REQUIRE(enrichedPrompt.find("dynamic and fast-paced") != std::string::npos);
	}
}
