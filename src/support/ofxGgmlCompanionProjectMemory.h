#pragma once

#include "ofMain.h"

#include <map>
#include <string>
#include <vector>

struct ofxGgmlCompanionMemoryReference {
	std::string id;
	std::string type;
	std::string title;
	std::string uri;
	std::string note;
	float confidence = 0.0f;
	std::map<std::string, std::string> metadata;

	ofJson toJson() const {
		ofJson json;
		json["id"] = id;
		json["type"] = type;
		json["title"] = title;
		json["uri"] = uri;
		json["note"] = note;
		json["confidence"] = confidence;
		json["metadata"] = ofJson::object();
		for (const auto & item : metadata) {
			json["metadata"][item.first] = item.second;
		}
		return json;
	}
};

struct ofxGgmlCompanionMemoryToolSetting {
	std::string tool;
	std::string key;
	std::string value;
	std::string reason;
	std::map<std::string, std::string> metadata;

	ofJson toJson() const {
		ofJson json;
		json["tool"] = tool;
		json["key"] = key;
		json["value"] = value;
		json["reason"] = reason;
		json["metadata"] = ofJson::object();
		for (const auto & item : metadata) {
			json["metadata"][item.first] = item.second;
		}
		return json;
	}
};

struct ofxGgmlCompanionMemoryManifestLink {
	std::string id;
	std::string workflowType;
	std::string path;
	std::string relationship;
	std::string note;
	std::map<std::string, std::string> metadata;

	ofJson toJson() const {
		ofJson json;
		json["id"] = id;
		json["workflow_type"] = workflowType;
		json["path"] = path;
		json["relationship"] = relationship;
		json["note"] = note;
		json["metadata"] = ofJson::object();
		for (const auto & item : metadata) {
			json["metadata"][item.first] = item.second;
		}
		return json;
	}
};

struct ofxGgmlCompanionProjectMemory {
	std::string schemaVersion = "ofxGgml.companion_project_memory.v1";
	std::string projectId;
	std::string title;
	std::string updatedAt;
	std::vector<std::string> creativeIntent;
	std::vector<std::string> acceptedPrompts;
	std::vector<ofxGgmlCompanionMemoryReference> acceptedReferences;
	std::vector<std::string> styleNotes;
	std::vector<std::string> continuityRules;
	std::vector<ofxGgmlCompanionMemoryToolSetting> preferredToolSettings;
	std::vector<ofxGgmlCompanionMemoryManifestLink> workflowManifests;
	std::vector<std::string> reviewNotes;
	std::map<std::string, std::string> metadata;

	void addCreativeIntent(const std::string & intent) {
		if (!intent.empty()) {
			creativeIntent.push_back(intent);
		}
	}

	void addAcceptedPrompt(const std::string & prompt) {
		if (!prompt.empty()) {
			acceptedPrompts.push_back(prompt);
		}
	}

	void addStyleNote(const std::string & note) {
		if (!note.empty()) {
			styleNotes.push_back(note);
		}
	}

	void addContinuityRule(const std::string & rule) {
		if (!rule.empty()) {
			continuityRules.push_back(rule);
		}
	}

	void addReviewNote(const std::string & note) {
		if (!note.empty()) {
			reviewNotes.push_back(note);
		}
	}

	void addReference(
		const std::string & id,
		const std::string & type,
		const std::string & title,
		const std::string & uri = "") {
		ofxGgmlCompanionMemoryReference reference;
		reference.id = id;
		reference.type = type;
		reference.title = title;
		reference.uri = uri;
		acceptedReferences.push_back(reference);
	}

	void addToolSetting(
		const std::string & tool,
		const std::string & key,
		const std::string & value,
		const std::string & reason = "") {
		ofxGgmlCompanionMemoryToolSetting setting;
		setting.tool = tool;
		setting.key = key;
		setting.value = value;
		setting.reason = reason;
		preferredToolSettings.push_back(setting);
	}

	void addWorkflowManifest(
		const std::string & id,
		const std::string & workflowType,
		const std::string & path,
		const std::string & relationship = "") {
		ofxGgmlCompanionMemoryManifestLink manifest;
		manifest.id = id;
		manifest.workflowType = workflowType;
		manifest.path = path;
		manifest.relationship = relationship;
		workflowManifests.push_back(manifest);
	}

	ofJson toJson() const {
		ofJson json;
		json["schema_version"] = schemaVersion;
		json["project_id"] = projectId;
		json["title"] = title;
		json["updated_at"] = updatedAt;
		json["creative_intent"] = toStringArray(creativeIntent);
		json["accepted_prompts"] = toStringArray(acceptedPrompts);

		ofJson references = ofJson::array();
		for (const auto & reference : acceptedReferences) {
			references.push_back(reference.toJson());
		}
		json["accepted_references"] = std::move(references);

		json["style_notes"] = toStringArray(styleNotes);
		json["continuity_rules"] = toStringArray(continuityRules);

		ofJson toolSettings = ofJson::array();
		for (const auto & setting : preferredToolSettings) {
			toolSettings.push_back(setting.toJson());
		}
		json["preferred_tool_settings"] = std::move(toolSettings);

		ofJson manifests = ofJson::array();
		for (const auto & manifest : workflowManifests) {
			manifests.push_back(manifest.toJson());
		}
		json["workflow_manifests"] = std::move(manifests);

		json["review_notes"] = toStringArray(reviewNotes);

		json["metadata"] = ofJson::object();
		for (const auto & item : metadata) {
			json["metadata"][item.first] = item.second;
		}
		return json;
	}

	std::string toJsonString() const {
		return toJson().dump();
	}

private:
	static ofJson toStringArray(const std::vector<std::string> & values) {
		ofJson array = ofJson::array();
		for (const auto & value : values) {
			array.push_back(value);
		}
		return array;
	}
};
