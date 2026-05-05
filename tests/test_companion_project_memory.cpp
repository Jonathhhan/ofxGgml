#include "catch2.hpp"
#include "../src/ofxGgmlWorkflows.h"

TEST_CASE("Companion project memory serializes creative continuity state", "[companion_project_memory]") {
	ofxGgmlCompanionProjectMemory memory;
	memory.projectId = "project-001";
	memory.title = "Bioluminescent forest short";
	memory.updatedAt = "2026-05-05T16:47:25Z";
	memory.addCreativeIntent("Keep the film meditative and ecological.");
	memory.addAcceptedPrompt("Macro shot of glowing spores drifting through mist.");
	memory.addReference("ref-forest", "image", "Night forest reference", "file://refs/forest.png");
	memory.acceptedReferences.front().note = "Accepted palette reference.";
	memory.acceptedReferences.front().confidence = 0.92f;
	memory.acceptedReferences.front().metadata["source"] = "artist-curated";
	memory.addStyleNote("Use cool cyan highlights against deep green shadows.");
	memory.addContinuityRule("The same botanist appears in every generated scene.");
	memory.addToolSetting("video_planner", "shot_density", "low", "Preserve slow pacing.");
	memory.preferredToolSettings.front().metadata["scope"] = "scene-planning";
	memory.addReviewNote("Confirm reference licenses before publishing.");
	memory.metadata["owner"] = "companion-example";

	const auto json = memory.toJsonString();
	REQUIRE(json.find("\"schema_version\"") != std::string::npos);
	REQUIRE(json.find("ofxGgml.companion_project_memory.v1") != std::string::npos);
	REQUIRE(json.find("Bioluminescent forest short") != std::string::npos);
	REQUIRE(json.find("\"creative_intent\"") != std::string::npos);
	REQUIRE(json.find("glowing spores") != std::string::npos);
	REQUIRE(json.find("\"accepted_references\"") != std::string::npos);
	REQUIRE(json.find("artist-curated") != std::string::npos);
	REQUIRE(json.find("\"style_notes\"") != std::string::npos);
	REQUIRE(json.find("\"continuity_rules\"") != std::string::npos);
	REQUIRE(json.find("\"preferred_tool_settings\"") != std::string::npos);
	REQUIRE(json.find("shot_density") != std::string::npos);
}

TEST_CASE("Companion project memory ignores empty convenience entries", "[companion_project_memory]") {
	ofxGgmlCompanionProjectMemory memory;
	memory.addCreativeIntent("");
	memory.addAcceptedPrompt("");
	memory.addStyleNote("");
	memory.addContinuityRule("");
	memory.addReviewNote("");

	const auto json = memory.toJsonString();
	REQUIRE(json.find("\"creative_intent\":[]") != std::string::npos);
	REQUIRE(json.find("\"accepted_prompts\":[]") != std::string::npos);
	REQUIRE(json.find("\"style_notes\":[]") != std::string::npos);
	REQUIRE(json.find("\"continuity_rules\":[]") != std::string::npos);
	REQUIRE(json.find("\"review_notes\":[]") != std::string::npos);
}
