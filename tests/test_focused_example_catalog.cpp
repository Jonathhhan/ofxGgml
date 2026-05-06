#include "catch2.hpp"
#include "../src/support/ofxGgmlFocusedExampleCatalog.h"

TEST_CASE("Focused example catalog exposes roadmap example tracks", "[focused_examples]") {
	const auto catalog = ofxGgmlDefaultFocusedExampleCatalog();
	REQUIRE(catalog.schemaVersion == "ofxGgml.focused_examples.v1");
	REQUIRE(catalog.examples.size() == 5);

	bool hasResearch = false;
	bool hasVideoEssay = false;
	bool hasSpeech = false;
	bool hasCoding = false;
	bool hasVisual = false;

	for (const auto & example : catalog.examples) {
		hasResearch = hasResearch || example.id == "research-citations";
		hasVideoEssay = hasVideoEssay || example.id == "companion-video-essay";
		hasSpeech = hasSpeech || example.id == "speech-subtitles";
		hasCoding = hasCoding || example.id == "coding-assistant";
		hasVisual = hasVisual || example.id == "clip-image-planning";
		REQUIRE_FALSE(example.title.empty());
		REQUIRE_FALSE(example.summary.empty());
		REQUIRE_FALSE(example.workflowArea.empty());
		REQUIRE_FALSE(example.goals.empty());
		REQUIRE_FALSE(example.addonHeaders.empty());
		REQUIRE_FALSE(example.setupNotes.empty());
		REQUIRE_FALSE(example.handoffContracts.empty());
		REQUIRE_FALSE(example.outputArtifacts.empty());
	}

	REQUIRE(hasResearch);
	REQUIRE(hasVideoEssay);
	REQUIRE(hasSpeech);
	REQUIRE(hasCoding);
	REQUIRE(hasVisual);
}

TEST_CASE("Focused example catalog serializes stable JSON keys", "[focused_examples]") {
	ofxGgmlFocusedExampleCatalog catalog = ofxGgmlDefaultFocusedExampleCatalog();
	const auto json = catalog.toJsonString();

	REQUIRE(json.find("\"schema_version\"") != std::string::npos);
	REQUIRE(json.find("ofxGgml.focused_examples.v1") != std::string::npos);
	REQUIRE(json.find("\"examples\"") != std::string::npos);
	REQUIRE(json.find("\"workflow_area\"") != std::string::npos);
	REQUIRE(json.find("\"addon_headers\"") != std::string::npos);
	REQUIRE(json.find("\"companion_boundaries\"") != std::string::npos);
	REQUIRE(json.find("\"setup_notes\"") != std::string::npos);
	REQUIRE(json.find("\"handoff_contracts\"") != std::string::npos);
	REQUIRE(json.find("\"output_artifacts\"") != std::string::npos);
	REQUIRE(json.find("ofxGgmlWorkflowManifest") != std::string::npos);
	REQUIRE(json.find("ofxGgmlCompanionProjectMemory") != std::string::npos);
	REQUIRE(json.find("crawl_to_cite") != std::string::npos);
	REQUIRE(json.find("cite_to_outline") != std::string::npos);
	REQUIRE(json.find("outline_to_script") != std::string::npos);
	REQUIRE(json.find("script_to_subtitles") != std::string::npos);
	REQUIRE(json.find("subtitles_to_video_plan") != std::string::npos);
	REQUIRE(json.find("subtitle timing") != std::string::npos);
	REQUIRE(json.find("research-citations") != std::string::npos);
	REQUIRE(json.find("companion-video-essay") != std::string::npos);
	REQUIRE(json.find("speech-subtitles") != std::string::npos);
	REQUIRE(json.find("coding-assistant") != std::string::npos);
	REQUIRE(json.find("clip-image-planning") != std::string::npos);
}
