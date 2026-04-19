#include "catch2.hpp"
#include "../src/ofxGgml.h"

TEST_CASE("Video essay workflow parses markdown sections", "[video_essay]") {
	const std::string script =
		"## Hook\n"
		"This is the opening beat with a claim [Source 1].\n\n"
		"## Context\n"
		"More narration here [Source 2] with a second sentence.\n";

	const auto sections =
		ofxGgmlVideoEssayWorkflow::parseSectionsFromScript(script, 60.0);

	REQUIRE(sections.size() == 2);
	REQUIRE(sections[0].title == "Hook");
	REQUIRE(sections[0].narrationText.find("[Source 1]") != std::string::npos);
	REQUIRE(sections[0].sourceIndices.size() == 1);
	REQUIRE(sections[0].sourceIndices[0] == 1);
	REQUIRE(sections[1].title == "Context");
	REQUIRE(sections[1].estimatedDurationSeconds > 1.0);
}

TEST_CASE("Video essay workflow builds sequential voice cues and SRT", "[video_essay]") {
	std::vector<ofxGgmlVideoEssaySection> sections;
	sections.push_back({
		0,
		"Intro",
		"Short summary",
		"First sentence. Second sentence adds context [Source 1].",
		12.0,
		{1}
	});
	sections.push_back({
		1,
		"Payoff",
		"Another summary",
		"Third sentence closes the argument [Source 2].",
		10.0,
		{2}
	});

	const auto cues = ofxGgmlVideoEssayWorkflow::buildVoiceCueSheet(sections);
	REQUIRE(cues.size() >= 2);
	REQUIRE(cues.front().sectionIndex == 0);
	REQUIRE(cues.back().sectionIndex == 1);
	REQUIRE(cues.front().endSeconds > cues.front().startSeconds);
	REQUIRE(cues.back().startSeconds >= cues.front().endSeconds);

	const std::string srt = ofxGgmlVideoEssayWorkflow::buildSrt(cues);
	REQUIRE(srt.find("00:00:00,000 -->") != std::string::npos);
	REQUIRE(srt.find("First sentence.") != std::string::npos);
	REQUIRE(srt.find("Third sentence closes the argument") != std::string::npos);
}
