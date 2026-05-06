#include "catch2.hpp"
#include "../src/support/ofxGgmlIntegrationSurface.h"

#include <set>

TEST_CASE("Integration surface exposes roadmap integration targets", "[integration_surface]") {
	const auto surface = ofxGgmlDefaultIntegrationSurface();
	REQUIRE(surface.schemaVersion == "ofxGgml.integration_surface.v1");
	REQUIRE(surface.targets.size() == 4);
	REQUIRE(surface.boundaryRules.size() == 3);
	REQUIRE(surface.reviewNotes.size() == 2);

	std::set<std::string> categories;
	for (const auto & target : surface.targets) {
		categories.insert(target.category);
		REQUIRE_FALSE(target.id.empty());
		REQUIRE_FALSE(target.name.empty());
		REQUIRE_FALSE(target.hostBoundary.empty());
		REQUIRE_FALSE(target.addonHeader.empty());
		REQUIRE_FALSE(target.endpoints.empty());
		REQUIRE_FALSE(target.requiredCapabilities.empty());
		REQUIRE_FALSE(target.setupRequirements.empty());
		REQUIRE_FALSE(target.safetyRequirements.empty());
		REQUIRE_FALSE(target.compatibilityNotes.empty());

		for (const auto & endpoint : target.endpoints) {
			REQUIRE_FALSE(endpoint.id.empty());
			REQUIRE_FALSE(endpoint.label.empty());
			REQUIRE_FALSE(endpoint.direction.empty());
			REQUIRE_FALSE(endpoint.contract.empty());
			REQUIRE_FALSE(endpoint.payloadKeys.empty());
			REQUIRE_FALSE(endpoint.failureModes.empty());
		}
	}

	REQUIRE(categories.count("editor") == 1);
	REQUIRE(categories.count("renderer") == 1);
	REQUIRE(categories.count("search_research") == 1);
	REQUIRE(categories.count("hardware_runtime") == 1);
}

TEST_CASE("Integration surface serializes stable JSON keys", "[integration_surface]") {
	auto surface = ofxGgmlDefaultIntegrationSurface();
	surface.metadata["owner"] = "ecosystem";
	surface.targets.front().metadata["status"] = "draft";
	surface.targets.front().endpoints.front().metadata["shape"] = "structured";

	const auto json = surface.toJsonString();
	REQUIRE(json.find("\"schema_version\"") != std::string::npos);
	REQUIRE(json.find("ofxGgml.integration_surface.v1") != std::string::npos);
	REQUIRE(json.find("\"surface_id\"") != std::string::npos);
	REQUIRE(json.find("\"targets\"") != std::string::npos);
	REQUIRE(json.find("\"host_boundary\"") != std::string::npos);
	REQUIRE(json.find("\"addon_header\"") != std::string::npos);
	REQUIRE(json.find("\"endpoints\"") != std::string::npos);
	REQUIRE(json.find("\"direction\"") != std::string::npos);
	REQUIRE(json.find("\"payload_keys\"") != std::string::npos);
	REQUIRE(json.find("\"failure_modes\"") != std::string::npos);
	REQUIRE(json.find("\"required_capabilities\"") != std::string::npos);
	REQUIRE(json.find("\"setup_requirements\"") != std::string::npos);
	REQUIRE(json.find("\"safety_requirements\"") != std::string::npos);
	REQUIRE(json.find("\"compatibility_notes\"") != std::string::npos);
	REQUIRE(json.find("\"boundary_rules\"") != std::string::npos);
	REQUIRE(json.find("\"review_notes\"") != std::string::npos);
	REQUIRE(json.find("ofxGgmlWorkflowManifest") != std::string::npos);
	REQUIRE(json.find("ofxGgmlAssistantTeamSpec") != std::string::npos);
	REQUIRE(json.find("structured") != std::string::npos);
}

TEST_CASE("Integration surface ignores empty convenience entries", "[integration_surface]") {
	ofxGgmlIntegrationEndpoint endpoint;
	endpoint.addPayloadKey("");
	endpoint.addFailureMode("");

	ofxGgmlIntegrationTarget target;
	target.addEndpoint(endpoint);
	target.addRequiredCapability("");
	target.addSetupRequirement("");
	target.addSafetyRequirement("");
	target.addCompatibilityNote("");

	ofxGgmlIntegrationSurface surface;
	surface.addTarget(target);
	surface.addBoundaryRule("");
	surface.addReviewNote("");

	const auto json = surface.toJsonString();
	REQUIRE(json.find("\"payload_keys\":[]") != std::string::npos);
	REQUIRE(json.find("\"failure_modes\":[]") != std::string::npos);
	REQUIRE(json.find("\"required_capabilities\":[]") != std::string::npos);
	REQUIRE(json.find("\"setup_requirements\":[]") != std::string::npos);
	REQUIRE(json.find("\"safety_requirements\":[]") != std::string::npos);
	REQUIRE(json.find("\"compatibility_notes\":[]") != std::string::npos);
	REQUIRE(json.find("\"boundary_rules\":[]") != std::string::npos);
	REQUIRE(json.find("\"review_notes\":[]") != std::string::npos);
}
