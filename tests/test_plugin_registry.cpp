#include "catch2.hpp"
#include "../src/ofxGgmlCore.h"

#include <set>

TEST_CASE("Plugin registry exposes roadmap plugin categories", "[plugin_registry]") {
	const auto registry = ofxGgmlDefaultPluginRegistry();
	REQUIRE(registry.schemaVersion == "ofxGgml.plugin_registry.v1");
	REQUIRE(registry.abiVersion == "ofxGgml.plugin.v1");
	REQUIRE(registry.plugins.size() == 5);
	REQUIRE(registry.compatibilityRules.size() == 3);
	REQUIRE(registry.reviewNotes.size() == 2);

	std::set<std::string> categories;
	for (const auto & plugin : registry.plugins) {
		categories.insert(plugin.category);
		REQUIRE_FALSE(plugin.id.empty());
		REQUIRE_FALSE(plugin.name.empty());
		REQUIRE_FALSE(plugin.addonHeader.empty());
		REQUIRE_FALSE(plugin.entryPoint.empty());
		REQUIRE_FALSE(plugin.capabilities.empty());
		REQUIRE_FALSE(plugin.lifecycleNotes.empty());
		REQUIRE_FALSE(plugin.safetyRequirements.empty());
		REQUIRE(plugin.abiVersion == "ofxGgml.plugin.v1");

		for (const auto & capability : plugin.capabilities) {
			REQUIRE_FALSE(capability.id.empty());
			REQUIRE_FALSE(capability.category.empty());
			REQUIRE_FALSE(capability.description.empty());
			REQUIRE_FALSE(capability.requiredContracts.empty());
		}
	}

	REQUIRE(categories.count("inference_backend") == 1);
	REQUIRE(categories.count("workflow_node") == 1);
	REQUIRE(categories.count("modality_renderer") == 1);
	REQUIRE(categories.count("retrieval_provider") == 1);
	REQUIRE(categories.count("assistant_tool") == 1);
}

TEST_CASE("Plugin registry serializes stable JSON keys", "[plugin_registry]") {
	auto registry = ofxGgmlDefaultPluginRegistry();
	registry.metadata["owner"] = "ecosystem";
	registry.plugins.front().metadata["stability"] = "experimental";
	registry.plugins.front().capabilities.front().metadata["scope"] = "local";

	const auto json = registry.toJsonString();
	REQUIRE(json.find("\"schema_version\"") != std::string::npos);
	REQUIRE(json.find("ofxGgml.plugin_registry.v1") != std::string::npos);
	REQUIRE(json.find("\"registry_id\"") != std::string::npos);
	REQUIRE(json.find("\"abi_version\"") != std::string::npos);
	REQUIRE(json.find("\"plugins\"") != std::string::npos);
	REQUIRE(json.find("\"addon_header\"") != std::string::npos);
	REQUIRE(json.find("\"companion_boundary\"") != std::string::npos);
	REQUIRE(json.find("\"entry_point\"") != std::string::npos);
	REQUIRE(json.find("\"capabilities\"") != std::string::npos);
	REQUIRE(json.find("\"required_contracts\"") != std::string::npos);
	REQUIRE(json.find("\"lifecycle_notes\"") != std::string::npos);
	REQUIRE(json.find("\"safety_requirements\"") != std::string::npos);
	REQUIRE(json.find("\"compatibility_rules\"") != std::string::npos);
	REQUIRE(json.find("\"review_notes\"") != std::string::npos);
	REQUIRE(json.find("ofxGgmlWorkflowManifest") != std::string::npos);
	REQUIRE(json.find("ofxGgmlAssistantTeamSpec") != std::string::npos);
	REQUIRE(json.find("experimental") != std::string::npos);
}

TEST_CASE("Plugin registry ignores empty convenience entries", "[plugin_registry]") {
	ofxGgmlPluginCapability capability;
	capability.addRequiredContract("");

	ofxGgmlPluginDescriptor plugin;
	plugin.addCapability(capability);
	plugin.addLifecycleNote("");
	plugin.addSafetyRequirement("");

	ofxGgmlPluginRegistry registry;
	registry.addPlugin(plugin);
	registry.addCompatibilityRule("");
	registry.addReviewNote("");

	const auto json = registry.toJsonString();
	REQUIRE(json.find("\"required_contracts\":[]") != std::string::npos);
	REQUIRE(json.find("\"lifecycle_notes\":[]") != std::string::npos);
	REQUIRE(json.find("\"safety_requirements\":[]") != std::string::npos);
	REQUIRE(json.find("\"compatibility_rules\":[]") != std::string::npos);
	REQUIRE(json.find("\"review_notes\":[]") != std::string::npos);
}
