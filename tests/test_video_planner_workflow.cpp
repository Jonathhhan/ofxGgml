#include "catch2.hpp"
#include "../src/inference/ofxGgmlVideoPlanner.h"

namespace {

ofxGgmlVideoEditPlan makeSampleEditPlan() {
	ofxGgmlVideoEditPlan plan;
	plan.originalGoal = "Turn the clip into a faster, more cinematic teaser.";
	plan.overallDirection = "Build a punchy teaser with one strong hook and a clean payoff.";
	plan.pacingStrategy = "Start with the hook, then tighten the reaction shots.";
	plan.assetSuggestions = {
		"Generate a dramatic title card.",
		"Prepare a clean b-roll insert for the payoff."
	};
	plan.globalNotes = {
		"Keep the emotional beat readable.",
		"Avoid over-cutting the final reaction."
	};
	plan.clips = {
		{1, 2.0, 4.5, "Hook moment", "Main reveal", "Trim aggressively and hold the reaction", "hard cut", "Tonight"},
		{2, 8.0, 10.0, "Payoff", "Closing reaction", "Let the moment breathe", "dip to black", ""}
	};
	plan.actions = {
		{1, "b-roll insert", 4.5, 6.0, "Add a short atmospheric cutaway", "Give the teaser room to reset", "neon city b-roll"},
		{2, "title card", 10.0, 11.5, "Add a bold closing title", "Land the teaser cleanly", ""}
	};
	return plan;
}

} // namespace

TEST_CASE("Video planner builds actionable edit workflow", "[video_planner][workflow]") {
	const ofxGgmlVideoEditPlan plan = makeSampleEditPlan();
	ofxGgmlVideoEditWorkflowContext context;
	context.hasSourceVideo = true;
	context.hasMontageTimedPreview = true;
	context.hasSubtitlePreview = true;

	const auto workflow = ofxGgmlVideoPlanner::buildEditWorkflow(plan, context);

	REQUIRE_FALSE(workflow.headline.empty());
	REQUIRE_FALSE(workflow.nextAction.empty());
	REQUIRE_FALSE(workflow.previewHint.empty());
	REQUIRE(workflow.steps.size() >= 5);
	REQUIRE(workflow.steps.front().handoffMode == "Vision");
	REQUIRE(workflow.steps[1].handoffMode == "Write");
	REQUIRE(workflow.steps[2].handoffMode == "Write");
	REQUIRE(workflow.steps[3].handoffMode == "Diffusion");
	REQUIRE(workflow.steps[4].handoffMode == "Write");
	REQUIRE(workflow.checklist.size() >= 4);
}

TEST_CASE("Video planner workflow summary reflects step routing", "[video_planner][workflow]") {
	const ofxGgmlVideoEditPlan plan = makeSampleEditPlan();
	const auto workflow = ofxGgmlVideoPlanner::buildEditWorkflow(plan);
	const std::string summary = ofxGgmlVideoPlanner::summarizeEditWorkflow(workflow);

	REQUIRE(summary.find("Next action:") != std::string::npos);
	REQUIRE(summary.find("handoff: Write") != std::string::npos);
	REQUIRE(summary.find("handoff: Diffusion") != std::string::npos);
}

TEST_CASE("Video planner supports music-video section-aware plans", "[video_planner][music_video]") {
	ofxGgmlVideoPlannerRequest request;
	request.prompt = "Stylized night-drive music video with a strong chorus payoff.";
	request.durationSeconds = 24.0;
	request.beatCount = 8;
	request.sceneCount = 4;
	request.multiScene = true;
	request.musicVideoMode = true;
	request.sectionCount = 5;
	request.sectionStructureHint = "intro -> verse -> chorus -> bridge -> outro";
	request.cutIntensity = 0.85f;

	const std::string planningPrompt = ofxGgmlVideoPlanner::buildPlanningPrompt(request);
	REQUIRE(planningPrompt.find("Requested section count: 5") != std::string::npos);
	REQUIRE(planningPrompt.find("Music-video cut intensity: aggressive") != std::string::npos);
	REQUIRE(planningPrompt.find("\"sections\"") != std::string::npos);

#ifdef OFXGGML_HEADLESS_STUBS
	SUCCEED("JSON parsing is unavailable in headless stubs.");
#else
	const std::string jsonText = R"json(
{
  "originalPrompt": "night-drive music video",
  "style": "neon cinematic",
  "overallScene": "city streets at night",
  "overallCamera": "gliding car-mounted camera",
  "continuityNotes": "keep the lead performer visually consistent",
  "negativePrompt": "muddy lighting",
  "constraints": ["night exterior"],
  "subjects": [],
  "entities": [],
  "beats": [
    {
      "startSeconds": 0.0,
      "endSeconds": 6.0,
      "summary": "intro build",
      "camera": "slow push",
      "scene": "streetlights",
      "motion": "steady cruise",
      "visualGoal": "set mood",
      "subjects": ["lead"]
    }
  ],
  "sections": [
    {
      "index": 1,
      "label": "Verse 1",
      "role": "setup",
      "startSeconds": 0.0,
      "endSeconds": 8.0,
      "energy": "measured",
      "cutDensity": "restrained",
      "visualFocus": "character and city texture"
    },
    {
      "index": 2,
      "label": "Chorus",
      "role": "payoff",
      "startSeconds": 8.0,
      "endSeconds": 16.0,
      "energy": "high",
      "cutDensity": "fast",
      "visualFocus": "performance and lights"
    }
  ],
  "scenes": [
    {
      "index": 1,
      "title": "City ride",
      "summary": "driver glides through the city",
      "eventPrompt": "stylized night drive performance",
      "background": "wet neon streets",
      "cameraMovement": "smooth tracking",
      "transition": "light streak whip",
      "durationSeconds": 12.0,
      "entityIds": []
    }
  ]
}
)json";

	const auto parsed = ofxGgmlVideoPlanner::parsePlanJson(jsonText);
	REQUIRE(parsed.isOk());
	REQUIRE(parsed.value().sections.size() == 2);
	REQUIRE(parsed.value().sections[1].label == "Chorus");

	const std::string generationPrompt =
		ofxGgmlVideoPlanner::buildGenerationPrompt(parsed.value());
	REQUIRE(generationPrompt.find("Music-video sections:") != std::string::npos);
	REQUIRE(generationPrompt.find("cut density: fast") != std::string::npos);

	const std::string summary = ofxGgmlVideoPlanner::summarizePlan(parsed.value());
	REQUIRE(summary.find("Sections:") != std::string::npos);
	REQUIRE(summary.find("Chorus") != std::string::npos);
#endif
}
