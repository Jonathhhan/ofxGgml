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
