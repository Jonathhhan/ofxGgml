#include "catch2.hpp"
#include "../src/inference/ofxGgmlValidationLoop.h"

#include <string>
#include <vector>

// Simple test types for validation loop testing
struct TestGenerated {
	std::string content;
	int seed = 0;
	bool success = false;
};

struct TestAnalysis {
	std::string description;
	float quality = 0.0f;
	bool success = false;
};

TEST_CASE("Validation loop configuration", "[validation_loop]") {
	ofxGgmlValidationLoopConfig config;

	SECTION("Default configuration") {
		REQUIRE(config.maxAttempts == 3);
		REQUIRE(config.qualityThreshold == 0.6f);
		REQUIRE(config.enableRefinement == true);
		REQUIRE(config.collectAllAttempts == false);
		REQUIRE(config.improvementThreshold == 0.1f);
	}

	SECTION("Custom configuration") {
		config.maxAttempts = 5;
		config.qualityThreshold = 0.8f;
		config.enableRefinement = false;

		REQUIRE(config.maxAttempts == 5);
		REQUIRE(config.qualityThreshold == 0.8f);
		REQUIRE_FALSE(config.enableRefinement);
	}
}

TEST_CASE("Validation loop attempt structure", "[validation_loop]") {
	ofxGgmlValidationAttempt<TestGenerated, TestAnalysis> attempt;

	REQUIRE(attempt.attemptNumber == 0);
	REQUIRE_FALSE(attempt.success);
	REQUIRE(attempt.score == 0.0f);
	REQUIRE(attempt.elapsedMs == 0.0f);
	REQUIRE(attempt.error.empty());
	REQUIRE(attempt.feedback.empty());
}

TEST_CASE("Validation loop result structure", "[validation_loop]") {
	ofxGgmlValidationLoopResult<TestGenerated, TestAnalysis> result;

	REQUIRE_FALSE(result.success);
	REQUIRE(result.totalAttempts == 0);
	REQUIRE(result.totalElapsedMs == 0.0f);
	REQUIRE(result.bestScore == 0.0f);
	REQUIRE(result.bestAttemptIndex == -1);
	REQUIRE(result.error.empty());
	REQUIRE(result.attempts.empty());
	REQUIRE(result.warnings.empty());
}

TEST_CASE("Validation loop - successful on first attempt", "[validation_loop]") {
	ofxGgmlValidationLoop<TestGenerated, TestAnalysis> loop;

	int generatorCalls = 0;
	int validatorCalls = 0;
	int scorerCalls = 0;

	loop.setGenerator([&](int attempt) {
		generatorCalls++;
		TestGenerated gen;
		gen.content = "Generated content " + std::to_string(attempt);
		gen.seed = attempt * 100;
		gen.success = true;
		return gen;
	});

	loop.setValidator([&](const TestGenerated& gen) {
		validatorCalls++;
		TestAnalysis analysis;
		analysis.description = "Analysis of: " + gen.content;
		analysis.quality = 0.9f; // High quality
		analysis.success = true;
		return analysis;
	});

	loop.setScorer([&](const TestGenerated&, const TestAnalysis& analysis) {
		scorerCalls++;
		return analysis.quality;
	});

	auto result = loop.run();

	REQUIRE(result.success);
	REQUIRE(result.totalAttempts == 1); // Should stop after first attempt
	REQUIRE(result.bestScore == 0.9f);
	REQUIRE(result.bestAttemptIndex == 0);
	REQUIRE(generatorCalls == 1);
	REQUIRE(validatorCalls == 1);
	REQUIRE(scorerCalls == 1);
}

TEST_CASE("Validation loop - requires multiple attempts", "[validation_loop]") {
	ofxGgmlValidationLoopConfig config;
	config.maxAttempts = 5;
	config.qualityThreshold = 0.8f;

	ofxGgmlValidationLoop<TestGenerated, TestAnalysis> loop(config);

	float scores[] = {0.5f, 0.65f, 0.85f}; // Third attempt meets threshold
	int attemptIndex = 0;

	loop.setGenerator([&](int attempt) {
		TestGenerated gen;
		gen.content = "Attempt " + std::to_string(attempt);
		gen.success = true;
		return gen;
	});

	loop.setValidator([&](const TestGenerated&) {
		TestAnalysis analysis;
		analysis.quality = scores[std::min(attemptIndex, 2)];
		analysis.success = true;
		attemptIndex++;
		return analysis;
	});

	loop.setScorer([](const TestGenerated&, const TestAnalysis& analysis) {
		return analysis.quality;
	});

	auto result = loop.run();

	REQUIRE(result.success);
	REQUIRE(result.totalAttempts == 3); // Stops when threshold met
	REQUIRE(result.bestScore == 0.85f);
	REQUIRE(result.bestAttemptIndex == 2);
}

TEST_CASE("Validation loop - collect all attempts", "[validation_loop]") {
	ofxGgmlValidationLoopConfig config;
	config.maxAttempts = 3;
	config.collectAllAttempts = true;
	config.qualityThreshold = 1.0f; // Unreachable, will run all attempts

	ofxGgmlValidationLoop<TestGenerated, TestAnalysis> loop(config);

	loop.setGenerator([](int attempt) {
		TestGenerated gen;
		gen.content = "Attempt " + std::to_string(attempt);
		return gen;
	});

	loop.setValidator([](const TestGenerated&) {
		TestAnalysis analysis;
		analysis.quality = 0.5f;
		return analysis;
	});

	loop.setScorer([](const TestGenerated&, const TestAnalysis& analysis) {
		return analysis.quality;
	});

	auto result = loop.run();

	REQUIRE(result.totalAttempts == 3);
	REQUIRE(result.attempts.size() == 3);

	for (int i = 0; i < 3; i++) {
		REQUIRE(result.attempts[i].attemptNumber == i + 1);
		REQUIRE(result.attempts[i].success);
	}
}

TEST_CASE("Validation loop - progress callback", "[validation_loop]") {
	ofxGgmlValidationLoop<TestGenerated, TestAnalysis> loop;

	int progressCalls = 0;
	bool shouldCancel = false;

	loop.setGenerator([](int attempt) {
		TestGenerated gen;
		gen.content = "Content";
		return gen;
	});

	loop.setValidator([](const TestGenerated&) {
		TestAnalysis analysis;
		analysis.quality = 0.5f;
		return analysis;
	});

	loop.setScorer([](const TestGenerated&, const TestAnalysis& analysis) {
		return analysis.quality;
	});

	loop.setProgressCallback([&](const ofxGgmlValidationAttempt<TestGenerated, TestAnalysis>& attempt) {
		progressCalls++;
		if (progressCalls == 2) {
			shouldCancel = true;
		}
		return !shouldCancel;
	});

	ofxGgmlValidationLoopConfig config;
	config.maxAttempts = 5;
	config.qualityThreshold = 1.0f; // Won't reach naturally
	loop.setConfig(config);

	auto result = loop.run();

	REQUIRE(progressCalls == 2);
	REQUIRE(result.totalAttempts == 2); // Cancelled after second attempt
	REQUIRE_FALSE(result.warnings.empty());
	REQUIRE(result.warnings[0].find("cancelled") != std::string::npos);
}

TEST_CASE("Validation loop - refinement function", "[validation_loop]") {
	ofxGgmlValidationLoopConfig config;
	config.maxAttempts = 3;
	config.enableRefinement = true;
	config.qualityThreshold = 1.0f; // Won't reach

	ofxGgmlValidationLoop<TestGenerated, TestAnalysis> loop(config);

	int refinerCalls = 0;

	loop.setGenerator([](int attempt) {
		TestGenerated gen;
		gen.content = "Content " + std::to_string(attempt);
		return gen;
	});

	loop.setValidator([](const TestGenerated&) {
		TestAnalysis analysis;
		analysis.quality = 0.5f;
		return analysis;
	});

	loop.setScorer([](const TestGenerated&, const TestAnalysis& analysis) {
		return analysis.quality;
	});

	loop.setRefiner([&](TestGenerated& gen, const TestAnalysis&, float score) {
		refinerCalls++;
		gen.content += " (refined)";
	});

	auto result = loop.run();

	// Refiner should be called between attempts (attempts - 1 times)
	REQUIRE(refinerCalls == 2);
}

TEST_CASE("Validation loop - missing configuration", "[validation_loop]") {
	ofxGgmlValidationLoop<TestGenerated, TestAnalysis> loop;

	SECTION("No generator") {
		loop.setValidator([](const TestGenerated&) { return TestAnalysis(); });
		loop.setScorer([](const TestGenerated&, const TestAnalysis&) { return 0.5f; });

		auto result = loop.run();
		REQUIRE_FALSE(result.success);
		REQUIRE_FALSE(result.error.empty());
		REQUIRE(result.error.find("not fully configured") != std::string::npos);
	}

	SECTION("No validator") {
		loop.setGenerator([](int) { return TestGenerated(); });
		loop.setScorer([](const TestGenerated&, const TestAnalysis&) { return 0.5f; });

		auto result = loop.run();
		REQUIRE_FALSE(result.success);
		REQUIRE_FALSE(result.error.empty());
	}

	SECTION("No scorer") {
		loop.setGenerator([](int) { return TestGenerated(); });
		loop.setValidator([](const TestGenerated&) { return TestAnalysis(); });

		auto result = loop.run();
		REQUIRE_FALSE(result.success);
		REQUIRE_FALSE(result.error.empty());
	}
}

TEST_CASE("Validation loop - validateOnce helper", "[validation_loop][helpers]") {
	int calls = 0;

	auto generator = [&]() {
		calls++;
		TestGenerated gen;
		gen.content = "Single attempt";
		return gen;
	};

	auto validator = [](const TestGenerated&) {
		TestAnalysis analysis;
		analysis.quality = 0.7f;
		return analysis;
	};

	auto scorer = [](const TestGenerated&, const TestAnalysis& analysis) {
		return analysis.quality;
	};

	auto result = ofxGgmlValidationLoops::validateOnce<TestGenerated, TestAnalysis>(
		generator, validator, scorer);

	REQUIRE(result.success);
	REQUIRE(result.totalAttempts == 1);
	REQUIRE(result.bestScore == 0.7f);
	REQUIRE(calls == 1);
}

TEST_CASE("Validation loop - improvement threshold", "[validation_loop]") {
	ofxGgmlValidationLoopConfig config;
	config.maxAttempts = 10;
	config.enableRefinement = true;
	config.qualityThreshold = 0.9f; // High threshold
	config.improvementThreshold = 0.1f; // Need 0.1 improvement to continue

	ofxGgmlValidationLoop<TestGenerated, TestAnalysis> loop(config);

	float scores[] = {0.5f, 0.52f, 0.54f}; // Tiny improvements, below threshold
	int attemptIndex = 0;

	loop.setGenerator([](int attempt) {
		TestGenerated gen;
		gen.content = "Attempt " + std::to_string(attempt);
		return gen;
	});

	loop.setValidator([&](const TestGenerated&) {
		TestAnalysis analysis;
		analysis.quality = scores[std::min(attemptIndex, 2)];
		analysis.success = true;
		attemptIndex++;
		return analysis;
	});

	loop.setScorer([](const TestGenerated&, const TestAnalysis& analysis) {
		return analysis.quality;
	});

	auto result = loop.run();

	// Should stop early because improvements are too small
	REQUIRE(result.totalAttempts <= 5);
	REQUIRE(result.totalAttempts >= 2); // At least two attempts to measure improvement
}
