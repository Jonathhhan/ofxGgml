#include "catch2.hpp"
#include "../src/ofxGgml.h"

TEST_CASE("AceStep bridge builds instrument-aware request JSON", "[acestep_bridge]") {
	ofxGgmlAceStepRequest request;
	request.caption = "glossy synth-pop night drive";
	request.instrumentalOnly = true;
	request.bpm = 118;
	request.durationSeconds = 42.0f;
	request.keyscale = "C minor";
	request.timesignature = "4";
	request.batchSize = 2;

	const ofJson json = ofxGgmlAceStepBridge::buildRequestJson(request);
	REQUIRE(json["caption"].get<std::string>() == "glossy synth-pop night drive");
	REQUIRE(json["lyrics"].get<std::string>() == "[Instrumental]");
	REQUIRE(json["bpm"].get<int>() == 118);
	REQUIRE(json["duration"].get<float>() == Approx(42.0f));
	REQUIRE(json["keyscale"].get<std::string>() == "C minor");
	REQUIRE(json["batch_size"].get<int>() == 2);
}

TEST_CASE("AceStep bridge summarizes request and understand results", "[acestep_bridge]") {
	ofJson json = ofJson::object();
	json["caption"] = "warm neo-soul groove";
	json["lyrics"] = "soft chorus, bright refrain";
	json["bpm"] = 96;
	json["duration"] = 28.0f;
	json["keyscale"] = "E major";
	json["timesignature"] = "4";

	const std::string requestSummary =
		ofxGgmlAceStepBridge::summarizeRequestJson(json);
	REQUIRE(requestSummary.find("warm neo-soul groove") != std::string::npos);
	REQUIRE(requestSummary.find("96 BPM") != std::string::npos);
	REQUIRE(requestSummary.find("E major") != std::string::npos);

	ofxGgmlAceStepUnderstandResult result;
	result.caption = "bright indie chorus";
	result.lyrics = "hands up in the summer night";
	result.bpm = 124;
	result.durationSeconds = 31.5f;
	result.keyscale = "A minor";
	result.timesignature = "4";

	const std::string understandSummary =
		ofxGgmlAceStepBridge::summarizeUnderstandResult(result);
	REQUIRE(understandSummary.find("bright indie chorus") != std::string::npos);
	REQUIRE(understandSummary.find("124 BPM") != std::string::npos);
	REQUIRE(understandSummary.find("A minor") != std::string::npos);
}
