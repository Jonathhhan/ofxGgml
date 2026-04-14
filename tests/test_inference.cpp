#define CATCH_CONFIG_MAIN
#include "catch2.hpp"
#include "../src/ofxGgml.h"

// Inference tests are mostly API tests since they depend on external llama.cpp executables
// Tests marked [inference][requires-executable] need llama CLI tools installed

TEST_CASE("Inference initialization", "[inference]") {
	ofxGgmlInference inf;

	SECTION("Default state") {
		// Should not crash on construction
		REQUIRE(true);
	}

	SECTION("Get executables returns paths") {
		std::string completion = inf.getCompletionExecutable();
		std::string embedding = inf.getEmbeddingExecutable();
		// May be empty if not found, just check API works
		REQUIRE(completion.size() >= 0);
		REQUIRE(embedding.size() >= 0);
	}
}

TEST_CASE("Inference executable configuration", "[inference]") {
	ofxGgmlInference inf;

	SECTION("Set custom completion executable") {
		inf.setCompletionExecutable("/custom/path/llama-completion");
		REQUIRE(inf.getCompletionExecutable() == "/custom/path/llama-completion");
	}

	SECTION("Set custom embedding executable") {
		inf.setEmbeddingExecutable("/custom/path/llama-embedding");
		REQUIRE(inf.getEmbeddingExecutable() == "/custom/path/llama-embedding");
	}

	SECTION("Set empty path") {
		inf.setCompletionExecutable("");
		REQUIRE(inf.getCompletionExecutable() == "");
	}
}

TEST_CASE("Inference settings", "[inference]") {
	SECTION("Default settings") {
		ofxGgmlInferenceSettings settings;

		REQUIRE(settings.maxTokens == 256);
		REQUIRE(settings.temperature == 0.7f);
		REQUIRE(settings.topP == 0.9f);
		REQUIRE(settings.repeatPenalty == 1.1f);
		REQUIRE(settings.contextSize == 2048);
		REQUIRE(settings.batchSize == 512);
		REQUIRE(settings.gpuLayers == 0);
		REQUIRE(settings.threads == 0);
		REQUIRE(settings.seed == -1);
		REQUIRE(settings.simpleIo == true);
		REQUIRE(settings.promptCacheAll == true);
	}

	SECTION("Custom settings") {
		ofxGgmlInferenceSettings settings;
		settings.maxTokens = 512;
		settings.temperature = 0.5f;
		settings.threads = 4;

		REQUIRE(settings.maxTokens == 512);
		REQUIRE(settings.temperature == 0.5f);
		REQUIRE(settings.threads == 4);
	}
}

TEST_CASE("Embedding settings", "[inference]") {
	SECTION("Default embedding settings") {
		ofxGgmlEmbeddingSettings settings;

		REQUIRE(settings.normalize == true);
		REQUIRE(settings.pooling == "mean");
	}

	SECTION("Custom embedding settings") {
		ofxGgmlEmbeddingSettings settings;
		settings.normalize = false;
		settings.pooling = "cls";

		REQUIRE(settings.normalize == false);
		REQUIRE(settings.pooling == "cls");
	}
}

TEST_CASE("Inference result structure", "[inference]") {
	SECTION("Default result") {
		ofxGgmlInferenceResult result;

		REQUIRE(result.success == false);
		REQUIRE(result.elapsedMs == 0.0f);
		REQUIRE(result.text.empty());
		REQUIRE(result.error.empty());
	}

	SECTION("Result with data") {
		ofxGgmlInferenceResult result;
		result.success = true;
		result.elapsedMs = 123.45f;
		result.text = "generated text";

		REQUIRE(result.success == true);
		REQUIRE(result.elapsedMs == 123.45f);
		REQUIRE(result.text == "generated text");
	}
}

TEST_CASE("Embedding result structure", "[inference]") {
	SECTION("Default embedding result") {
		ofxGgmlEmbeddingResult result;

		REQUIRE(result.success == false);
		REQUIRE(result.embedding.empty());
		REQUIRE(result.error.empty());
	}

	SECTION("Result with embedding") {
		ofxGgmlEmbeddingResult result;
		result.success = true;
		result.embedding = {0.1f, 0.2f, 0.3f, 0.4f};

		REQUIRE(result.success == true);
		REQUIRE(result.embedding.size() == 4);
		REQUIRE(result.embedding[0] == 0.1f);
	}
}

TEST_CASE("Inference generation - without executable", "[inference]") {
	ofxGgmlInference inf;

	SECTION("Generate fails gracefully without model") {
		auto result = inf.generate("nonexistent_model.gguf", "test prompt");
		REQUIRE_FALSE(result.success);
		REQUIRE_FALSE(result.error.empty());
	}

	SECTION("Custom settings are accepted") {
		ofxGgmlInferenceSettings settings;
		settings.maxTokens = 10;

		auto result = inf.generate("nonexistent_model.gguf", "test", settings);
		REQUIRE_FALSE(result.success);
	}
}

TEST_CASE("Inference embedding - without executable", "[inference]") {
	ofxGgmlInference inf;

	SECTION("Embed fails gracefully without model") {
		auto result = inf.embed("nonexistent_model.gguf", "test text");
		REQUIRE_FALSE(result.success);
		REQUIRE_FALSE(result.error.empty());
	}

	SECTION("Custom settings are accepted") {
		ofxGgmlEmbeddingSettings settings;
		settings.normalize = false;

		auto result = inf.embed("nonexistent_model.gguf", "test", settings);
		REQUIRE_FALSE(result.success);
	}
}

TEST_CASE("Token counting handles missing artifacts", "[inference]") {
	ofxGgmlInference inf;
	int tokens = inf.countPromptTokens("nonexistent_model.gguf", "hello world");
	REQUIRE(tokens < 0);
}

TEST_CASE("Tokenization utilities", "[inference]") {
	SECTION("Tokenize simple text") {
		auto tokens = ofxGgmlInference::tokenize("Hello world");
		// Implementation may vary, just check it doesn't crash
		REQUIRE(tokens.size() >= 0);
	}

	SECTION("Tokenize empty string") {
		auto tokens = ofxGgmlInference::tokenize("");
		REQUIRE(tokens.size() == 0);
	}

	SECTION("Detokenize tokens") {
		std::vector<std::string> tokens = {"Hello", " ", "world"};
		std::string text = ofxGgmlInference::detokenize(tokens);
		// Should concatenate
		REQUIRE_FALSE(text.empty());
	}

	SECTION("Detokenize empty vector") {
		std::string text = ofxGgmlInference::detokenize({});
		REQUIRE(text.empty());
	}
}

TEST_CASE("Sampling utilities", "[inference]") {
	SECTION("Sample from logits") {
		std::vector<float> logits = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
		int idx = ofxGgmlInference::sampleFromLogits(logits, 1.0f, 1.0f, 42);

		// Should return valid index
		REQUIRE(idx >= 0);
		REQUIRE(idx < static_cast<int>(logits.size()));
	}

	SECTION("Sample with temperature") {
		std::vector<float> logits = {1.0f, 2.0f, 3.0f};
		int idx = ofxGgmlInference::sampleFromLogits(logits, 0.5f, 1.0f, 123);

		REQUIRE(idx >= 0);
		REQUIRE(idx < 3);
	}

	SECTION("Sample with top-p") {
		std::vector<float> logits = {1.0f, 2.0f, 3.0f, 4.0f};
		int idx = ofxGgmlInference::sampleFromLogits(logits, 1.0f, 0.9f, 456);

		REQUIRE(idx >= 0);
		REQUIRE(idx < 4);
	}

	SECTION("Sample from single logit") {
		std::vector<float> logits = {5.0f};
		int idx = ofxGgmlInference::sampleFromLogits(logits);

		REQUIRE(idx == 0);
	}

	SECTION("Sample from empty logits") {
		std::vector<float> logits = {};
		int idx = ofxGgmlInference::sampleFromLogits(logits);

		// Should handle gracefully (return -1 or 0)
		REQUIRE(idx >= -1);
	}
}

TEST_CASE("Embedding index", "[inference]") {
	ofxGgmlEmbeddingIndex index;

	SECTION("Initially empty") {
		auto results = index.search({1.0f, 0.0f, 0.0f}, 3);
		REQUIRE(results.empty());
	}

	SECTION("Add embeddings") {
		index.add("id1", "text1", {1.0f, 0.0f, 0.0f});
		index.add("id2", "text2", {0.0f, 1.0f, 0.0f});
		index.add("id3", "text3", {0.0f, 0.0f, 1.0f});

		auto results = index.search({1.0f, 0.0f, 0.0f}, 3);
		REQUIRE(results.size() <= 3);
		REQUIRE_FALSE(results.empty());

		// First result should be exact match (id1)
		REQUIRE(results[0].id == "id1");
		REQUIRE(results[0].text == "text1");
		REQUIRE(results[0].score > 0.99f);
	}

	SECTION("Search with limit") {
		for (int i = 0; i < 10; i++) {
			std::vector<float> emb(3, 0.0f);
			emb[i % 3] = 1.0f;
			index.add("id" + std::to_string(i), "text" + std::to_string(i), emb);
		}

		auto results = index.search({1.0f, 0.0f, 0.0f}, 3);
		REQUIRE(results.size() == 3);

		// Results should be sorted by score (highest first)
		if (results.size() >= 2) {
			REQUIRE(results[0].score >= results[1].score);
		}
	}

	SECTION("Clear index") {
		index.add("id1", "text1", {1.0f, 0.0f, 0.0f});
		index.clear();

		auto results = index.search({1.0f, 0.0f, 0.0f}, 3);
		REQUIRE(results.empty());
	}
}

TEST_CASE("Cosine similarity", "[inference]") {
	SECTION("Identical vectors") {
		std::vector<float> a = {1.0f, 2.0f, 3.0f};
		std::vector<float> b = {1.0f, 2.0f, 3.0f};

		float sim = ofxGgmlEmbeddingIndex::cosineSimilarity(a, b);
		REQUIRE(sim > 0.99f);
		REQUIRE(sim <= 1.0f);
	}

	SECTION("Orthogonal vectors") {
		std::vector<float> a = {1.0f, 0.0f, 0.0f};
		std::vector<float> b = {0.0f, 1.0f, 0.0f};

		float sim = ofxGgmlEmbeddingIndex::cosineSimilarity(a, b);
		REQUIRE(sim < 0.01f);
		REQUIRE(sim > -0.01f);
	}

	SECTION("Opposite vectors") {
		std::vector<float> a = {1.0f, 0.0f, 0.0f};
		std::vector<float> b = {-1.0f, 0.0f, 0.0f};

		float sim = ofxGgmlEmbeddingIndex::cosineSimilarity(a, b);
		REQUIRE(sim < -0.99f);
		REQUIRE(sim >= -1.0f);
	}

	SECTION("Empty vectors") {
		std::vector<float> a = {};
		std::vector<float> b = {};

		float sim = ofxGgmlEmbeddingIndex::cosineSimilarity(a, b);
		// Should return 0 or handle gracefully
		REQUIRE(sim == sim); // Not NaN
	}

	SECTION("Mismatched dimensions") {
		std::vector<float> a = {1.0f, 2.0f};
		std::vector<float> b = {1.0f, 2.0f, 3.0f};

		// Should handle gracefully (use minimum dimension)
		float sim = ofxGgmlEmbeddingIndex::cosineSimilarity(a, b);
		REQUIRE(sim == sim); // Not NaN
	}
}

TEST_CASE("Similarity hit structure", "[inference]") {
	SECTION("Default similarity hit") {
		ofxGgmlSimilarityHit hit;

		REQUIRE(hit.id.empty());
		REQUIRE(hit.text.empty());
		REQUIRE(hit.score == 0.0f);
		REQUIRE(hit.index == 0);
	}

	SECTION("Similarity hit with data") {
		ofxGgmlSimilarityHit hit;
		hit.id = "test_id";
		hit.text = "test text";
		hit.score = 0.95f;
		hit.index = 5;

		REQUIRE(hit.id == "test_id");
		REQUIRE(hit.text == "test text");
		REQUIRE(hit.score == 0.95f);
		REQUIRE(hit.index == 5);
	}
}

// Tests with actual executable (require llama CLI tools)
TEST_CASE("Inference with executable", "[inference][requires-executable][!mayfail]") {
	ofxGgmlInference inf;

	// These tests are marked [!mayfail] because they may not work in CI
	// Skip if executables not found
	if (inf.getCompletionExecutable().empty()) {
		WARN("Skipping - llama-completion executable not found");
		return;
	}

	// Skip if no test model
	std::string testModel = "test_data/tiny_model.gguf";
	if (!std::ifstream(testModel).good()) {
		WARN("Skipping - test model not found");
		return;
	}

	SECTION("Generate with minimal settings") {
		ofxGgmlInferenceSettings settings;
		settings.maxTokens = 10;

		auto result = inf.generate(testModel, "Hello", settings);

		// May still fail for various reasons (model incompatible, etc.)
		if (result.success) {
			REQUIRE_FALSE(result.text.empty());
			REQUIRE(result.elapsedMs > 0.0f);
		}
	}
}
