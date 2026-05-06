#include "catch2.hpp"
#include "../src/support/ofxGgmlCollaborativeWorkflow.h"

#include <set>

TEST_CASE("Collaborative workflow space exposes realtime collaboration surfaces", "[collaborative_workflow]") {
	const auto space = ofxGgmlDefaultCollaborativeWorkflowSpace();
	REQUIRE(space.schemaVersion == "ofxGgml.collaborative_workflow.v1");
	REQUIRE(space.projectMemoryRef == "ofxGgml.companion_project_memory.v1");
	REQUIRE(space.workflowManifestRef == "ofxGgml.workflow_manifest.v1");
	REQUIRE(space.participants.size() == 4);
	REQUIRE(space.channels.size() == 4);
	REQUIRE(space.checkpoints.size() == 3);
	REQUIRE(space.syncRules.size() == 3);
	REQUIRE(space.reviewNotes.size() == 2);

	std::set<std::string> roles;
	for (const auto & participant : space.participants) {
		roles.insert(participant.role);
		REQUIRE_FALSE(participant.id.empty());
		REQUIRE_FALSE(participant.displayName.empty());
		REQUIRE_FALSE(participant.permissions.empty());
		REQUIRE_FALSE(participant.presenceSignals.empty());
	}

	std::set<std::string> channelTypes;
	for (const auto & channel : space.channels) {
		channelTypes.insert(channel.channelType);
		REQUIRE_FALSE(channel.id.empty());
		REQUIRE_FALSE(channel.label.empty());
		REQUIRE_FALSE(channel.transportBoundary.empty());
		REQUIRE(channel.latencyBudgetMs > 0);
		REQUIRE_FALSE(channel.payloadKeys.empty());
		REQUIRE_FALSE(channel.failureModes.empty());
	}

	for (const auto & checkpoint : space.checkpoints) {
		REQUIRE_FALSE(checkpoint.id.empty());
		REQUIRE_FALSE(checkpoint.label.empty());
		REQUIRE(checkpoint.approvalMode == "explicit");
		REQUIRE_FALSE(checkpoint.requiredRoles.empty());
		REQUIRE_FALSE(checkpoint.evidenceRefs.empty());
	}

	REQUIRE(roles.count("owner") == 1);
	REQUIRE(roles.count("reviewer") == 1);
	REQUIRE(roles.count("assistant") == 1);
	REQUIRE(roles.count("runtime") == 1);
	REQUIRE(channelTypes.count("state_sync") == 1);
	REQUIRE(channelTypes.count("approval") == 1);
	REQUIRE(channelTypes.count("preview") == 1);
	REQUIRE(channelTypes.count("artifact_handoff") == 1);
}

TEST_CASE("Collaborative workflow space serializes stable JSON keys", "[collaborative_workflow]") {
	auto space = ofxGgmlDefaultCollaborativeWorkflowSpace();
	space.metadata["owner"] = "ecosystem";
	space.participants.front().metadata["presence"] = "local";
	space.channels.front().metadata["shape"] = "structured";
	space.checkpoints.front().metadata["blocking"] = "true";

	const auto json = space.toJsonString();
	REQUIRE(json.find("\"schema_version\"") != std::string::npos);
	REQUIRE(json.find("ofxGgml.collaborative_workflow.v1") != std::string::npos);
	REQUIRE(json.find("\"space_id\"") != std::string::npos);
	REQUIRE(json.find("\"session_mode\"") != std::string::npos);
	REQUIRE(json.find("\"project_memory_ref\"") != std::string::npos);
	REQUIRE(json.find("\"workflow_manifest_ref\"") != std::string::npos);
	REQUIRE(json.find("\"participants\"") != std::string::npos);
	REQUIRE(json.find("\"display_name\"") != std::string::npos);
	REQUIRE(json.find("\"permissions\"") != std::string::npos);
	REQUIRE(json.find("\"presence_signals\"") != std::string::npos);
	REQUIRE(json.find("\"channels\"") != std::string::npos);
	REQUIRE(json.find("\"channel_type\"") != std::string::npos);
	REQUIRE(json.find("\"transport_boundary\"") != std::string::npos);
	REQUIRE(json.find("\"latency_budget_ms\"") != std::string::npos);
	REQUIRE(json.find("\"payload_keys\"") != std::string::npos);
	REQUIRE(json.find("\"failure_modes\"") != std::string::npos);
	REQUIRE(json.find("\"checkpoints\"") != std::string::npos);
	REQUIRE(json.find("\"approval_mode\"") != std::string::npos);
	REQUIRE(json.find("\"required_roles\"") != std::string::npos);
	REQUIRE(json.find("\"evidence_refs\"") != std::string::npos);
	REQUIRE(json.find("\"sync_rules\"") != std::string::npos);
	REQUIRE(json.find("\"review_notes\"") != std::string::npos);
	REQUIRE(json.find("structured") != std::string::npos);
	REQUIRE(json.find("blocking") != std::string::npos);
}

TEST_CASE("Collaborative workflow space ignores empty convenience entries", "[collaborative_workflow]") {
	ofxGgmlCollaborativeParticipant participant;
	participant.addPermission("");
	participant.addPresenceSignal("");

	ofxGgmlRealtimeChannel channel;
	channel.addPayloadKey("");
	channel.addFailureMode("");

	ofxGgmlCollaborativeCheckpoint checkpoint;
	checkpoint.addRequiredRole("");
	checkpoint.addEvidenceRef("");

	ofxGgmlCollaborativeWorkflowSpace space;
	space.addParticipant(participant);
	space.addChannel(channel);
	space.addCheckpoint(checkpoint);
	space.addSyncRule("");
	space.addReviewNote("");

	const auto json = space.toJsonString();
	REQUIRE(json.find("\"permissions\":[]") != std::string::npos);
	REQUIRE(json.find("\"presence_signals\":[]") != std::string::npos);
	REQUIRE(json.find("\"payload_keys\":[]") != std::string::npos);
	REQUIRE(json.find("\"failure_modes\":[]") != std::string::npos);
	REQUIRE(json.find("\"required_roles\":[]") != std::string::npos);
	REQUIRE(json.find("\"evidence_refs\":[]") != std::string::npos);
	REQUIRE(json.find("\"sync_rules\":[]") != std::string::npos);
	REQUIRE(json.find("\"review_notes\":[]") != std::string::npos);
}
