#pragma once

#include "core/ofxGgmlResult.h"
#include "inference/ofxGgmlSpeechInference.h"

#include <string>
#include <vector>

struct ofxGgmlMontageSegment {
	std::string sourceId;
	std::string reelName;
	double startSeconds = 0.0;
	double endSeconds = 0.0;
	std::string text;
	std::vector<std::string> keywords;
};

struct ofxGgmlMontageMatch {
	size_t segmentIndex = 0;
	double lexicalScore = 0.0;
	double coverageScore = 0.0;
	double totalScore = 0.0;
	std::string rationale;
};

struct ofxGgmlMontageClip {
	int index = 0;
	std::string sourceId;
	std::string reelName;
	double startSeconds = 0.0;
	double endSeconds = 0.0;
	double score = 0.0;
	std::string clipName;
	std::string note;
	std::string themeBucket;
	std::string transitionSuggestion;
	std::string sourceFilePath;
	std::string audioTrack = "A";
	int transitionDurationFrames = 0;
};

struct ofxGgmlMontageSubtitleCue {
	int index = 0;
	std::string sourceId;
	std::string reelName;
	double startSeconds = 0.0;
	double endSeconds = 0.0;
	std::string text;
};

struct ofxGgmlMontageSubtitleTrack {
	std::string title;
	std::vector<ofxGgmlMontageSubtitleCue> cues;
};

struct ofxGgmlMontagePlan {
	std::string goal;
	std::string strategy;
	std::vector<std::string> notes;
	std::vector<std::string> recurringKeywords;
	std::vector<ofxGgmlMontageMatch> matches;
	std::vector<ofxGgmlMontageClip> clips;
};

struct ofxGgmlMontagePlannerRequest {
	std::string goal;
	std::vector<ofxGgmlMontageSegment> segments;
	size_t maxClips = 8;
	double minScore = 0.18;
	double minSpacingSeconds = 0.0;
	double preRollSeconds = 0.0;
	double postRollSeconds = 0.0;
	double targetDurationSeconds = 0.0;
	bool preserveChronology = true;
	std::string fallbackReelName = "AX";
	std::string sourceFilePath;
	bool dropFrameTimecode = false;
};

struct ofxGgmlMontagePlannerResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string error;
	ofxGgmlMontagePlan plan;
};

class ofxGgmlMontagePlanner {
public:
	static std::vector<ofxGgmlMontageSegment> segmentsFromSpeechSegments(
		const std::vector<ofxGgmlSpeechSegment> & segments,
		const std::string & reelName = "AX");
	static Result<std::vector<ofxGgmlMontageSegment>> loadSegmentsFromSrt(
		const std::string & srtPath,
		const std::string & reelName = "AX");
	static std::vector<std::string> extractKeywords(const std::string & text);
	static ofxGgmlMontageMatch scoreSegment(
		const std::string & goal,
		const ofxGgmlMontageSegment & segment,
		size_t segmentIndex);
	static ofxGgmlMontagePlannerResult plan(
		const ofxGgmlMontagePlannerRequest & request);
	static std::string summarizePlan(const ofxGgmlMontagePlan & plan);
	static std::string buildEditorBrief(const ofxGgmlMontagePlan & plan);
	static ofxGgmlMontageSubtitleTrack buildSubtitleTrack(
		const ofxGgmlMontagePlan & plan,
		const std::string & title = "MONTAGE");
	static ofxGgmlMontageSubtitleTrack buildSourceSubtitleTrack(
		const ofxGgmlMontagePlan & plan,
		const std::string & title = "MONTAGE");
	static std::string buildSrt(const ofxGgmlMontageSubtitleTrack & track);
	static std::string buildVtt(const ofxGgmlMontageSubtitleTrack & track);
	static std::string buildEdl(
		const ofxGgmlMontagePlan & plan,
		const std::string & title = "MONTAGE",
		int fps = 25,
		bool dropFrame = false);
	static std::string buildEdlWithAudio(
		const ofxGgmlMontagePlan & plan,
		const std::string & title = "MONTAGE",
		int fps = 25,
		bool dropFrame = false);
	static double computePlanDurationSeconds(const ofxGgmlMontagePlan & plan);
};
