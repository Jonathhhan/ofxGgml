#include "catch2.hpp"
#include "../src/inference/ofxGgmlNvigiGptBackend.h"
#include "../src/inference/ofxGgmlNvigiRagBackend.h"
#include "../src/inference/ofxGgmlNvigiReloadController.h"

TEST_CASE("NVIGI GPT backend stays optional by default", "[nvigi][gpt]") {
	ofxGgmlNvigiGptBackend backend;
	REQUIRE(backend.backendName() == "NVIGI GPT");
	REQUIRE_FALSE(ofxGgmlNvigiGptBackend::isSdkEnabled());
	REQUIRE_FALSE(backend.isConfigured());

	const auto result = backend.generate("model.gguf", "hello");
	REQUIRE_FALSE(result.success);
	REQUIRE(result.error.find("OFXGGML_ENABLE_NVIGI") != std::string::npos);
}

TEST_CASE("NVIGI GPT backend records callback configuration", "[nvigi][gpt]") {
	ofxGgmlNvigiGptBackend backend(
		[](const std::string &,
			const std::string &,
			const ofxGgmlInferenceSettings &,
			std::function<bool(const std::string &)>) {
			ofxGgmlInferenceResult result;
			result.success = true;
			result.text = "generated";
			return result;
		});
	REQUIRE(backend.isConfigured());
}

TEST_CASE("NVIGI RAG backend stores documents and stays optional by default", "[nvigi][rag]") {
	ofxGgmlNvigiRagBackend backend;
	REQUIRE(backend.backendName() == "NVIGI RAG");
	REQUIRE_FALSE(ofxGgmlNvigiRagBackend::isSdkEnabled());

	ofxGgmlRAGDocument doc;
	doc.id = "doc";
	doc.content = "NVIGI RAG context.";
	backend.addDocument(doc);
	REQUIRE(backend.documentCount() == 1);
	REQUIRE(backend.getDocuments().front().id == "doc");

	ofxGgmlRAGQuery query;
	query.query = "context";
	const auto result = backend.retrieve(query);
	REQUIRE_FALSE(result.success);
	REQUIRE(result.error.find("OFXGGML_ENABLE_NVIGI") != std::string::npos);
}

TEST_CASE("NVIGI RAG backend records callback configuration", "[nvigi][rag]") {
	ofxGgmlNvigiRagBackend backend(
		[](const ofxGgmlRAGQuery &,
			const std::vector<ofxGgmlRAGDocument> &) {
			ofxGgmlRAGRetrievalResult result;
			result.success = true;
			return result;
		});
	REQUIRE(backend.isConfigured());
}

TEST_CASE("NVIGI reload controller stays optional by default", "[nvigi][reload]") {
	ofxGgmlNvigiReloadController controller;
	REQUIRE(controller.controllerName() == "NVIGI Reload");
	REQUIRE_FALSE(ofxGgmlNvigiReloadController::isSdkEnabled());
	REQUIRE_FALSE(controller.isConfigured());
	REQUIRE(std::string(
		ofxGgmlNvigiReloadController::actionLabel(
			ofxGgmlNvigiReloadAction::Reload)) == "Reload");

	const auto result = controller.reload("gpt");
	REQUIRE_FALSE(result.success);
	REQUIRE(result.componentId == "gpt");
	REQUIRE(result.action == ofxGgmlNvigiReloadAction::Reload);
	REQUIRE(result.error.find("OFXGGML_ENABLE_NVIGI") != std::string::npos);
}

TEST_CASE("NVIGI reload controller records callback configuration", "[nvigi][reload]") {
	ofxGgmlNvigiReloadController controller(
		[](const ofxGgmlNvigiReloadRequest & request) {
			ofxGgmlNvigiReloadResult result;
			result.success = true;
			result.componentId = request.componentId;
			return result;
		});
	REQUIRE(controller.isConfigured());
}
