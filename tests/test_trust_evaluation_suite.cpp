#include "catch2.hpp"
#include "../src/ofxGgmlWorkflows.h"

TEST_CASE("Trust evaluation suite exposes Phase 3 reliability criteria", "[trust_eval]") {
	const auto suite = ofxGgmlDefaultTrustEvaluationSuite();
	REQUIRE(suite.schemaVersion == "ofxGgml.trust_evaluation_suite.v1");
	REQUIRE(suite.suiteId == "phase-3-trust-evaluation");
	REQUIRE(suite.manifestRef == "ofxGgml.workflow_manifest.v1");
	REQUIRE(suite.safetyModel == "approval-first");
	REQUIRE(suite.criteria.size() == 5);
	REQUIRE(suite.scenarios.size() == 3);
	REQUIRE(suite.requiredReports.size() == 5);
	REQUIRE_FALSE(suite.workspaceRules.empty());

	bool hasCitationQuality = false;
	bool hasWorkflowCorrectness = false;
	bool hasLatencyThroughput = false;
	bool hasMultimodalCoherence = false;
	bool hasAssistantSafety = false;

	for (const auto & criterion : suite.criteria) {
		hasCitationQuality = hasCitationQuality || criterion.id == "citation_quality";
		hasWorkflowCorrectness = hasWorkflowCorrectness || criterion.id == "workflow_correctness";
		hasLatencyThroughput = hasLatencyThroughput || criterion.id == "latency_throughput";
		hasMultimodalCoherence = hasMultimodalCoherence || criterion.id == "multimodal_coherence";
		hasAssistantSafety = hasAssistantSafety || criterion.id == "assistant_safety";
		REQUIRE_FALSE(criterion.category.empty());
		REQUIRE_FALSE(criterion.description.empty());
		REQUIRE_FALSE(criterion.targetSignal.empty());
		REQUIRE_FALSE(criterion.passCondition.empty());
	}

	REQUIRE(hasCitationQuality);
	REQUIRE(hasWorkflowCorrectness);
	REQUIRE(hasLatencyThroughput);
	REQUIRE(hasMultimodalCoherence);
	REQUIRE(hasAssistantSafety);
}

TEST_CASE("Trust evaluation suite serializes stable JSON contract", "[trust_eval]") {
	ofxGgmlTrustEvaluationSuite suite = ofxGgmlDefaultTrustEvaluationSuite();
	suite.metadata["owner"] = "host-application";
	suite.criteria.front().metadata["source"] = "roadmap";
	suite.scenarios.front().metadata["fixture_ready"] = "true";

	const auto json = suite.toJsonString();
	REQUIRE(json.find("\"schema_version\"") != std::string::npos);
	REQUIRE(json.find("ofxGgml.trust_evaluation_suite.v1") != std::string::npos);
	REQUIRE(json.find("\"suite_id\"") != std::string::npos);
	REQUIRE(json.find("\"safety_model\"") != std::string::npos);
	REQUIRE(json.find("approval-first") != std::string::npos);
	REQUIRE(json.find("\"criteria\"") != std::string::npos);
	REQUIRE(json.find("\"target_signal\"") != std::string::npos);
	REQUIRE(json.find("\"pass_condition\"") != std::string::npos);
	REQUIRE(json.find("\"scenarios\"") != std::string::npos);
	REQUIRE(json.find("\"criteria_refs\"") != std::string::npos);
	REQUIRE(json.find("\"required_artifacts\"") != std::string::npos);
	REQUIRE(json.find("\"required_reports\"") != std::string::npos);
	REQUIRE(json.find("\"workspace_rules\"") != std::string::npos);
	REQUIRE(json.find("fixture_ready") != std::string::npos);
}

TEST_CASE("Trust evaluation suite ignores empty convenience entries", "[trust_eval]") {
	ofxGgmlTrustEvaluationScenario scenario;
	scenario.addCriterionRef("");
	scenario.addRequiredArtifact("");

	ofxGgmlTrustEvaluationSuite suite;
	suite.addRequiredReport("");
	suite.addWorkspaceRule("");
	suite.addScenario(scenario);

	const auto json = suite.toJsonString();
	REQUIRE(json.find("\"criteria_refs\":[]") != std::string::npos);
	REQUIRE(json.find("\"required_artifacts\":[]") != std::string::npos);
	REQUIRE(json.find("\"required_reports\":[]") != std::string::npos);
	REQUIRE(json.find("\"workspace_rules\":[]") != std::string::npos);
}
