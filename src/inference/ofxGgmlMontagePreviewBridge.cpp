#include "ofxGgmlMontagePreviewBridge.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

std::string sanitizeFileStem(const std::string & text) {
	std::string result;
	result.reserve(text.size());
	for (unsigned char ch : text) {
		if (std::isalnum(ch)) {
			result.push_back(static_cast<char>(std::tolower(ch)));
		} else if (ch == '-' || ch == '_') {
			result.push_back(static_cast<char>(ch));
		} else if (std::isspace(ch) || ch == '.') {
			if (result.empty() || result.back() == '_') {
				continue;
			}
			result.push_back('_');
		}
	}
	while (!result.empty() && result.back() == '_') {
		result.pop_back();
	}
	return result.empty() ? "montage" : result;
}

ofxGgmlMontageSubtitleTrack toSubtitleTrack(
	const ofxGgmlMontagePreviewTrack & track) {
	ofxGgmlMontageSubtitleTrack subtitleTrack;
	subtitleTrack.title = track.title;
	subtitleTrack.cues = track.cues;
	return subtitleTrack;
}

} // namespace

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

const ofxGgmlMontagePreviewTrack & ofxGgmlMontagePreviewBridge::selectTrack(
	const ofxGgmlMontagePreviewBundle & bundle,
	ofxGgmlMontagePreviewTimingMode timingMode) {
	return timingMode == ofxGgmlMontagePreviewTimingMode::Source
		? bundle.sourceTrack
		: bundle.montageTrack;
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

double ofxGgmlMontagePreviewBridge::getTrackDuration(
	const ofxGgmlMontagePreviewTrack & track) {
	double duration = 0.0;
	for (const auto & cue : track.cues) {
		duration = std::max(duration, cue.endSeconds);
	}
	return duration;
}

std::string ofxGgmlMontagePreviewBridge::buildTrackText(
	const ofxGgmlMontagePreviewTrack & track,
	ofxGgmlMontagePreviewTextFormat format) {
	const ofxGgmlMontageSubtitleTrack subtitleTrack = toSubtitleTrack(track);
	return format == ofxGgmlMontagePreviewTextFormat::Vtt
		? ofxGgmlMontagePlanner::buildVtt(subtitleTrack)
		: ofxGgmlMontagePlanner::buildSrt(subtitleTrack);
}

std::string ofxGgmlMontagePreviewBridge::suggestSubtitleFileName(
	const ofxGgmlMontagePreviewTrack & track,
	ofxGgmlMontagePreviewTextFormat format) {
	const std::string stem = sanitizeFileStem(track.title.empty() ? "montage" : track.title);
	const std::string timingSuffix =
		track.timingMode == ofxGgmlMontagePreviewTimingMode::Source
			? "_source_timed"
			: "_montage_timed";
	const std::string extension =
		format == ofxGgmlMontagePreviewTextFormat::Vtt ? ".vtt" : ".srt";
	return stem + timingSuffix + extension;
}

bool ofxGgmlMontagePreviewBridge::exportTrack(
	const ofxGgmlMontagePreviewTrack & track,
	const std::string & outputPath,
	ofxGgmlMontagePreviewTextFormat format,
	std::string * error) {
	if (outputPath.empty()) {
		if (error) {
			*error = "Subtitle export path is empty.";
		}
		return false;
	}
	const std::string text = buildTrackText(track, format);
	std::error_code ec;
	const std::filesystem::path path(outputPath);
	if (path.has_parent_path()) {
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec) {
			if (error) {
				*error = "Failed to create subtitle export directory.";
			}
			return false;
		}
	}
	std::ofstream out(path, std::ios::binary);
	if (!out.is_open()) {
		if (error) {
			*error = "Failed to open subtitle export file.";
		}
		return false;
	}
	out << text;
	if (!out.good()) {
		if (error) {
			*error = "Failed to write subtitle export file.";
		}
		return false;
	}
	return true;
}

std::string ofxGgmlMontagePreviewBridge::summarizeBundle(
	const ofxGgmlMontagePreviewBundle & bundle) {
	std::ostringstream out;
	out << "Source video: "
		<< (bundle.sourceVideoPath.empty() ? "(not set)" : bundle.sourceVideoPath)
		<< "\nPlaylist clips: " << bundle.playlistClips.size()
		<< "\n" << summarizeTrack(bundle.sourceTrack)
		<< "\n" << summarizeTrack(bundle.montageTrack);
	return out.str();
}

std::string ofxGgmlMontagePreviewBridge::summarizeTrack(
	const ofxGgmlMontagePreviewTrack & track) {
	std::ostringstream out;
	out << (track.timingMode == ofxGgmlMontagePreviewTimingMode::Montage
		? "Montage-timed"
		: "Source-timed")
		<< " subtitle track with "
		<< track.cues.size()
		<< " cue(s)"
		<< " over "
		<< getTrackDuration(track)
		<< " s";
	if (!track.cues.empty()) {
		out << "\nFirst cue: " << track.cues.front().text;
	}
	return out.str();
}
