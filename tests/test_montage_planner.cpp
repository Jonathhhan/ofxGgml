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

TEST_CASE("EDL export supports drop-frame timecode and audio tracks", "[montage][edl]") {
	ofxGgmlMontagePlannerRequest request;
	request.goal = "train action";
	request.segments = makeSegments();
	request.maxClips = 2;
	request.minScore = 0.05;
	request.sourceFilePath = "/path/to/source/video.mp4";
	request.dropFrameTimecode = true;

	const auto result = ofxGgmlMontagePlanner::plan(request);
	REQUIRE(result.success);
	REQUIRE(result.plan.clips.size() == 2);

	SECTION("EDL with drop-frame timecode uses semicolon separator") {
		const std::string edl = ofxGgmlMontagePlanner::buildEdl(result.plan, "TRAIN_MONTAGE", 30, true);
		REQUIRE_FALSE(edl.empty());
		REQUIRE(edl.find("FCM: DROP FRAME") != std::string::npos);
		REQUIRE(edl.find(";") != std::string::npos);
	}

	SECTION("EDL without drop-frame uses colon separator") {
		const std::string edl = ofxGgmlMontagePlanner::buildEdl(result.plan, "TRAIN_MONTAGE", 30, false);
		REQUIRE_FALSE(edl.empty());
		REQUIRE(edl.find("FCM: NON-DROP FRAME") != std::string::npos);
	}

	SECTION("EDL includes source file path references") {
		const std::string edl = ofxGgmlMontagePlanner::buildEdl(result.plan, "TRAIN_MONTAGE", 25, false);
		REQUIRE(edl.find("SOURCE FILE: /path/to/source/video.mp4") != std::string::npos);
	}

	SECTION("EDL with audio tracks includes both video and audio entries") {
		const std::string edl = ofxGgmlMontagePlanner::buildEdlWithAudio(result.plan, "TRAIN_MONTAGE", 25, false);
		REQUIRE_FALSE(edl.empty());
		REQUIRE(edl.find("V     C") != std::string::npos);
		REQUIRE(edl.find("A     C") != std::string::npos);
	}
}

TEST_CASE("EDL export handles transition metadata", "[montage][edl]") {
	ofxGgmlMontagePlannerRequest request;
	request.goal = "bright moments";
	request.segments = makeSegments();
	request.maxClips = 2;
	request.minScore = 0.05;

	auto result = ofxGgmlMontagePlanner::plan(request);
	REQUIRE(result.success);

	// Manually add transition duration to first clip
	result.plan.clips[0].transitionDurationFrames = 12;

	SECTION("EDL includes transition duration when specified") {
		const std::string edl = ofxGgmlMontagePlanner::buildEdl(result.plan, "TEST", 25, false);
		REQUIRE(edl.find("TRANSITION DURATION: 12 frames") != std::string::npos);
	}
}
