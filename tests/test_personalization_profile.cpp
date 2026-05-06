#include "catch2.hpp"
#include "../src/support/ofxGgmlPersonalizationProfile.h"

#include <set>

TEST_CASE("Personalization profile set exposes roadmap adaptation entries", "[personalization_profile]") {
	const auto profileSet = ofxGgmlDefaultPersonalizationProfileSet();
	REQUIRE(profileSet.schemaVersion == "ofxGgml.personalization_profile_set.v1");
	REQUIRE(profileSet.adapters.size() == 2);
	REQUIRE(profileSet.styleProfiles.size() == 3);
	REQUIRE(profileSet.projectPresets.size() == 2);
	REQUIRE(profileSet.adaptationRules.size() == 3);
	REQUIRE(profileSet.reviewNotes.size() == 2);

	std::set<std::string> adapterTypes;
	for (const auto & adapter : profileSet.adapters) {
		adapterTypes.insert(adapter.adapterType);
		REQUIRE_FALSE(adapter.id.empty());
		REQUIRE_FALSE(adapter.label.empty());
		REQUIRE_FALSE(adapter.baseModelRef.empty());
		REQUIRE_FALSE(adapter.activationMode.empty());
		REQUIRE_FALSE(adapter.assetRefs.empty());
		REQUIRE_FALSE(adapter.requiredCapabilities.empty());
		REQUIRE_FALSE(adapter.safetyRequirements.empty());
	}

	std::set<std::string> modalities;
	for (const auto & profile : profileSet.styleProfiles) {
		modalities.insert(profile.modality);
		REQUIRE_FALSE(profile.id.empty());
		REQUIRE_FALSE(profile.name.empty());
		REQUIRE_FALSE(profile.description.empty());
		REQUIRE_FALSE(profile.positiveTraits.empty());
		REQUIRE_FALSE(profile.negativeConstraints.empty());
		REQUIRE_FALSE(profile.referenceRefs.empty());
	}

	for (const auto & preset : profileSet.projectPresets) {
		REQUIRE_FALSE(preset.id.empty());
		REQUIRE_FALSE(preset.name.empty());
		REQUIRE_FALSE(preset.scope.empty());
		REQUIRE_FALSE(preset.description.empty());
		REQUIRE_FALSE(preset.styleProfileRefs.empty());
		REQUIRE_FALSE(preset.defaultAdapterRefs.empty());
		REQUIRE_FALSE(preset.settings.empty());
		REQUIRE_FALSE(preset.reviewNotes.empty());
	}

	REQUIRE(adapterTypes.count("lora") == 1);
	REQUIRE(modalities.count("text") == 1);
	REQUIRE(modalities.count("visual") == 1);
	REQUIRE(modalities.count("audio") == 1);
}

TEST_CASE("Personalization profile set serializes stable JSON keys", "[personalization_profile]") {
	auto profileSet = ofxGgmlDefaultPersonalizationProfileSet();
	profileSet.metadata["owner"] = "ecosystem";
	profileSet.adapters.front().metadata["status"] = "draft";
	profileSet.styleProfiles.front().metadata["tone"] = "plain";
	profileSet.projectPresets.front().metadata["sharing"] = "local";

	const auto parsed = profileSet.toJson();
	const auto json = profileSet.toJsonString();
	REQUIRE(json.find("\"schema_version\"") != std::string::npos);
	REQUIRE(json.find("ofxGgml.personalization_profile_set.v1") != std::string::npos);
	REQUIRE(json.find("\"profile_set_id\"") != std::string::npos);
	REQUIRE(json.find("\"adapters\"") != std::string::npos);
	REQUIRE(json.find("\"adapter_type\"") != std::string::npos);
	REQUIRE(json.find("\"base_model_ref\"") != std::string::npos);
	REQUIRE(json.find("\"activation_mode\"") != std::string::npos);
	REQUIRE(json.find("\"asset_refs\"") != std::string::npos);
	REQUIRE(json.find("\"required_capabilities\"") != std::string::npos);
	REQUIRE(json.find("\"safety_requirements\"") != std::string::npos);
	REQUIRE(json.find("\"style_profiles\"") != std::string::npos);
	REQUIRE(json.find("\"positive_traits\"") != std::string::npos);
	REQUIRE(json.find("\"negative_constraints\"") != std::string::npos);
	REQUIRE(json.find("\"reference_refs\"") != std::string::npos);
	REQUIRE(json.find("\"project_presets\"") != std::string::npos);
	REQUIRE(json.find("\"style_profile_refs\"") != std::string::npos);
	REQUIRE(json.find("\"default_adapter_refs\"") != std::string::npos);
	REQUIRE(json.find("\"adaptation_rules\"") != std::string::npos);
	REQUIRE(json.find("\"review_notes\"") != std::string::npos);
	REQUIRE(parsed["adapters"][0]["asset_refs"][0].get<std::string>() == "project_memory:accepted_prompts");
	REQUIRE(parsed["style_profiles"][1]["reference_refs"][0].get<std::string>() == "continuity_asset_ledger:rules");
	REQUIRE(json.find("plain") != std::string::npos);
}

TEST_CASE("Personalization profile set ignores empty convenience entries", "[personalization_profile]") {
	ofxGgmlPersonalizationAdapter adapter;
	adapter.addAssetRef("");
	adapter.addRequiredCapability("");
	adapter.addSafetyRequirement("");

	ofxGgmlStyleProfile profile;
	profile.addPositiveTrait("");
	profile.addNegativeConstraint("");
	profile.addReferenceRef("");

	ofxGgmlProjectPreset preset;
	preset.addStyleProfileRef("");
	preset.addDefaultAdapterRef("");
	preset.addReviewNote("");

	ofxGgmlPersonalizationProfileSet profileSet;
	profileSet.addAdapter(adapter);
	profileSet.addStyleProfile(profile);
	profileSet.addProjectPreset(preset);
	profileSet.addAdaptationRule("");
	profileSet.addReviewNote("");

	const auto json = profileSet.toJsonString();
	REQUIRE(json.find("\"asset_refs\":[]") != std::string::npos);
	REQUIRE(json.find("\"required_capabilities\":[]") != std::string::npos);
	REQUIRE(json.find("\"safety_requirements\":[]") != std::string::npos);
	REQUIRE(json.find("\"positive_traits\":[]") != std::string::npos);
	REQUIRE(json.find("\"negative_constraints\":[]") != std::string::npos);
	REQUIRE(json.find("\"reference_refs\":[]") != std::string::npos);
	REQUIRE(json.find("\"style_profile_refs\":[]") != std::string::npos);
	REQUIRE(json.find("\"default_adapter_refs\":[]") != std::string::npos);
	REQUIRE(json.find("\"review_notes\":[]") != std::string::npos);
	REQUIRE(json.find("\"adaptation_rules\":[]") != std::string::npos);
}
