#include "catch2.hpp"
#include "../src/support/ofxGgmlTrustEvaluationSuite.h"

#include <set>

TEST_CASE("Trust evaluation suite exposes roadmap trust dimensions", "[trust_evaluation]") {
	const auto suite = ofxGgmlDefaultTrustEvaluationSuite();
	REQUIRE(suite.schemaVersion == "ofxGgml.trust_evaluation_suite.v1");
	REQUIRE(suite.manifestRef == "ofxGgml.workflow_manifest.v1");
	REQUIRE(suite.assistantTeamRef == "ofxGgml.assistant_team.v1");
	REQUIRE(suite.metrics.size() == 5);
	REQUIRE(suite.cases.size() == 5);
	REQUIRE(suite.approvalRules.size() == 2);
	REQUIRE(suite.reviewNotes.size() == 2);

	std::set<std::string> metricIds;

	for (const auto & metric : suite.metrics) {
		metricIds.insert(metric.id);
		REQUIRE_FALSE(metric.category.empty());
		REQUIRE_FALSE(metric.description.empty());
		REQUIRE_FALSE(metric.target.empty());
		REQUIRE(metric.threshold > 0.0);
		REQUIRE_FALSE(metric.evidenceRefs.empty());
	}

	REQUIRE(metricIds.count("citation_quality") == 1);
	REQUIRE(metricIds.count("workflow_correctness") == 1);
	REQUIRE(metricIds.count("latency_throughput") == 1);
	REQUIRE(metricIds.count("multimodal_coherence") == 1);
	REQUIRE(metricIds.count("assistant_safety") == 1);

	for (const auto & evalCase : suite.cases) {
		REQUIRE_FALSE(evalCase.id.empty());
		REQUIRE_FALSE(evalCase.title.empty());
		REQUIRE_FALSE(evalCase.evaluationType.empty());
		REQUIRE_FALSE(evalCase.sourceRef.empty());
		REQUIRE_FALSE(evalCase.expectedBehavior.empty());
		REQUIRE_FALSE(evalCase.requiredSignals.empty());
	}
}

TEST_CASE("Trust evaluation suite serializes stable JSON keys", "[trust_evaluation]") {
	auto suite = ofxGgmlDefaultTrustEvaluationSuite();
	suite.metadata["owner"] = "companion-example";
	suite.metrics.front().metadata["source"] = "citation-search";
	suite.cases.front().metadata["priority"] = "high";
	suite.cases.front().addRequiredSignal("warnings");

	const auto json = suite.toJsonString();
	REQUIRE(json.find("\"schema_version\"") != std::string::npos);
	REQUIRE(json.find("ofxGgml.trust_evaluation_suite.v1") != std::string::npos);
	REQUIRE(json.find("\"suite_id\"") != std::string::npos);
	REQUIRE(json.find("\"manifest_ref\"") != std::string::npos);
	REQUIRE(json.find("\"assistant_team_ref\"") != std::string::npos);
	REQUIRE(json.find("\"metrics\"") != std::string::npos);
	REQUIRE(json.find("\"evidence_refs\"") != std::string::npos);
	REQUIRE(json.find("\"cases\"") != std::string::npos);
	REQUIRE(json.find("\"evaluation_type\"") != std::string::npos);
	REQUIRE(json.find("\"required_signals\"") != std::string::npos);
	REQUIRE(json.find("\"approval_rules\"") != std::string::npos);
	REQUIRE(json.find("\"review_notes\"") != std::string::npos);
	REQUIRE(json.find("warnings") != std::string::npos);
	REQUIRE(json.find("companion-example") != std::string::npos);
}

TEST_CASE("Trust evaluation suite ignores empty convenience entries", "[trust_evaluation]") {
	ofxGgmlTrustEvaluationMetric metric;
	metric.addEvidenceRef("");

	ofxGgmlTrustEvaluationCase evalCase;
	evalCase.addRequiredSignal("");

	ofxGgmlTrustEvaluationSuite suite;
	suite.addApprovalRule("");
	suite.addReviewNote("");
	suite.addMetric(metric);
	suite.addCase(evalCase);

	const auto json = suite.toJsonString();
	REQUIRE(json.find("\"evidence_refs\":[]") != std::string::npos);
	REQUIRE(json.find("\"required_signals\":[]") != std::string::npos);
	REQUIRE(json.find("\"approval_rules\":[]") != std::string::npos);
	REQUIRE(json.find("\"review_notes\":[]") != std::string::npos);
}
