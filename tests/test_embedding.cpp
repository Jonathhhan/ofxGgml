#include "test_harness.h"
#include "../src/ofxGgmlText.h"

#include <memory>
#include <string>

OFXGGML_TEST(embedding_default_backend_exists) {
	ofxGgmlEmbeddingGenerator generator;

	OFXGGML_REQUIRE(generator.getBackend() != nullptr);
	OFXGGML_REQUIRE(!generator.getBackend()->backendName().empty());
}

OFXGGML_TEST(embedding_unconfigured_backend_reports_error) {
	ofxGgmlEmbeddingGenerator generator;
	const auto result = generator.embed("hello");

	OFXGGML_REQUIRE(!result.success);
	OFXGGML_REQUIRE(!result.error.empty());
	OFXGGML_REQUIRE(result.backendName == "EmbeddingBridge");
}

OFXGGML_TEST(embedding_bridge_backend_runs_callback) {
	auto backend = std::dynamic_pointer_cast<ofxGgmlEmbeddingBridgeBackend>(
		ofxGgmlEmbeddingGenerator::createEmbeddingBridgeBackend());
	OFXGGML_REQUIRE(backend != nullptr);
	OFXGGML_REQUIRE(!backend->isConfigured());

	backend->setEmbedFunction(
		[](const ofxGgmlEmbeddingRequest & request) {
			ofxGgmlEmbeddingResult result;
			result.success = true;
			result.embedding = {
				static_cast<float>(request.input.size()),
				1.0f
			};
			result.embeddings.push_back(result.embedding);
			return result;
		});

	ofxGgmlEmbeddingGenerator generator;
	generator.setBackend(backend);
	const auto result = generator.embed("hello");

	OFXGGML_REQUIRE(result.success);
	OFXGGML_REQUIRE(result.backendName == "EmbeddingBridge");
	OFXGGML_REQUIRE(result.embedding.size() == 2);
	OFXGGML_REQUIRE(result.embedding[0] == 5.0f);
}

OFXGGML_TEST(llama_server_embedding_normalizes_urls) {
	OFXGGML_REQUIRE(
		ofxGgmlLlamaServerEmbeddingBackend::normalizeServerUrl("") ==
		"http://127.0.0.1:8081/v1/embeddings");
	OFXGGML_REQUIRE(
		ofxGgmlLlamaServerEmbeddingBackend::normalizeServerUrl("http://127.0.0.1:8080") ==
		"http://127.0.0.1:8080/v1/embeddings");
	OFXGGML_REQUIRE(
		ofxGgmlLlamaServerEmbeddingBackend::normalizeServerUrl("http://127.0.0.1:8080/v1") ==
		"http://127.0.0.1:8080/v1/embeddings");
}

OFXGGML_TEST(llama_server_embedding_builds_openai_payload) {
	ofxGgmlEmbeddingRequest request;
	request.input = "hello";

	const std::string body =
		ofxGgmlLlamaServerEmbeddingBackend::buildRequestBody(request, "local-model");

	OFXGGML_REQUIRE(body.find("\"model\":\"local-model\"") != std::string::npos);
	OFXGGML_REQUIRE(body.find("\"input\":\"hello\"") != std::string::npos);

	request.input.clear();
	request.inputs = { "alpha", "beta" };
	const std::string batchBody =
		ofxGgmlLlamaServerEmbeddingBackend::buildRequestBody(request, "local-model");
	OFXGGML_REQUIRE(batchBody.find("\"input\":[\"alpha\",\"beta\"]") != std::string::npos);
}

OFXGGML_TEST(llama_server_embedding_extracts_vectors) {
	const auto embeddings =
		ofxGgmlLlamaServerEmbeddingBackend::extractEmbeddingsFromResponse(
			"{\"data\":[{\"embedding\":[0.25,-1.5,2e-1]},{\"embedding\":[3,4]}]}");

	OFXGGML_REQUIRE(embeddings.size() == 2);
	OFXGGML_REQUIRE(embeddings[0].size() == 3);
	OFXGGML_REQUIRE(embeddings[0][0] == 0.25f);
	OFXGGML_REQUIRE(embeddings[0][1] == -1.5f);
	OFXGGML_REQUIRE(embeddings[1][1] == 4.0f);
}

OFXGGML_TEST(llama_server_embedding_backend_runs_injected_runner) {
	ofxGgmlTextServerRequest capturedRequest;
	ofxGgmlLlamaServerEmbeddingBackend backend(
		"http://127.0.0.1:8080",
		[&](const ofxGgmlTextServerRequest & request) {
			capturedRequest = request;
			ofxGgmlTextServerResponse response;
			response.started = true;
			response.status = 200;
			response.body = "{\"data\":[{\"embedding\":[0.1,0.2,0.3]}]}";
			return response;
		});

	ofxGgmlEmbeddingRequest request;
	request.input = "hello";
	request.settings.serverUrl = "http://localhost:8080";
	request.settings.serverModel = "local-model";

	const auto result = backend.embed(request);

	OFXGGML_REQUIRE(result.success);
	OFXGGML_REQUIRE(result.backendName == "llama-server-embedding");
	OFXGGML_REQUIRE(result.embedding.size() == 3);
	OFXGGML_REQUIRE(result.embeddings.size() == 1);
	OFXGGML_REQUIRE(
		capturedRequest.url == "http://localhost:8080/v1/embeddings");
	OFXGGML_REQUIRE(capturedRequest.body.find("\"input\":\"hello\"") != std::string::npos);
}

OFXGGML_TEST(llama_server_embedding_backend_reports_unreachable_server) {
	ofxGgmlLlamaServerEmbeddingBackend backend(
		"http://127.0.0.1:8080",
		[](const ofxGgmlTextServerRequest &) {
			ofxGgmlTextServerResponse response;
			response.started = true;
			response.status = 0;
			response.error = "connection refused";
			return response;
		});

	ofxGgmlEmbeddingRequest request;
	request.input = "hello";

	const auto result = backend.embed(request);

	OFXGGML_REQUIRE(!result.success);
	OFXGGML_REQUIRE(
		result.error.find("llama-server is not reachable") != std::string::npos);
	OFXGGML_REQUIRE(result.error.find("connection refused") != std::string::npos);
}
