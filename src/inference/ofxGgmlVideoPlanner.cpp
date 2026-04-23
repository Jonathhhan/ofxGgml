#include "ofxGgmlVideoPlanner.h"
#include "ofxGgmlPlannerCommon.h"
#include "ofxGgmlClipInference.h"

#ifndef OFXGGML_HEADLESS_STUBS
#include "ofJson.h"
#endif

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace {

using ofxGgmlPlannerCommon::trim;
using ofxGgmlPlannerCommon::toLower;
using ofxGgmlPlannerCommon::formatSeconds;
using ofxGgmlPlannerCommon::describeTimeRange;
using ofxGgmlPlannerCommon::containsAnyToken;

template <typename T>
T jsonValueOr(const ofJson & json, const char * key, const T & fallback) {
	if (!json.is_object() || key == nullptr || !json.contains(key)) {
		return fallback;
	}
	const auto & item = json[key];
	try {
		return item.get<T>();
	} catch (...) {
		return fallback;
	}
}

std::vector<std::string> jsonStringArray(const ofJson & json, const char * key) {
	std::vector<std::string> values;
	if (!json.is_object() || key == nullptr || !json.contains(key)) {
		return values;
	}
	const auto & items = json[key];
	if (!items.is_array()) {
		return values;
	}
	for (const auto & item : items) {
		if (item.is_string()) {
			values.push_back(item.get<std::string>());
		}
	}
	return values;
}

inline std::string describeCutIntensity(float intensity) {
	if (intensity <= 0.33f) {
		return "restrained";
	}
	if (intensity <= 0.66f) {
		return "balanced";
	}
	return "aggressive";
}

std::string appendSentence(const std::string & base, const std::string & addition) {
	const std::string trimmedBase = trim(base);
	const std::string trimmedAddition = trim(addition);
	if (trimmedAddition.empty()) {
		return trimmedBase;
	}
	if (trimmedBase.empty()) {
		return trimmedAddition;
	}
	return trimmedBase + " " + trimmedAddition;
}

std::string buildClipWorkflowText(const ofxGgmlVideoEditClip & clip) {
	std::ostringstream text;
	text << "Prepare an editor-facing rough-cut note for clip " << clip.index << ".";
	const std::string timeRange = describeTimeRange(clip.startSeconds, clip.endSeconds);
	if (!timeRange.empty()) {
		text << " Focus on " << timeRange << ".";
	}
	if (!trim(clip.purpose).empty()) {
		text << " Purpose: " << clip.purpose << ".";
	}
	if (!trim(clip.sourceDescription).empty()) {
		text << " Source moment: " << clip.sourceDescription << ".";
	}
	if (!trim(clip.treatment).empty()) {
		text << " Treatment: " << clip.treatment << ".";
	}
	if (!trim(clip.transition).empty()) {
		text << " Transition: " << clip.transition << ".";
	}
	if (!trim(clip.textOverlay).empty()) {
		text << " Suggested on-screen text: " << clip.textOverlay << ".";
	}
	return text.str();
}

std::string buildActionWorkflowText(const ofxGgmlVideoEditAction & action) {
	std::ostringstream text;
	text << "Help execute edit action " << action.index << ".";
	const std::string timeRange = describeTimeRange(action.startSeconds, action.endSeconds);
	if (!timeRange.empty()) {
		text << " Focus on " << timeRange << ".";
	}
	if (!trim(action.type).empty()) {
		text << " Action type: " << action.type << ".";
	}
	if (!trim(action.instruction).empty()) {
		text << " Instruction: " << action.instruction << ".";
	}
	if (!trim(action.rationale).empty()) {
		text << " Why: " << action.rationale << ".";
	}
	if (!trim(action.assetHint).empty()) {
		text << " Asset direction: " << action.assetHint << ".";
	}
	return text.str();
}

std::string chooseActionHandoffMode(const ofxGgmlVideoEditAction & action) {
	const std::string actionContext =
		trim(action.type) + " " + trim(action.instruction) + " " + trim(action.assetHint);
	if (containsAnyToken(
			actionContext,
			{"asset", "b-roll", "b roll", "cutaway", "insert", "plate", "graphic", "still", "replace", "background"})) {
		return "Diffusion";
	}
	if (containsAnyToken(
			actionContext,
			{"title", "caption", "subtitle", "lower third", "lower-third", "overlay text", "text"})) {
		return "Write";
	}
	if (containsAnyToken(
			actionContext,
			{"review", "check", "continuity", "verify", "match", "compare"})) {
		return "Vision";
	}
	return "Custom";
}

const ofxGgmlVideoPlanEntity * findEntityByIdOrLabel(
	const ofxGgmlVideoPlan & plan,
	const std::string & entityId) {
	const auto it = std::find_if(
		plan.entities.begin(),
		plan.entities.end(),
		[&](const ofxGgmlVideoPlanEntity & entity) {
			return entity.id == entityId || entity.label == entityId;
		});
	return it == plan.entities.end() ? nullptr : &(*it);
}

} // namespace

std::string ofxGgmlVideoPlanner::buildPlanningPrompt(const ofxGgmlVideoPlannerRequest & request) {
	const double durationSeconds = std::max(1.0, request.durationSeconds);
	const int beatCount = std::clamp(request.beatCount, 1, 12);
	const int sceneCount = std::clamp(request.sceneCount, 1, 8);
	const int sectionCount = std::clamp(request.sectionCount, 1, 8);
	std::ostringstream prompt;
	prompt
		<< "You are a spatiotemporal planning assistant for text-to-video generation.\n"
		<< "Turn the user's prompt into a compact JSON video plan that a downstream video diffusion system can follow.\n"
		<< "Return JSON only. Do not wrap it in markdown fences. Do not add commentary.\n\n"
		<< "Planning goals:\n"
		<< "- keep the original intent faithful\n"
		<< "- make motion, camera, and subject continuity explicit\n"
		<< "- split the clip into clear temporal beats\n"
		<< "- prefer visually generatable descriptions over abstract story prose\n";
	if (request.multiScene) {
		prompt
			<< "- decompose the story into multiple coherent scenes with recurring entities\n"
			<< "- keep character and object naming consistent across scenes\n";
	}
	if (request.musicVideoMode) {
		prompt
			<< "- treat the clip like a music video with explicit song sections such as intro, verse, chorus, bridge, and outro when useful\n"
			<< "- vary cut density across sections so chorus/payoff moments can cut faster than intros or bridges\n"
			<< "- keep the section progression musically readable and visually distinctive\n";
	}
	prompt << "\n"
		<< "Requested duration: " << formatSeconds(durationSeconds) << "\n"
		<< "Requested beat count: " << beatCount << "\n";
	if (request.multiScene) {
		prompt << "Requested scene count: " << sceneCount << "\n";
	}
	if (request.musicVideoMode) {
		prompt << "Requested section count: " << sectionCount << "\n";
		prompt << "Music-video cut intensity: " << describeCutIntensity(request.cutIntensity) << "\n";
	}
	if (!trim(request.preferredStyle).empty()) {
		prompt << "Preferred style: " << trim(request.preferredStyle) << "\n";
	}
	if (!trim(request.negativePrompt).empty()) {
		prompt << "Negative prompt hints: " << trim(request.negativePrompt) << "\n";
	}
	if (!trim(request.sectionStructureHint).empty()) {
		prompt << "Preferred section structure: " << trim(request.sectionStructureHint) << "\n";
	}
	prompt
		<< "\nJSON schema:\n"
		<< "{\n"
		<< "  \"originalPrompt\": string,\n"
		<< "  \"style\": string,\n"
		<< "  \"overallScene\": string,\n"
		<< "  \"overallCamera\": string,\n"
		<< "  \"continuityNotes\": string,\n"
		<< "  \"negativePrompt\": string,\n"
		<< "  \"constraints\": [string],\n"
		<< "  \"entities\": [\n"
		<< "    {\"id\": string, \"label\": string, \"description\": string, \"role\": string}\n"
		<< "  ],\n"
		<< "  \"subjects\": [\n"
		<< "    {\"id\": string, \"label\": string, \"description\": string}\n"
		<< "  ],\n"
		<< "  \"beats\": [\n"
		<< "    {\n"
		<< "      \"startSeconds\": number,\n"
		<< "      \"endSeconds\": number,\n"
		<< "      \"summary\": string,\n"
		<< "      \"camera\": string,\n"
		<< "      \"scene\": string,\n"
		<< "      \"motion\": string,\n"
		<< "      \"visualGoal\": string,\n"
		<< "      \"subjects\": [string]\n"
		<< "    }\n"
		<< "  ],\n"
		<< "  \"sections\": [\n"
		<< "    {\n"
		<< "      \"index\": number,\n"
		<< "      \"label\": string,\n"
		<< "      \"role\": string,\n"
		<< "      \"startSeconds\": number,\n"
		<< "      \"endSeconds\": number,\n"
		<< "      \"energy\": string,\n"
		<< "      \"cutDensity\": string,\n"
		<< "      \"visualFocus\": string\n"
		<< "    }\n"
		<< "  ],\n"
		<< "  \"scenes\": [\n"
		<< "    {\n"
		<< "      \"index\": number,\n"
		<< "      \"title\": string,\n"
		<< "      \"summary\": string,\n"
		<< "      \"eventPrompt\": string,\n"
		<< "      \"background\": string,\n"
		<< "      \"cameraMovement\": string,\n"
		<< "      \"transition\": string,\n"
		<< "      \"durationSeconds\": number,\n"
		<< "      \"entityIds\": [string]\n"
		<< "    }\n"
		<< "  ]\n"
		<< "}\n\n"
		<< (request.multiScene
			? "Prefer filling the scenes array richly. beats can summarize finer within-scene timing.\n\n"
			: "Prefer filling the beats array richly. scenes can be empty for single-scene prompts.\n\n")
		<< (request.musicVideoMode
			? "Fill the sections array meaningfully and align beat pacing with section energy. Use labels like intro, verse, chorus, bridge, drop, outro when appropriate.\n\n"
			: "")
		<< "User prompt:\n"
		<< trim(request.prompt);
	return prompt.str();
}

std::string ofxGgmlVideoPlanner::buildEditingPrompt(const ofxGgmlVideoEditPlannerRequest & request) {
	const double targetDurationSeconds = std::max(1.0, request.targetDurationSeconds);
	const int clipCount = std::clamp(request.clipCount, 1, 16);
	std::ostringstream prompt;
	prompt
		<< "You are an AI-assisted video editor and post-production planner.\n"
		<< "Turn the user's editing goal and source-video analysis into a practical JSON edit plan.\n"
		<< "Return JSON only. Do not use markdown fences. Do not add commentary.\n\n"
		<< "Planning goals:\n"
		<< "- produce a realistic edit plan for a human editor or timeline tool\n"
		<< "- identify the strongest clip selections in chronological order unless told otherwise\n"
		<< "- keep instructions actionable: trim, hold, cutaway, slow down, add text, transition, sound design, or replace with generated assets\n"
		<< "- use source analysis as grounding, not as fiction\n"
		<< "- prefer concise editorial language over story prose\n\n"
		<< "Requested target duration: " << formatSeconds(targetDurationSeconds) << "\n"
		<< "Requested clip count: " << clipCount << "\n"
		<< "Preserve chronology: " << (request.preserveChronology ? "yes" : "no") << "\n\n"
		<< "JSON schema:\n"
		<< "{\n"
		<< "  \"originalGoal\": string,\n"
		<< "  \"sourceSummary\": string,\n"
		<< "  \"overallDirection\": string,\n"
		<< "  \"pacingStrategy\": string,\n"
		<< "  \"visualStyle\": string,\n"
		<< "  \"audioStrategy\": string,\n"
		<< "  \"targetDurationSeconds\": number,\n"
		<< "  \"globalNotes\": [string],\n"
		<< "  \"assetSuggestions\": [string],\n"
		<< "  \"clips\": [\n"
		<< "    {\n"
		<< "      \"index\": number,\n"
		<< "      \"startSeconds\": number,\n"
		<< "      \"endSeconds\": number,\n"
		<< "      \"purpose\": string,\n"
		<< "      \"sourceDescription\": string,\n"
		<< "      \"treatment\": string,\n"
		<< "      \"transition\": string,\n"
		<< "      \"textOverlay\": string\n"
		<< "    }\n"
		<< "  ],\n"
		<< "  \"actions\": [\n"
		<< "    {\n"
		<< "      \"index\": number,\n"
		<< "      \"type\": string,\n"
		<< "      \"startSeconds\": number,\n"
		<< "      \"endSeconds\": number,\n"
		<< "      \"instruction\": string,\n"
		<< "      \"rationale\": string,\n"
		<< "      \"assetHint\": string\n"
		<< "    }\n"
		<< "  ]\n"
		<< "}\n\n"
		<< "User editing goal:\n"
		<< trim(request.editGoal) << "\n\n";
	if (!trim(request.sourcePrompt).empty()) {
		prompt << "Original video prompt or source note:\n"
			<< trim(request.sourcePrompt) << "\n\n";
	}
	if (!trim(request.sourceAnalysis).empty()) {
		prompt << "Source video analysis:\n"
			<< trim(request.sourceAnalysis) << "\n";
	}
	return prompt.str();
}

std::string ofxGgmlVideoPlanner::extractJsonObject(const std::string & text) {
	const std::string trimmed = trim(text);
	if (trimmed.empty()) {
		return {};
	}
	const size_t fenceStart = trimmed.find("```");
	if (fenceStart != std::string::npos) {
		size_t jsonStart = trimmed.find('{', fenceStart);
		size_t fenceEnd = trimmed.rfind("```");
		if (jsonStart != std::string::npos && fenceEnd != std::string::npos && fenceEnd > jsonStart) {
			return trim(trimmed.substr(jsonStart, fenceEnd - jsonStart));
		}
	}
	const size_t start = trimmed.find('{');
	const size_t end = trimmed.rfind('}');
	if (start == std::string::npos || end == std::string::npos || end <= start) {
		return {};
	}
	return trim(trimmed.substr(start, end - start + 1));
}

Result<ofxGgmlVideoPlan> ofxGgmlVideoPlanner::parsePlanJson(const std::string & jsonText) {
#ifdef OFXGGML_HEADLESS_STUBS
	(void) jsonText;
	return Result<ofxGgmlVideoPlan>(ofxGgmlErrorCode::NotImplemented, "JSON parsing is unavailable in headless stubs.");
#else
	const std::string extracted = extractJsonObject(jsonText);
	if (extracted.empty()) {
		return Result<ofxGgmlVideoPlan>(ofxGgmlErrorCode::InferenceOutputInvalid, "Planner output did not contain a JSON object.");
	}

	ofJson json;
	try {
		json = ofJson::parse(extracted);
	} catch (const std::exception & e) {
		return Result<ofxGgmlVideoPlan>(ofxGgmlErrorCode::InferenceOutputInvalid, std::string("Failed to parse planner JSON: ") + e.what());
	}
	if (!json.is_object()) {
		return Result<ofxGgmlVideoPlan>(ofxGgmlErrorCode::InferenceOutputInvalid, "Planner JSON root must be an object.");
	}

	ofxGgmlVideoPlan plan;
	plan.originalPrompt = jsonValueOr<std::string>(json, "originalPrompt", "");
	plan.style = jsonValueOr<std::string>(json, "style", "");
	plan.overallScene = jsonValueOr<std::string>(json, "overallScene", "");
	plan.overallCamera = jsonValueOr<std::string>(json, "overallCamera", "");
	plan.continuityNotes = jsonValueOr<std::string>(json, "continuityNotes", "");
	plan.negativePrompt = jsonValueOr<std::string>(json, "negativePrompt", "");
	plan.constraints = jsonStringArray(json, "constraints");

	if (json.contains("entities") && json["entities"].is_array()) {
		for (const auto & item : json["entities"]) {
			if (!item.is_object()) {
				continue;
			}
			ofxGgmlVideoPlanEntity entity;
			entity.id = jsonValueOr<std::string>(item, "id", "");
			entity.label = jsonValueOr<std::string>(item, "label", "");
			entity.description = jsonValueOr<std::string>(item, "description", "");
			entity.role = jsonValueOr<std::string>(item, "role", "");
			entity.referenceImagePath = jsonValueOr<std::string>(item, "referenceImagePath", "");
			if (!entity.id.empty() || !entity.label.empty() || !entity.description.empty()) {
				plan.entities.push_back(entity);
			}
		}
	}

	if (json.contains("subjects") && json["subjects"].is_array()) {
		for (const auto & item : json["subjects"]) {
			if (!item.is_object()) {
				continue;
			}
			ofxGgmlVideoPlanSubject subject;
			subject.id = jsonValueOr<std::string>(item, "id", "");
			subject.label = jsonValueOr<std::string>(item, "label", "");
			subject.description = jsonValueOr<std::string>(item, "description", "");
			if (!subject.label.empty() || !subject.description.empty() || !subject.id.empty()) {
				plan.subjects.push_back(subject);
			}
		}
	}

	if (json.contains("beats") && json["beats"].is_array()) {
		for (const auto & item : json["beats"]) {
			if (!item.is_object()) {
				continue;
			}
			ofxGgmlVideoPlanBeat beat;
			beat.startSeconds = jsonValueOr<double>(item, "startSeconds", 0.0);
			beat.endSeconds = jsonValueOr<double>(item, "endSeconds", beat.startSeconds);
			if (beat.endSeconds < beat.startSeconds) {
				std::swap(beat.startSeconds, beat.endSeconds);
			}
			beat.summary = jsonValueOr<std::string>(item, "summary", "");
			beat.camera = jsonValueOr<std::string>(item, "camera", "");
			beat.scene = jsonValueOr<std::string>(item, "scene", "");
			beat.motion = jsonValueOr<std::string>(item, "motion", "");
			beat.visualGoal = jsonValueOr<std::string>(item, "visualGoal", "");
			beat.subjects = jsonStringArray(item, "subjects");
			plan.beats.push_back(beat);
		}
	}

	if (json.contains("sections") && json["sections"].is_array()) {
		for (const auto & item : json["sections"]) {
			if (!item.is_object()) {
				continue;
			}
			ofxGgmlVideoPlanSection section;
			section.index = jsonValueOr<int>(item, "index", 0);
			section.label = jsonValueOr<std::string>(item, "label", "");
			section.role = jsonValueOr<std::string>(item, "role", "");
			section.startSeconds = jsonValueOr<double>(item, "startSeconds", 0.0);
			section.endSeconds = jsonValueOr<double>(item, "endSeconds", section.startSeconds);
			if (section.endSeconds < section.startSeconds) {
				std::swap(section.startSeconds, section.endSeconds);
			}
			section.energy = jsonValueOr<std::string>(item, "energy", "");
			section.cutDensity = jsonValueOr<std::string>(item, "cutDensity", "");
			section.visualFocus = jsonValueOr<std::string>(item, "visualFocus", "");
			if (section.index != 0 ||
				!section.label.empty() ||
				!section.role.empty() ||
				section.endSeconds > section.startSeconds ||
				!section.energy.empty() ||
				!section.cutDensity.empty() ||
				!section.visualFocus.empty()) {
				plan.sections.push_back(section);
			}
		}
	}

	if (json.contains("scenes") && json["scenes"].is_array()) {
		for (const auto & item : json["scenes"]) {
			if (!item.is_object()) {
				continue;
			}
			ofxGgmlVideoPlanScene scene;
			scene.index = jsonValueOr<int>(item, "index", static_cast<int>(plan.scenes.size() + 1));
			scene.title = jsonValueOr<std::string>(item, "title", "");
			scene.summary = jsonValueOr<std::string>(item, "summary", "");
			scene.eventPrompt = jsonValueOr<std::string>(item, "eventPrompt", "");
			scene.background = jsonValueOr<std::string>(item, "background", "");
			scene.cameraMovement = jsonValueOr<std::string>(item, "cameraMovement", "");
			scene.transition = jsonValueOr<std::string>(item, "transition", "");
			scene.durationSeconds = jsonValueOr<double>(item, "durationSeconds", 0.0);
			scene.entityIds = jsonStringArray(item, "entityIds");
			if (!scene.title.empty() || !scene.summary.empty() || !scene.eventPrompt.empty()) {
				plan.scenes.push_back(scene);
			}
		}
	}

	if (plan.beats.empty() && plan.scenes.empty()) {
		return Result<ofxGgmlVideoPlan>(ofxGgmlErrorCode::InferenceOutputInvalid, "Planner JSON did not contain any beats or scenes.");
	}
	return Result<ofxGgmlVideoPlan>(plan);
#endif
}

Result<ofxGgmlVideoEditPlan> ofxGgmlVideoPlanner::parseEditPlanJson(const std::string & jsonText) {
#ifdef OFXGGML_HEADLESS_STUBS
	(void) jsonText;
	return Result<ofxGgmlVideoEditPlan>(ofxGgmlErrorCode::NotImplemented, "JSON parsing is unavailable in headless stubs.");
#else
	const std::string extracted = extractJsonObject(jsonText);
	if (extracted.empty()) {
		return Result<ofxGgmlVideoEditPlan>(ofxGgmlErrorCode::InferenceOutputInvalid, "Edit planner output did not contain a JSON object.");
	}

	ofJson json;
	try {
		json = ofJson::parse(extracted);
	} catch (const std::exception & e) {
		return Result<ofxGgmlVideoEditPlan>(ofxGgmlErrorCode::InferenceOutputInvalid, std::string("Failed to parse edit planner JSON: ") + e.what());
	}
	if (!json.is_object()) {
		return Result<ofxGgmlVideoEditPlan>(ofxGgmlErrorCode::InferenceOutputInvalid, "Edit planner JSON root must be an object.");
	}

	ofxGgmlVideoEditPlan plan;
	plan.originalGoal = jsonValueOr<std::string>(json, "originalGoal", "");
	plan.sourceSummary = jsonValueOr<std::string>(json, "sourceSummary", "");
	plan.overallDirection = jsonValueOr<std::string>(json, "overallDirection", "");
	plan.pacingStrategy = jsonValueOr<std::string>(json, "pacingStrategy", "");
	plan.visualStyle = jsonValueOr<std::string>(json, "visualStyle", "");
	plan.audioStrategy = jsonValueOr<std::string>(json, "audioStrategy", "");
	plan.targetDurationSeconds = jsonValueOr<double>(json, "targetDurationSeconds", 0.0);
	plan.globalNotes = jsonStringArray(json, "globalNotes");
	plan.assetSuggestions = jsonStringArray(json, "assetSuggestions");

	if (json.contains("clips") && json["clips"].is_array()) {
		for (const auto & item : json["clips"]) {
			if (!item.is_object()) {
				continue;
			}
			ofxGgmlVideoEditClip clip;
			clip.index = jsonValueOr<int>(item, "index", static_cast<int>(plan.clips.size() + 1));
			clip.startSeconds = jsonValueOr<double>(item, "startSeconds", 0.0);
			clip.endSeconds = jsonValueOr<double>(item, "endSeconds", clip.startSeconds);
			if (clip.endSeconds < clip.startSeconds) {
				std::swap(clip.startSeconds, clip.endSeconds);
			}
			clip.purpose = jsonValueOr<std::string>(item, "purpose", "");
			clip.sourceDescription = jsonValueOr<std::string>(item, "sourceDescription", "");
			clip.treatment = jsonValueOr<std::string>(item, "treatment", "");
			clip.transition = jsonValueOr<std::string>(item, "transition", "");
			clip.textOverlay = jsonValueOr<std::string>(item, "textOverlay", "");
			if (!clip.purpose.empty() || !clip.sourceDescription.empty() || clip.endSeconds > clip.startSeconds) {
				plan.clips.push_back(clip);
			}
		}
	}

	if (json.contains("actions") && json["actions"].is_array()) {
		for (const auto & item : json["actions"]) {
			if (!item.is_object()) {
				continue;
			}
			ofxGgmlVideoEditAction action;
			action.index = jsonValueOr<int>(item, "index", static_cast<int>(plan.actions.size() + 1));
			action.type = jsonValueOr<std::string>(item, "type", "");
			action.startSeconds = jsonValueOr<double>(item, "startSeconds", 0.0);
			action.endSeconds = jsonValueOr<double>(item, "endSeconds", action.startSeconds);
			if (action.endSeconds < action.startSeconds) {
				std::swap(action.startSeconds, action.endSeconds);
			}
			action.instruction = jsonValueOr<std::string>(item, "instruction", "");
			action.rationale = jsonValueOr<std::string>(item, "rationale", "");
			action.assetHint = jsonValueOr<std::string>(item, "assetHint", "");
			if (!action.type.empty() || !action.instruction.empty()) {
				plan.actions.push_back(action);
			}
		}
	}

	if (plan.clips.empty() && plan.actions.empty()) {
		return Result<ofxGgmlVideoEditPlan>(ofxGgmlErrorCode::InferenceOutputInvalid, "Edit planner JSON did not contain any clips or actions.");
	}
	return Result<ofxGgmlVideoEditPlan>(plan);
#endif
}

std::string ofxGgmlVideoPlanner::buildGenerationPrompt(const ofxGgmlVideoPlan & plan) {
	std::ostringstream prompt;
	if (!trim(plan.originalPrompt).empty()) {
		prompt << plan.originalPrompt;
	} else {
		prompt << "Generate a coherent short video that follows the structured beat plan below.";
	}
	prompt << "\n\nStructured video plan:";
	if (!trim(plan.style).empty()) {
		prompt << "\nStyle: " << plan.style;
	}
	if (!trim(plan.overallScene).empty()) {
		prompt << "\nOverall scene: " << plan.overallScene;
	}
	if (!trim(plan.overallCamera).empty()) {
		prompt << "\nOverall camera: " << plan.overallCamera;
	}
	if (!trim(plan.continuityNotes).empty()) {
		prompt << "\nContinuity: " << plan.continuityNotes;
	}
	if (!plan.entities.empty()) {
		prompt << "\nRecurring entities:";
		for (const auto & entity : plan.entities) {
			prompt << "\n- " << (!entity.label.empty() ? entity.label : entity.id);
			if (!trim(entity.role).empty()) {
				prompt << " (" << entity.role << ")";
			}
			if (!trim(entity.description).empty()) {
				prompt << ": " << entity.description;
			}
		}
	}
	if (!plan.subjects.empty()) {
		prompt << "\nSubjects:";
		for (const auto & subject : plan.subjects) {
			prompt << "\n- " << (!subject.label.empty() ? subject.label : subject.id);
			if (!trim(subject.description).empty()) {
				prompt << ": " << subject.description;
			}
		}
	}
	if (!plan.sections.empty()) {
		prompt << "\nMusic-video sections:";
		for (const auto & section : plan.sections) {
			prompt << "\n- ";
			if (section.index > 0) {
				prompt << section.index << ". ";
			}
			prompt << (!trim(section.label).empty() ? section.label : "section");
			if (!trim(section.role).empty()) {
				prompt << " (" << section.role << ")";
			}
			const std::string sectionRange =
				describeTimeRange(section.startSeconds, section.endSeconds);
			if (!trim(sectionRange).empty()) {
				prompt << " | " << sectionRange;
			}
			if (!trim(section.energy).empty()) {
				prompt << " | energy: " << section.energy;
			}
			if (!trim(section.cutDensity).empty()) {
				prompt << " | cut density: " << section.cutDensity;
			}
			if (!trim(section.visualFocus).empty()) {
				prompt << " | focus: " << section.visualFocus;
			}
		}
	}
	prompt << "\nTemporal beats:";
	for (size_t i = 0; i < plan.beats.size(); ++i) {
		const auto & beat = plan.beats[i];
		prompt << "\n" << (i + 1) << ". "
			<< formatSeconds(beat.startSeconds) << " to " << formatSeconds(beat.endSeconds)
			<< " - " << trim(beat.summary);
		if (!trim(beat.scene).empty()) {
			prompt << " | scene: " << beat.scene;
		}
		if (!trim(beat.camera).empty()) {
			prompt << " | camera: " << beat.camera;
		}
		if (!trim(beat.motion).empty()) {
			prompt << " | motion: " << beat.motion;
		}
		if (!trim(beat.visualGoal).empty()) {
			prompt << " | visual goal: " << beat.visualGoal;
		}
		if (!beat.subjects.empty()) {
			prompt << " | subjects: ";
			for (size_t subjectIndex = 0; subjectIndex < beat.subjects.size(); ++subjectIndex) {
				if (subjectIndex > 0) {
					prompt << ", ";
				}
				prompt << beat.subjects[subjectIndex];
			}
		}
	}
	if (!plan.scenes.empty()) {
		prompt << "\nScenes:";
		for (const auto & scene : plan.scenes) {
			prompt << "\n- Scene " << scene.index;
			if (!trim(scene.title).empty()) {
				prompt << " (" << scene.title << ")";
			}
			if (!trim(scene.summary).empty()) {
				prompt << ": " << scene.summary;
			}
			if (!trim(scene.eventPrompt).empty()) {
				prompt << " | prompt: " << scene.eventPrompt;
			}
			if (!trim(scene.background).empty()) {
				prompt << " | background: " << scene.background;
			}
			if (!trim(scene.cameraMovement).empty()) {
				prompt << " | camera: " << scene.cameraMovement;
			}
		}
	}
	if (!plan.constraints.empty()) {
		prompt << "\nConstraints:";
		for (const auto & constraint : plan.constraints) {
			prompt << "\n- " << constraint;
		}
	}
	if (!trim(plan.negativePrompt).empty()) {
		prompt << "\nNegative prompt: " << plan.negativePrompt;
	}
	return prompt.str();
}

std::string ofxGgmlVideoPlanner::buildScenePrompt(const ofxGgmlVideoPlan & plan, size_t sceneIndex) {
	if (sceneIndex >= plan.scenes.size()) {
		return buildGenerationPrompt(plan);
	}
	const auto & scene = plan.scenes[sceneIndex];
	std::ostringstream prompt;
	if (!trim(scene.eventPrompt).empty()) {
		prompt << scene.eventPrompt;
	} else if (!trim(scene.summary).empty()) {
		prompt << scene.summary;
	} else {
		prompt << plan.originalPrompt;
	}
	if (!trim(plan.style).empty()) {
		prompt << "\nStyle: " << plan.style;
	}
	if (!trim(scene.background).empty()) {
		prompt << "\nBackground: " << scene.background;
	}
	if (!trim(scene.cameraMovement).empty()) {
		prompt << "\nCamera: " << scene.cameraMovement;
	}
	if (scene.durationSeconds > 0.0) {
		prompt << "\nTarget scene duration: " << formatSeconds(scene.durationSeconds);
	}
	if (!trim(scene.transition).empty()) {
		prompt << "\nTransition: " << scene.transition;
	}
	if (!trim(plan.continuityNotes).empty()) {
		prompt << "\nContinuity: " << plan.continuityNotes;
	}
	if (sceneIndex > 0) {
		const auto & previousScene = plan.scenes[sceneIndex - 1];
		const std::string previousSceneLabel =
			!trim(previousScene.title).empty() ? previousScene.title : previousScene.summary;
		if (!trim(previousSceneLabel).empty()) {
			prompt << "\nPrevious scene context: " << previousSceneLabel;
		}
	}
	if (sceneIndex + 1 < plan.scenes.size()) {
		const auto & nextScene = plan.scenes[sceneIndex + 1];
		const std::string nextSceneLabel =
			!trim(nextScene.title).empty() ? nextScene.title : nextScene.summary;
		if (!trim(nextSceneLabel).empty()) {
			prompt << "\nNext scene should transition toward: " << nextSceneLabel;
		}
	}
	if (!scene.entityIds.empty() && !plan.entities.empty()) {
		prompt << "\nEntities:";
		for (const auto & entityId : scene.entityIds) {
			const auto * entity = findEntityByIdOrLabel(plan, entityId);
			if (entity != nullptr) {
				prompt << "\n- " << (!entity->label.empty() ? entity->label : entity->id);
				if (!trim(entity->role).empty()) {
					prompt << " (" << entity->role << ")";
				}
				if (!trim(entity->description).empty()) {
					prompt << ": " << entity->description;
				}
				if (!trim(entity->referenceImagePath).empty()) {
					prompt << " | reference image: " << entity->referenceImagePath;
				}
			}
		}
	}
	if (!trim(plan.negativePrompt).empty()) {
		prompt << "\nNegative prompt: " << plan.negativePrompt;
	}
	return prompt.str();
}

std::string ofxGgmlVideoPlanner::buildSceneSequencePrompt(const ofxGgmlVideoPlan & plan) {
	if (plan.scenes.empty()) {
		return buildGenerationPrompt(plan);
	}

	std::ostringstream prompt;
	if (!trim(plan.originalPrompt).empty()) {
		prompt << plan.originalPrompt;
	} else {
		prompt << "Generate a coherent multi-scene video that follows the structured scene sequence below.";
	}
	if (!trim(plan.style).empty()) {
		prompt << "\nStyle: " << plan.style;
	}
	if (!trim(plan.overallScene).empty()) {
		prompt << "\nOverall setting: " << plan.overallScene;
	}
	if (!trim(plan.overallCamera).empty()) {
		prompt << "\nOverall camera language: " << plan.overallCamera;
	}
	if (!trim(plan.continuityNotes).empty()) {
		prompt << "\nContinuity notes: " << plan.continuityNotes;
	}
	if (!plan.entities.empty()) {
		prompt << "\nRecurring entities:";
		for (const auto & entity : plan.entities) {
			prompt << "\n- " << (!entity.label.empty() ? entity.label : entity.id);
			if (!trim(entity.role).empty()) {
				prompt << " (" << entity.role << ")";
			}
			if (!trim(entity.description).empty()) {
				prompt << ": " << entity.description;
			}
			if (!trim(entity.referenceImagePath).empty()) {
				prompt << " | reference image: " << entity.referenceImagePath;
			}
		}
	}
	if (!plan.sections.empty()) {
		prompt << "\nSection progression:";
		for (const auto & section : plan.sections) {
			prompt << "\n- ";
			if (section.index > 0) {
				prompt << section.index << ". ";
			}
			prompt << (!trim(section.label).empty() ? section.label : "section");
			if (!trim(section.role).empty()) {
				prompt << " (" << section.role << ")";
			}
			const std::string sectionRange =
				describeTimeRange(section.startSeconds, section.endSeconds);
			if (!trim(sectionRange).empty()) {
				prompt << " | " << sectionRange;
			}
			if (!trim(section.energy).empty()) {
				prompt << " | energy: " << section.energy;
			}
			if (!trim(section.cutDensity).empty()) {
				prompt << " | cut density: " << section.cutDensity;
			}
			if (!trim(section.visualFocus).empty()) {
				prompt << " | focus: " << section.visualFocus;
			}
		}
	}
	prompt << "\nScene sequence:";
	for (const auto & scene : plan.scenes) {
		prompt << "\n" << scene.index << ". ";
		if (!trim(scene.title).empty()) {
			prompt << scene.title;
		} else {
			prompt << "Scene " << scene.index;
		}
		if (scene.durationSeconds > 0.0) {
			prompt << " (" << formatSeconds(scene.durationSeconds) << ")";
		}
		if (!trim(scene.summary).empty()) {
			prompt << " | " << scene.summary;
		}
		if (!trim(scene.eventPrompt).empty()) {
			prompt << " | prompt: " << scene.eventPrompt;
		}
		if (!trim(scene.background).empty()) {
			prompt << " | background: " << scene.background;
		}
		if (!trim(scene.cameraMovement).empty()) {
			prompt << " | camera: " << scene.cameraMovement;
		}
		if (!trim(scene.transition).empty()) {
			prompt << " | transition: " << scene.transition;
		}
		if (!scene.entityIds.empty()) {
			prompt << " | entities: ";
			for (size_t entityIndex = 0; entityIndex < scene.entityIds.size(); ++entityIndex) {
				if (entityIndex > 0) {
					prompt << ", ";
				}
				const auto * entity = findEntityByIdOrLabel(plan, scene.entityIds[entityIndex]);
				prompt << (entity != nullptr && !entity->label.empty() ? entity->label : scene.entityIds[entityIndex]);
			}
		}
	}
	if (!trim(plan.negativePrompt).empty()) {
		prompt << "\nNegative prompt: " << plan.negativePrompt;
	}
	return prompt.str();
}

std::string ofxGgmlVideoPlanner::summarizePlan(const ofxGgmlVideoPlan & plan) {
	std::ostringstream summary;
	if (!plan.scenes.empty()) {
		summary << "Multi-scene video plan with " << plan.scenes.size() << " scene(s)";
	} else {
		summary << "Video plan with " << plan.beats.size() << " beat(s)";
	}
	if (!trim(plan.style).empty()) {
		summary << "\nStyle: " << plan.style;
	}
	if (!trim(plan.overallCamera).empty()) {
		summary << "\nCamera: " << plan.overallCamera;
	}
	if (!trim(plan.continuityNotes).empty()) {
		summary << "\nContinuity: " << plan.continuityNotes;
	}
	if (!plan.entities.empty()) {
		summary << "\nEntities:";
		for (const auto & entity : plan.entities) {
			summary << "\n- " << (!entity.label.empty() ? entity.label : entity.id);
			if (!trim(entity.role).empty()) {
				summary << " (" << entity.role << ")";
			}
		}
	}
	if (!plan.sections.empty()) {
		summary << "\nSections:";
		for (const auto & section : plan.sections) {
			summary << "\n- ";
			if (section.index > 0) {
				summary << section.index << ". ";
			}
			summary << (!trim(section.label).empty() ? section.label : "section");
			if (!trim(section.role).empty()) {
				summary << " (" << section.role << ")";
			}
			const std::string sectionRange =
				describeTimeRange(section.startSeconds, section.endSeconds);
			if (!trim(sectionRange).empty()) {
				summary << " | " << sectionRange;
			}
			if (!trim(section.energy).empty()) {
				summary << " | " << section.energy;
			}
			if (!trim(section.cutDensity).empty()) {
				summary << " | cuts: " << section.cutDensity;
			}
			if (!trim(section.visualFocus).empty()) {
				summary << " | focus: " << section.visualFocus;
			}
		}
	}
	if (!plan.scenes.empty()) {
		summary << "\nScenes:";
		for (const auto & scene : plan.scenes) {
			summary << "\n" << scene.index << ". ";
			if (!trim(scene.title).empty()) {
				summary << scene.title;
			} else {
				summary << "Scene " << scene.index;
			}
			if (!trim(scene.summary).empty()) {
				summary << ": " << scene.summary;
			}
			if (scene.durationSeconds > 0.0) {
				summary << " | " << formatSeconds(scene.durationSeconds);
			}
			if (!trim(scene.cameraMovement).empty()) {
				summary << " | " << scene.cameraMovement;
			}
			if (!scene.entityIds.empty()) {
				summary << " | entities: ";
				for (size_t entityIndex = 0; entityIndex < scene.entityIds.size(); ++entityIndex) {
					if (entityIndex > 0) {
						summary << ", ";
					}
					const auto * entity = findEntityByIdOrLabel(plan, scene.entityIds[entityIndex]);
					summary << (entity != nullptr && !entity->label.empty() ? entity->label : scene.entityIds[entityIndex]);
				}
			}
		}
	}
	for (size_t i = 0; i < plan.beats.size(); ++i) {
		const auto & beat = plan.beats[i];
		summary << "\n" << (i + 1) << ". "
			<< formatSeconds(beat.startSeconds) << " - " << formatSeconds(beat.endSeconds)
			<< ": " << trim(beat.summary);
		if (!trim(beat.camera).empty()) {
			summary << " | " << beat.camera;
		}
		if (!trim(beat.motion).empty()) {
			summary << " | " << beat.motion;
		}
	}
	return summary.str();
}

std::string ofxGgmlVideoPlanner::buildEditorBrief(const ofxGgmlVideoEditPlan & plan) {
	std::ostringstream brief;
	brief << "Video edit brief";
	if (!trim(plan.originalGoal).empty()) {
		brief << "\nGoal: " << plan.originalGoal;
	}
	if (!trim(plan.overallDirection).empty()) {
		brief << "\nDirection: " << plan.overallDirection;
	}
	if (!trim(plan.pacingStrategy).empty()) {
		brief << "\nPacing: " << plan.pacingStrategy;
	}
	if (!trim(plan.visualStyle).empty()) {
		brief << "\nVisual style: " << plan.visualStyle;
	}
	if (!trim(plan.audioStrategy).empty()) {
		brief << "\nAudio: " << plan.audioStrategy;
	}
	if (!plan.globalNotes.empty()) {
		brief << "\nGlobal notes:";
		for (const auto & note : plan.globalNotes) {
			brief << "\n- " << note;
		}
	}
	if (!plan.clips.empty()) {
		brief << "\nTimeline:";
		for (const auto & clip : plan.clips) {
			brief << "\n" << clip.index << ". "
				<< formatSeconds(clip.startSeconds) << " - " << formatSeconds(clip.endSeconds)
				<< " | " << (!trim(clip.purpose).empty() ? clip.purpose : clip.sourceDescription);
			if (!trim(clip.treatment).empty()) {
				brief << " | " << clip.treatment;
			}
			if (!trim(clip.transition).empty()) {
				brief << " | transition: " << clip.transition;
			}
			if (!trim(clip.textOverlay).empty()) {
				brief << " | text: " << clip.textOverlay;
			}
		}
	}
	if (!plan.actions.empty()) {
		brief << "\nEdit actions:";
		for (const auto & action : plan.actions) {
			brief << "\n- [" << action.type << "] "
				<< (!trim(action.instruction).empty() ? action.instruction : action.rationale);
			if (action.endSeconds > action.startSeconds) {
				brief << " (" << formatSeconds(action.startSeconds) << " - " << formatSeconds(action.endSeconds) << ")";
			}
			if (!trim(action.assetHint).empty()) {
				brief << " | asset: " << action.assetHint;
			}
		}
	}
	if (!plan.assetSuggestions.empty()) {
		brief << "\nSuggested assets:";
		for (const auto & asset : plan.assetSuggestions) {
			brief << "\n- " << asset;
		}
	}
	return brief.str();
}

ofxGgmlVideoEditWorkflow ofxGgmlVideoPlanner::buildEditWorkflow(
	const ofxGgmlVideoEditPlan & plan,
	const ofxGgmlVideoEditWorkflowContext & context) {
	ofxGgmlVideoEditWorkflow workflow;
	workflow.headline = !trim(plan.overallDirection).empty()
		? plan.overallDirection
		: !trim(plan.originalGoal).empty()
			? "Editing goal: " + trim(plan.originalGoal)
			: "AI-assisted video editing workflow";

	if (context.hasSourceVideo) {
		ofxGgmlVideoEditWorkflowStep reviewStep;
		reviewStep.index = 1;
		reviewStep.title = "Review source material";
		reviewStep.detail =
			"Open the source clip in Vision mode, confirm the strongest emotional/action beats, and check whether the current analysis still matches the intended edit direction.";
		reviewStep.handoffMode = "Vision";
		reviewStep.handoffText = appendSentence(
			"Review this source clip like an editor before cutting.",
			!trim(plan.originalGoal).empty()
				? "Editing goal: " + trim(plan.originalGoal)
				: trim(plan.sourceSummary));
		workflow.steps.push_back(reviewStep);
	}

	for (const auto & clip : plan.clips) {
		ofxGgmlVideoEditWorkflowStep clipStep;
		clipStep.index = static_cast<int>(workflow.steps.size() + 1);
		clipStep.title = "Rough cut clip " + std::to_string(clip.index);
		const std::string clipTimeRange = describeTimeRange(clip.startSeconds, clip.endSeconds);
		const std::string clipPurpose = trim(clip.purpose);
		clipStep.detail =
			!clipTimeRange.empty() && !clipPurpose.empty()
				? clipTimeRange + " | " + clipPurpose
				: !clipTimeRange.empty()
					? clipTimeRange
					: clipPurpose;
		clipStep.handoffMode = "Write";
		clipStep.handoffText = buildClipWorkflowText(clip);
		clipStep.startSeconds = clip.startSeconds;
		clipStep.endSeconds = clip.endSeconds;
		workflow.steps.push_back(clipStep);
	}

	for (const auto & action : plan.actions) {
		ofxGgmlVideoEditWorkflowStep actionStep;
		actionStep.index = static_cast<int>(workflow.steps.size() + 1);
		actionStep.title =
			trim(action.type).empty()
				? "Apply edit action " + std::to_string(action.index)
				: trim(action.type);
		actionStep.detail =
			trim(action.instruction).empty()
				? trim(action.rationale)
				: trim(action.instruction);
		actionStep.handoffMode = chooseActionHandoffMode(action);
		actionStep.handoffText = buildActionWorkflowText(action);
		actionStep.startSeconds = action.startSeconds;
		actionStep.endSeconds = action.endSeconds;
		workflow.steps.push_back(actionStep);
	}

	if (context.hasSubtitlePreview || context.hasSourceTimedPreview || context.hasMontageTimedPreview) {
		ofxGgmlVideoEditWorkflowStep previewStep;
		previewStep.index = static_cast<int>(workflow.steps.size() + 1);
		previewStep.title = "Quality-check subtitle timing";
		previewStep.detail =
			context.hasMontageTimedPreview
				? "Preview the montage-timed subtitle track and verify cue rhythm against the intended pacing."
				: "Preview the source-timed subtitle track and verify cue alignment against the source clip.";
		previewStep.handoffMode = "Montage";
		previewStep.handoffText = !trim(plan.originalGoal).empty()
			? plan.originalGoal
			: "Review subtitle timing and montage continuity for this edit.";
		workflow.steps.push_back(previewStep);
	}

	if (!plan.globalNotes.empty()) {
		workflow.checklist.insert(
			workflow.checklist.end(),
			plan.globalNotes.begin(),
			plan.globalNotes.end());
	}
	for (const auto & asset : plan.assetSuggestions) {
		if (!trim(asset).empty()) {
			workflow.checklist.push_back("Prepare asset: " + trim(asset));
		}
	}

	if (!workflow.steps.empty()) {
		const auto & firstActionableStep = workflow.steps.front();
		workflow.nextAction =
			"Start with \"" + firstActionableStep.title + "\""
			+ (firstActionableStep.handoffMode.empty()
				? std::string()
				: " in " + firstActionableStep.handoffMode + " mode");
	} else {
		workflow.nextAction = "Refine the edit goal or add more source analysis to generate an actionable workflow.";
	}

	if (context.hasMontageTimedPreview) {
		workflow.previewHint =
			"Montage-timed subtitle preview is ready, so you can quality-check pacing after each major edit step.";
	} else if (context.hasSourceTimedPreview) {
		workflow.previewHint =
			"Source-timed subtitle preview is ready for continuity review against the original clip.";
	} else if (context.hasSourceVideo) {
		workflow.previewHint =
			"A source video is loaded, so Vision mode can be used as the live review surface while applying the plan.";
	}

	return workflow;
}

std::string ofxGgmlVideoPlanner::summarizeEditPlan(const ofxGgmlVideoEditPlan & plan) {
	std::ostringstream summary;
	summary << "Video edit plan with " << plan.clips.size() << " clip(s) and " << plan.actions.size() << " action(s)";
	if (plan.targetDurationSeconds > 0.0) {
		summary << "\nTarget duration: " << formatSeconds(plan.targetDurationSeconds);
	}
	if (!trim(plan.overallDirection).empty()) {
		summary << "\nDirection: " << plan.overallDirection;
	}
	if (!trim(plan.pacingStrategy).empty()) {
		summary << "\nPacing: " << plan.pacingStrategy;
	}
	if (!trim(plan.visualStyle).empty()) {
		summary << "\nVisual style: " << plan.visualStyle;
	}
	if (!plan.clips.empty()) {
		summary << "\nClips:";
		for (const auto & clip : plan.clips) {
			summary << "\n" << clip.index << ". "
				<< formatSeconds(clip.startSeconds) << " - " << formatSeconds(clip.endSeconds)
				<< ": " << (!trim(clip.purpose).empty() ? clip.purpose : clip.sourceDescription);
			if (!trim(clip.treatment).empty()) {
				summary << " | " << clip.treatment;
			}
		}
	}
	if (!plan.actions.empty()) {
		summary << "\nActions:";
		for (const auto & action : plan.actions) {
			summary << "\n- " << (!trim(action.type).empty() ? action.type : "edit");
			if (!trim(action.instruction).empty()) {
				summary << ": " << action.instruction;
			} else if (!trim(action.rationale).empty()) {
				summary << ": " << action.rationale;
			}
		}
	}
	return summary.str();
}

std::string ofxGgmlVideoPlanner::summarizeEditWorkflow(const ofxGgmlVideoEditWorkflow & workflow) {
	std::ostringstream summary;
	if (!trim(workflow.headline).empty()) {
		summary << workflow.headline;
	} else {
		summary << "Video editing workflow";
	}
	if (!trim(workflow.nextAction).empty()) {
		summary << "\nNext action: " << workflow.nextAction;
	}
	if (!trim(workflow.previewHint).empty()) {
		summary << "\nPreview: " << workflow.previewHint;
	}
	if (!workflow.checklist.empty()) {
		summary << "\nChecklist:";
		for (const auto & item : workflow.checklist) {
			summary << "\n- " << item;
		}
	}
	if (!workflow.steps.empty()) {
		summary << "\nSteps:";
		for (const auto & step : workflow.steps) {
			summary << "\n" << step.index << ". " << step.title;
			if (!trim(step.detail).empty()) {
				summary << " | " << step.detail;
			}
			if (!trim(step.handoffMode).empty()) {
				summary << " | handoff: " << step.handoffMode;
			}
		}
	}
	return summary.str();
}

ofxGgmlVideoPlannerResult ofxGgmlVideoPlanner::plan(
	const std::string & modelPath,
	const ofxGgmlVideoPlannerRequest & request,
	const ofxGgmlInferenceSettings & settings,
	const ofxGgmlInference & inference) const {
	ofxGgmlVideoPlannerResult result;
	result.planningPrompt = buildPlanningPrompt(request);
	if (trim(modelPath).empty() && !settings.useServerBackend) {
		result.error = "No text model selected for video planning.";
		return result;
	}

	const ofxGgmlInferenceResult inferenceResult = inference.generate(
		modelPath,
		result.planningPrompt,
		settings);
	result.elapsedMs = inferenceResult.elapsedMs;
	result.rawText = inferenceResult.text;
	if (!inferenceResult.success) {
		result.error = inferenceResult.error.empty()
			? "Video planning request failed."
			: inferenceResult.error;
		return result;
	}

	const auto parsedPlan = parsePlanJson(inferenceResult.text);
	if (!parsedPlan.isOk()) {
		result.error = parsedPlan.error().message;
		return result;
	}

	result.success = true;
	result.plan = parsedPlan.value();
	return result;
}

ofxGgmlVideoEditPlannerResult ofxGgmlVideoPlanner::planEdits(
	const std::string & modelPath,
	const ofxGgmlVideoEditPlannerRequest & request,
	const ofxGgmlInferenceSettings & settings,
	const ofxGgmlInference & inference) const {
	ofxGgmlVideoEditPlannerResult result;
	result.planningPrompt = buildEditingPrompt(request);
	if (trim(modelPath).empty() && !settings.useServerBackend) {
		result.error = "No text model selected for video edit planning.";
		return result;
	}

	const ofxGgmlInferenceResult inferenceResult = inference.generate(
		modelPath,
		result.planningPrompt,
		settings);
	result.elapsedMs = inferenceResult.elapsedMs;
	result.rawText = inferenceResult.text;
	if (!inferenceResult.success) {
		result.error = inferenceResult.error.empty()
			? "Video edit planning request failed."
			: inferenceResult.error;
		return result;
	}

	const auto parsedPlan = parseEditPlanJson(inferenceResult.text);
	if (!parsedPlan.isOk()) {
		result.error = parsedPlan.error().message;
		return result;
	}

	result.success = true;
	result.plan = parsedPlan.value();
	return result;
}

ofxGgmlVideoPlanner::SceneCoherenceResult
ofxGgmlVideoPlanner::validateSceneCoherence(
	const ofxGgmlVideoPlan & plan,
	ofxGgmlClipInference * clipInference) {
	SceneCoherenceResult result;

	if (!clipInference || !clipInference->getBackend()) {
		result.error = "CLIP inference backend not configured";
		return result;
	}

	if (plan.scenes.empty()) {
		result.success = true;
		result.averageScore = 1.0f;
		return result;
	}

	// Embed the overall scene description as reference
	auto overallEmbed = clipInference->embedText(
		plan.overallScene.empty() ? plan.originalPrompt : plan.overallScene,
		true);
	if (!overallEmbed.success) {
		result.error = "Failed to embed overall scene: " + overallEmbed.error;
		return result;
	}

	// Check each scene's coherence with the overall vision
	float totalScore = 0.0f;
	for (const auto & scene : plan.scenes) {
		std::string scenePrompt = scene.eventPrompt;
		if (scenePrompt.empty()) {
			scenePrompt = scene.title + " " + scene.summary;
		}

		auto sceneEmbed = clipInference->embedText(scenePrompt, true);
		if (!sceneEmbed.success) {
			result.warnings.push_back(
				"Failed to validate scene " + std::to_string(scene.index) +
				": " + sceneEmbed.error);
			result.sceneScores.push_back(0.0f);
			continue;
		}

		float score = ofxGgmlClipInference::cosineSimilarity(
			overallEmbed.embedding,
			sceneEmbed.embedding);
		result.sceneScores.push_back(score);
		totalScore += score;

		// Warn about low coherence scenes
		if (score < 0.5f) {
			result.warnings.push_back(
				"Scene " + std::to_string(scene.index) + " (" + scene.title +
				") has low coherence with overall vision (score: " +
				std::to_string(score) + ")");
		}
	}

	result.success = true;
	result.averageScore = plan.scenes.empty() ? 1.0f : totalScore / plan.scenes.size();
	return result;
}

ofxGgmlVideoPlanner::VideoAnalysisHints
ofxGgmlVideoPlanner::extractHintsFromVideoAnalysis(
	const ofxGgmlVideoStructuredAnalysis & analysis) {
	VideoAnalysisHints hints;

	// Extract emotion data
	hints.primaryEmotion = analysis.primaryLabel;
	hints.emotionConfidence = analysis.confidence;

	// Extract actions
	hints.actionLabels = analysis.secondaryLabels;

	// Extract timeline
	hints.timeline = analysis.timeline;

	// Suggest pacing based on analysis type and content
	if (analysis.analysisType == "Action") {
		// High-action content suggests faster pacing
		if (!hints.actionLabels.empty()) {
			hints.suggestedPacing = "dynamic and fast-paced";
		} else {
			hints.suggestedPacing = "moderate";
		}
	} else if (analysis.analysisType == "Emotion") {
		// Emotional content suggests slower, more contemplative pacing
		const std::string valence = toLower(analysis.valence);
		const std::string arousal = toLower(analysis.arousal);

		if (valence.find("negative") != std::string::npos ||
			arousal.find("low") != std::string::npos) {
			hints.suggestedPacing = "slow and contemplative";
			hints.suggestedTone = "reflective";
		} else if (arousal.find("high") != std::string::npos) {
			hints.suggestedPacing = "energetic";
			hints.suggestedTone = "intense";
		} else {
			hints.suggestedPacing = "moderate";
			hints.suggestedTone = "balanced";
		}
	}

	return hints;
}

std::string ofxGgmlVideoPlanner::enrichPlanningPromptWithHints(
	const std::string & basePrompt,
	const VideoAnalysisHints & hints) {
	std::ostringstream enriched;
	enriched << basePrompt;

	if (!hints.primaryEmotion.empty()) {
		enriched << "\n\nVideo analysis hints:";
		enriched << "\n- Detected primary emotion/action: " << hints.primaryEmotion;
		if (hints.emotionConfidence > 0.0f) {
			enriched << " (confidence: " << hints.emotionConfidence << ")";
		}
	}

	if (!hints.actionLabels.empty()) {
		enriched << "\n- Secondary elements:";
		for (const auto & action : hints.actionLabels) {
			enriched << " " << action << ",";
		}
	}

	if (!hints.suggestedPacing.empty()) {
		enriched << "\n- Suggested pacing: " << hints.suggestedPacing;
	}

	if (!hints.suggestedTone.empty()) {
		enriched << "\n- Suggested tone: " << hints.suggestedTone;
	}

	if (!hints.timeline.empty()) {
		enriched << "\n- Timeline progression:";
		for (const auto & moment : hints.timeline) {
			enriched << "\n  * " << moment;
		}
	}

	enriched << "\n\nUse these hints to inform scene pacing, tone, and progression.";

	return enriched.str();
}
