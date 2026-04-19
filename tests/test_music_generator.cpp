#include "catch2.hpp"
#include "../src/ofxGgml.h"

TEST_CASE("Music generator prepares a backend-friendly music prompt", "[music_generator]") {
	ofxGgmlMusicGenerator generator;
	ofxGgmlMusicPromptRequest request;
	request.sourceConcept = "moonlit city skyline with slow camera drift and melancholy motion";
	request.style = "cinematic instrumental soundtrack";
	request.instrumentation = "analog synths, soft piano, distant percussion";
	request.targetDurationSeconds = 33;
	request.instrumentalOnly = true;

	const auto prepared = generator.prepareMusicPrompt(request);
	REQUIRE(prepared.label == "Generate music prompt.");
	REQUIRE(prepared.prompt.find("text-to-music") != std::string::npos);
	REQUIRE(prepared.prompt.find("moonlit city skyline") != std::string::npos);
	REQUIRE(prepared.prompt.find("analog synths") != std::string::npos);
	REQUIRE(prepared.prompt.find("33 seconds") != std::string::npos);
}

TEST_CASE("Music generator sanitizes and validates ABC notation", "[music_generator]") {
	const std::string raw =
		"```abc\n"
		"X:1\n"
		"T:Night Theme\n"
		"M:4/4\n"
		"L:1/8\n"
		"Q:1/4=92\n"
		"K:Cm\n"
		"|: C2 G2 A2 G2 | E2 D2 C4 :|\n"
		"```\n";

	const std::string sanitized = ofxGgmlMusicGenerator::sanitizeAbcNotation(raw);
	REQUIRE(sanitized.find("```") == std::string::npos);
	REQUIRE(sanitized.find("T:Night Theme") != std::string::npos);
	REQUIRE(sanitized.find("|: C2") != std::string::npos);

	const auto validation = ofxGgmlMusicGenerator::validateAbcNotation(sanitized);
	REQUIRE(validation.valid);
	REQUIRE(validation.issues.empty());
}
