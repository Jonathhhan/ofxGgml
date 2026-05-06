#pragma once

#include "ofMain.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ofxGgmlPluginRegistryDetail {
inline ofJson stringArrayToJson(const std::vector<std::string> & values) {
	ofJson array = ofJson::array();
	for (const auto & value : values) {
		array.push_back(value);
	}
	return array;
}
} // namespace ofxGgmlPluginRegistryDetail

struct ofxGgmlPluginCapability {
	std::string id;
	std::string category;
	std::string description;
	std::vector<std::string> requiredContracts;
	std::map<std::string, std::string> metadata;

	void addRequiredContract(const std::string & contract) {
		if (!contract.empty()) {
			requiredContracts.push_back(contract);
		}
	}

	ofJson toJson() const {
		ofJson json;
		json["id"] = id;
		json["category"] = category;
		json["description"] = description;
		json["required_contracts"] =
			ofxGgmlPluginRegistryDetail::stringArrayToJson(requiredContracts);
		json["metadata"] = ofJson::object();
		for (const auto & item : metadata) {
			json["metadata"][item.first] = item.second;
		}
		return json;
	}
};

struct ofxGgmlPluginDescriptor {
	std::string id;
	std::string name;
	std::string category;
	std::string version;
	std::string abiVersion = "ofxGgml.plugin.v1";
	std::string addonHeader;
	std::string companionBoundary;
	std::string entryPoint;
	std::vector<ofxGgmlPluginCapability> capabilities;
	std::vector<std::string> lifecycleNotes;
	std::vector<std::string> safetyRequirements;
	std::map<std::string, std::string> metadata;

	void addCapability(const ofxGgmlPluginCapability & capability) {
		capabilities.push_back(capability);
	}

	void addLifecycleNote(const std::string & note) {
		if (!note.empty()) {
			lifecycleNotes.push_back(note);
		}
	}

	void addSafetyRequirement(const std::string & requirement) {
		if (!requirement.empty()) {
			safetyRequirements.push_back(requirement);
		}
	}

	ofJson toJson() const {
		ofJson json;
		json["id"] = id;
		json["name"] = name;
		json["category"] = category;
		json["version"] = version;
		json["abi_version"] = abiVersion;
		json["addon_header"] = addonHeader;
		json["companion_boundary"] = companionBoundary;
		json["entry_point"] = entryPoint;

		ofJson capabilityArray = ofJson::array();
		for (const auto & capability : capabilities) {
			capabilityArray.push_back(capability.toJson());
		}
		json["capabilities"] = std::move(capabilityArray);
		json["lifecycle_notes"] =
			ofxGgmlPluginRegistryDetail::stringArrayToJson(lifecycleNotes);
		json["safety_requirements"] =
			ofxGgmlPluginRegistryDetail::stringArrayToJson(safetyRequirements);

		json["metadata"] = ofJson::object();
		for (const auto & item : metadata) {
			json["metadata"][item.first] = item.second;
		}
		return json;
	}
};

struct ofxGgmlPluginRegistry {
	std::string schemaVersion = "ofxGgml.plugin_registry.v1";
	std::string registryId;
	std::string title;
	std::string abiVersion = "ofxGgml.plugin.v1";
	std::vector<ofxGgmlPluginDescriptor> plugins;
	std::vector<std::string> compatibilityRules;
	std::vector<std::string> reviewNotes;
	std::map<std::string, std::string> metadata;

	void addPlugin(const ofxGgmlPluginDescriptor & plugin) {
		plugins.push_back(plugin);
	}

	void addCompatibilityRule(const std::string & rule) {
		if (!rule.empty()) {
			compatibilityRules.push_back(rule);
		}
	}

	void addReviewNote(const std::string & note) {
		if (!note.empty()) {
			reviewNotes.push_back(note);
		}
	}

	ofJson toJson() const {
		ofJson json;
		json["schema_version"] = schemaVersion;
		json["registry_id"] = registryId;
		json["title"] = title;
		json["abi_version"] = abiVersion;

		ofJson pluginArray = ofJson::array();
		for (const auto & plugin : plugins) {
			pluginArray.push_back(plugin.toJson());
		}
		json["plugins"] = std::move(pluginArray);
		json["compatibility_rules"] =
			ofxGgmlPluginRegistryDetail::stringArrayToJson(compatibilityRules);
		json["review_notes"] =
			ofxGgmlPluginRegistryDetail::stringArrayToJson(reviewNotes);

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

inline ofxGgmlPluginCapability ofxGgmlMakePluginCapability(
	const std::string & id,
	const std::string & category,
	const std::string & description) {
	ofxGgmlPluginCapability capability;
	capability.id = id;
	capability.category = category;
	capability.description = description;
	return capability;
}

inline ofxGgmlPluginDescriptor ofxGgmlMakePluginDescriptor(
	const std::string & id,
	const std::string & name,
	const std::string & category,
	const std::string & addonHeader) {
	ofxGgmlPluginDescriptor plugin;
	plugin.id = id;
	plugin.name = name;
	plugin.category = category;
	plugin.addonHeader = addonHeader;
	return plugin;
}

inline ofxGgmlPluginRegistry ofxGgmlDefaultPluginRegistry() {
	ofxGgmlPluginRegistry registry;
	registry.registryId = "ecosystem-plugin-foundation";
	registry.title = "Ecosystem and extensibility plugin registry";

	auto inferenceBackend = ofxGgmlMakePluginDescriptor(
		"custom_inference_backend",
		"Custom inference backend",
		"inference_backend",
		"ofxGgmlBasic.h");
	inferenceBackend.entryPoint = "configureTextBackend";
	inferenceBackend.addCapability(ofxGgmlMakePluginCapability(
		"text_generation",
		"inference",
		"Provides local or app-owned text generation behind existing inference settings."));
	inferenceBackend.capabilities.back().addRequiredContract("ofxGgmlInferenceSettings");
	inferenceBackend.addLifecycleNote("Backend ownership remains with the host application or companion addon.");
	inferenceBackend.addSafetyRequirement("Report degraded mode and queue pressure through health diagnostics.");
	registry.addPlugin(inferenceBackend);

	auto workflowNode = ofxGgmlMakePluginDescriptor(
		"companion_workflow_node",
		"Companion workflow node",
		"workflow_node",
		"ofxGgmlWorkflows.h");
	workflowNode.companionBoundary = "ofxGgmlCompanionWorkflows.h";
	workflowNode.entryPoint = "workflow_manifest_contract";
	workflowNode.addCapability(ofxGgmlMakePluginCapability(
		"stage_handoff",
		"workflow",
		"Consumes and produces typed workflow manifest inputs, outputs, artifacts, and warnings."));
	workflowNode.capabilities.back().addRequiredContract("ofxGgmlWorkflowManifest");
	workflowNode.addLifecycleNote("Keep long-running workflow orchestration outside the default addon boundary.");
	workflowNode.addSafetyRequirement("Preserve provenance and replay metadata for every downstream handoff.");
	registry.addPlugin(workflowNode);

	auto modalityRenderer = ofxGgmlMakePluginDescriptor(
		"modality_renderer",
		"Modality or renderer adapter",
		"modality_renderer",
		"ofxGgmlModalities.h");
	modalityRenderer.companionBoundary = "optional companion addon";
	modalityRenderer.entryPoint = "adapter_factory";
	modalityRenderer.addCapability(ofxGgmlMakePluginCapability(
		"media_generation",
		"multimodal",
		"Bridges app-owned speech, vision, image, audio, or render runtimes into explicit modality layers."));
	modalityRenderer.capabilities.back().addRequiredContract("explicit opt-in header");
	modalityRenderer.addLifecycleNote("Do not require heavyweight media runtimes from core-only users.");
	modalityRenderer.addSafetyRequirement("Expose model/runtime provenance and user-visible failure messages.");
	registry.addPlugin(modalityRenderer);

	auto retrievalProvider = ofxGgmlMakePluginDescriptor(
		"search_retrieval_provider",
		"Search or retrieval provider",
		"retrieval_provider",
		"ofxGgmlWorkflows.h");
	retrievalProvider.entryPoint = "source_provider";
	retrievalProvider.addCapability(ofxGgmlMakePluginCapability(
		"source_grounding",
		"retrieval",
		"Adds local, domain, or app-provided source retrieval while keeping citation evidence inspectable."));
	retrievalProvider.capabilities.back().addRequiredContract("citation evidence refs");
	retrievalProvider.addLifecycleNote("Providers should be replaceable without changing downstream citation contracts.");
	retrievalProvider.addSafetyRequirement("Track source URLs, local paths, timestamps, and unresolved warnings.");
	registry.addPlugin(retrievalProvider);

	auto assistantTool = ofxGgmlMakePluginDescriptor(
		"assistant_tool_adapter",
		"Assistant tool adapter",
		"assistant_tool",
		"ofxGgmlAssistants.h");
	assistantTool.entryPoint = "tool_registry";
	assistantTool.addCapability(ofxGgmlMakePluginCapability(
		"approval_first_tool",
		"assistant",
		"Registers assistant capabilities that can be planned, reviewed, approved, and verified."));
	assistantTool.capabilities.back().addRequiredContract("ofxGgmlAssistantTeamSpec");
	assistantTool.addLifecycleNote("Tool adapters should support dry-run previews where possible.");
	assistantTool.addSafetyRequirement("Risky or destructive operations require explicit approval callbacks.");
	registry.addPlugin(assistantTool);

	registry.addCompatibilityRule("Declare schema and ABI versions before dynamic loading or companion handoff.");
	registry.addCompatibilityRule("Prefer explicit layered headers over implicit all-in-one plugin activation.");
	registry.addCompatibilityRule("Keep heavyweight runtimes optional and companion-owned.");
	registry.addReviewNote("This registry is a metadata contract, not a dynamic loader.");
	registry.addReviewNote("Use plugin descriptors to document boundaries before adding executable extension points.");

	return registry;
}
