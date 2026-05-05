#pragma once

#include "ofMain.h"

#include <map>
#include <string>
#include <vector>

struct ofxGgmlWorkflowManifestInput {
	std::string name;
	std::string type;
	std::string value;
	std::string source;
	std::map<std::string, std::string> metadata;

	ofJson toJson() const {
		ofJson json;
		json["name"] = name;
		json["type"] = type;
		json["value"] = value;
		json["source"] = source;
		json["metadata"] = ofJson::object();
		for (const auto & item : metadata) {
			json["metadata"][item.first] = item.second;
		}
		return json;
	}
};

struct ofxGgmlWorkflowManifestArtifact {
	std::string id;
	std::string type;
	std::string path;
	std::string mimeType;
	std::string description;
	std::map<std::string, std::string> metadata;

	ofJson toJson() const {
		ofJson json;
		json["id"] = id;
		json["type"] = type;
		json["path"] = path;
		json["mime_type"] = mimeType;
		json["description"] = description;
		json["metadata"] = ofJson::object();
		for (const auto & item : metadata) {
			json["metadata"][item.first] = item.second;
		}
		return json;
	}
};

struct ofxGgmlWorkflowHandoff {
	std::string target;
	std::string mode;
	std::string contract;
	std::string notes;
	std::map<std::string, std::string> metadata;

	bool empty() const {
		return target.empty() && mode.empty() && contract.empty() && notes.empty() && metadata.empty();
	}

	ofJson toJson() const {
		ofJson json;
		json["target"] = target;
		json["mode"] = mode;
		json["contract"] = contract;
		json["notes"] = notes;
		json["metadata"] = ofJson::object();
		for (const auto & item : metadata) {
			json["metadata"][item.first] = item.second;
		}
		return json;
	}
};

struct ofxGgmlWorkflowManifest {
	std::string schemaVersion = "ofxGgml.workflow_manifest.v1";
	std::string workflowType;
	std::string runId;
	std::string createdAt;
	std::string status;
	std::string summary;
	std::vector<ofxGgmlWorkflowManifestInput> inputs;
	std::vector<ofxGgmlWorkflowManifestArtifact> artifacts;
	std::vector<ofxGgmlWorkflowManifestArtifact> intermediateOutputs;
	std::vector<std::string> warnings;
	std::vector<std::string> reviewNotes;
	ofxGgmlWorkflowHandoff handoff;
	std::map<std::string, std::string> metadata;

	void addInput(
		const std::string & name,
		const std::string & type,
		const std::string & value,
		const std::string & source = "") {
		ofxGgmlWorkflowManifestInput input;
		input.name = name;
		input.type = type;
		input.value = value;
		input.source = source;
		inputs.push_back(input);
	}

	void addArtifact(
		const std::string & id,
		const std::string & type,
		const std::string & path,
		const std::string & description = "") {
		ofxGgmlWorkflowManifestArtifact artifact;
		artifact.id = id;
		artifact.type = type;
		artifact.path = path;
		artifact.description = description;
		artifacts.push_back(artifact);
	}

	void addIntermediateOutput(
		const std::string & id,
		const std::string & type,
		const std::string & path,
		const std::string & description = "") {
		ofxGgmlWorkflowManifestArtifact artifact;
		artifact.id = id;
		artifact.type = type;
		artifact.path = path;
		artifact.description = description;
		intermediateOutputs.push_back(artifact);
	}

	ofJson toJson() const {
		ofJson json;
		json["schema_version"] = schemaVersion;
		json["workflow_type"] = workflowType;
		json["run_id"] = runId;
		json["created_at"] = createdAt;
		json["status"] = status;
		json["summary"] = summary;

		ofJson inputArray = ofJson::array();
		for (const auto & input : inputs) {
			inputArray.push_back(input.toJson());
		}
		json["inputs"] = std::move(inputArray);

		ofJson artifactArray = ofJson::array();
		for (const auto & artifact : artifacts) {
			artifactArray.push_back(artifact.toJson());
		}
		json["artifacts"] = std::move(artifactArray);

		ofJson intermediateArray = ofJson::array();
		for (const auto & artifact : intermediateOutputs) {
			intermediateArray.push_back(artifact.toJson());
		}
		json["intermediate_outputs"] = std::move(intermediateArray);

		ofJson warningArray = ofJson::array();
		for (const auto & warning : warnings) {
			warningArray.push_back(warning);
		}
		json["warnings"] = std::move(warningArray);

		ofJson reviewArray = ofJson::array();
		for (const auto & note : reviewNotes) {
			reviewArray.push_back(note);
		}
		json["review_notes"] = std::move(reviewArray);

		json["handoff"] = handoff.empty() ? ofJson::object() : handoff.toJson();
		json["metadata"] = ofJson::object();
		for (const auto & item : metadata) {
			json["metadata"][item.first] = item.second;
		}
		return json;
	}

	std::string toJsonString() const {
		return toJson().dump();
	}
};
