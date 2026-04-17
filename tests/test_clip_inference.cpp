#include "catch2.hpp"
#include "../src/inference/ofxGgmlClipInference.h"

#include <cmath>

TEST_CASE("CLIP Inference initialization", "[clip_inference]") {
	ofxGgmlClipInference clip;

	SECTION("Default construction") {
		REQUIRE_NOTHROW(ofxGgmlClipInference());
	}

	SECTION("Has default backend") {
		REQUIRE(clip.getBackend() != nullptr);
	}
}

TEST_CASE("CLIP bridge backend creation", "[clip_inference]") {
	SECTION("Create generic bridge backend") {
		auto backend = ofxGgmlClipInference::createClipBridgeBackend();
		REQUIRE(backend != nullptr);
		REQUIRE_FALSE(backend->backendName().empty());
	}

	SECTION("Create with custom name") {
		auto backend = ofxGgmlClipInference::createClipBridgeBackend({}, "CustomCLIP");
		REQUIRE(backend != nullptr);
		REQUIRE(backend->backendName() == "CustomCLIP");
	}

	SECTION("Create Stable Diffusion CLIP backend") {
		auto backend = ofxGgmlClipInference::createStableDiffusionClipBridgeBackend();
		REQUIRE(backend != nullptr);
		REQUIRE_FALSE(backend->backendName().empty());
	}
}

TEST_CASE("CLIP bridge backend configuration", "[clip_inference]") {
	auto backend = std::dynamic_pointer_cast<ofxGgmlClipBridgeBackend>(
		ofxGgmlClipInference::createClipBridgeBackend());

	SECTION("Unconfigured by default") {
		REQUIRE_FALSE(backend->isConfigured());
	}

	SECTION("Configured after setting function") {
		backend->setEmbedFunction([](const ofxGgmlClipEmbeddingRequest &) {
			ofxGgmlClipEmbeddingResult result;
			result.success = true;
			return result;
		});
		REQUIRE(backend->isConfigured());
	}
}

TEST_CASE("CLIP backend setting and getting", "[clip_inference]") {
	ofxGgmlClipInference clip;

	SECTION("Set backend") {
		auto backend = ofxGgmlClipInference::createClipBridgeBackend();
		clip.setBackend(backend);
		REQUIRE(clip.getBackend() == backend);
	}

	SECTION("Replace backend") {
		auto backend1 = ofxGgmlClipInference::createClipBridgeBackend({}, "Backend1");
		auto backend2 = ofxGgmlClipInference::createClipBridgeBackend({}, "Backend2");

		clip.setBackend(backend1);
		REQUIRE(clip.getBackend()->backendName() == "Backend1");

		clip.setBackend(backend2);
		REQUIRE(clip.getBackend()->backendName() == "Backend2");
	}

	SECTION("Set null backend creates default") {
		auto backend = ofxGgmlClipInference::createClipBridgeBackend();
		clip.setBackend(backend);
		clip.setBackend(nullptr);
		REQUIRE(clip.getBackend() != nullptr);
	}
}

TEST_CASE("CLIP embedding request structure", "[clip_inference]") {
	ofxGgmlClipEmbeddingRequest request;

	SECTION("Default modality is Text") {
		REQUIRE(request.modality == ofxGgmlClipEmbeddingModality::Text);
	}

	SECTION("Default normalization is true") {
		REQUIRE(request.normalize);
	}

	SECTION("Text can be set") {
		request.text = "A photo of a cat";
		REQUIRE(request.text == "A photo of a cat");
	}

	SECTION("Image path can be set") {
		request.modality = ofxGgmlClipEmbeddingModality::Image;
		request.imagePath = "/path/to/image.jpg";
		REQUIRE(request.imagePath == "/path/to/image.jpg");
	}

	SECTION("Input ID and label can be set") {
		request.inputId = "id123";
		request.label = "Test Label";
		REQUIRE(request.inputId == "id123");
		REQUIRE(request.label == "Test Label");
	}
}

TEST_CASE("CLIP embedding result structure", "[clip_inference]") {
	ofxGgmlClipEmbeddingResult result;

	SECTION("Default state is failure") {
		REQUIRE_FALSE(result.success);
		REQUIRE(result.elapsedMs == 0.0f);
		REQUIRE(result.embedding.empty());
		REQUIRE(result.metadata.empty());
	}

	SECTION("Default modality is Text") {
		REQUIRE(result.modality == ofxGgmlClipEmbeddingModality::Text);
	}

	SECTION("Embedding vector can be populated") {
		result.embedding = {0.1f, 0.2f, 0.3f};
		REQUIRE(result.embedding.size() == 3);
		REQUIRE(result.embedding[0] == 0.1f);
		REQUIRE(result.embedding[1] == 0.2f);
		REQUIRE(result.embedding[2] == 0.3f);
	}
}

TEST_CASE("CLIP cosine similarity", "[clip_inference]") {
	SECTION("Identical vectors") {
		std::vector<float> a = {1.0f, 0.0f, 0.0f};
		std::vector<float> b = {1.0f, 0.0f, 0.0f};
		float sim = ofxGgmlClipInference::cosineSimilarity(a, b);
		REQUIRE(std::abs(sim - 1.0f) < 0.001f);
	}

	SECTION("Orthogonal vectors") {
		std::vector<float> a = {1.0f, 0.0f, 0.0f};
		std::vector<float> b = {0.0f, 1.0f, 0.0f};
		float sim = ofxGgmlClipInference::cosineSimilarity(a, b);
		REQUIRE(std::abs(sim - 0.0f) < 0.001f);
	}

	SECTION("Opposite vectors") {
		std::vector<float> a = {1.0f, 0.0f, 0.0f};
		std::vector<float> b = {-1.0f, 0.0f, 0.0f};
		float sim = ofxGgmlClipInference::cosineSimilarity(a, b);
		REQUIRE(std::abs(sim - (-1.0f)) < 0.001f);
	}

	SECTION("Normalized vectors") {
		std::vector<float> a = {0.6f, 0.8f, 0.0f};
		std::vector<float> b = {0.6f, 0.8f, 0.0f};
		float sim = ofxGgmlClipInference::cosineSimilarity(a, b);
		REQUIRE(std::abs(sim - 1.0f) < 0.001f);
	}

	SECTION("Different magnitude vectors") {
		std::vector<float> a = {1.0f, 1.0f, 1.0f};
		std::vector<float> b = {2.0f, 2.0f, 2.0f};
		float sim = ofxGgmlClipInference::cosineSimilarity(a, b);
		// Should be 1.0 because direction is same
		REQUIRE(std::abs(sim - 1.0f) < 0.001f);
	}

	SECTION("Empty vectors") {
		std::vector<float> a;
		std::vector<float> b;
		float sim = ofxGgmlClipInference::cosineSimilarity(a, b);
		REQUIRE(sim == 0.0f);
	}

	SECTION("Different length vectors") {
		std::vector<float> a = {1.0f, 0.0f};
		std::vector<float> b = {1.0f, 0.0f, 0.0f};
		float sim = ofxGgmlClipInference::cosineSimilarity(a, b);
		// Should handle gracefully
		REQUIRE(sim == 0.0f);
	}
}

TEST_CASE("CLIP embed text with mock backend", "[clip_inference]") {
	ofxGgmlClipInference clip;

	SECTION("Embed with no backend returns error") {
		auto result = clip.embedText("test");
		REQUIRE_FALSE(result.success);
	}

	SECTION("Embed text with configured backend") {
		auto backend = std::dynamic_pointer_cast<ofxGgmlClipBridgeBackend>(
			ofxGgmlClipInference::createClipBridgeBackend());

		backend->setEmbedFunction([](const ofxGgmlClipEmbeddingRequest & req) {
			ofxGgmlClipEmbeddingResult res;
			res.success = true;
			res.modality = req.modality;
			res.text = req.text;
			res.inputId = req.inputId;
			res.label = req.label;
			res.embedding = {0.1f, 0.2f, 0.3f};
			res.backendName = "MockCLIP";
			return res;
		});

		clip.setBackend(backend);

		auto result = clip.embedText("A cat", true, "test-id", "Test Label");

		REQUIRE(result.success);
		REQUIRE(result.modality == ofxGgmlClipEmbeddingModality::Text);
		REQUIRE(result.text == "A cat");
		REQUIRE(result.inputId == "test-id");
		REQUIRE(result.label == "Test Label");
		REQUIRE(result.embedding.size() == 3);
		REQUIRE(result.backendName == "MockCLIP");
	}
}

TEST_CASE("CLIP embed image with mock backend", "[clip_inference]") {
	ofxGgmlClipInference clip;

	SECTION("Embed image with configured backend") {
		auto backend = std::dynamic_pointer_cast<ofxGgmlClipBridgeBackend>(
			ofxGgmlClipInference::createClipBridgeBackend());

		backend->setEmbedFunction([](const ofxGgmlClipEmbeddingRequest & req) {
			ofxGgmlClipEmbeddingResult res;
			res.success = true;
			res.modality = req.modality;
			res.imagePath = req.imagePath;
			res.inputId = req.inputId;
			res.label = req.label;
			res.embedding = {0.5f, 0.6f, 0.7f};
			return res;
		});

		clip.setBackend(backend);

		auto result = clip.embedImage("/path/to/image.jpg", true, "img-id", "Image Label");

		REQUIRE(result.success);
		REQUIRE(result.modality == ofxGgmlClipEmbeddingModality::Image);
		REQUIRE(result.imagePath == "/path/to/image.jpg");
		REQUIRE(result.inputId == "img-id");
		REQUIRE(result.label == "Image Label");
		REQUIRE(result.embedding.size() == 3);
	}
}

TEST_CASE("CLIP similarity hit structure", "[clip_inference]") {
	ofxGgmlClipSimilarityHit hit;

	SECTION("Default values") {
		REQUIRE(hit.inputId.empty());
		REQUIRE(hit.label.empty());
		REQUIRE(hit.imagePath.empty());
		REQUIRE(hit.score == 0.0f);
		REQUIRE(hit.index == 0);
	}

	SECTION("Values can be set") {
		hit.inputId = "id1";
		hit.label = "Label1";
		hit.imagePath = "/path/image.jpg";
		hit.score = 0.95f;
		hit.index = 5;

		REQUIRE(hit.inputId == "id1");
		REQUIRE(hit.label == "Label1");
		REQUIRE(hit.imagePath == "/path/image.jpg");
		REQUIRE(hit.score == 0.95f);
		REQUIRE(hit.index == 5);
	}
}

TEST_CASE("CLIP image ranking request structure", "[clip_inference]") {
	ofxGgmlClipImageRankingRequest request;

	SECTION("Default values") {
		REQUIRE(request.prompt.empty());
		REQUIRE(request.imagePaths.empty());
		REQUIRE(request.topK == 0);
		REQUIRE(request.normalizeEmbeddings);
	}

	SECTION("Values can be set") {
		request.prompt = "A cat";
		request.promptId = "prompt1";
		request.imagePaths = {"/img1.jpg", "/img2.jpg", "/img3.jpg"};
		request.topK = 2;

		REQUIRE(request.prompt == "A cat");
		REQUIRE(request.promptId == "prompt1");
		REQUIRE(request.imagePaths.size() == 3);
		REQUIRE(request.topK == 2);
	}
}

TEST_CASE("CLIP image ranking result structure", "[clip_inference]") {
	ofxGgmlClipImageRankingResult result;

	SECTION("Default state is failure") {
		REQUIRE_FALSE(result.success);
		REQUIRE(result.hits.empty());
		REQUIRE(result.imageEmbeddings.empty());
	}

	SECTION("Hits can be populated") {
		ofxGgmlClipSimilarityHit hit1;
		hit1.score = 0.9f;
		hit1.index = 0;

		ofxGgmlClipSimilarityHit hit2;
		hit2.score = 0.8f;
		hit2.index = 1;

		result.hits = {hit1, hit2};

		REQUIRE(result.hits.size() == 2);
		REQUIRE(result.hits[0].score == 0.9f);
		REQUIRE(result.hits[1].score == 0.8f);
	}
}

TEST_CASE("CLIP rank images with mock backend", "[clip_inference]") {
	ofxGgmlClipInference clip;

	SECTION("Rank with no backend returns error") {
		ofxGgmlClipImageRankingRequest request;
		request.prompt = "A cat";
		request.imagePaths = {"/img1.jpg", "/img2.jpg"};

		auto result = clip.rankImagesForText(request);
		REQUIRE_FALSE(result.success);
	}

	SECTION("Rank images with configured backend") {
		auto backend = std::dynamic_pointer_cast<ofxGgmlClipBridgeBackend>(
			ofxGgmlClipInference::createClipBridgeBackend());

		int callCount = 0;
		backend->setEmbedFunction([&callCount](const ofxGgmlClipEmbeddingRequest & req) {
			ofxGgmlClipEmbeddingResult res;
			res.success = true;
			res.modality = req.modality;
			res.text = req.text;
			res.imagePath = req.imagePath;

			// Return different embeddings for text vs images
			if (req.modality == ofxGgmlClipEmbeddingModality::Text) {
				res.embedding = {1.0f, 0.0f, 0.0f};
			} else {
				// First image similar, second less similar
				if (callCount == 0) {
					res.embedding = {0.9f, 0.1f, 0.0f};
				} else {
					res.embedding = {0.0f, 1.0f, 0.0f};
				}
				callCount++;
			}

			return res;
		});

		clip.setBackend(backend);

		ofxGgmlClipImageRankingRequest request;
		request.prompt = "A cat";
		request.imagePaths = {"/img1.jpg", "/img2.jpg"};
		request.topK = 2;

		auto result = clip.rankImagesForText(request);

		REQUIRE(result.success);
		REQUIRE(result.hits.size() == 2);
		// Hits should be sorted by score descending
		REQUIRE(result.hits[0].score >= result.hits[1].score);
		REQUIRE(result.imageEmbeddings.size() == 2);
	}
}

TEST_CASE("CLIP metadata handling", "[clip_inference]") {
	ofxGgmlClipEmbeddingResult result;

	SECTION("Add metadata") {
		result.metadata.push_back({"model", "clip-vit-base"});
		result.metadata.push_back({"dimension", "512"});

		REQUIRE(result.metadata.size() == 2);
		REQUIRE(result.metadata[0].first == "model");
		REQUIRE(result.metadata[0].second == "clip-vit-base");
		REQUIRE(result.metadata[1].first == "dimension");
		REQUIRE(result.metadata[1].second == "512");
	}
}
