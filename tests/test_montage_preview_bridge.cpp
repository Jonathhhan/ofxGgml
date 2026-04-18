#include "catch2.hpp"
#include "../src/inference/ofxGgmlMontagePreviewBridge.h"

#include <filesystem>
#include <fstream>

namespace {

ofxGgmlMontagePlan makeSampleMontagePlan() {
	ofxGgmlMontagePlan plan;
	plan.goal = "Build a short emotional callback montage.";
	plan.clips = {
		{
			1,
			"scene-a",
			"AX",
			10.0,
			12.5,
			0.91,
			"Arrival",
			"He arrives at the station."
		},
		{
			2,
			"scene-b",
			"AX",
			30.0,
			31.0,
			0.88,
			"Reaction",
			"She looks back."
		}
	};
	return plan;
}

} // namespace

TEST_CASE("Montage preview bridge builds montage and source tracks", "[montage_preview]") {
	const ofxGgmlMontagePlan plan = makeSampleMontagePlan();
	const auto bundle = ofxGgmlMontagePreviewBridge::buildBundle(
		plan,
		"My Montage",
		"C:/media/source.mp4");

	REQUIRE(bundle.sourceVideoPath == "C:/media/source.mp4");
	REQUIRE(bundle.playlistClips.size() == 2);

	SECTION("Montage track is retimed sequentially") {
		REQUIRE(bundle.montageTrack.timingMode == ofxGgmlMontagePreviewTimingMode::Montage);
		REQUIRE(bundle.montageTrack.cues.size() == 2);
		REQUIRE(bundle.montageTrack.cues[0].startSeconds == Approx(0.0));
		REQUIRE(bundle.montageTrack.cues[0].endSeconds == Approx(2.5));
		REQUIRE(bundle.montageTrack.cues[1].startSeconds == Approx(2.5));
		REQUIRE(bundle.montageTrack.cues[1].endSeconds == Approx(3.5));
	}

	SECTION("Source track preserves source timing") {
		REQUIRE(bundle.sourceTrack.timingMode == ofxGgmlMontagePreviewTimingMode::Source);
		REQUIRE(bundle.sourceTrack.cues.size() == 2);
		REQUIRE(bundle.sourceTrack.cues[0].startSeconds == Approx(10.0));
		REQUIRE(bundle.sourceTrack.cues[0].endSeconds == Approx(12.5));
		REQUIRE(bundle.sourceTrack.cues[1].startSeconds == Approx(30.0));
		REQUIRE(bundle.sourceTrack.cues[1].endSeconds == Approx(31.0));
	}
}

TEST_CASE("Montage preview bridge exports subtitle text and files", "[montage_preview]") {
	const ofxGgmlMontagePlan plan = makeSampleMontagePlan();
	const auto bundle = ofxGgmlMontagePreviewBridge::buildBundle(plan, "My Montage");
	const auto & montageTrack = ofxGgmlMontagePreviewBridge::selectTrack(
		bundle,
		ofxGgmlMontagePreviewTimingMode::Montage);

	SECTION("Text generation produces SRT and VTT") {
		const std::string srt =
			ofxGgmlMontagePreviewBridge::buildTrackText(
				montageTrack,
				ofxGgmlMontagePreviewTextFormat::Srt);
		const std::string vtt =
			ofxGgmlMontagePreviewBridge::buildTrackText(
				montageTrack,
				ofxGgmlMontagePreviewTextFormat::Vtt);

		REQUIRE(srt.find("1\n00:00:00,000 --> 00:00:02,500") != std::string::npos);
		REQUIRE(srt.find("He arrives at the station.") != std::string::npos);
		REQUIRE(vtt.find("WEBVTT - My Montage") != std::string::npos);
		REQUIRE(vtt.find("00:00:02.500 --> 00:00:03.500") != std::string::npos);
	}

	SECTION("File name suggestion reflects timing mode") {
		const std::string fileName =
			ofxGgmlMontagePreviewBridge::suggestSubtitleFileName(
				montageTrack,
				ofxGgmlMontagePreviewTextFormat::Srt);
		REQUIRE(fileName == "my_montage_montage_timed.srt");
	}

	SECTION("Track export writes the expected file") {
		const std::filesystem::path outputPath =
			std::filesystem::temp_directory_path() / "ofxggml_montage_preview_test.srt";
		std::string error;
		const bool exported = ofxGgmlMontagePreviewBridge::exportTrack(
			montageTrack,
			outputPath.string(),
			ofxGgmlMontagePreviewTextFormat::Srt,
			&error);

		REQUIRE(exported);
		REQUIRE(error.empty());

		std::ifstream input(outputPath.string(), std::ios::binary);
		REQUIRE(input.is_open());
		const std::string contents(
			(std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
		REQUIRE(contents.find("She looks back.") != std::string::npos);

		std::error_code ec;
		std::filesystem::remove(outputPath, ec);
	}
}
