#include "catch2.hpp"
#include "../src/inference/ofxGgmlMontagePlanner.h"

namespace {

std::vector<ofxGgmlMontageSegment> makeSegments() {
	return {
		{"cue_1", "AX", 1.0, 2.0, "train arrives with bright lights", {"arrives", "bright", "lights", "train"}},
		{"cue_2", "AX", 2.2, 3.0, "train doors open and people rush", {"doors", "open", "people", "rush", "train"}},
		{"cue_3", "AX", 8.0, 9.0, "quiet reaction shot with bright eyes", {"bright", "eyes", "quiet", "reaction"}},
		{"cue_4", "AX", 14.0, 15.0, "final train departure at night", {"departure", "night", "train"}}
	};
}

} // namespace

TEST_CASE("Montage planner enforces spacing and applies handles", "[montage]") {
	ofxGgmlMontagePlannerRequest request;
	request.goal = "bright train reaction";
	request.segments = makeSegments();
	request.maxClips = 3;
	request.minScore = 0.05;
	request.minSpacingSeconds = 1.5;
	request.preRollSeconds = 0.5;
	request.postRollSeconds = 0.75;
	request.targetDurationSeconds = 6.5;
	request.preserveChronology = true;

	const auto result = ofxGgmlMontagePlanner::plan(request);
	REQUIRE(result.success);
	REQUIRE(result.plan.clips.size() == 3);

	SECTION("Nearby subtitle hits are de-clustered") {
		REQUIRE(result.plan.clips[0].sourceId == "cue_1");
		REQUIRE(result.plan.clips[1].sourceId == "cue_3");
		REQUIRE(result.plan.clips[2].sourceId == "cue_4");
	}

	SECTION("Visual handles extend clip bounds") {
		REQUIRE(result.plan.clips[0].startSeconds == Approx(0.5));
		REQUIRE(result.plan.clips[0].endSeconds == Approx(2.75));
		REQUIRE(result.plan.clips[1].startSeconds == Approx(7.5));
		REQUIRE(result.plan.clips[1].endSeconds == Approx(9.75));
	}

	SECTION("Estimated duration reflects handled clips") {
		REQUIRE(ofxGgmlMontagePlanner::computePlanDurationSeconds(result.plan) == Approx(6.75));
	}

	SECTION("Planner assigns themes and transition guidance") {
		REQUIRE(result.plan.clips[0].themeBucket == "bright");
		REQUIRE_FALSE(result.plan.clips[0].transitionSuggestion.empty());
		REQUIRE_FALSE(result.plan.clips[1].transitionSuggestion.empty());
	}
}
