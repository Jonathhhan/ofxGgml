#include "catch2.hpp"
#include "../src/ofxGgmlWorkflows.h"

TEST_CASE("Workflow manifest serializes shared handoff contract", "[workflow_manifest]") {
	ofxGgmlWorkflowManifest manifest;
	manifest.workflowType = "citation_to_video_plan";
	manifest.runId = "run-001";
	manifest.createdAt = "2026-05-05T16:32:11Z";
	manifest.status = "ready";
	manifest.summary = "Grounded outline ready for downstream planning.";
	manifest.addInput("topic", "text", "Local-first creative AI", "user");
	manifest.inputs.front().metadata["language"] = "en";
	manifest.addIntermediateOutput("outline", "markdown", "outputs/outline.md", "Cited outline");
	manifest.addArtifact("srt", "subtitle", "outputs/narration.srt", "Narration timing");
	manifest.artifacts.front().mimeType = "application/x-subrip";
	manifest.warnings.push_back("One citation has low confidence.");
	manifest.reviewNotes.push_back("Check source freshness before publishing.");
	manifest.handoff.target = "video_planner";
	manifest.handoff.mode = "scene_outline";
	manifest.handoff.contract = "crawl->cite->outline->script->tts->subtitles->video_plan";
	manifest.handoff.metadata["requires_review"] = "true";
	manifest.metadata["model"] = "mock-model.gguf";

	const auto json = manifest.toJsonString();
	REQUIRE(json.find("\"schema_version\"") != std::string::npos);
	REQUIRE(json.find("ofxGgml.workflow_manifest.v1") != std::string::npos);
	REQUIRE(json.find("citation_to_video_plan") != std::string::npos);
	REQUIRE(json.find("\"intermediate_outputs\"") != std::string::npos);
	REQUIRE(json.find("outputs/outline.md") != std::string::npos);
	REQUIRE(json.find("application/x-subrip") != std::string::npos);
	REQUIRE(json.find("\"handoff\"") != std::string::npos);
	REQUIRE(json.find("video_planner") != std::string::npos);
	REQUIRE(json.find("requires_review") != std::string::npos);
}

TEST_CASE("Workflow manifest emits empty arrays and handoff object by default", "[workflow_manifest]") {
	ofxGgmlWorkflowManifest manifest;
	manifest.workflowType = "empty";

	const auto json = manifest.toJsonString();
	REQUIRE(json.find("\"workflow_type\"") != std::string::npos);
	REQUIRE(json.find("empty") != std::string::npos);
	REQUIRE(json.find("\"inputs\":[]") != std::string::npos);
	REQUIRE(json.find("\"artifacts\":[]") != std::string::npos);
	REQUIRE(json.find("\"intermediate_outputs\":[]") != std::string::npos);
	REQUIRE(json.find("\"handoff\":{}") != std::string::npos);
}
