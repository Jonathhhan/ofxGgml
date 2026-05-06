#include "catch2.hpp"
#include "../src/assistants/ofxGgmlAssistantTeam.h"

TEST_CASE("Specialist assistant team exposes Phase 3 roles", "[assistant_team]") {
	const auto team = ofxGgmlDefaultSpecialistAssistantTeam();
	REQUIRE(team.schemaVersion == "ofxGgml.assistant_team.v1");
	REQUIRE(team.teamId == "specialist-assistant-team");
	REQUIRE(team.safetyModel == "approval-first");
	REQUIRE(team.roles.size() == 5);
	REQUIRE(team.handoffs.size() == 3);
	REQUIRE_FALSE(team.workspaceRules.empty());

	bool hasResearcher = false;
	bool hasPlanner = false;
	bool hasCritic = false;
	bool hasEditor = false;
	bool hasRenderer = false;

	for (const auto & role : team.roles) {
		hasResearcher = hasResearcher || role.id == "researcher";
		hasPlanner = hasPlanner || role.id == "planner";
		hasCritic = hasCritic || role.id == "critic";
		hasEditor = hasEditor || role.id == "editor";
		hasRenderer = hasRenderer || role.id == "renderer";
		REQUIRE_FALSE(role.title.empty());
		REQUIRE_FALSE(role.responsibility.empty());
		REQUIRE_FALSE(role.allowedActions.empty());
	}

	REQUIRE(hasResearcher);
	REQUIRE(hasPlanner);
	REQUIRE(hasCritic);
	REQUIRE(hasEditor);
	REQUIRE(hasRenderer);
}

TEST_CASE("Specialist assistant team serializes stable JSON contract", "[assistant_team]") {
	ofxGgmlAssistantTeamSpec team = ofxGgmlDefaultSpecialistAssistantTeam();
	team.metadata["owner"] = "host-application";
	team.roles.front().metadata["source"] = "roadmap";
	team.handoffs.front().metadata["requires_manifest"] = "true";

	const auto json = team.toJsonString();
	REQUIRE(json.find("\"schema_version\"") != std::string::npos);
	REQUIRE(json.find("ofxGgml.assistant_team.v1") != std::string::npos);
	REQUIRE(json.find("\"team_id\"") != std::string::npos);
	REQUIRE(json.find("\"safety_model\"") != std::string::npos);
	REQUIRE(json.find("approval-first") != std::string::npos);
	REQUIRE(json.find("\"roles\"") != std::string::npos);
	REQUIRE(json.find("\"allowed_actions\"") != std::string::npos);
	REQUIRE(json.find("\"handoff_targets\"") != std::string::npos);
	REQUIRE(json.find("\"handoffs\"") != std::string::npos);
	REQUIRE(json.find("\"required_context\"") != std::string::npos);
	REQUIRE(json.find("\"workspace_rules\"") != std::string::npos);
	REQUIRE(json.find("requires_manifest") != std::string::npos);
}

TEST_CASE("Specialist assistant team ignores empty convenience entries", "[assistant_team]") {
	ofxGgmlAssistantTeamRole role;
	role.addAllowedAction("");
	role.addHandoffTarget("");

	ofxGgmlAssistantTeamHandoff handoff;
	handoff.addRequiredContext("");

	ofxGgmlAssistantTeamSpec team;
	team.addWorkspaceRule("");
	team.addRole(role);
	team.addHandoff(handoff);

	const auto json = team.toJsonString();
	REQUIRE(json.find("\"allowed_actions\":[]") != std::string::npos);
	REQUIRE(json.find("\"handoff_targets\":[]") != std::string::npos);
	REQUIRE(json.find("\"required_context\":[]") != std::string::npos);
	REQUIRE(json.find("\"workspace_rules\":[]") != std::string::npos);
}
