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

bool isStopWord(const std::string & token) {
	static const std::unordered_set<std::string> stopWords = {
		"a", "an", "and", "are", "as", "at", "be", "but", "by", "for", "from", "has",
		"he", "in", "is", "it", "its", "of", "on", "or", "that", "the", "their", "there",
		"they", "this", "to", "was", "were", "will", "with", "you", "your", "into", "over"
	};
	return stopWords.find(token) != stopWords.end();
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
	matches.resize(clipCount);
	if (request.preserveChronology) {
		std::sort(matches.begin(), matches.end(), [](const ofxGgmlMontageMatch & a, const ofxGgmlMontageMatch & b) {
			return a.segmentIndex < b.segmentIndex;
		});
	}

	plan.matches = matches;
	for (size_t i = 0; i < matches.size(); ++i) {
		const auto & match = matches[i];
		const auto & segment = request.segments[match.segmentIndex];
		ofxGgmlMontageClip clip;
		clip.index = static_cast<int>(i + 1);
		clip.sourceId = segment.sourceId;
		clip.reelName = sanitizeReelName(segment.reelName, request.fallbackReelName);
		clip.startSeconds = segment.startSeconds;
		clip.endSeconds = segment.endSeconds;
		clip.score = match.totalScore;
		clip.clipName = "SEG_" + std::to_string(match.segmentIndex + 1);
		clip.note = segment.text;
		plan.clips.push_back(std::move(clip));
	}

	plan.notes.push_back("Heuristic montage selected from subtitle similarity against the edit goal.");
	plan.notes.push_back(request.preserveChronology
		? "Clips are ordered chronologically after relevance filtering."
		: "Clips are ordered by similarity score.");
	plan.notes.push_back("Selected " + std::to_string(plan.clips.size()) + " clip(s) from " + std::to_string(request.segments.size()) + " subtitle segments.");
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
			if (!clip.note.empty()) {
				out << " | " << clip.note;
			}
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
		recordCursorSeconds = recordOut;
	}
	return edl.str();
}
