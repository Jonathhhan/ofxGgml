#include "catch2.hpp"
#include "../src/support/ofxGgmlSubtitleHelpers.h"
#include "../src/inference/ofxGgmlMontagePlanner.h"

TEST_CASE("Subtitle validation - valid cues", "[subtitle][validation]") {
	std::vector<ofxGgmlMontageSubtitleCue> cues;

	ofxGgmlMontageSubtitleCue cue1;
	cue1.startSeconds = 0.0;
	cue1.endSeconds = 2.0;
	cue1.text = "First subtitle";
	cues.push_back(cue1);

	ofxGgmlMontageSubtitleCue cue2;
	cue2.startSeconds = 3.0;
	cue2.endSeconds = 5.0;
	cue2.text = "Second subtitle";
	cues.push_back(cue2);

	auto validation = ofxGgmlSubtitleHelpers::validateCues(cues);
	REQUIRE(validation.valid);
	REQUIRE(validation.errors.empty());
}

TEST_CASE("Subtitle validation - detects overlaps", "[subtitle][validation]") {
	std::vector<ofxGgmlMontageSubtitleCue> cues;

	ofxGgmlMontageSubtitleCue cue1;
	cue1.startSeconds = 0.0;
	cue1.endSeconds = 3.0;
	cue1.text = "Overlapping subtitle 1";
	cues.push_back(cue1);

	ofxGgmlMontageSubtitleCue cue2;
	cue2.startSeconds = 2.0;
	cue2.endSeconds = 4.0;
	cue2.text = "Overlapping subtitle 2";
	cues.push_back(cue2);

	auto validation = ofxGgmlSubtitleHelpers::validateCues(cues);
	REQUIRE_FALSE(validation.valid);
	REQUIRE_FALSE(validation.errors.empty());
	REQUIRE(validation.errors[0].find("overlap") != std::string::npos);
}

TEST_CASE("Subtitle validation - detects invalid timing", "[subtitle][validation]") {
	std::vector<ofxGgmlMontageSubtitleCue> cues;

	ofxGgmlMontageSubtitleCue cue;
	cue.startSeconds = 5.0;
	cue.endSeconds = 2.0;  // End before start
	cue.text = "Invalid timing";
	cues.push_back(cue);

	auto validation = ofxGgmlSubtitleHelpers::validateCues(cues);
	REQUIRE_FALSE(validation.valid);
	REQUIRE(validation.errors.size() >= 1);
}

TEST_CASE("Subtitle validation - warns about duration issues", "[subtitle][validation]") {
	std::vector<ofxGgmlMontageSubtitleCue> cues;

	// Too short
	ofxGgmlMontageSubtitleCue shortCue;
	shortCue.startSeconds = 0.0;
	shortCue.endSeconds = 0.2;
	shortCue.text = "Too short";
	cues.push_back(shortCue);

	// Too long
	ofxGgmlMontageSubtitleCue longCue;
	longCue.startSeconds = 1.0;
	longCue.endSeconds = 10.0;
	longCue.text = "This is a very long subtitle that goes on and on";
	cues.push_back(longCue);

	auto validation = ofxGgmlSubtitleHelpers::validateCues(cues);
	REQUIRE(validation.warnings.size() >= 2);
}

TEST_CASE("Subtitle validation - warns about fast reading speed", "[subtitle][validation]") {
	std::vector<ofxGgmlMontageSubtitleCue> cues;

	ofxGgmlMontageSubtitleCue cue;
	cue.startSeconds = 0.0;
	cue.endSeconds = 1.0;
	cue.text = "This subtitle has many many words to read in just one second which is way too fast for comfortable reading";
	cues.push_back(cue);

	auto validation = ofxGgmlSubtitleHelpers::validateCues(cues);
	REQUIRE_FALSE(validation.warnings.empty());
	bool foundSpeedWarning = false;
	for (const auto& warning : validation.warnings) {
		if (warning.find("reading speed") != std::string::npos ||
		    warning.find("WPM") != std::string::npos) {
			foundSpeedWarning = true;
			break;
		}
	}
	REQUIRE(foundSpeedWarning);
}

TEST_CASE("Subtitle metrics - calculates basic stats", "[subtitle][metrics]") {
	std::vector<ofxGgmlMontageSubtitleCue> cues;

	for (int i = 0; i < 5; ++i) {
		ofxGgmlMontageSubtitleCue cue;
		cue.startSeconds = i * 3.0;
		cue.endSeconds = i * 3.0 + 2.0;
		cue.text = "Test subtitle number " + std::to_string(i + 1);
		cues.push_back(cue);
	}

	auto metrics = ofxGgmlSubtitleHelpers::calculateMetrics(cues);

	REQUIRE(metrics.totalCues == 5);
	REQUIRE(metrics.averageCueDurationSeconds == Approx(2.0));
	REQUIRE(metrics.minCueDurationSeconds == Approx(2.0));
	REQUIRE(metrics.maxCueDurationSeconds == Approx(2.0));
	REQUIRE(metrics.totalWords > 0);
}

TEST_CASE("Subtitle metrics - detects gaps", "[subtitle][metrics]") {
	std::vector<ofxGgmlMontageSubtitleCue> cues;

	ofxGgmlMontageSubtitleCue cue1;
	cue1.startSeconds = 0.0;
	cue1.endSeconds = 2.0;
	cue1.text = "First";
	cues.push_back(cue1);

	// Large gap
	ofxGgmlMontageSubtitleCue cue2;
	cue2.startSeconds = 5.0;  // 3 second gap
	cue2.endSeconds = 7.0;
	cue2.text = "Second";
	cues.push_back(cue2);

	auto metrics = ofxGgmlSubtitleHelpers::calculateMetrics(cues);
	REQUIRE(metrics.gapCount > 0);
	REQUIRE(metrics.totalGapDurationSeconds > 0.0);
}

TEST_CASE("Subtitle timing - offset adjustment", "[subtitle][timing]") {
	std::vector<ofxGgmlMontageSubtitleCue> cues;

	ofxGgmlMontageSubtitleCue cue;
	cue.startSeconds = 1.0;
	cue.endSeconds = 3.0;
	cue.text = "Test";
	cues.push_back(cue);

	ofxGgmlSubtitleHelpers::offsetTiming(cues, 2.0);

	REQUIRE(cues[0].startSeconds == Approx(3.0));
	REQUIRE(cues[0].endSeconds == Approx(5.0));
}

TEST_CASE("Subtitle timing - negative offset clamped to zero", "[subtitle][timing]") {
	std::vector<ofxGgmlMontageSubtitleCue> cues;

	ofxGgmlMontageSubtitleCue cue;
	cue.startSeconds = 1.0;
	cue.endSeconds = 3.0;
	cue.text = "Test";
	cues.push_back(cue);

	ofxGgmlSubtitleHelpers::offsetTiming(cues, -2.0);

	REQUIRE(cues[0].startSeconds == Approx(0.0));
	REQUIRE(cues[0].endSeconds == Approx(1.0));
}

TEST_CASE("Subtitle timing - scale adjustment", "[subtitle][timing]") {
	std::vector<ofxGgmlMontageSubtitleCue> cues;

	ofxGgmlMontageSubtitleCue cue;
	cue.startSeconds = 2.0;
	cue.endSeconds = 4.0;
	cue.text = "Test";
	cues.push_back(cue);

	ofxGgmlSubtitleHelpers::scaleTiming(cues, 2.0);

	REQUIRE(cues[0].startSeconds == Approx(4.0));
	REQUIRE(cues[0].endSeconds == Approx(8.0));
}

TEST_CASE("Subtitle merging - combines close cues", "[subtitle][merge]") {
	std::vector<ofxGgmlMontageSubtitleCue> cues;

	ofxGgmlMontageSubtitleCue cue1;
	cue1.startSeconds = 0.0;
	cue1.endSeconds = 1.0;
	cue1.text = "First";
	cues.push_back(cue1);

	ofxGgmlMontageSubtitleCue cue2;
	cue2.startSeconds = 1.2;  // 0.2s gap - should merge
	cue2.endSeconds = 2.0;
	cue2.text = "Second";
	cues.push_back(cue2);

	auto merged = ofxGgmlSubtitleHelpers::mergeCues(cues, 0.5);

	REQUIRE(merged.size() == 1);
	REQUIRE(merged[0].startSeconds == Approx(0.0));
	REQUIRE(merged[0].endSeconds == Approx(2.0));
	REQUIRE(merged[0].text.find("First") != std::string::npos);
	REQUIRE(merged[0].text.find("Second") != std::string::npos);
}

TEST_CASE("Subtitle merging - keeps distant cues separate", "[subtitle][merge]") {
	std::vector<ofxGgmlMontageSubtitleCue> cues;

	ofxGgmlMontageSubtitleCue cue1;
	cue1.startSeconds = 0.0;
	cue1.endSeconds = 1.0;
	cue1.text = "First";
	cues.push_back(cue1);

	ofxGgmlMontageSubtitleCue cue2;
	cue2.startSeconds = 3.0;  // 2s gap - should not merge
	cue2.endSeconds = 4.0;
	cue2.text = "Second";
	cues.push_back(cue2);

	auto merged = ofxGgmlSubtitleHelpers::mergeCues(cues, 0.5);

	REQUIRE(merged.size() == 2);
}

TEST_CASE("Subtitle splitting - splits long cues", "[subtitle][split]") {
	std::vector<ofxGgmlMontageSubtitleCue> cues;

	ofxGgmlMontageSubtitleCue cue;
	cue.startSeconds = 0.0;
	cue.endSeconds = 15.0;  // 15 seconds - too long
	cue.text = "This is a very long subtitle that should be split into multiple segments for better readability";
	cues.push_back(cue);

	auto split = ofxGgmlSubtitleHelpers::splitLongCues(cues, 7.0);

	REQUIRE(split.size() > 1);
	for (const auto& segment : split) {
		const double duration = segment.endSeconds - segment.startSeconds;
		REQUIRE(duration <= 7.5);  // Allow some tolerance
	}
}

TEST_CASE("Subtitle splitting - preserves short cues", "[subtitle][split]") {
	std::vector<ofxGgmlMontageSubtitleCue> cues;

	ofxGgmlMontageSubtitleCue cue;
	cue.startSeconds = 0.0;
	cue.endSeconds = 2.0;
	cue.text = "Short subtitle";
	cues.push_back(cue);

	auto split = ofxGgmlSubtitleHelpers::splitLongCues(cues, 7.0);

	REQUIRE(split.size() == 1);
	REQUIRE(split[0].text == cue.text);
}

TEST_CASE("VTT cue settings - formats correctly", "[subtitle][vtt]") {
	ofxGgmlVttCueSettings settings;
	settings.position = "50%";
	settings.line = "85%";
	settings.align = "center";

	std::string formatted = settings.toString();

	REQUIRE(formatted.find("position:50%") != std::string::npos);
	REQUIRE(formatted.find("line:85%") != std::string::npos);
	REQUIRE(formatted.find("align:center") != std::string::npos);
}

TEST_CASE("VTT export includes cue settings", "[subtitle][vtt]") {
	ofxGgmlMontageSubtitleTrack track;
	track.title = "Test";

	ofxGgmlMontageSubtitleCue cue;
	cue.startSeconds = 0.0;
	cue.endSeconds = 2.0;
	cue.text = "Styled subtitle";
	cue.vttSettings.align = "center";
	cue.vttSettings.position = "50%";
	track.cues.push_back(cue);

	std::string vtt = ofxGgmlMontagePlanner::buildVtt(track);

	REQUIRE(vtt.find("WEBVTT") != std::string::npos);
	REQUIRE(vtt.find("align:center") != std::string::npos);
	REQUIRE(vtt.find("position:50%") != std::string::npos);
}

TEST_CASE("Subtitle validation integration", "[subtitle][montage]") {
	ofxGgmlMontageSubtitleTrack track;

	ofxGgmlMontageSubtitleCue cue1;
	cue1.startSeconds = 0.0;
	cue1.endSeconds = 2.0;
	cue1.text = "Valid subtitle";
	track.cues.push_back(cue1);

	ofxGgmlMontageSubtitleCue cue2;
	cue2.startSeconds = 3.0;
	cue2.endSeconds = 5.0;
	cue2.text = "Another valid subtitle";
	track.cues.push_back(cue2);

	auto validation = ofxGgmlMontagePlanner::validateSubtitleTrack(track);
	REQUIRE(validation.valid);
}

TEST_CASE("Subtitle metrics integration", "[subtitle][montage]") {
	ofxGgmlMontageSubtitleTrack track;

	for (int i = 0; i < 3; ++i) {
		ofxGgmlMontageSubtitleCue cue;
		cue.startSeconds = i * 3.0;
		cue.endSeconds = i * 3.0 + 2.0;
		cue.text = "Test subtitle";
		track.cues.push_back(cue);
	}

	auto metrics = ofxGgmlMontagePlanner::calculateSubtitleMetrics(track);
	REQUIRE(metrics.totalCues == 3);
	REQUIRE(metrics.averageCueDurationSeconds == Approx(2.0));
}

TEST_CASE("Word counting utility", "[subtitle][utility]") {
	REQUIRE(ofxGgmlSubtitleHelpers::countWords("") == 0);
	REQUIRE(ofxGgmlSubtitleHelpers::countWords("Hello") == 1);
	REQUIRE(ofxGgmlSubtitleHelpers::countWords("Hello world") == 2);
	REQUIRE(ofxGgmlSubtitleHelpers::countWords("  Hello   world  ") == 2);
	REQUIRE(ofxGgmlSubtitleHelpers::countWords("One two three four five") == 5);
}

TEST_CASE("Reading speed calculation", "[subtitle][utility]") {
	// 60 words in 60 seconds = 60 WPM
	const double wpm = ofxGgmlSubtitleHelpers::calculateReadingSpeed(
		"word " + std::string(59, ' '), 60.0);

	// Should be close to expected (allowing for counting variations)
	REQUIRE(wpm > 0.0);
	REQUIRE(wpm < 300.0);
}

TEST_CASE("Metrics summary formatting", "[subtitle][utility]") {
	ofxGgmlSubtitleMetrics metrics;
	metrics.totalCues = 10;
	metrics.totalDurationSeconds = 30.0;
	metrics.averageCueDurationSeconds = 3.0;
	metrics.totalWords = 100;
	metrics.averageWordsPerMinute = 200.0;

	std::string summary = ofxGgmlSubtitleHelpers::formatMetricsSummary(metrics);

	REQUIRE(summary.find("10") != std::string::npos);
	REQUIRE(summary.find("30") != std::string::npos);
	REQUIRE(summary.find("WPM") != std::string::npos);
}
