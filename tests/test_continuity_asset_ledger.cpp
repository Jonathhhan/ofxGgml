#include "catch2.hpp"
#include "../src/support/ofxGgmlContinuityAssetLedger.h"

TEST_CASE("Continuity asset ledger exposes roadmap continuity surfaces", "[continuity_asset_ledger]") {
	const auto ledger = ofxGgmlDefaultContinuityAssetLedger();
	REQUIRE(ledger.schemaVersion == "ofxGgml.continuity_asset_ledger.v1");
	REQUIRE(ledger.manifestRef == "ofxGgml.workflow_manifest.v1");
	REQUIRE(ledger.memoryRef == "ofxGgml.companion_project_memory.v1");
	REQUIRE(ledger.continuityRules.size() == 2);
	REQUIRE(ledger.styleConstraints.size() == 2);
	REQUIRE(ledger.reusableAssets.size() == 2);
	REQUIRE(ledger.reviewNotes.size() == 2);

	bool hasSceneIdentity = false;
	bool hasTimelineOrder = false;
	for (const auto & rule : ledger.continuityRules) {
		hasSceneIdentity = hasSceneIdentity || rule.id == "scene_identity";
		hasTimelineOrder = hasTimelineOrder || rule.id == "timeline_order";
		REQUIRE_FALSE(rule.scope.empty());
		REQUIRE_FALSE(rule.description.empty());
		REQUIRE(rule.severity == "required");
	}

	bool hasPalette = false;
	bool hasNegativeStyle = false;
	for (const auto & constraint : ledger.styleConstraints) {
		hasPalette = hasPalette || constraint.id == "palette";
		hasNegativeStyle = hasNegativeStyle || constraint.id == "negative_style";
		REQUIRE_FALSE(constraint.category.empty());
		REQUIRE_FALSE(constraint.value.empty());
	}

	bool hasHeroReference = false;
	bool hasMotifPrompt = false;
	for (const auto & asset : ledger.reusableAssets) {
		hasHeroReference = hasHeroReference || asset.id == "hero_reference";
		hasMotifPrompt = hasMotifPrompt || asset.id == "motif_prompt";
		REQUIRE_FALSE(asset.type.empty());
		REQUIRE_FALSE(asset.title.empty());
		REQUIRE_FALSE(asset.reuseGuidance.empty());
		REQUIRE_FALSE(asset.tags.empty());
	}

	REQUIRE(hasSceneIdentity);
	REQUIRE(hasTimelineOrder);
	REQUIRE(hasPalette);
	REQUIRE(hasNegativeStyle);
	REQUIRE(hasHeroReference);
	REQUIRE(hasMotifPrompt);
}

TEST_CASE("Continuity asset ledger serializes stable JSON keys", "[continuity_asset_ledger]") {
	auto ledger = ofxGgmlDefaultContinuityAssetLedger();
	ledger.metadata["owner"] = "companion-example";
	ledger.continuityRules.front().metadata["source"] = "memory";
	ledger.styleConstraints.front().metadata["locked"] = "true";
	ledger.reusableAssets.front().metadata["approved"] = "true";
	ledger.reusableAssets.front().license = "project-approved";

	const auto json = ledger.toJsonString();
	REQUIRE(json.find("\"schema_version\"") != std::string::npos);
	REQUIRE(json.find("ofxGgml.continuity_asset_ledger.v1") != std::string::npos);
	REQUIRE(json.find("\"manifest_ref\"") != std::string::npos);
	REQUIRE(json.find("\"memory_ref\"") != std::string::npos);
	REQUIRE(json.find("\"continuity_rules\"") != std::string::npos);
	REQUIRE(json.find("\"style_constraints\"") != std::string::npos);
	REQUIRE(json.find("\"reusable_assets\"") != std::string::npos);
	REQUIRE(json.find("\"reuse_guidance\"") != std::string::npos);
	REQUIRE(json.find("\"review_notes\"") != std::string::npos);
	REQUIRE(json.find("project-approved") != std::string::npos);
	REQUIRE(json.find("approved") != std::string::npos);
}

TEST_CASE("Continuity asset ledger ignores empty review notes", "[continuity_asset_ledger]") {
	ofxGgmlContinuityAssetLedger ledger;
	ledger.addReviewNote("");

	const auto json = ledger.toJsonString();
	REQUIRE(json.find("\"continuity_rules\":[]") != std::string::npos);
	REQUIRE(json.find("\"style_constraints\":[]") != std::string::npos);
	REQUIRE(json.find("\"reusable_assets\":[]") != std::string::npos);
	REQUIRE(json.find("\"review_notes\":[]") != std::string::npos);
}
