#include "catch2.hpp"
#include "../src/support/ofxGgmlTimelineCopilotPlan.h"

TEST_CASE("Timeline copilot plan exposes roadmap media lanes", "[timeline_copilot]") {
	const auto plan = ofxGgmlDefaultTimelineCopilotPlan();
	REQUIRE(plan.schemaVersion == "ofxGgml.timeline_copilot.v1");
	REQUIRE(plan.manifestHandoff == "ofxGgml.workflow_manifest.v1");
	REQUIRE(plan.memoryLink == "ofxGgml.companion_project_memory.v1");
	REQUIRE(plan.lanes.size() == 5);
	REQUIRE(plan.reviewCheckpoints.size() == 3);
	REQUIRE_FALSE(plan.workspaceRules.empty());

	bool hasVideoEssay = false;
	bool hasMontage = false;
	bool hasMusicVideo = false;
	bool hasSubtitleRevision = false;
	bool hasGenerativeVisuals = false;

	for (const auto & lane : plan.lanes) {
		hasVideoEssay = hasVideoEssay || lane.id == "video_essay";
		hasMontage = hasMontage || lane.id == "montage";
		hasMusicVideo = hasMusicVideo || lane.id == "music_video";
		hasSubtitleRevision = hasSubtitleRevision || lane.id == "subtitle_revision";
		hasGenerativeVisuals = hasGenerativeVisuals || lane.id == "generative_visuals";
		REQUIRE_FALSE(lane.title.empty());
		REQUIRE_FALSE(lane.workflowType.empty());
		REQUIRE_FALSE(lane.assistantRole.empty());
		REQUIRE(lane.companionBoundary == "ofxGgmlCompanionWorkflows.h");
		REQUIRE_FALSE(lane.anchors.empty());
	}

	REQUIRE(hasVideoEssay);
	REQUIRE(hasMontage);
	REQUIRE(hasMusicVideo);
	REQUIRE(hasSubtitleRevision);
	REQUIRE(hasGenerativeVisuals);
}

TEST_CASE("Timeline copilot plan serializes stable JSON keys", "[timeline_copilot]") {
	ofxGgmlTimelineCopilotPlan plan = ofxGgmlDefaultTimelineCopilotPlan();
	plan.metadata["owner"] = "companion-example";
	plan.lanes.front().metadata["scope"] = "planning";
	plan.lanes.front().anchors.front().metadata["source"] = "manifest";
	plan.reviewCheckpoints.front().metadata["requires_human"] = "true";

	const auto json = plan.toJsonString();
	REQUIRE(json.find("\"schema_version\"") != std::string::npos);
	REQUIRE(json.find("ofxGgml.timeline_copilot.v1") != std::string::npos);
	REQUIRE(json.find("\"project_id\"") != std::string::npos);
	REQUIRE(json.find("\"creative_intent\"") != std::string::npos);
	REQUIRE(json.find("\"manifest_handoff\"") != std::string::npos);
	REQUIRE(json.find("\"memory_link\"") != std::string::npos);
	REQUIRE(json.find("\"lanes\"") != std::string::npos);
	REQUIRE(json.find("\"timeline_anchors\"") != std::string::npos);
	REQUIRE(json.find("\"review_checkpoints\"") != std::string::npos);
	REQUIRE(json.find("\"required_approval\"") != std::string::npos);
	REQUIRE(json.find("\"workspace_rules\"") != std::string::npos);
	REQUIRE(json.find("requires_human") != std::string::npos);
}

TEST_CASE("Timeline copilot plan ignores empty convenience entries", "[timeline_copilot]") {
	ofxGgmlTimelineCopilotLane lane;
	lane.addHandoffTarget("");

	ofxGgmlTimelineCopilotPlan plan;
	plan.addWorkspaceRule("");
	plan.addLane(lane);

	const auto json = plan.toJsonString();
	REQUIRE(json.find("\"handoff_targets\":[]") != std::string::npos);
	REQUIRE(json.find("\"workspace_rules\":[]") != std::string::npos);
}
