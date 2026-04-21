#pragma once

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

/// WebVTT cue settings for positioning and styling
struct ofxGgmlVttCueSettings {
	std::string position;      // e.g., "50%" (horizontal position)
	std::string line;          // e.g., "85%" (vertical position)
	std::string size;          // e.g., "80%" (width of cue box)
	std::string align;         // "start", "center", "end", "left", "right"
	std::string vertical;      // "lr" (left-to-right) or "rl" (right-to-left)
	std::string region;        // Named region ID

	/// Build cue settings string for VTT format
	std::string toString() const {
		std::ostringstream out;
		bool first = true;

		auto append = [&](const std::string& key, const std::string& value) {
			if (!value.empty()) {
				if (!first) out << " ";
				out << key << ":" << value;
				first = false;
			}
		};

		append("position", position);
		append("line", line);
		append("size", size);
		append("align", align);
		append("vertical", vertical);
		append("region", region);

		return out.str();
	}
};

/// Subtitle validation result
struct ofxGgmlSubtitleValidation {
	bool valid = true;
	std::vector<std::string> errors;
	std::vector<std::string> warnings;

	void addError(const std::string& message) {
		valid = false;
		errors.push_back(message);
	}

	void addWarning(const std::string& message) {
		warnings.push_back(message);
	}

	std::string summary() const {
		std::ostringstream out;
		if (valid) {
			out << "Valid subtitle file";
			if (!warnings.empty()) {
				out << " with " << warnings.size() << " warning(s)";
			}
		} else {
			out << "Invalid subtitle file with " << errors.size() << " error(s)";
		}
		return out.str();
	}
};

/// Subtitle quality metrics
struct ofxGgmlSubtitleMetrics {
	size_t totalCues = 0;
	double totalDurationSeconds = 0.0;
	double averageCueDurationSeconds = 0.0;
	double minCueDurationSeconds = 0.0;
	double maxCueDurationSeconds = 0.0;
	size_t totalWords = 0;
	double averageWordsPerMinute = 0.0;
	size_t overlapCount = 0;
	size_t gapCount = 0;
	double totalGapDurationSeconds = 0.0;
	size_t tooShortCount = 0;  // Cues under 0.3s
	size_t tooLongCount = 0;   // Cues over 7s
	size_t tooFastCount = 0;   // Reading speed > 200 WPM
};

namespace ofxGgmlSubtitleHelpers {

/// Count words in text (simple whitespace-based)
inline size_t countWords(const std::string& text) {
	if (text.empty()) return 0;

	size_t count = 0;
	bool inWord = false;

	for (unsigned char ch : text) {
		if (std::isspace(ch)) {
			inWord = false;
		} else if (!inWord) {
			inWord = true;
			count++;
		}
	}

	return count;
}

/// Calculate reading speed in words per minute
inline double calculateReadingSpeed(const std::string& text, double durationSeconds) {
	if (durationSeconds <= 0.0) return 0.0;
	const size_t words = countWords(text);
	return (words * 60.0) / durationSeconds;
}

/// Check if two time ranges overlap
inline bool timeRangesOverlap(double start1, double end1, double start2, double end2) {
	return !(end1 <= start2 || start1 >= end2);
}

/// Validate subtitle cue timing and content
template<typename CueType>
inline ofxGgmlSubtitleValidation validateCues(const std::vector<CueType>& cues) {
	ofxGgmlSubtitleValidation result;

	for (size_t i = 0; i < cues.size(); ++i) {
		const auto& cue = cues[i];

		// Check timing validity
		if (cue.endSeconds < cue.startSeconds) {
			result.addError("Cue " + std::to_string(i + 1) +
				": End time before start time");
		}

		// Check for negative times
		if (cue.startSeconds < 0.0 || cue.endSeconds < 0.0) {
			result.addError("Cue " + std::to_string(i + 1) +
				": Negative time value");
		}

		// Check for empty text
		if (cue.text.empty()) {
			result.addWarning("Cue " + std::to_string(i + 1) +
				": Empty text");
		}

		const double duration = cue.endSeconds - cue.startSeconds;

		// Check duration bounds
		if (duration < 0.3 && duration > 0.0) {
			result.addWarning("Cue " + std::to_string(i + 1) +
				": Very short duration (" + std::to_string(duration) + "s)");
		}

		if (duration > 7.0) {
			result.addWarning("Cue " + std::to_string(i + 1) +
				": Very long duration (" + std::to_string(duration) + "s)");
		}

		// Check reading speed
		if (duration > 0.0) {
			const double wpm = calculateReadingSpeed(cue.text, duration);
			if (wpm > 200.0) {
				result.addWarning("Cue " + std::to_string(i + 1) +
					": Fast reading speed (" + std::to_string(static_cast<int>(wpm)) + " WPM)");
			}
		}

		// Check for overlaps with next cue
		if (i + 1 < cues.size()) {
			const auto& nextCue = cues[i + 1];
			if (timeRangesOverlap(cue.startSeconds, cue.endSeconds,
			                      nextCue.startSeconds, nextCue.endSeconds)) {
				result.addError("Cue " + std::to_string(i + 1) +
					" overlaps with cue " + std::to_string(i + 2));
			}
		}
	}

	return result;
}

/// Calculate quality metrics for subtitle cues
template<typename CueType>
inline ofxGgmlSubtitleMetrics calculateMetrics(const std::vector<CueType>& cues) {
	ofxGgmlSubtitleMetrics metrics;

	if (cues.empty()) {
		return metrics;
	}

	metrics.totalCues = cues.size();
	metrics.minCueDurationSeconds = std::numeric_limits<double>::max();
	metrics.maxCueDurationSeconds = 0.0;

	double totalDuration = 0.0;

	for (size_t i = 0; i < cues.size(); ++i) {
		const auto& cue = cues[i];
		const double duration = std::max(0.0, cue.endSeconds - cue.startSeconds);

		totalDuration += duration;
		metrics.minCueDurationSeconds = std::min(metrics.minCueDurationSeconds, duration);
		metrics.maxCueDurationSeconds = std::max(metrics.maxCueDurationSeconds, duration);

		metrics.totalWords += countWords(cue.text);

		// Count quality issues
		if (duration < 0.3 && duration > 0.0) {
			metrics.tooShortCount++;
		}
		if (duration > 7.0) {
			metrics.tooLongCount++;
		}

		if (duration > 0.0) {
			const double wpm = calculateReadingSpeed(cue.text, duration);
			if (wpm > 200.0) {
				metrics.tooFastCount++;
			}
		}

		// Check for overlaps and gaps
		if (i + 1 < cues.size()) {
			const auto& nextCue = cues[i + 1];
			if (timeRangesOverlap(cue.startSeconds, cue.endSeconds,
			                      nextCue.startSeconds, nextCue.endSeconds)) {
				metrics.overlapCount++;
			}

			const double gap = nextCue.startSeconds - cue.endSeconds;
			if (gap > 0.5) {  // Gap threshold: 0.5 seconds
				metrics.gapCount++;
				metrics.totalGapDurationSeconds += gap;
			}
		}

		metrics.totalDurationSeconds = std::max(metrics.totalDurationSeconds, cue.endSeconds);
	}

	metrics.averageCueDurationSeconds = totalDuration / static_cast<double>(metrics.totalCues);

	if (metrics.totalDurationSeconds > 0.0) {
		metrics.averageWordsPerMinute =
			(static_cast<double>(metrics.totalWords) * 60.0) / metrics.totalDurationSeconds;
	}

	return metrics;
}

/// Adjust all subtitle timings by an offset
template<typename CueType>
inline void offsetTiming(std::vector<CueType>& cues, double offsetSeconds) {
	for (auto& cue : cues) {
		cue.startSeconds = std::max(0.0, cue.startSeconds + offsetSeconds);
		cue.endSeconds = std::max(0.0, cue.endSeconds + offsetSeconds);
	}
}

/// Scale subtitle timing (speed up or slow down)
template<typename CueType>
inline void scaleTiming(std::vector<CueType>& cues, double factor) {
	if (factor <= 0.0) return;

	for (auto& cue : cues) {
		cue.startSeconds *= factor;
		cue.endSeconds *= factor;
	}
}

/// Merge consecutive cues that are close together
template<typename CueType>
inline std::vector<CueType> mergeCues(
	const std::vector<CueType>& cues,
	double maxGapSeconds = 0.5) {

	if (cues.empty()) {
		return cues;
	}

	std::vector<CueType> merged;
	merged.reserve(cues.size());

	CueType current = cues[0];

	for (size_t i = 1; i < cues.size(); ++i) {
		const auto& next = cues[i];
		const double gap = next.startSeconds - current.endSeconds;

		if (gap <= maxGapSeconds && gap >= 0.0) {
			// Merge with current
			current.endSeconds = next.endSeconds;
			if (!current.text.empty() && !next.text.empty()) {
				current.text += " " + next.text;
			} else if (current.text.empty()) {
				current.text = next.text;
			}
		} else {
			// Save current and start new
			merged.push_back(current);
			current = next;
		}
	}

	merged.push_back(current);
	return merged;
}

/// Split long cues into smaller segments
template<typename CueType>
inline std::vector<CueType> splitLongCues(
	const std::vector<CueType>& cues,
	double maxDurationSeconds = 7.0) {

	std::vector<CueType> result;
	result.reserve(cues.size());

	for (const auto& cue : cues) {
		const double duration = cue.endSeconds - cue.startSeconds;

		if (duration <= maxDurationSeconds) {
			result.push_back(cue);
			continue;
		}

		// Split into multiple segments
		const size_t words = countWords(cue.text);
		if (words == 0) {
			result.push_back(cue);
			continue;
		}

		const int segments = static_cast<int>(std::ceil(duration / maxDurationSeconds));
		const double segmentDuration = duration / segments;

		// Simple split - divide text and time equally
		std::istringstream textStream(cue.text);
		std::vector<std::string> allWords;
		std::string word;
		while (textStream >> word) {
			allWords.push_back(word);
		}

		const size_t wordsPerSegment = std::max<size_t>(1, allWords.size() / segments);

		for (int i = 0; i < segments; ++i) {
			CueType segment = cue;
			segment.startSeconds = cue.startSeconds + (i * segmentDuration);
			segment.endSeconds = (i == segments - 1)
				? cue.endSeconds
				: cue.startSeconds + ((i + 1) * segmentDuration);

			// Assign words to this segment
			const size_t startWord = i * wordsPerSegment;
			const size_t endWord = (i == segments - 1)
				? allWords.size()
				: std::min(allWords.size(), (i + 1) * wordsPerSegment);

			std::ostringstream segmentText;
			for (size_t w = startWord; w < endWord; ++w) {
				if (w > startWord) segmentText << " ";
				segmentText << allWords[w];
			}
			segment.text = segmentText.str();

			if (!segment.text.empty()) {
				result.push_back(segment);
			}
		}
	}

	return result;
}

/// Format metrics as human-readable summary
inline std::string formatMetricsSummary(const ofxGgmlSubtitleMetrics& metrics) {
	std::ostringstream out;
	out << "Subtitle Metrics:\n";
	out << "  Total cues: " << metrics.totalCues << "\n";
	out << "  Total duration: " << std::fixed << std::setprecision(1)
	    << metrics.totalDurationSeconds << "s\n";
	out << "  Average cue duration: " << std::fixed << std::setprecision(2)
	    << metrics.averageCueDurationSeconds << "s\n";
	out << "  Duration range: " << std::fixed << std::setprecision(2)
	    << metrics.minCueDurationSeconds << "s - "
	    << metrics.maxCueDurationSeconds << "s\n";
	out << "  Total words: " << metrics.totalWords << "\n";
	out << "  Average reading speed: " << std::fixed << std::setprecision(0)
	    << metrics.averageWordsPerMinute << " WPM\n";

	if (metrics.overlapCount > 0) {
		out << "  Overlaps detected: " << metrics.overlapCount << "\n";
	}
	if (metrics.gapCount > 0) {
		out << "  Gaps detected: " << metrics.gapCount
		    << " (total " << std::fixed << std::setprecision(1)
		    << metrics.totalGapDurationSeconds << "s)\n";
	}
	if (metrics.tooShortCount > 0) {
		out << "  Too short cues (< 0.3s): " << metrics.tooShortCount << "\n";
	}
	if (metrics.tooLongCount > 0) {
		out << "  Too long cues (> 7s): " << metrics.tooLongCount << "\n";
	}
	if (metrics.tooFastCount > 0) {
		out << "  Too fast cues (> 200 WPM): " << metrics.tooFastCount << "\n";
	}

	return out.str();
}

} // namespace ofxGgmlSubtitleHelpers
