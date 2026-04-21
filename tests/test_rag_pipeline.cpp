#include "catch2.hpp"
#include "../src/ofxGgml.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path makeRagTestDir(const std::string & name) {
	const auto base = std::filesystem::temp_directory_path() / "ofxggml_rag_tests";
	std::filesystem::create_directories(base);
	const auto dir = base / (name + "_" + std::to_string(std::rand()));
	std::filesystem::create_directories(dir);
	return dir;
}

} // namespace

TEST_CASE("RAG pipeline chunks documents correctly", "[rag]") {
	ofxGgmlRAGDocument doc;
	doc.id = "doc1";
	doc.content =
		"The quick brown fox jumped over the lazy dog. "
		"Artificial intelligence is a broad field of computer science. "
		"Machine learning is a subset of artificial intelligence. "
		"Deep learning uses neural networks with many layers. "
		"Natural language processing helps computers understand human text.";
	doc.sourceLabel = "Test document";

	const auto chunks = ofxGgmlRAGPipeline::chunkDocument(doc, 80, 20);
	REQUIRE_FALSE(chunks.empty());

	for (const auto & chunk : chunks) {
		REQUIRE_FALSE(chunk.text.empty());
		REQUIRE(chunk.docId == "doc1");
		REQUIRE(chunk.sourceLabel == "Test document");
	}

	// First chunk should start with the beginning of the document.
	REQUIRE(chunks.front().text.find("quick brown fox") != std::string::npos);
	// With overlap, the last chunk should still contain content.
	REQUIRE_FALSE(chunks.back().text.empty());
}

TEST_CASE("RAG pipeline scores chunks by keyword overlap", "[rag]") {
	ofxGgmlRAGChunk chunkA;
	chunkA.text = "Machine learning is a subset of artificial intelligence and uses data.";
	chunkA.docId = "doc1";

	ofxGgmlRAGChunk chunkB;
	chunkB.text = "The quick brown fox jumped over the lazy dog.";
	chunkB.docId = "doc2";

	const float scoreA = ofxGgmlRAGPipeline::scoreChunk(chunkA, "machine learning artificial intelligence");
	const float scoreB = ofxGgmlRAGPipeline::scoreChunk(chunkB, "machine learning artificial intelligence");

	// chunkA should score higher since it contains query terms.
	REQUIRE(scoreA > scoreB);
}

TEST_CASE("RAG pipeline retrieves top-K chunks from documents", "[rag]") {
	ofxGgmlRAGPipeline pipeline;

	ofxGgmlRAGDocument doc1;
	doc1.id = "doc-ai";
	doc1.content =
		"Artificial intelligence enables machines to learn from data. "
		"Deep learning and neural networks are core AI techniques. "
		"Natural language processing is a key AI application.";
	doc1.sourceLabel = "AI Overview";
	pipeline.addDocument(doc1);

	ofxGgmlRAGDocument doc2;
	doc2.id = "doc-cooking";
	doc2.content =
		"A good pasta recipe requires fresh tomatoes and basil. "
		"Italian cooking uses olive oil and garlic extensively. "
		"Homemade bread needs flour, yeast, water, and salt.";
	doc2.sourceLabel = "Cooking Guide";
	pipeline.addDocument(doc2);

	REQUIRE(pipeline.documentCount() == 2);

	ofxGgmlRAGQuery query;
	query.query = "What is artificial intelligence and deep learning?";
	query.topK = 3;
	query.chunkSize = 100;
	query.chunkOverlap = 20;

	const auto result = pipeline.retrieve(query);
	REQUIRE(result.success);
	REQUIRE_FALSE(result.chunks.empty());
	REQUIRE_FALSE(result.augmentedContext.empty());

	// The top chunks should be from the AI document, not the cooking doc.
	REQUIRE(result.chunks.front().docId == "doc-ai");
	REQUIRE(result.augmentedContext.find("[Passage 1]") != std::string::npos);
	REQUIRE(result.augmentedContext.find("AI Overview") != std::string::npos);
}

TEST_CASE("RAG pipeline returns error when no documents are loaded", "[rag]") {
	ofxGgmlRAGPipeline pipeline;

	ofxGgmlRAGQuery query;
	query.query = "What is machine learning?";

	const auto result = pipeline.retrieve(query);
	REQUIRE_FALSE(result.success);
	REQUIRE_FALSE(result.error.empty());
}

TEST_CASE("RAG pipeline returns error for empty query", "[rag]") {
	ofxGgmlRAGPipeline pipeline;
	ofxGgmlRAGDocument doc;
	doc.id = "doc1";
	doc.content = "Some content.";
	pipeline.addDocument(doc);

	ofxGgmlRAGQuery query;
	query.query = "";

	const auto result = pipeline.retrieve(query);
	REQUIRE_FALSE(result.success);
	REQUIRE_FALSE(result.error.empty());
}

TEST_CASE("RAG pipeline builds augmented context with source headers", "[rag]") {
	std::vector<ofxGgmlRAGChunk> chunks;

	ofxGgmlRAGChunk c1;
	c1.text = "AI is transforming industries.";
	c1.sourceLabel = "TechReport";
	c1.sourceUri = "https://example.com/report";
	chunks.push_back(c1);

	ofxGgmlRAGChunk c2;
	c2.text = "Neural networks mimic the brain.";
	c2.sourceLabel = "ScienceJournal";
	chunks.push_back(c2);

	const auto context = ofxGgmlRAGPipeline::buildAugmentedContext(chunks, true);
	REQUIRE(context.find("[Passage 1]") != std::string::npos);
	REQUIRE(context.find("TechReport") != std::string::npos);
	REQUIRE(context.find("https://example.com/report") != std::string::npos);
	REQUIRE(context.find("[Passage 2]") != std::string::npos);
	REQUIRE(context.find("ScienceJournal") != std::string::npos);
	REQUIRE(context.find("AI is transforming") != std::string::npos);
}

TEST_CASE("RAG pipeline builds augmented context without source headers", "[rag]") {
	std::vector<ofxGgmlRAGChunk> chunks;
	ofxGgmlRAGChunk c;
	c.text = "Machine learning requires data.";
	c.sourceLabel = "Hidden";
	chunks.push_back(c);

	const auto context = ofxGgmlRAGPipeline::buildAugmentedContext(chunks, false);
	REQUIRE(context.find("Machine learning requires data.") != std::string::npos);
	REQUIRE(context.find("Hidden") == std::string::npos);
}

TEST_CASE("RAG pipeline builds augmented prompt", "[rag]") {
	const std::string context = "[Passage 1] Source\nSome relevant text.\n";
	const auto prompt =
		ofxGgmlRAGPipeline::buildAugmentedPrompt(context, "What is relevant?", "");
	REQUIRE(prompt.find("Passages:") != std::string::npos);
	REQUIRE(prompt.find("Some relevant text.") != std::string::npos);
	REQUIRE(prompt.find("What is relevant?") != std::string::npos);
	REQUIRE(prompt.find("Answer:") != std::string::npos);
}

TEST_CASE("RAG pipeline respects custom prompt prefix", "[rag]") {
	const auto prompt = ofxGgmlRAGPipeline::buildAugmentedPrompt(
		"[Passage 1]\nContext text.\n",
		"My question",
		"Use only the passages below.");
	REQUIRE(prompt.find("Use only the passages below.") != std::string::npos);
	REQUIRE(prompt.find("My question") != std::string::npos);
}

TEST_CASE("RAG pipeline addTextDocument helper works", "[rag]") {
	ofxGgmlRAGPipeline pipeline;
	pipeline.addTextDocument("Hello world content.", "my-id", "My Label", "http://example.com");
	REQUIRE(pipeline.documentCount() == 1);

	pipeline.clearDocuments();
	REQUIRE(pipeline.documentCount() == 0);
}

TEST_CASE("Easy API exposes RAG pipeline", "[easy_api][rag]") {
	ofxGgmlEasy easy;

	// Without configureText, ragQuery should fail gracefully.
	const auto missingConfig = easy.ragQuery("What is AI?");
	REQUIRE_FALSE(missingConfig.success);
	REQUIRE(missingConfig.error.find("configureText") != std::string::npos);

	// Add a document to the pipeline through the easy accessor.
	easy.getRAGPipeline().addTextDocument(
		"Artificial intelligence is the simulation of human intelligence by machines.",
		"doc1",
		"AI Basics");

	REQUIRE(easy.getRAGPipeline().documentCount() == 1);

	// Retrieve-only (no model needed).
	ofxGgmlRAGQuery query;
	query.query = "artificial intelligence machines";
	query.topK = 2;
	const auto retrieval = easy.getRAGPipeline().retrieve(query);
	REQUIRE(retrieval.success);
	REQUIRE_FALSE(retrieval.chunks.empty());
	REQUIRE(retrieval.augmentedContext.find("Artificial intelligence") != std::string::npos);
}
