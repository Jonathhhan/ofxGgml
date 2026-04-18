#pragma once

#include "inference/ofxGgmlMontagePlanner.h"

#include <string>
#include <vector>

enum class ofxGgmlMontagePreviewTimingMode {
	Montage = 0,
	Source
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
	static int findCueAtTime(
		const ofxGgmlMontagePreviewTrack & track,
		double seconds);
	static std::string summarizeTrack(
		const ofxGgmlMontagePreviewTrack & track);
};
