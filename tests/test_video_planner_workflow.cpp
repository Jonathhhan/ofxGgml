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
