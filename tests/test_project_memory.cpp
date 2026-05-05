#include "catch2.hpp"
#include "../src/support/ofxGgmlProjectMemory.h"

TEST_CASE("Project memory basic lifecycle", "[project_memory]") {
	ofxGgmlProjectMemory mem;

	SECTION("Default state") {
		REQUIRE(mem.isEnabled());
		REQUIRE(mem.empty());
		REQUIRE(mem.getMaxChars() == 16000);
	}

	SECTION("Enable/disable") {
		mem.setEnabled(false);
		REQUIRE_FALSE(mem.isEnabled());
		mem.setEnabled(true);
		REQUIRE(mem.isEnabled());
	}

	SECTION("Clear") {
		REQUIRE(mem.addInteraction("req", "res"));
		REQUIRE_FALSE(mem.empty());
		mem.clear();
		REQUIRE(mem.empty());
	}
}

TEST_CASE("Project memory validation and clamping", "[project_memory]") {
	ofxGgmlProjectMemory mem;

	SECTION("Reject empty interactions") {
		REQUIRE_FALSE(mem.addInteraction("", "res"));
		REQUIRE_FALSE(mem.addInteraction("req", ""));
		REQUIRE(mem.empty());
	}

	SECTION("Clamp total memory") {
		mem.setMaxChars(120);
		REQUIRE(mem.addInteraction("request one", "response one with enough text to fill memory"));
		REQUIRE(mem.addInteraction("request two", "response two with enough text to trigger clamp"));
		REQUIRE(mem.getMemoryText().size() <= mem.getMaxChars());
	}

	SECTION("Clamp preserves newest complete entries when possible") {
		mem.setMaxChars(180);
		REQUIRE(mem.addInteraction("old request", "old response with enough text to be discarded first"));
		REQUIRE(mem.addInteraction("middle request", "middle response should remain only if there is room"));
		REQUIRE(mem.addInteraction("newest request", "newest response should be preferred"));
		const std::string & text = mem.getMemoryText();
		REQUIRE(text.size() <= mem.getMaxChars());
		REQUIRE(text.find("newest request") != std::string::npos);
	}

	SECTION("Clamp falls back to tail slice for one oversized entry") {
		mem.setMaxChars(64);
		mem.setMemoryText(std::string(256, 'x'));
		REQUIRE(mem.getMemoryText().size() == 64);
	}

	SECTION("Repeated clamps preserve newest complete entries") {
		mem.setMaxChars(220);
		for (int i = 0; i < 12; ++i) {
			REQUIRE(mem.addInteraction(
				"request-" + std::to_string(i),
				"response body for request " + std::to_string(i) + " with enough text to force rolling retention"));
			REQUIRE(mem.getMemoryText().size() <= mem.getMaxChars());
		}
		const std::string & text = mem.getMemoryText();
		REQUIRE(text.find("request-11") != std::string::npos);
		REQUIRE(text.find("response body for request 11") != std::string::npos);
		REQUIRE(text.find("request-0") == std::string::npos);
		REQUIRE(text.rfind("Request:\n", 0) == 0);
	}

	SECTION("Repeated interaction insertion stays bounded") {
		mem.setMaxChars(256);
		for (int i = 0; i < 64; ++i) {
			REQUIRE(mem.addInteraction(
				"bounded request " + std::to_string(i),
				"bounded response " + std::to_string(i) + " with repeated content that should not grow without limit"));
		}
		REQUIRE(mem.getMemoryText().size() <= mem.getMaxChars());
		REQUIRE(mem.getMemoryText().find("bounded request 63") != std::string::npos);
	}
}

TEST_CASE("Project memory prompt context behavior", "[project_memory]") {
	ofxGgmlProjectMemory mem;
	REQUIRE(mem.addInteraction("build parser", "implemented parser and tests"));

	SECTION("Context emitted when enabled") {
		std::string ctx = mem.buildPromptContext("Memory:");
		REQUIRE_FALSE(ctx.empty());
		REQUIRE(ctx.find("Memory:") != std::string::npos);
		REQUIRE(ctx.find("Request:") != std::string::npos);
		REQUIRE(ctx.find("Response:") != std::string::npos);
	}

	SECTION("Context suppressed when disabled") {
		mem.setEnabled(false);
		REQUIRE(mem.buildPromptContext().empty());
	}
}
