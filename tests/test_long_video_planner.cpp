#include "catch2.hpp"
#include "../src/ofxGgml.h"

TEST_CASE("Long video planner validates requests", "[long_video]") {
	ofxGgmlLongVideoPlanRequest request;
	request.conceptText.clear();
	request.chunkCount = 0;
	request.targetDurationSeconds = 0.0;

	const auto validation = ofxGgmlLongVideoPlanner::validateRequest(request);
	REQUIRE_FALSE(validation.ok);
	REQUIRE(validation.errors.size() >= 3);
}

TEST_CASE("Long video planner builds heuristic chunks and continuity bible", "[long_video]") {
	ofxGgmlLongVideoPlanRequest request;
	request.conceptText = "A lone rider crossing a futuristic desert highway at dusk";
	request.style = "anamorphic, cinematic, wind-swept, neon horizon";
	request.targetDurationSeconds = 72.0;
	request.chunkCount = 4;
	request.structureHint = "music-driven rise with a strong payoff section";
	request.pacingProfile = "aggressive escalation with shorter setup and denser climax beats";
	request.width = 640;
	request.height = 384;
	request.fps = 12;
	request.framesPerChunk = 49;
	request.seed = 100;

	const auto result = ofxGgmlLongVideoPlanner().run(request);
	REQUIRE(result.success);
	REQUIRE(result.chunks.size() == 4);
	REQUIRE(result.chunks.front().title == "Intro");
	REQUIRE(result.chunks.front().seed == 100);
	REQUIRE(result.chunks[1].seed == 101);
	REQUIRE_FALSE(result.chunks.front().usePreviousLastFrame);
	REQUIRE(result.chunks[1].usePreviousLastFrame);
	REQUIRE(result.chunks.front().startSeconds == Approx(0.0));
	REQUIRE(result.chunks.front().endSeconds > result.chunks.front().startSeconds);
	REQUIRE(result.chunks.back().endSeconds == Approx(72.0).margin(0.01));
	REQUIRE(result.chunks[1].startSeconds >= result.chunks.front().endSeconds);
	REQUIRE(result.chunks[2].progressionWeight >= result.chunks.front().progressionWeight);
	REQUIRE_FALSE(result.chunks[1].transitionHint.empty());
	REQUIRE(result.continuityBible.find("futuristic desert highway") != std::string::npos);
	REQUIRE(result.continuityBible.find("Structure: music-driven rise with a strong payoff section") != std::string::npos);
	REQUIRE(result.continuityBible.find("Pacing: aggressive escalation with shorter setup and denser climax beats") != std::string::npos);
	REQUIRE(result.chunks[2].prompt.find("Style: anamorphic") != std::string::npos);
}

TEST_CASE("Long video planner builds manifest json", "[long_video]") {
	ofxGgmlLongVideoPlanRequest request;
	request.conceptText = "A botanist documenting a bioluminescent forest at night";
	request.chunkCount = 3;
	request.targetDurationSeconds = 45.0;
	request.structureHint = "loopable ambient progression with a seamless ending";
	request.pacingProfile = "gentle build with longer observation beats before the payoff";
	request.favorLoopableEnding = true;

	const auto result = ofxGgmlLongVideoPlanner().run(request);
	REQUIRE(result.success);
	REQUIRE(result.manifestJson.find("\"project_type\"") != std::string::npos);
	REQUIRE(result.manifestJson.find("long_video_plan") != std::string::npos);
	REQUIRE(result.manifestJson.find("\"chunk_count\"") != std::string::npos);
	REQUIRE(result.manifestJson.find("\"use_previous_last_frame\"") != std::string::npos);
	REQUIRE(result.manifestJson.find("\"structure_hint\"") != std::string::npos);
	REQUIRE(result.manifestJson.find("\"pacing_profile\"") != std::string::npos);
	REQUIRE(result.manifestJson.find("\"transition_hint\"") != std::string::npos);
	REQUIRE(result.manifestJson.find("\"start_seconds\"") != std::string::npos);
	REQUIRE(result.manifestJson.find("\"progression_weight\"") != std::string::npos);
	REQUIRE(result.manifestJson.find("true") != std::string::npos);
	REQUIRE(result.manifestJson.find("bioluminescent forest") != std::string::npos);
}

TEST_CASE("Easy API exposes long video planner", "[easy_api][long_video]") {
	ofxGgmlEasy easy;
	ofxGgmlEasyTextConfig textConfig;
	textConfig.modelPath = "mock-model.gguf";
	textConfig.completionExecutable = "llama-cli";
	easy.configureText(textConfig);

	ofxGgmlLongVideoPlanRequest request;
	request.conceptText = "A drifting orbital station rotating above a storm planet";
	request.chunkCount = 2;
	request.targetDurationSeconds = 24.0;

	const auto result = easy.planLongVideo(request);
	REQUIRE(result.success);
	REQUIRE(result.chunks.size() == 2);
	REQUIRE(result.manifestJson.find("storm planet") != std::string::npos);
}
