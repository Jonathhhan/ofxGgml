#include "catch2.hpp"
#include "../src/ofxGgml.h"

TEST_CASE("Media prompt generator prepares a music-to-image prompt", "[media_prompt]") {
	ofxGgmlMediaPromptGenerator generator;
	ofxGgmlMusicToImageRequest request;
	request.musicDescription = "Warm analog synths, slow-building drums, reflective nighttime mood.";
	request.lyrics = "city lights, fading memories, running through the rain";
	request.visualStyle = "cinematic still, soft neon, shallow depth of field";
	request.includeLyrics = true;

	const auto prepared = generator.prepareMusicToImagePrompt(request);
	REQUIRE(prepared.label == "Generate music-inspired visual prompt.");
	REQUIRE(prepared.prompt.find("music descriptions") != std::string::npos);
	REQUIRE(prepared.prompt.find("Warm analog synths") != std::string::npos);
	REQUIRE(prepared.prompt.find("city lights") != std::string::npos);
	REQUIRE(prepared.prompt.find("soft neon") != std::string::npos);
}

TEST_CASE("Media prompt generator sanitizes prompt wrappers", "[media_prompt]") {
	const std::string raw =
		"```text\n"
		"Visual prompt: cinematic rain-soaked boulevard, neon reflections, solitary figure, moody blue palette\n"
		"```\n";

	const std::string sanitized = ofxGgmlMediaPromptGenerator::sanitizeVisualPrompt(raw);
	REQUIRE(sanitized.find("```") == std::string::npos);
	REQUIRE(sanitized.find("Visual prompt:") == std::string::npos);
	REQUIRE(sanitized.find("neon reflections") != std::string::npos);
}

TEST_CASE("Media prompt generator prepares an image-to-music prompt", "[media_prompt]") {
	ofxGgmlMediaPromptGenerator generator;
	ofxGgmlImageToMusicRequest request;
	request.imageDescription = "Rainy neon alley with mirrored puddles and a solitary figure.";
	request.sceneNotes = "slow dolly movement, introspective, late-night city tension";
	request.musicalStyle = "cinematic synthwave score";
	request.instrumentation = "analog synth bass, glassy pads, restrained drums";
	request.targetDurationSeconds = 24;

	const auto prepared = generator.prepareImageToMusicPrompt(request);
	REQUIRE(prepared.label == "Generate image-inspired music prompt.");
	REQUIRE(prepared.prompt.find("visual descriptions") != std::string::npos);
	REQUIRE(prepared.prompt.find("Rainy neon alley") != std::string::npos);
	REQUIRE(prepared.prompt.find("cinematic synthwave score") != std::string::npos);
	REQUIRE(prepared.prompt.find("24 seconds") != std::string::npos);
}
