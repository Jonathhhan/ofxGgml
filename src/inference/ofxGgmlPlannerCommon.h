#pragma once

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

/// Common utilities and structures shared across video/audio planners
namespace ofxGgmlPlannerCommon {

/// Trim whitespace from both ends of a string.
inline std::string trim(const std::string & text) {
	size_t start = 0;
	while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
		++start;
	}
	size_t end = text.size();
	while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
		--end;
	}
	return text.substr(start, end - start);
}

/// Convert string to lowercase.
inline std::string toLower(const std::string & text) {
	std::string lowered = text;
	std::transform(
		lowered.begin(),
		lowered.end(),
		lowered.begin(),
		[](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});
	return lowered;
}

/// Format seconds as "5.0s" string.
inline std::string formatSeconds(double seconds) {
	std::ostringstream out;
	out << std::fixed << std::setprecision(1) << std::max(0.0, seconds) << "s";
	return out.str();
}

/// Format time range as "1.5s - 2.0s" string.
inline std::string describeTimeRange(double startSeconds, double endSeconds) {
	if (endSeconds > startSeconds) {
		return formatSeconds(startSeconds) + " - " + formatSeconds(endSeconds);
	}
	if (startSeconds > 0.0) {
		return "around " + formatSeconds(startSeconds);
	}
	return "start";
}

/// Format seconds as timecode "HH:MM:SS:FF" for EDL (frame-accurate).
/// @param seconds Time in seconds
/// @param fps Frame rate (e.g., 25, 30, 24)
/// @param dropFrame If true, use drop-frame timecode (e.g., for 29.97fps)
inline std::string formatTimecode(double seconds, int fps = 30, bool dropFrame = false) {
	const int totalFrames = static_cast<int>(seconds * fps);
	int frames = totalFrames % fps;
	int totalSeconds = totalFrames / fps;
	const int secs = totalSeconds % 60;
	const int mins = (totalSeconds / 60) % 60;
	const int hours = totalSeconds / 3600;

	// Drop-frame timecode skips frame numbers 0 and 1 at the start of each minute,
	// except for every tenth minute (00, 10, 20, 30, 40, 50)
	if (dropFrame && fps == 30) {
		const int dropFrames = 2;
		const int framesPerMinute = fps * 60;
		const int framesPerTenMinutes = framesPerMinute * 10;
		const int tenMinuteGroups = totalFrames / framesPerTenMinutes;
		const int remainingFrames = totalFrames % framesPerTenMinutes;

		if (remainingFrames > 0) {
			const int minutesInGroup = remainingFrames / framesPerMinute;
			frames = totalFrames + (tenMinuteGroups * 9 * dropFrames) + (minutesInGroup * dropFrames);
		}
	}

	std::ostringstream out;
	out << std::setfill('0') << std::setw(2) << hours << ":"
		<< std::setw(2) << mins << ":"
		<< std::setw(2) << secs << (dropFrame ? ";" : ":")
		<< std::setw(2) << frames;
	return out.str();
}

/// Format seconds as subtitle timestamp "HH:MM:SS.mmm" (SRT/VTT format).
///
/// @param seconds Time in seconds
/// @param webVttStyle If true, use WebVTT format (HH:MM:SS.mmm), else SRT (HH:MM:SS,mmm)
inline std::string formatSubtitleTimestamp(double seconds, bool webVttStyle = false) {
	const int totalMs = static_cast<int>(seconds * 1000.0);
	const int ms = totalMs % 1000;
	const int totalSeconds = totalMs / 1000;
	const int secs = totalSeconds % 60;
	const int mins = (totalSeconds / 60) % 60;
	const int hours = totalSeconds / 3600;

	std::ostringstream out;
	out << std::setfill('0') << std::setw(2) << hours << ":"
		<< std::setw(2) << mins << ":"
		<< std::setw(2) << secs
		<< (webVttStyle ? "." : ",")
		<< std::setw(3) << ms;
	return out.str();
}

/// Collapse consecutive whitespace into single spaces.
inline std::string collapseWhitespace(const std::string & text) {
	std::string result;
	bool prevWasSpace = false;
	for (const char ch : text) {
		const bool isSpace = std::isspace(static_cast<unsigned char>(ch));
		if (isSpace) {
			if (!prevWasSpace) {
				result.push_back(' ');
				prevWasSpace = true;
			}
		} else {
			result.push_back(ch);
			prevWasSpace = false;
		}
	}
	return result;
}

/// Check if text contains any of the given tokens (case-insensitive).
inline bool containsAnyToken(const std::string & text, const std::vector<std::string> & tokens) {
	const std::string lowered = toLower(text);
	return std::any_of(
		tokens.begin(),
		tokens.end(),
		[&](const std::string & token) {
			return !token.empty() && lowered.find(token) != std::string::npos;
		});
}

/// Base structure for items with temporal extent.
struct TemporalRange {
	double startSeconds = 0.0;
	double endSeconds = 0.0;

	/// Get duration in seconds.
	double duration() const {
		return std::max(0.0, endSeconds - startSeconds);
	}

	/// Check if this range overlaps with another.
	bool overlaps(const TemporalRange & other) const {
		return !(endSeconds <= other.startSeconds || startSeconds >= other.endSeconds);
	}
};

} // namespace ofxGgmlPlannerCommon
