#include "ofxGgmlMontagePreviewBridge.h"

#include <algorithm>
#include <sstream>

ofxGgmlMontagePreviewTrack ofxGgmlMontagePreviewBridge::buildMontageTrack(
	const ofxGgmlMontagePlan & plan,
	const std::string & title) {
	ofxGgmlMontagePreviewTrack track;
	track.title = title;
	track.timingMode = ofxGgmlMontagePreviewTimingMode::Montage;
	track.cues = ofxGgmlMontagePlanner::buildSubtitleTrack(plan, title).cues;
	return track;
}

ofxGgmlMontagePreviewTrack ofxGgmlMontagePreviewBridge::buildSourceTrack(
	const ofxGgmlMontagePlan & plan,
	const std::string & title) {
	ofxGgmlMontagePreviewTrack track;
	track.title = title;
	track.timingMode = ofxGgmlMontagePreviewTimingMode::Source;
	track.cues = ofxGgmlMontagePlanner::buildSourceSubtitleTrack(plan, title).cues;
	return track;
}

ofxGgmlMontagePreviewBundle ofxGgmlMontagePreviewBridge::buildBundle(
	const ofxGgmlMontagePlan & plan,
	const std::string & title,
	const std::string & sourceVideoPath) {
	ofxGgmlMontagePreviewBundle bundle;
	bundle.sourceVideoPath = sourceVideoPath;
	bundle.playlistClips = plan.clips;
	bundle.montageTrack = buildMontageTrack(plan, title);
	bundle.sourceTrack = buildSourceTrack(plan, title);
	return bundle;
}

int ofxGgmlMontagePreviewBridge::findCueAtTime(
	const ofxGgmlMontagePreviewTrack & track,
	double seconds) {
	const double clampedSeconds = std::max(0.0, seconds);
	for (size_t i = 0; i < track.cues.size(); ++i) {
		const auto & cue = track.cues[i];
		if (clampedSeconds >= cue.startSeconds &&
			clampedSeconds <= cue.endSeconds) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

std::string ofxGgmlMontagePreviewBridge::summarizeTrack(
	const ofxGgmlMontagePreviewTrack & track) {
	std::ostringstream out;
	out << (track.timingMode == ofxGgmlMontagePreviewTimingMode::Montage
		? "Montage-timed"
		: "Source-timed")
		<< " subtitle track with "
		<< track.cues.size()
		<< " cue(s)";
	if (!track.cues.empty()) {
		out << "\nFirst cue: " << track.cues.front().text;
	}
	return out.str();
}
