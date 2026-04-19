#include "ofxGgmlMontagePlanner.h"

#include "support/ofxGgmlSimpleSrtSubtitleParser.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace {

std::string trimMontageCopy(const std::string & text) {
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

std::string sanitizeReelName(const std::string & value, const std::string & fallback) {
	std::string sanitized;
	sanitized.reserve(8);
	for (char c : value) {
		const unsigned char uc = static_cast<unsigned char>(c);
		if (std::isalnum(uc)) {
			sanitized.push_back(static_cast<char>(std::toupper(uc)));
			if (sanitized.size() >= 8) {
				break;
			}
		}
	}
	if (!sanitized.empty()) {
		return sanitized;
	}
	return sanitizeReelName(fallback.empty() ? "AX" : fallback, "AX");
}

std::string collapseWhitespace(const std::string & text) {
	std::ostringstream out;
	bool previousWasSpace = false;
	for (char c : text) {
		if (std::isspace(static_cast<unsigned char>(c))) {
			if (!previousWasSpace) {
				out << ' ';
				previousWasSpace = true;
			}
			continue;
		}
		out << c;
		previousWasSpace = false;
	}
	return trimMontageCopy(out.str());
}

std::unordered_set<std::string> toTokenLookup(const std::vector<std::string> & tokens) {
	return std::unordered_set<std::string>(tokens.begin(), tokens.end());
}

std::string joinStrings(const std::vector<std::string> & values, const std::string & separator) {
	std::ostringstream out;
	for (size_t i = 0; i < values.size(); ++i) {
		if (i > 0) {
			out << separator;
		}
		out << values[i];
	}
	return out.str();
}

std::string formatTimecode(double seconds, int fps) {
	const int safeFps = std::max(1, fps);
	const double clamped = std::max(0.0, seconds);
	const int totalFrames = static_cast<int>(std::llround(clamped * static_cast<double>(safeFps)));
	const int frames = totalFrames % safeFps;
	const int totalSeconds = totalFrames / safeFps;
	const int secs = totalSeconds % 60;
	const int totalMinutes = totalSeconds / 60;
	const int mins = totalMinutes % 60;
	const int hours = totalMinutes / 60;
	std::ostringstream out;
	out << std::setfill('0')
		<< std::setw(2) << hours << ':'
		<< std::setw(2) << mins << ':'
		<< std::setw(2) << secs << ':'
		<< std::setw(2) << frames;
	return out.str();
}

std::string formatSubtitleTimestamp(double seconds, bool webVttStyle) {
	const int totalMs = static_cast<int>(std::llround(std::max(0.0, seconds) * 1000.0));
	const int millis = totalMs % 1000;
	const int totalSeconds = totalMs / 1000;
	const int secs = totalSeconds % 60;
	const int totalMinutes = totalSeconds / 60;
	const int mins = totalMinutes % 60;
	const int hours = totalMinutes / 60;
	std::ostringstream out;
	out << std::setfill('0')
		<< std::setw(2) << hours << ':'
		<< std::setw(2) << mins << ':'
		<< std::setw(2) << secs
		<< (webVttStyle ? '.' : ',')
		<< std::setw(3) << millis;
	return out.str();
}

bool isStopWord(const std::string & token) {
	static const std::unordered_set<std::string> stopWords = {
		"a", "an", "and", "are", "as", "at", "be", "but", "by", "for", "from", "has",
		"he", "in", "is", "it", "its", "of", "on", "or", "that", "the", "their", "there",
		"they", "this", "to", "was", "were", "will", "with", "you", "your", "into", "over"
	};
	return stopWords.find(token) != stopWords.end();
}

bool clipsRespectSpacing(
	const ofxGgmlMontageSegment & candidate,
	const std::vector<size_t> & selectedIndices,
	const std::vector<ofxGgmlMontageSegment> & segments,
	double minSpacingSeconds) {
	if (minSpacingSeconds <= 0.0) {
		return true;
	}
	for (size_t selectedIndex : selectedIndices) {
		if (selectedIndex >= segments.size()) {
			continue;
		}
		const auto & selected = segments[selectedIndex];
		const bool separated =
			candidate.endSeconds + minSpacingSeconds <= selected.startSeconds ||
			candidate.startSeconds >= selected.endSeconds + minSpacingSeconds;
		if (!separated) {
			return false;
		}
	}
	return true;
}

std::string sanitizeEdlTitle(const std::string & value) {
	const std::string collapsed = collapseWhitespace(value);
	if (collapsed.empty()) {
		return "MONTAGE";
	}

	std::string sanitized;
	sanitized.reserve(collapsed.size());
	for (char c : collapsed) {
		if (c == '\r' || c == '\n') {
			continue;
		}
		sanitized.push_back(c);
	}
	return sanitized.empty() ? "MONTAGE" : sanitized;
}

std::string sanitizeSubtitleText(const std::string & text) {
	std::string sanitized = trimMontageCopy(text);
	for (char & c : sanitized) {
		if (c == '\r') {
			c = '\n';
		}
	}
	return sanitized;
}

std::string chooseThemeBucket(
	const std::vector<std::string> & goalTokens,
	const std::unordered_set<std::string> & goalLookup,
	const ofxGgmlMontageSegment & segment) {
	for (const auto & token : goalTokens) {
		if (token.size() < 4) {
			continue;
		}
		const auto & segmentTokens = segment.keywords;
		if (std::find(segmentTokens.begin(), segmentTokens.end(), token) != segmentTokens.end()) {
			return token;
		}
	}
	for (const auto & token : segment.keywords) {
		if (goalLookup.find(token) != goalLookup.end() && token.size() >= 4) {
			return token;
		}
	}
	return segment.keywords.empty() ? std::string("general") : segment.keywords.front();
}

std::string suggestTransition(
	const ofxGgmlMontageClip * previousClip,
	const ofxGgmlMontageClip & clip) {
	if (previousClip == nullptr) {
		return "Open clean on the first strong beat.";
	}
	const double sourceGapSeconds = clip.startSeconds - previousClip->endSeconds;
	if (!previousClip->themeBucket.empty() &&
		previousClip->themeBucket == clip.themeBucket) {
		if (sourceGapSeconds <= 2.0) {
			return "Use a match cut or rhythmic straight cut to keep the motif flowing.";
		}
		return "Use an audio bridge or L-cut to reconnect the recurring motif.";
	}
	if (sourceGapSeconds >= 8.0) {
		return "Use a contrast cut with a short audio lead to signal the time jump.";
	}
	if (sourceGapSeconds >= 3.0) {
		return "Use a quick dip or breath frame between the motif change.";
	}
	return "Use a straight cut on motion or cadence.";
}

ofxGgmlMontageMatch scoreSegmentAgainstGoalTokens(
	const std::string & trimmedGoal,
	const std::vector<std::string> & goalTokens,
	const std::unordered_set<std::string> & goalLookup,
	const ofxGgmlMontageSegment & segment,
	size_t segmentIndex) {
	ofxGgmlMontageMatch match;
	match.segmentIndex = segmentIndex;

	const std::vector<std::string> segmentTokens = segment.keywords.empty()
		? ofxGgmlMontagePlanner::extractKeywords(segment.text)
		: segment.keywords;

	if (goalTokens.empty() || segmentTokens.empty()) {
		match.totalScore = 0.0;
		match.rationale = "No meaningful token overlap available.";
		return match;
	}

	const std::unordered_set<std::string> segmentLookup = toTokenLookup(segmentTokens);

	size_t intersectionCount = 0;
	std::vector<std::string> shared;
	shared.reserve(std::min(goalTokens.size(), segmentTokens.size()));
	for (const auto & token : goalTokens) {
		if (segmentLookup.find(token) != segmentLookup.end()) {
			++intersectionCount;
			shared.push_back(token);
		}
	}

	const double coverage =
		goalLookup.empty() ? 0.0
		                   : static_cast<double>(intersectionCount) / static_cast<double>(goalLookup.size());
	const double diceDenominator = static_cast<double>(goalLookup.size() + segmentLookup.size());
	const double lexical =
		diceDenominator <= 0.0 ? 0.0
		                       : (2.0 * static_cast<double>(intersectionCount)) / diceDenominator;
	const double phraseBoost =
		trimmedGoal.empty() || segment.text.find(trimmedGoal) == std::string::npos
			? 0.0
			: 0.15;

	match.coverageScore = coverage;
	match.lexicalScore = lexical;
	match.totalScore = std::clamp((coverage * 0.65) + (lexical * 0.35) + phraseBoost, 0.0, 1.0);
	match.rationale = shared.empty()
		? "Weak subtitle overlap."
		: "Shared terms: " + joinStrings(shared, ", ");
	return match;
}

} // namespace

std::vector<ofxGgmlMontageSegment> ofxGgmlMontagePlanner::segmentsFromSpeechSegments(
	const std::vector<ofxGgmlSpeechSegment> & segments,
	const std::string & reelName) {
	std::vector<ofxGgmlMontageSegment> result;
	result.reserve(segments.size());
	const std::string normalizedReel = sanitizeReelName(reelName, "AX");
	for (size_t i = 0; i < segments.size(); ++i) {
		const auto & segment = segments[i];
		ofxGgmlMontageSegment montageSegment;
		montageSegment.sourceId = "speech_" + std::to_string(i + 1);
		montageSegment.reelName = normalizedReel;
		montageSegment.startSeconds = std::max(0.0, segment.startSeconds);
		montageSegment.endSeconds = std::max(montageSegment.startSeconds, segment.endSeconds);
		montageSegment.text = collapseWhitespace(segment.text);
		montageSegment.keywords = extractKeywords(montageSegment.text);
		if (!montageSegment.text.empty()) {
			result.push_back(std::move(montageSegment));
		}
	}
	return result;
}

Result<std::vector<ofxGgmlMontageSegment>> ofxGgmlMontagePlanner::loadSegmentsFromSrt(
	const std::string & srtPath,
	const std::string & reelName) {
	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;
	if (!ofxGgmlSimpleSrtSubtitleParser::parseFile(srtPath, cues, error)) {
		return Result<std::vector<ofxGgmlMontageSegment>>(
			ofxGgmlErrorCode::ModelLoadFailed,
			error.empty() ? "Failed to parse subtitle file." : error);
	}

	std::vector<ofxGgmlMontageSegment> result;
	result.reserve(cues.size());
	const std::string normalizedReel = sanitizeReelName(reelName, "AX");
	for (size_t i = 0; i < cues.size(); ++i) {
		const auto & cue = cues[i];
		ofxGgmlMontageSegment segment;
		segment.sourceId = "cue_" + std::to_string(i + 1);
		segment.reelName = normalizedReel;
		segment.startSeconds = std::max(0.0, static_cast<double>(cue.startMs) / 1000.0);
		segment.endSeconds = std::max(segment.startSeconds, static_cast<double>(cue.endMs) / 1000.0);
		segment.text = collapseWhitespace(cue.text);
		segment.keywords = extractKeywords(segment.text);
		if (!segment.text.empty()) {
			result.push_back(std::move(segment));
		}
	}

	if (result.empty()) {
		return Result<std::vector<ofxGgmlMontageSegment>>(
			ofxGgmlErrorCode::InferenceOutputInvalid,
			"Subtitle file did not produce any usable montage segments.");
	}

	return Result<std::vector<ofxGgmlMontageSegment>>(result);
}

std::vector<std::string> ofxGgmlMontagePlanner::extractKeywords(const std::string & text) {
	std::vector<std::string> tokens;
	std::string current;
	current.reserve(24);

	auto flushCurrent = [&]() {
		if (current.size() >= 2 && !isStopWord(current)) {
			tokens.push_back(current);
		}
		current.clear();
	};

	for (char c : text) {
		const unsigned char uc = static_cast<unsigned char>(c);
		if (std::isalnum(uc)) {
			current.push_back(static_cast<char>(std::tolower(uc)));
		} else {
			flushCurrent();
		}
	}
	flushCurrent();

	std::sort(tokens.begin(), tokens.end());
	tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
	return tokens;
}

ofxGgmlMontageMatch ofxGgmlMontagePlanner::scoreSegment(
	const std::string & goal,
	const ofxGgmlMontageSegment & segment,
	size_t segmentIndex) {
	const std::string trimmedGoal = trimMontageCopy(goal);
	const std::vector<std::string> goalTokens = extractKeywords(trimmedGoal);
	const std::unordered_set<std::string> goalLookup = toTokenLookup(goalTokens);
	return scoreSegmentAgainstGoalTokens(trimmedGoal, goalTokens, goalLookup, segment, segmentIndex);
}

ofxGgmlMontagePlannerResult ofxGgmlMontagePlanner::plan(
	const ofxGgmlMontagePlannerRequest & request) {
	ofxGgmlMontagePlannerResult result;
	const auto started = std::chrono::steady_clock::now();

	const std::string goal = trimMontageCopy(request.goal);
	if (goal.empty()) {
		result.error = "Montage goal is empty.";
		return result;
	}
	if (request.segments.empty()) {
		result.error = "No subtitle segments were provided for montage planning.";
		return result;
	}
	const std::vector<std::string> goalTokens = extractKeywords(goal);
	if (goalTokens.empty()) {
		result.error = "Montage goal needs at least one meaningful keyword.";
		return result;
	}
	const std::unordered_set<std::string> goalLookup = toTokenLookup(goalTokens);

	ofxGgmlMontagePlan plan;
	plan.goal = goal;
	plan.strategy = request.preserveChronology
		? "Select the strongest subtitle-related clips while keeping the final montage in source order."
		: "Select the strongest subtitle-related clips regardless of original ordering.";
	plan.recurringKeywords = extractKeywords(goal);

	std::vector<ofxGgmlMontageMatch> matches;
	matches.reserve(request.segments.size());
	for (size_t i = 0; i < request.segments.size(); ++i) {
		auto match = scoreSegmentAgainstGoalTokens(goal, goalTokens, goalLookup, request.segments[i], i);
		if (match.totalScore >= request.minScore) {
			matches.push_back(std::move(match));
		}
	}

	if (matches.empty()) {
		result.error = "No subtitle segments met the montage similarity threshold.";
		return result;
	}

	std::sort(matches.begin(), matches.end(), [](const ofxGgmlMontageMatch & a, const ofxGgmlMontageMatch & b) {
		if (a.totalScore == b.totalScore) {
			return a.segmentIndex < b.segmentIndex;
		}
		return a.totalScore > b.totalScore;
	});

	const size_t clipCount = std::clamp<size_t>(request.maxClips, 1, matches.size());
	std::vector<ofxGgmlMontageMatch> selectedMatches;
	selectedMatches.reserve(clipCount);
	std::vector<size_t> selectedSegmentIndices;
	selectedSegmentIndices.reserve(clipCount);
	std::unordered_set<std::string> usedThemeBuckets;
	double selectedDurationSeconds = 0.0;
	const double targetDurationSeconds = std::max(0.0, request.targetDurationSeconds);
	for (const auto & match : matches) {
		if (selectedMatches.size() >= clipCount) {
			break;
		}
		if (match.segmentIndex >= request.segments.size()) {
			continue;
		}
		if (!clipsRespectSpacing(
				request.segments[match.segmentIndex],
				selectedSegmentIndices,
				request.segments,
				std::max(0.0, request.minSpacingSeconds))) {
			continue;
		}
		const auto & candidateSegment = request.segments[match.segmentIndex];
		const std::string themeBucket =
			chooseThemeBucket(goalTokens, goalLookup, candidateSegment);
		const double handledDurationSeconds =
			std::max(0.0, candidateSegment.endSeconds - candidateSegment.startSeconds) +
			std::max(0.0, request.preRollSeconds) +
			std::max(0.0, request.postRollSeconds);
		if (targetDurationSeconds > 0.0 &&
			!selectedMatches.empty() &&
			selectedDurationSeconds >= targetDurationSeconds &&
			usedThemeBuckets.find(themeBucket) != usedThemeBuckets.end()) {
			continue;
		}
		selectedSegmentIndices.push_back(match.segmentIndex);
		selectedMatches.push_back(match);
		usedThemeBuckets.insert(themeBucket);
		selectedDurationSeconds += handledDurationSeconds;
		if (targetDurationSeconds > 0.0 &&
			selectedDurationSeconds >= targetDurationSeconds &&
			selectedMatches.size() >= 2) {
			break;
		}
	}
	if (selectedMatches.empty()) {
		result.error = "No subtitle segments met the montage spacing and similarity rules.";
		return result;
	}
	matches = std::move(selectedMatches);
	if (request.preserveChronology) {
		std::sort(matches.begin(), matches.end(), [](const ofxGgmlMontageMatch & a, const ofxGgmlMontageMatch & b) {
			return a.segmentIndex < b.segmentIndex;
		});
	}

	plan.matches = matches;
	ofxGgmlMontageClip * previousClip = nullptr;
	for (size_t i = 0; i < matches.size(); ++i) {
		const auto & match = matches[i];
		const auto & segment = request.segments[match.segmentIndex];
		ofxGgmlMontageClip clip;
		clip.index = static_cast<int>(i + 1);
		clip.sourceId = segment.sourceId;
		clip.reelName = sanitizeReelName(segment.reelName, request.fallbackReelName);
		clip.startSeconds = std::max(0.0, segment.startSeconds - std::max(0.0, request.preRollSeconds));
		clip.endSeconds = std::max(
			clip.startSeconds,
			segment.endSeconds + std::max(0.0, request.postRollSeconds));
		clip.score = match.totalScore;
		clip.clipName = "SEG_" + std::to_string(match.segmentIndex + 1);
		clip.note = segment.text;
		clip.themeBucket = chooseThemeBucket(goalTokens, goalLookup, segment);
		clip.transitionSuggestion = suggestTransition(previousClip, clip);
		plan.clips.push_back(std::move(clip));
		previousClip = &plan.clips.back();
	}

	plan.notes.push_back("Heuristic montage selected from subtitle similarity against the edit goal.");
	plan.notes.push_back(request.preserveChronology
		? "Clips are ordered chronologically after relevance filtering."
		: "Clips are ordered by similarity score.");
	plan.notes.push_back("Selected " + std::to_string(plan.clips.size()) + " clip(s) from " + std::to_string(request.segments.size()) + " subtitle segments.");
	if (request.minSpacingSeconds > 0.0) {
		std::ostringstream note;
		note << "Enforced a minimum spacing of "
			 << std::fixed << std::setprecision(2)
			 << request.minSpacingSeconds
			 << " s between selected subtitle moments to improve visual variety.";
		plan.notes.push_back(note.str());
	}
	if (request.preRollSeconds > 0.0 || request.postRollSeconds > 0.0) {
		std::ostringstream note;
		note << "Applied visual context handles";
		if (request.preRollSeconds > 0.0) {
			note << " (pre-roll " << std::fixed << std::setprecision(2) << request.preRollSeconds << " s";
			if (request.postRollSeconds > 0.0) {
				note << ", ";
			} else {
				note << ")";
			}
		}
		if (request.postRollSeconds > 0.0) {
			if (request.preRollSeconds <= 0.0) {
				note << " (";
			}
			note << "post-roll " << std::fixed << std::setprecision(2) << request.postRollSeconds << " s)";
		}
		plan.notes.push_back(note.str());
	}
	if (targetDurationSeconds > 0.0) {
		std::ostringstream note;
		note << "Planned toward a target montage duration of "
			 << std::fixed << std::setprecision(2)
			 << targetDurationSeconds
			 << " s.";
		plan.notes.push_back(note.str());
	}
	if (!plan.clips.empty()) {
		std::unordered_set<std::string> buckets;
		for (const auto & clip : plan.clips) {
			if (!clip.themeBucket.empty()) {
				buckets.insert(clip.themeBucket);
			}
		}
		if (!buckets.empty()) {
			std::vector<std::string> sortedBuckets(buckets.begin(), buckets.end());
			std::sort(sortedBuckets.begin(), sortedBuckets.end());
			plan.notes.push_back(
				"Theme buckets in play: " + joinStrings(sortedBuckets, ", ") + ".");
		}
	}
	{
		std::ostringstream note;
		note << "Estimated montage duration: "
			 << std::fixed << std::setprecision(2)
			 << computePlanDurationSeconds(plan)
			 << " s.";
		plan.notes.push_back(note.str());
	}
	plan.notes.push_back("Review durations and transitions before final conform.");

	for (const auto & token : plan.recurringKeywords) {
		if (token.size() >= 4) {
			plan.notes.push_back("Consider a recurring visual or caption motif around \"" + token + "\".");
			break;
		}
	}

	result.success = true;
	result.plan = std::move(plan);
	result.elapsedMs = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - started).count();
	return result;
}

std::string ofxGgmlMontagePlanner::summarizePlan(const ofxGgmlMontagePlan & plan) {
	std::ostringstream out;
	out << "Montage plan with " << plan.clips.size() << " selected clip(s)";
	if (!plan.goal.empty()) {
		out << "\nGoal: " << plan.goal;
	}
	if (!plan.strategy.empty()) {
		out << "\nStrategy: " << plan.strategy;
	}
	if (!plan.clips.empty()) {
		out << "\nClips:";
		for (const auto & clip : plan.clips) {
			out << "\n" << clip.index << ". "
				<< formatTimecode(clip.startSeconds, 25) << " - "
				<< formatTimecode(clip.endSeconds, 25)
				<< " | score " << std::fixed << std::setprecision(2) << clip.score;
			if (!clip.themeBucket.empty()) {
				out << " | theme " << clip.themeBucket;
			}
			if (!clip.note.empty()) {
				out << " | " << clip.note;
			}
		}
	}
	if (!plan.recurringKeywords.empty()) {
		const size_t keywordCount = std::min<size_t>(plan.recurringKeywords.size(), 6);
		std::vector<std::string> topKeywords(
			plan.recurringKeywords.begin(),
			plan.recurringKeywords.begin() + static_cast<std::ptrdiff_t>(keywordCount));
		out << "\nKeywords: " << joinStrings(topKeywords, ", ");
	}
	return out.str();
}

std::string ofxGgmlMontagePlanner::buildEditorBrief(const ofxGgmlMontagePlan & plan) {
	std::ostringstream out;
	out << "Subtitle-driven montage brief";
	if (!plan.goal.empty()) {
		out << "\nGoal: " << plan.goal;
	}
	if (!plan.strategy.empty()) {
		out << "\nDirection: " << plan.strategy;
	}
	if (!plan.notes.empty()) {
		out << "\nNotes:";
		for (const auto & note : plan.notes) {
			out << "\n- " << note;
		}
	}
	if (!plan.clips.empty()) {
		out << "\nSelected clips:";
		for (const auto & clip : plan.clips) {
			out << "\n- " << clip.reelName
				<< " " << formatTimecode(clip.startSeconds, 25)
				<< " - " << formatTimecode(clip.endSeconds, 25);
			if (!clip.themeBucket.empty()) {
				out << " | theme: " << clip.themeBucket;
			}
			if (!clip.note.empty()) {
				out << " | " << clip.note;
			}
			if (!clip.transitionSuggestion.empty()) {
				out << "\n  transition: " << clip.transitionSuggestion;
			}
		}
	}
	return out.str();
}

ofxGgmlMontageSubtitleTrack ofxGgmlMontagePlanner::buildSubtitleTrack(
	const ofxGgmlMontagePlan & plan,
	const std::string & title) {
	ofxGgmlMontageSubtitleTrack track;
	track.title = sanitizeEdlTitle(title);

	double cursorSeconds = 0.0;
	track.cues.reserve(plan.clips.size());
	for (size_t i = 0; i < plan.clips.size(); ++i) {
		const auto & clip = plan.clips[i];
		const double duration = std::max(0.0, clip.endSeconds - clip.startSeconds);
		if (duration <= 0.0) {
			continue;
		}

		ofxGgmlMontageSubtitleCue cue;
		cue.index = static_cast<int>(track.cues.size() + 1);
		cue.sourceId = clip.sourceId;
		cue.reelName = clip.reelName;
		cue.startSeconds = cursorSeconds;
		cue.endSeconds = cursorSeconds + duration;
		cue.text = sanitizeSubtitleText(clip.note);
		if (cue.text.empty()) {
			cue.text = clip.clipName.empty() ? ("Clip " + std::to_string(i + 1)) : clip.clipName;
		}
		track.cues.push_back(std::move(cue));
		cursorSeconds += duration;
	}

	return track;
}

ofxGgmlMontageSubtitleTrack ofxGgmlMontagePlanner::buildSourceSubtitleTrack(
	const ofxGgmlMontagePlan & plan,
	const std::string & title) {
	ofxGgmlMontageSubtitleTrack track;
	track.title = sanitizeEdlTitle(title);
	track.cues.reserve(plan.clips.size());
	for (size_t i = 0; i < plan.clips.size(); ++i) {
		const auto & clip = plan.clips[i];
		const double duration = std::max(0.0, clip.endSeconds - clip.startSeconds);
		if (duration <= 0.0) {
			continue;
		}

		ofxGgmlMontageSubtitleCue cue;
		cue.index = static_cast<int>(track.cues.size() + 1);
		cue.sourceId = clip.sourceId;
		cue.reelName = clip.reelName;
		cue.startSeconds = clip.startSeconds;
		cue.endSeconds = clip.endSeconds;
		cue.text = sanitizeSubtitleText(clip.note);
		if (cue.text.empty()) {
			cue.text = clip.clipName.empty() ? ("Clip " + std::to_string(i + 1)) : clip.clipName;
		}
		track.cues.push_back(std::move(cue));
	}

	std::sort(track.cues.begin(), track.cues.end(), [](const ofxGgmlMontageSubtitleCue & a, const ofxGgmlMontageSubtitleCue & b) {
		if (a.startSeconds == b.startSeconds) {
			return a.endSeconds < b.endSeconds;
		}
		return a.startSeconds < b.startSeconds;
	});
	for (size_t i = 0; i < track.cues.size(); ++i) {
		track.cues[i].index = static_cast<int>(i + 1);
	}

	return track;
}

std::string ofxGgmlMontagePlanner::buildSrt(const ofxGgmlMontageSubtitleTrack & track) {
	std::ostringstream out;
	for (size_t i = 0; i < track.cues.size(); ++i) {
		const auto & cue = track.cues[i];
		out << (i + 1) << "\n"
			<< formatSubtitleTimestamp(cue.startSeconds, false)
			<< " --> "
			<< formatSubtitleTimestamp(cue.endSeconds, false) << "\n"
			<< cue.text << "\n";
		if (i + 1 < track.cues.size()) {
			out << "\n";
		}
	}
	return out.str();
}

std::string ofxGgmlMontagePlanner::buildVtt(const ofxGgmlMontageSubtitleTrack & track) {
	std::ostringstream out;
	out << "WEBVTT";
	if (!track.title.empty()) {
		out << " - " << track.title;
	}
	out << "\n\n";
	for (size_t i = 0; i < track.cues.size(); ++i) {
		const auto & cue = track.cues[i];
		out << (i + 1) << "\n"
			<< formatSubtitleTimestamp(cue.startSeconds, true)
			<< " --> "
			<< formatSubtitleTimestamp(cue.endSeconds, true) << "\n"
			<< cue.text << "\n";
		if (i + 1 < track.cues.size()) {
			out << "\n";
		}
	}
	return out.str();
}

std::string ofxGgmlMontagePlanner::buildEdl(
	const ofxGgmlMontagePlan & plan,
	const std::string & title,
	int fps) {
	std::ostringstream edl;
	edl << "TITLE: " << sanitizeEdlTitle(title) << "\n";
	edl << "FCM: NON-DROP FRAME\n\n";

	double recordCursorSeconds = 0.0;
	for (size_t i = 0; i < plan.clips.size(); ++i) {
		const auto & clip = plan.clips[i];
		const double duration = std::max(0.0, clip.endSeconds - clip.startSeconds);
		const double recordOut = recordCursorSeconds + duration;
		edl << std::setfill('0') << std::setw(3) << (i + 1)
			<< "  " << sanitizeReelName(clip.reelName, "AX")
			<< "       V     C        "
			<< formatTimecode(clip.startSeconds, fps) << ' '
			<< formatTimecode(clip.endSeconds, fps) << ' '
			<< formatTimecode(recordCursorSeconds, fps) << ' '
			<< formatTimecode(recordOut, fps) << "\n";
		if (!clip.clipName.empty()) {
			edl << "* FROM CLIP NAME: " << clip.clipName << "\n";
		}
		if (!clip.note.empty()) {
			edl << "* COMMENT: " << clip.note << "\n";
		}
		if (!clip.themeBucket.empty()) {
			edl << "* THEME: " << clip.themeBucket << "\n";
		}
		if (!clip.transitionSuggestion.empty()) {
			edl << "* TRANSITION: " << clip.transitionSuggestion << "\n";
		}
		recordCursorSeconds = recordOut;
	}
	return edl.str();
}

double ofxGgmlMontagePlanner::computePlanDurationSeconds(const ofxGgmlMontagePlan & plan) {
	double duration = 0.0;
	for (const auto & clip : plan.clips) {
		duration += std::max(0.0, clip.endSeconds - clip.startSeconds);
	}
	return duration;
}
