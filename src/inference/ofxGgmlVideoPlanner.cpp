#include "ofxGgmlVideoPlanner.h"

#ifndef OFXGGML_HEADLESS_STUBS
#include "ofJson.h"
#endif

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace {

std::string trimCopy(const std::string & text) {
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

template <typename T>
T jsonValueOr(const ofJson & json, const char * key, const T & fallback) {
	if (!json.is_object()) {
		return fallback;
	}
	const auto it = json.find(key);
	if (it == json.end() || it->is_null()) {
		return fallback;
	}
	try {
		return it->get<T>();
	} catch (...) {
		return fallback;
	}
}

std::vector<std::string> jsonStringArray(const ofJson & json, const char * key) {
	std::vector<std::string> values;
	if (!json.is_object()) {
		return values;
	}
	const auto it = json.find(key);
	if (it == json.end() || !it->is_array()) {
		return values;
	}
	for (const auto & item : *it) {
		if (item.is_string()) {
			values.push_back(item.get<std::string>());
		}
	}
	return values;
}

std::string formatSeconds(double seconds) {
	std::ostringstream out;
	out << std::fixed << std::setprecision(1) << std::max(0.0, seconds) << "s";
	return out.str();
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
	prompt << "\n"
		<< "Requested duration: " << formatSeconds(durationSeconds) << "\n"
		<< "Requested beat count: " << beatCount << "\n";
	if (request.multiScene) {
		prompt << "Requested scene count: " << sceneCount << "\n";
	}
	if (!trimCopy(request.preferredStyle).empty()) {
		prompt << "Preferred style: " << trimCopy(request.preferredStyle) << "\n";
	}
	if (!trimCopy(request.negativePrompt).empty()) {
		prompt << "Negative prompt hints: " << trimCopy(request.negativePrompt) << "\n";
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
		<< "User prompt:\n"
		<< trimCopy(request.prompt);
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
		<< trimCopy(request.editGoal) << "\n\n";
	if (!trimCopy(request.sourcePrompt).empty()) {
		prompt << "Original video prompt or source note:\n"
			<< trimCopy(request.sourcePrompt) << "\n\n";
	}
	if (!trimCopy(request.sourceAnalysis).empty()) {
		prompt << "Source video analysis:\n"
			<< trimCopy(request.sourceAnalysis) << "\n";
	}
	return prompt.str();
}

std::string ofxGgmlVideoPlanner::extractJsonObject(const std::string & text) {
	const std::string trimmed = trimCopy(text);
	if (trimmed.empty()) {
		return {};
	}
	const size_t fenceStart = trimmed.find("```");
	if (fenceStart != std::string::npos) {
		size_t jsonStart = trimmed.find('{', fenceStart);
		size_t fenceEnd = trimmed.rfind("```");
		if (jsonStart != std::string::npos && fenceEnd != std::string::npos && fenceEnd > jsonStart) {
			return trimCopy(trimmed.substr(jsonStart, fenceEnd - jsonStart));
		}
	}
	const size_t start = trimmed.find('{');
	const size_t end = trimmed.rfind('}');
	if (start == std::string::npos || end == std::string::npos || end <= start) {
		return {};
	}
	return trimCopy(trimmed.substr(start, end - start + 1));
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

	const auto entitiesIt = json.find("entities");
	if (entitiesIt != json.end() && entitiesIt->is_array()) {
		for (const auto & item : *entitiesIt) {
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

	const auto subjectsIt = json.find("subjects");
	if (subjectsIt != json.end() && subjectsIt->is_array()) {
		for (const auto & item : *subjectsIt) {
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

	const auto beatsIt = json.find("beats");
	if (beatsIt != json.end() && beatsIt->is_array()) {
		for (const auto & item : *beatsIt) {
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

	const auto scenesIt = json.find("scenes");
	if (scenesIt != json.end() && scenesIt->is_array()) {
		for (const auto & item : *scenesIt) {
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

	const auto clipsIt = json.find("clips");
	if (clipsIt != json.end() && clipsIt->is_array()) {
		for (const auto & item : *clipsIt) {
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

	const auto actionsIt = json.find("actions");
	if (actionsIt != json.end() && actionsIt->is_array()) {
		for (const auto & item : *actionsIt) {
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
	if (!trimCopy(plan.originalPrompt).empty()) {
		prompt << plan.originalPrompt;
	} else {
		prompt << "Generate a coherent short video that follows the structured beat plan below.";
	}
	prompt << "\n\nStructured video plan:";
	if (!trimCopy(plan.style).empty()) {
		prompt << "\nStyle: " << plan.style;
	}
	if (!trimCopy(plan.overallScene).empty()) {
		prompt << "\nOverall scene: " << plan.overallScene;
	}
	if (!trimCopy(plan.overallCamera).empty()) {
		prompt << "\nOverall camera: " << plan.overallCamera;
	}
	if (!trimCopy(plan.continuityNotes).empty()) {
		prompt << "\nContinuity: " << plan.continuityNotes;
	}
	if (!plan.entities.empty()) {
		prompt << "\nRecurring entities:";
		for (const auto & entity : plan.entities) {
			prompt << "\n- " << (!entity.label.empty() ? entity.label : entity.id);
			if (!trimCopy(entity.role).empty()) {
				prompt << " (" << entity.role << ")";
			}
			if (!trimCopy(entity.description).empty()) {
				prompt << ": " << entity.description;
			}
		}
	}
	if (!plan.subjects.empty()) {
		prompt << "\nSubjects:";
		for (const auto & subject : plan.subjects) {
			prompt << "\n- " << (!subject.label.empty() ? subject.label : subject.id);
			if (!trimCopy(subject.description).empty()) {
				prompt << ": " << subject.description;
			}
		}
	}
	prompt << "\nTemporal beats:";
	for (size_t i = 0; i < plan.beats.size(); ++i) {
		const auto & beat = plan.beats[i];
		prompt << "\n" << (i + 1) << ". "
			<< formatSeconds(beat.startSeconds) << " to " << formatSeconds(beat.endSeconds)
			<< " - " << trimCopy(beat.summary);
		if (!trimCopy(beat.scene).empty()) {
			prompt << " | scene: " << beat.scene;
		}
		if (!trimCopy(beat.camera).empty()) {
			prompt << " | camera: " << beat.camera;
		}
		if (!trimCopy(beat.motion).empty()) {
			prompt << " | motion: " << beat.motion;
		}
		if (!trimCopy(beat.visualGoal).empty()) {
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
			if (!trimCopy(scene.title).empty()) {
				prompt << " (" << scene.title << ")";
			}
			if (!trimCopy(scene.summary).empty()) {
				prompt << ": " << scene.summary;
			}
			if (!trimCopy(scene.eventPrompt).empty()) {
				prompt << " | prompt: " << scene.eventPrompt;
			}
			if (!trimCopy(scene.background).empty()) {
				prompt << " | background: " << scene.background;
			}
			if (!trimCopy(scene.cameraMovement).empty()) {
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
	if (!trimCopy(plan.negativePrompt).empty()) {
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
	if (!trimCopy(scene.eventPrompt).empty()) {
		prompt << scene.eventPrompt;
	} else if (!trimCopy(scene.summary).empty()) {
		prompt << scene.summary;
	} else {
		prompt << plan.originalPrompt;
	}
	if (!trimCopy(plan.style).empty()) {
		prompt << "\nStyle: " << plan.style;
	}
	if (!trimCopy(scene.background).empty()) {
		prompt << "\nBackground: " << scene.background;
	}
	if (!trimCopy(scene.cameraMovement).empty()) {
		prompt << "\nCamera: " << scene.cameraMovement;
	}
	if (scene.durationSeconds > 0.0) {
		prompt << "\nTarget scene duration: " << formatSeconds(scene.durationSeconds);
	}
	if (!trimCopy(scene.transition).empty()) {
		prompt << "\nTransition: " << scene.transition;
	}
	if (!trimCopy(plan.continuityNotes).empty()) {
		prompt << "\nContinuity: " << plan.continuityNotes;
	}
	if (sceneIndex > 0) {
		const auto & previousScene = plan.scenes[sceneIndex - 1];
		const std::string previousSceneLabel =
			!trimCopy(previousScene.title).empty() ? previousScene.title : previousScene.summary;
		if (!trimCopy(previousSceneLabel).empty()) {
			prompt << "\nPrevious scene context: " << previousSceneLabel;
		}
	}
	if (sceneIndex + 1 < plan.scenes.size()) {
		const auto & nextScene = plan.scenes[sceneIndex + 1];
		const std::string nextSceneLabel =
			!trimCopy(nextScene.title).empty() ? nextScene.title : nextScene.summary;
		if (!trimCopy(nextSceneLabel).empty()) {
			prompt << "\nNext scene should transition toward: " << nextSceneLabel;
		}
	}
	if (!scene.entityIds.empty() && !plan.entities.empty()) {
		prompt << "\nEntities:";
		for (const auto & entityId : scene.entityIds) {
			const auto * entity = findEntityByIdOrLabel(plan, entityId);
			if (entity != nullptr) {
				prompt << "\n- " << (!entity->label.empty() ? entity->label : entity->id);
				if (!trimCopy(entity->role).empty()) {
					prompt << " (" << entity->role << ")";
				}
				if (!trimCopy(entity->description).empty()) {
					prompt << ": " << entity->description;
				}
				if (!trimCopy(entity->referenceImagePath).empty()) {
					prompt << " | reference image: " << entity->referenceImagePath;
				}
			}
		}
	}
	if (!trimCopy(plan.negativePrompt).empty()) {
		prompt << "\nNegative prompt: " << plan.negativePrompt;
	}
	return prompt.str();
}

std::string ofxGgmlVideoPlanner::buildSceneSequencePrompt(const ofxGgmlVideoPlan & plan) {
	if (plan.scenes.empty()) {
		return buildGenerationPrompt(plan);
	}

	std::ostringstream prompt;
	if (!trimCopy(plan.originalPrompt).empty()) {
		prompt << plan.originalPrompt;
	} else {
		prompt << "Generate a coherent multi-scene video that follows the structured scene sequence below.";
	}
	if (!trimCopy(plan.style).empty()) {
		prompt << "\nStyle: " << plan.style;
	}
	if (!trimCopy(plan.overallScene).empty()) {
		prompt << "\nOverall setting: " << plan.overallScene;
	}
	if (!trimCopy(plan.overallCamera).empty()) {
		prompt << "\nOverall camera language: " << plan.overallCamera;
	}
	if (!trimCopy(plan.continuityNotes).empty()) {
		prompt << "\nContinuity notes: " << plan.continuityNotes;
	}
	if (!plan.entities.empty()) {
		prompt << "\nRecurring entities:";
		for (const auto & entity : plan.entities) {
			prompt << "\n- " << (!entity.label.empty() ? entity.label : entity.id);
			if (!trimCopy(entity.role).empty()) {
				prompt << " (" << entity.role << ")";
			}
			if (!trimCopy(entity.description).empty()) {
				prompt << ": " << entity.description;
			}
			if (!trimCopy(entity.referenceImagePath).empty()) {
				prompt << " | reference image: " << entity.referenceImagePath;
			}
		}
	}
	prompt << "\nScene sequence:";
	for (const auto & scene : plan.scenes) {
		prompt << "\n" << scene.index << ". ";
		if (!trimCopy(scene.title).empty()) {
			prompt << scene.title;
		} else {
			prompt << "Scene " << scene.index;
		}
		if (scene.durationSeconds > 0.0) {
			prompt << " (" << formatSeconds(scene.durationSeconds) << ")";
		}
		if (!trimCopy(scene.summary).empty()) {
			prompt << " | " << scene.summary;
		}
		if (!trimCopy(scene.eventPrompt).empty()) {
			prompt << " | prompt: " << scene.eventPrompt;
		}
		if (!trimCopy(scene.background).empty()) {
			prompt << " | background: " << scene.background;
		}
		if (!trimCopy(scene.cameraMovement).empty()) {
			prompt << " | camera: " << scene.cameraMovement;
		}
		if (!trimCopy(scene.transition).empty()) {
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
	if (!trimCopy(plan.negativePrompt).empty()) {
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
	if (!trimCopy(plan.style).empty()) {
		summary << "\nStyle: " << plan.style;
	}
	if (!trimCopy(plan.overallCamera).empty()) {
		summary << "\nCamera: " << plan.overallCamera;
	}
	if (!trimCopy(plan.continuityNotes).empty()) {
		summary << "\nContinuity: " << plan.continuityNotes;
	}
	if (!plan.entities.empty()) {
		summary << "\nEntities:";
		for (const auto & entity : plan.entities) {
			summary << "\n- " << (!entity.label.empty() ? entity.label : entity.id);
			if (!trimCopy(entity.role).empty()) {
				summary << " (" << entity.role << ")";
			}
		}
	}
	if (!plan.scenes.empty()) {
		summary << "\nScenes:";
		for (const auto & scene : plan.scenes) {
			summary << "\n" << scene.index << ". ";
			if (!trimCopy(scene.title).empty()) {
				summary << scene.title;
			} else {
				summary << "Scene " << scene.index;
			}
			if (!trimCopy(scene.summary).empty()) {
				summary << ": " << scene.summary;
			}
			if (scene.durationSeconds > 0.0) {
				summary << " | " << formatSeconds(scene.durationSeconds);
			}
			if (!trimCopy(scene.cameraMovement).empty()) {
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
			<< ": " << trimCopy(beat.summary);
		if (!trimCopy(beat.camera).empty()) {
			summary << " | " << beat.camera;
		}
		if (!trimCopy(beat.motion).empty()) {
			summary << " | " << beat.motion;
		}
	}
	return summary.str();
}

std::string ofxGgmlVideoPlanner::buildEditorBrief(const ofxGgmlVideoEditPlan & plan) {
	std::ostringstream brief;
	brief << "Video edit brief";
	if (!trimCopy(plan.originalGoal).empty()) {
		brief << "\nGoal: " << plan.originalGoal;
	}
	if (!trimCopy(plan.overallDirection).empty()) {
		brief << "\nDirection: " << plan.overallDirection;
	}
	if (!trimCopy(plan.pacingStrategy).empty()) {
		brief << "\nPacing: " << plan.pacingStrategy;
	}
	if (!trimCopy(plan.visualStyle).empty()) {
		brief << "\nVisual style: " << plan.visualStyle;
	}
	if (!trimCopy(plan.audioStrategy).empty()) {
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
				<< " | " << (!trimCopy(clip.purpose).empty() ? clip.purpose : clip.sourceDescription);
			if (!trimCopy(clip.treatment).empty()) {
				brief << " | " << clip.treatment;
			}
			if (!trimCopy(clip.transition).empty()) {
				brief << " | transition: " << clip.transition;
			}
			if (!trimCopy(clip.textOverlay).empty()) {
				brief << " | text: " << clip.textOverlay;
			}
		}
	}
	if (!plan.actions.empty()) {
		brief << "\nEdit actions:";
		for (const auto & action : plan.actions) {
			brief << "\n- [" << action.type << "] "
				<< (!trimCopy(action.instruction).empty() ? action.instruction : action.rationale);
			if (action.endSeconds > action.startSeconds) {
				brief << " (" << formatSeconds(action.startSeconds) << " - " << formatSeconds(action.endSeconds) << ")";
			}
			if (!trimCopy(action.assetHint).empty()) {
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

std::string ofxGgmlVideoPlanner::summarizeEditPlan(const ofxGgmlVideoEditPlan & plan) {
	std::ostringstream summary;
	summary << "Video edit plan with " << plan.clips.size() << " clip(s) and " << plan.actions.size() << " action(s)";
	if (plan.targetDurationSeconds > 0.0) {
		summary << "\nTarget duration: " << formatSeconds(plan.targetDurationSeconds);
	}
	if (!trimCopy(plan.overallDirection).empty()) {
		summary << "\nDirection: " << plan.overallDirection;
	}
	if (!trimCopy(plan.pacingStrategy).empty()) {
		summary << "\nPacing: " << plan.pacingStrategy;
	}
	if (!trimCopy(plan.visualStyle).empty()) {
		summary << "\nVisual style: " << plan.visualStyle;
	}
	if (!plan.clips.empty()) {
		summary << "\nClips:";
		for (const auto & clip : plan.clips) {
			summary << "\n" << clip.index << ". "
				<< formatSeconds(clip.startSeconds) << " - " << formatSeconds(clip.endSeconds)
				<< ": " << (!trimCopy(clip.purpose).empty() ? clip.purpose : clip.sourceDescription);
			if (!trimCopy(clip.treatment).empty()) {
				summary << " | " << clip.treatment;
			}
		}
	}
	if (!plan.actions.empty()) {
		summary << "\nActions:";
		for (const auto & action : plan.actions) {
			summary << "\n- " << (!trimCopy(action.type).empty() ? action.type : "edit");
			if (!trimCopy(action.instruction).empty()) {
				summary << ": " << action.instruction;
			} else if (!trimCopy(action.rationale).empty()) {
				summary << ": " << action.rationale;
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
	if (trimCopy(modelPath).empty() && !settings.useServerBackend) {
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
	if (trimCopy(modelPath).empty() && !settings.useServerBackend) {
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
