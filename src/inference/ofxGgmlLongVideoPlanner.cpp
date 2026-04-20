#include "inference/ofxGgmlLongVideoPlanner.h"

#include "ofJson.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <numeric>
#include <sstream>

namespace {

std::string trimCopy(const std::string & value) {
	size_t start = 0;
	while (start < value.size() &&
		std::isspace(static_cast<unsigned char>(value[start]))) {
		++start;
	}
	size_t end = value.size();
	while (end > start &&
		std::isspace(static_cast<unsigned char>(value[end - 1]))) {
		--end;
	}
	return value.substr(start, end - start);
}

std::string makeChunkId(int index) {
	std::ostringstream output;
	output << "chunk-" << (index + 1);
	return output.str();
}

std::string defaultChunkTitle(int index, int chunkCount) {
	static const std::array<const char *, 8> labels = {
		"Opening",
		"Setup",
		"Development",
		"Turn",
		"Escalation",
		"Climax",
		"Resolution",
		"Outro"
	};
	if (chunkCount <= static_cast<int>(labels.size())) {
		return labels[std::min(index, static_cast<int>(labels.size()) - 1)];
	}
	std::ostringstream output;
	output << "Sequence " << (index + 1);
	return output.str();
}

std::string toLowerCopy(const std::string & value) {
	std::string lowered = value;
	std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return lowered;
}

bool containsToken(const std::string & haystack, const std::string & needle) {
	return toLowerCopy(haystack).find(toLowerCopy(needle)) != std::string::npos;
}

std::vector<std::string> titlesForStructure(
	const ofxGgmlLongVideoPlanRequest & request,
	int chunkCount) {
	const std::string structure = toLowerCopy(request.structureHint);
	std::vector<std::string> titles;
	if (containsToken(structure, "music")) {
		titles = {"Intro", "Verse A", "Pre-Chorus", "Chorus", "Bridge", "Final Chorus", "Outro"};
	} else if (containsToken(structure, "ambient") || containsToken(structure, "loop")) {
		titles = {"Drift In", "Establish", "Variation", "Lift", "Resolve", "Loop Out"};
	} else if (containsToken(structure, "documentary") || containsToken(structure, "essay")) {
		titles = {"Hook", "Context", "Development", "Evidence", "Turn", "Resolution", "Close"};
	} else {
		titles = {"Opening", "Setup", "Development", "Turn", "Escalation", "Climax", "Resolution", "Outro"};
	}

	if (chunkCount <= static_cast<int>(titles.size())) {
		titles.resize(static_cast<size_t>(chunkCount));
		return titles;
	}
	for (int i = static_cast<int>(titles.size()); i < chunkCount; ++i) {
		titles.push_back(defaultChunkTitle(i, chunkCount));
	}
	return titles;
}

std::vector<double> durationWeightsForRequest(
	const ofxGgmlLongVideoPlanRequest & request,
	int chunkCount) {
	std::vector<double> weights(static_cast<size_t>(chunkCount), 1.0);
	const std::string structure = toLowerCopy(request.structureHint);
	const std::string pacing = toLowerCopy(request.pacingProfile);

	if (containsToken(structure, "three-act")) {
		for (int i = 0; i < chunkCount; ++i) {
			const double progress = chunkCount > 1
				? static_cast<double>(i) / static_cast<double>(chunkCount - 1)
				: 0.0;
			if (progress < 0.2) {
				weights[static_cast<size_t>(i)] = 1.1;
			} else if (progress < 0.7) {
				weights[static_cast<size_t>(i)] = 0.95;
			} else if (progress < 0.9) {
				weights[static_cast<size_t>(i)] = 1.25;
			} else {
				weights[static_cast<size_t>(i)] = request.favorLoopableEnding ? 1.1 : 0.9;
			}
		}
	}
	if (containsToken(structure, "music")) {
		for (int i = 0; i < chunkCount; ++i) {
			const double progress = chunkCount > 1
				? static_cast<double>(i) / static_cast<double>(chunkCount - 1)
				: 0.0;
			weights[static_cast<size_t>(i)] =
				(progress > 0.45 && progress < 0.8) ? 1.2 : 0.9;
		}
	}
	if (containsToken(structure, "ambient") || containsToken(structure, "loop")) {
		std::fill(weights.begin(), weights.end(), 1.0);
		if (!weights.empty()) {
			weights.front() = 1.15;
			weights.back() = request.favorLoopableEnding ? 1.15 : 1.0;
		}
	}

	if (containsToken(pacing, "aggressive") || containsToken(pacing, "fast")) {
		for (int i = 0; i < chunkCount; ++i) {
			const double progress = chunkCount > 1
				? static_cast<double>(i) / static_cast<double>(chunkCount - 1)
				: 0.0;
			weights[static_cast<size_t>(i)] *= (progress > 0.55) ? 1.15 : 0.9;
		}
	} else if (containsToken(pacing, "gentle") || containsToken(pacing, "slow")) {
		for (int i = 0; i < chunkCount; ++i) {
			const double progress = chunkCount > 1
				? static_cast<double>(i) / static_cast<double>(chunkCount - 1)
				: 0.0;
			weights[static_cast<size_t>(i)] *= (progress < 0.35) ? 1.15 : 0.95;
		}
	}

	return weights;
}

std::string buildSectionGoal(
	const ofxGgmlLongVideoPlanRequest & request,
	int index,
	int chunkCount) {
	const double progress = chunkCount > 1 ?
		static_cast<double>(index) / static_cast<double>(chunkCount - 1) :
		0.0;
	if (progress <= 0.15) {
		return "Establish the world, subject, and camera grammar for the concept: " + request.conceptText;
	}
	if (progress <= 0.35) {
		return "Develop motion, environment, and stakes while preserving identity and visual grammar.";
	}
	if (progress <= 0.65) {
		return "Push the concept forward with stronger action or escalation, but keep scene continuity believable.";
	}
	if (progress <= 0.85) {
		return "Deliver the most intense or visually revealing beat of the concept without breaking continuity.";
	}
	return "Resolve the motion into a stable final beat that feels like a natural continuation of earlier chunks.";
}

std::string buildTransitionHint(
	const ofxGgmlLongVideoPlanRequest & request,
	int index,
	int chunkCount) {
	if (index + 1 >= chunkCount) {
		return request.favorLoopableEnding
			? "End on a stable composition that can loop seamlessly back toward the opening mood."
			: "Land on a visually resolved composition that feels intentionally final.";
	}
	if (index == 0) {
		return "End on a clear directional cue so the next chunk can inherit motion without a reset.";
	}
	return "Carry subject direction, camera momentum, and lighting logic cleanly into the next chunk.";
}

std::string buildContinuityNote(
	const ofxGgmlLongVideoPlanRequest & request,
	int index,
	int chunkCount) {
	std::ostringstream output;
	output
		<< request.continuityGoal
		<< " Keep wardrobe, subject scale, and environment consistent.";
	if (index > 0) {
		output << " Start from the previous chunk's final frame and avoid abrupt camera or lighting resets.";
	}
	if (index + 1 < chunkCount) {
		output << " End on a frame that can hand off naturally into the next chunk.";
	}
	return output.str();
}

std::vector<ofxGgmlLongVideoPlanChunk> buildHeuristicChunks(
	const ofxGgmlLongVideoPlanRequest & request) {
	std::vector<ofxGgmlLongVideoPlanChunk> chunks;
	const int chunkCount = std::max(1, request.chunkCount);
	chunks.reserve(static_cast<size_t>(chunkCount));
	const std::vector<std::string> titles = titlesForStructure(request, chunkCount);
	const std::vector<double> weights = durationWeightsForRequest(request, chunkCount);
	const double totalWeight = std::accumulate(weights.begin(), weights.end(), 0.0);
	const double minDuration = std::min(8.0, std::max(2.5, request.targetDurationSeconds * 0.08));
	double cursor = 0.0;
	for (int i = 0; i < chunkCount; ++i) {
		ofxGgmlLongVideoPlanChunk chunk;
		chunk.index = i;
		chunk.id = makeChunkId(i);
		chunk.title = titles[static_cast<size_t>(i)];
		chunk.sectionGoal = buildSectionGoal(request, i, chunkCount);
		chunk.continuityNote = buildContinuityNote(request, i, chunkCount);
		chunk.transitionHint = buildTransitionHint(request, i, chunkCount);
		chunk.progressionWeight =
			totalWeight > 0.0 ? weights[static_cast<size_t>(i)] / totalWeight : 1.0;
		chunk.targetDurationSeconds = std::max(
			minDuration,
			request.targetDurationSeconds * chunk.progressionWeight);
		chunk.startSeconds = cursor;
		cursor += chunk.targetDurationSeconds;
		chunk.endSeconds = cursor;
		chunk.width = request.width;
		chunk.height = request.height;
		chunk.fps = request.fps;
		chunk.frameCount = request.framesPerChunk;
		chunk.seed = request.seed >= 0 ? request.seed + i : -1;
		chunk.usePreviousLastFrame = request.usePromptInheritance && i > 0;

		std::ostringstream prompt;
		prompt
			<< request.conceptText
			<< ". Style: " << request.style
			<< ". Chunk title: " << chunk.title
			<< ". Goal: " << chunk.sectionGoal
			<< ". Timing: " << chunk.startSeconds << "s to " << chunk.endSeconds << "s"
			<< ". Continuity: " << chunk.continuityNote
			<< ". Transition: " << chunk.transitionHint;
		chunk.prompt = prompt.str();
		chunk.negativePrompt = request.negativeStyle;
		chunks.push_back(std::move(chunk));
	}
	return chunks;
}

std::string buildContinuityBibleText(
	const ofxGgmlLongVideoPlanRequest & request,
	const std::vector<ofxGgmlLongVideoPlanChunk> & chunks) {
	std::ostringstream output;
	output
		<< "Concept: " << trimCopy(request.conceptText) << "\n"
		<< "Visual style: " << trimCopy(request.style) << "\n"
		<< "Tone: " << trimCopy(request.tone) << "\n"
		<< "Structure: " << trimCopy(request.structureHint) << "\n"
		<< "Pacing: " << trimCopy(request.pacingProfile) << "\n"
		<< "Continuity rule: " << trimCopy(request.continuityGoal) << "\n"
		<< "Render target: " << request.width << "x" << request.height
		<< " at " << request.fps << " fps, "
		<< request.framesPerChunk << " frames per chunk.\n\n"
		<< "Chunk continuity notes:\n";
	for (const auto & chunk : chunks) {
		output
			<< (chunk.index + 1) << ". "
			<< chunk.title << " [" << chunk.startSeconds << "s -> " << chunk.endSeconds << "s]: "
			<< chunk.continuityNote
			<< " Transition: " << chunk.transitionHint << "\n";
	}
	return trimCopy(output.str());
}

} // namespace

ofxGgmlTextAssistant & ofxGgmlLongVideoPlanner::getTextAssistant() {
	return m_textAssistant;
}

const ofxGgmlTextAssistant & ofxGgmlLongVideoPlanner::getTextAssistant() const {
	return m_textAssistant;
}

ofxGgmlLongVideoPlanValidation ofxGgmlLongVideoPlanner::validateRequest(
	const ofxGgmlLongVideoPlanRequest & request) {
	ofxGgmlLongVideoPlanValidation validation;
	if (trimCopy(request.conceptText).empty()) {
		validation.ok = false;
		validation.errors.push_back("Concept is empty.");
	}
	if (request.targetDurationSeconds <= 0.0) {
		validation.ok = false;
		validation.errors.push_back("Target duration must be greater than zero.");
	}
	if (request.chunkCount <= 0) {
		validation.ok = false;
		validation.errors.push_back("Chunk count must be greater than zero.");
	}
	if (request.width <= 0 || request.height <= 0) {
		validation.ok = false;
		validation.errors.push_back("Width and height must be greater than zero.");
	}
	if (request.fps <= 0) {
		validation.ok = false;
		validation.errors.push_back("FPS must be greater than zero.");
	}
	if (request.framesPerChunk <= 0) {
		validation.ok = false;
		validation.errors.push_back("Frames per chunk must be greater than zero.");
	}
	if (request.chunkCount > 16) {
		validation.warnings.push_back("Very high chunk counts can make continuity management harder.");
	}
	if (request.chunkCount == 1) {
		validation.warnings.push_back("A single chunk behaves more like a clip than a long-form chunked workflow.");
	}
	if (request.targetDurationSeconds / std::max(1, request.chunkCount) < 4.0) {
		validation.warnings.push_back("Average chunk duration is very short; transitions may feel abrupt.");
	}
	if (trimCopy(request.structureHint).empty()) {
		validation.warnings.push_back("Structure hint is empty. The plan may feel flatter than intended.");
	}
	if (trimCopy(request.pacingProfile).empty()) {
		validation.warnings.push_back("Pacing profile is empty. Chunk timing will rely on defaults.");
	}
	return validation;
}

std::string ofxGgmlLongVideoPlanner::buildPlanningPrompt(
	const ofxGgmlLongVideoPlanRequest & request) {
	std::ostringstream prompt;
	prompt
		<< "Plan a long-form video as a chunked native rendering workflow.\n"
		<< "Concept: " << trimCopy(request.conceptText) << "\n"
		<< "Style: " << trimCopy(request.style) << "\n"
		<< "Tone: " << trimCopy(request.tone) << "\n"
		<< "Structure: " << trimCopy(request.structureHint) << "\n"
		<< "Pacing: " << trimCopy(request.pacingProfile) << "\n"
		<< "Target duration: " << request.targetDurationSeconds << " seconds\n"
		<< "Chunk count: " << request.chunkCount << "\n"
		<< "Continuity goal: " << trimCopy(request.continuityGoal) << "\n"
		<< "Render target: " << request.width << "x" << request.height
		<< ", " << request.fps << " fps, "
		<< request.framesPerChunk << " frames per chunk.\n"
		<< "For each chunk, produce a title, one-sentence goal, continuity note, prompt, and negative prompt.\n";
	return prompt.str();
}

std::string ofxGgmlLongVideoPlanner::buildManifestJson(
	const ofxGgmlLongVideoPlanRequest & request,
	const std::vector<ofxGgmlLongVideoPlanChunk> & chunks,
	const std::string & continuityBible) {
	ofJson root;
	root["project_type"] = "long_video_plan";
	root["concept"] = request.conceptText;
	root["style"] = request.style;
	root["negative_style"] = request.negativeStyle;
	root["tone"] = request.tone;
	root["structure_hint"] = request.structureHint;
	root["pacing_profile"] = request.pacingProfile;
	root["continuity_goal"] = request.continuityGoal;
	root["target_duration_seconds"] = request.targetDurationSeconds;
	root["chunk_count"] = request.chunkCount;
	root["width"] = request.width;
	root["height"] = request.height;
	root["fps"] = request.fps;
	root["frames_per_chunk"] = request.framesPerChunk;
	root["seed"] = request.seed;
	root["render_backend"] = request.renderBackend;
	root["render_model"] = {
		{"name", request.renderModelName},
		{"path", request.renderModelPath},
		{"url", request.renderModelUrl}
	};
	root["continuity_bible"] = continuityBible;

	ofJson chunkArray = ofJson::array();
	for (const auto & chunk : chunks) {
		ofJson chunkJson;
		chunkJson["index"] = chunk.index;
		chunkJson["id"] = chunk.id;
		chunkJson["title"] = chunk.title;
		chunkJson["section_goal"] = chunk.sectionGoal;
		chunkJson["continuity_note"] = chunk.continuityNote;
		chunkJson["transition_hint"] = chunk.transitionHint;
		chunkJson["prompt"] = chunk.prompt;
		chunkJson["negative_prompt"] = chunk.negativePrompt;
		chunkJson["start_seconds"] = chunk.startSeconds;
		chunkJson["end_seconds"] = chunk.endSeconds;
		chunkJson["target_duration_seconds"] = chunk.targetDurationSeconds;
		chunkJson["progression_weight"] = chunk.progressionWeight;
		chunkJson["width"] = chunk.width;
		chunkJson["height"] = chunk.height;
		chunkJson["fps"] = chunk.fps;
		chunkJson["frame_count"] = chunk.frameCount;
		chunkJson["seed"] = chunk.seed;
		chunkJson["use_previous_last_frame"] = chunk.usePreviousLastFrame;
		chunkArray.push_back(chunkJson);
	}
	root["chunks"] = std::move(chunkArray);
	return root.dump(2);
}

ofxGgmlLongVideoPlanResult ofxGgmlLongVideoPlanner::run(
	const ofxGgmlLongVideoPlanRequest & request) const {
	ofxGgmlLongVideoPlanResult result;
	result.validation = validateRequest(request);
	result.plannerPrompt = buildPlanningPrompt(request);
	if (!result.validation.ok) {
		result.error = result.validation.errors.empty() ?
			"Invalid long-video plan request." :
			result.validation.errors.front();
		return result;
	}

	result.chunks = buildHeuristicChunks(request);
	result.continuityBible = buildContinuityBibleText(request, result.chunks);
	result.manifestJson = buildManifestJson(request, result.chunks, result.continuityBible);
	result.success = true;
	return result;
}
