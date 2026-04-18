#pragma once

#include "inference/ofxGgmlMontagePlanner.h"

#include <string>
#include <vector>

enum class ofxGgmlMontagePreviewTimingMode {
	Montage = 0,
	Source
};

enum class ofxGgmlMontagePreviewTextFormat {
	Srt = 0,
	Vtt
};

struct ofxGgmlMontagePreviewTrack {
	std::string title;
	ofxGgmlMontagePreviewTimingMode timingMode =
		ofxGgmlMontagePreviewTimingMode::Montage;
	std::vector<ofxGgmlMontageSubtitleCue> cues;
};

struct ofxGgmlMontagePreviewBundle {
	std::string sourceVideoPath;
	std::vector<ofxGgmlMontageClip> playlistClips;
	ofxGgmlMontagePreviewTrack montageTrack;
	ofxGgmlMontagePreviewTrack sourceTrack;
};

class ofxGgmlMontagePreviewBridge {
public:
	static ofxGgmlMontagePreviewTrack buildMontageTrack(
		const ofxGgmlMontagePlan & plan,
		const std::string & title = "MONTAGE");
	static ofxGgmlMontagePreviewTrack buildSourceTrack(
		const ofxGgmlMontagePlan & plan,
		const std::string & title = "MONTAGE");
	static ofxGgmlMontagePreviewBundle buildBundle(
		const ofxGgmlMontagePlan & plan,
		const std::string & title = "MONTAGE",
		const std::string & sourceVideoPath = "");
	static const ofxGgmlMontagePreviewTrack & selectTrack(
		const ofxGgmlMontagePreviewBundle & bundle,
		ofxGgmlMontagePreviewTimingMode timingMode);
	static int findCueAtTime(
		const ofxGgmlMontagePreviewTrack & track,
		double seconds);
	static double getTrackDuration(
		const ofxGgmlMontagePreviewTrack & track);
	static std::string buildTrackText(
		const ofxGgmlMontagePreviewTrack & track,
		ofxGgmlMontagePreviewTextFormat format = ofxGgmlMontagePreviewTextFormat::Srt);
	static std::string suggestSubtitleFileName(
		const ofxGgmlMontagePreviewTrack & track,
		ofxGgmlMontagePreviewTextFormat format = ofxGgmlMontagePreviewTextFormat::Srt);
	static bool exportTrack(
		const ofxGgmlMontagePreviewTrack & track,
		const std::string & outputPath,
		ofxGgmlMontagePreviewTextFormat format = ofxGgmlMontagePreviewTextFormat::Srt,
		std::string * error = nullptr);
	static std::string summarizeBundle(
		const ofxGgmlMontagePreviewBundle & bundle);
	static std::string summarizeTrack(
		const ofxGgmlMontagePreviewTrack & track);
};
