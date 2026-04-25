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

TEST_CASE("Video essay workflow only captures exact source references", "[video_essay]") {
	const std::string script =
		"## 2024 Overview\n"
		"Numbers in the topic should stay plain text, even when followed by bracketed notes like "
		"[Source 2024 data].\n"
		"Only exact citations such as [Source 2] should be collected.\n";

	const auto sections =
		ofxGgmlVideoEssayWorkflow::parseSectionsFromScript(script, 60.0);

	REQUIRE(sections.size() == 1);
	REQUIRE(sections[0].title == "2024 Overview");
	REQUIRE(sections[0].sourceIndices.size() == 1);
	REQUIRE(sections[0].sourceIndices[0] == 2);
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

TEST_CASE("Video essay workflow builds visual concept prompt from sections", "[video_essay]") {
	ofxGgmlVideoEssayRequest request;
	request.topic = "Night trains in Europe";
	request.targetDurationSeconds = 120.0;
	request.tone = "reflective, cinematic, and thoughtful";
	request.audience = "creative filmmaker";

	ofxGgmlCitationSearchResult citations;
	citations.summary = "Rail travel is being reframed as a slower, lower-emission alternative.";
	citations.citations.push_back({
		"Quote about overnight routes expanding",
		"Shows renewed demand.",
		"Example source",
		"https://example.com/night-trains",
		1
	});

	std::vector<ofxGgmlVideoEssaySection> sections;
	sections.push_back({
		0,
		"Hook",
		"Opening with stations, sleepers, and night windows.",
		"Night trains are returning as a practical and romantic form of travel [Source 1].",
		18.0,
		{1}
	});

	const std::string prompt =
		ofxGgmlVideoEssayWorkflow::buildVisualConceptPrompt(request, citations, sections);
	REQUIRE(prompt.find("Night trains in Europe") != std::string::npos);
	REQUIRE(prompt.find("Hook") != std::string::npos);
	REQUIRE(prompt.find("[Source 1]") != std::string::npos);
	REQUIRE(prompt.find("Visual concept:") != std::string::npos);
}

TEST_CASE("Video essay workflow builds edit source summary from sections", "[video_essay]") {
	std::vector<ofxGgmlVideoEssaySection> sections;
	sections.push_back({
		0,
		"Intro",
		"Quick setup of the problem.",
		"Intro narration",
		14.0,
		{1}
	});
	sections.push_back({
		1,
		"Payoff",
		"Closer on the human impact.",
		"Payoff narration",
		20.0,
		{2}
	});

	const std::string summary =
		ofxGgmlVideoEssayWorkflow::buildEditSourceSummary(sections);
	REQUIRE(summary.find("1. Intro") != std::string::npos);
	REQUIRE(summary.find("Quick setup of the problem.") != std::string::npos);
	REQUIRE(summary.find("2. Payoff") != std::string::npos);
}
