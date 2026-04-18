#include "catch2.hpp"
#include "../src/ofxGgml.h"

TEST_CASE("MilkDrop generator prepares a constrained preset prompt", "[milkdrop]") {
	ofxGgmlMilkDropGenerator generator;
	ofxGgmlMilkDropRequest request;
	request.prompt = "Create a neon geometric tunnel with bass-reactive zoom pulses.";
	request.category = "Geometric";
	request.randomness = 0.65f;

	const auto prepared = generator.preparePrompt(request);
	REQUIRE(prepared.label == "Generate MilkDrop preset.");
	REQUIRE(prepared.prompt.find("[preset00]") != std::string::npos);
	REQUIRE(prepared.prompt.find("Return only preset text") != std::string::npos);
	REQUIRE(prepared.prompt.find("Geometric") != std::string::npos);
}

TEST_CASE("MilkDrop generator sanitizes fenced preset output", "[milkdrop]") {
	const std::string raw =
		"```milkdrop\n"
		"Here is your preset.\n"
		"[preset00]\n"
		"fRating=3.0\n"
		"zoom=1.02\n"
		"```\n";

	const std::string sanitized = ofxGgmlMilkDropGenerator::sanitizePresetText(raw);
	REQUIRE(sanitized.find("[preset00]") == 0);
	REQUIRE(sanitized.find("```") == std::string::npos);
	REQUIRE(sanitized.find("zoom=1.02") != std::string::npos);
}
