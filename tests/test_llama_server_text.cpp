#include "test_harness.h"
#include "../src/ofxGgmlText.h"

#include <string>

OFXGGML_TEST(llama_server_response_reports_status) {
	ofxGgmlTextServerResponse response;
	OFXGGML_REQUIRE(!response);
	OFXGGML_REQUIRE(response.isError());

	response.started = true;
	response.status = 200;
	OFXGGML_REQUIRE(response);
	OFXGGML_REQUIRE(response.isOk());
	OFXGGML_REQUIRE(!response.isError());

	response.cancelled = true;
	OFXGGML_REQUIRE(!response);
}

OFXGGML_TEST(llama_server_normalizes_urls) {
	OFXGGML_REQUIRE(
		ofxGgmlLlamaServerTextBackend::normalizeServerUrl("") ==
		"http://127.0.0.1:8080/v1/chat/completions");
	OFXGGML_REQUIRE(
		ofxGgmlLlamaServerTextBackend::normalizeServerUrl("http://127.0.0.1:8080") ==
		"http://127.0.0.1:8080/v1/chat/completions");
	OFXGGML_REQUIRE(
		ofxGgmlLlamaServerTextBackend::normalizeServerUrl("http://127.0.0.1:8080/v1") ==
		"http://127.0.0.1:8080/v1/chat/completions");
}

OFXGGML_TEST(llama_server_builds_openai_payload) {
	ofxGgmlTextRequest request;
	request.prompt = "hello";
	request.systemPrompt = "be brief";
	request.settings.maxTokens = 32;
	request.settings.temperature = 0.25f;
	request.settings.topP = 0.9f;
	request.settings.topK = 20;
	request.settings.seed = 7;
	request.settings.stopSequences = { "</s>" };

	const std::string body = ofxGgmlLlamaServerTextBackend::buildRequestBody(
		request,
		request.prompt,
		"local-model");

	OFXGGML_REQUIRE(body.find("\"model\":\"local-model\"") != std::string::npos);
	OFXGGML_REQUIRE(body.find("\"role\":\"system\"") != std::string::npos);
	OFXGGML_REQUIRE(body.find("\"role\":\"user\"") != std::string::npos);
	OFXGGML_REQUIRE(body.find("\"max_tokens\":32") != std::string::npos);
	OFXGGML_REQUIRE(body.find("\"stream\":false") != std::string::npos);
	OFXGGML_REQUIRE(body.find("\"stop\":[\"</s>\"]") != std::string::npos);

	request.settings.stream = true;
	const std::string streamingBody = ofxGgmlLlamaServerTextBackend::buildRequestBody(
		request,
		request.prompt,
		"local-model");
	OFXGGML_REQUIRE(streamingBody.find("\"stream\":true") != std::string::npos);
}

OFXGGML_TEST(llama_server_extracts_common_response_shapes) {
	OFXGGML_REQUIRE(
		ofxGgmlLlamaServerTextBackend::extractTextFromResponse(
			"{\"choices\":[{\"message\":{\"content\":\"hello\"}}]}") == "hello");
	OFXGGML_REQUIRE(
		ofxGgmlLlamaServerTextBackend::extractTextFromResponse(
			"{\"choices\":[{\"text\":\"completion\"}]}") == "completion");
	OFXGGML_REQUIRE(
		ofxGgmlLlamaServerTextBackend::extractTextFromResponse(
			"{\"response\":\"fallback\"}") == "fallback");
}

OFXGGML_TEST(llama_server_backend_runs_injected_runner) {
	ofxGgmlTextServerRequest capturedRequest;
	ofxGgmlLlamaServerTextBackend backend(
		"http://127.0.0.1:8080",
		[&](const ofxGgmlTextServerRequest & request) {
			capturedRequest = request;
			ofxGgmlTextServerResponse response;
			response.started = true;
			response.status = 200;
			response.body = "{\"choices\":[{\"message\":{\"content\":\"server hello\"}}]}";
			return response;
		});

	ofxGgmlTextRequest request;
	request.prompt = "hello";
	request.settings.serverUrl = "http://localhost:8080";

	std::string streamed;
	const auto result = backend.generate(
		request,
		[&](const std::string & chunk) {
			streamed += chunk;
			return true;
		});

	OFXGGML_REQUIRE(result.success);
	OFXGGML_REQUIRE(result.backendName == "llama-server");
	OFXGGML_REQUIRE(result.text == "server hello");
	OFXGGML_REQUIRE(streamed == "server hello");
	OFXGGML_REQUIRE(
		capturedRequest.url == "http://localhost:8080/v1/chat/completions");
	OFXGGML_REQUIRE(capturedRequest.body.find("\"content\":\"hello\"") != std::string::npos);
}

OFXGGML_TEST(llama_server_backend_reports_unreachable_server) {
	ofxGgmlLlamaServerTextBackend backend(
		"http://127.0.0.1:8080",
		[](const ofxGgmlTextServerRequest &) {
			ofxGgmlTextServerResponse response;
			response.started = true;
			response.status = 0;
			response.error = "connection refused";
			return response;
		});

	ofxGgmlTextRequest request;
	request.prompt = "hello";
	request.settings.serverUrl = "http://127.0.0.1:8080";

	const auto result = backend.generate(request);

	OFXGGML_REQUIRE(!result.success);
	OFXGGML_REQUIRE(
		result.error.find("llama-server is not reachable") != std::string::npos);
	OFXGGML_REQUIRE(result.error.find("connection refused") != std::string::npos);
}

OFXGGML_TEST(llama_server_backend_accepts_streamed_runner_output) {
	ofxGgmlLlamaServerTextBackend backend(
		"http://127.0.0.1:8080",
		[](const ofxGgmlTextServerRequest & request) {
			ofxGgmlTextServerResponse response;
			response.started = true;
			response.status = 200;
			if (request.onChunk) {
				request.onChunk("stream ");
				request.onChunk("hello");
			}
			response.text = "stream hello";
			response.body = "data: {...}\n";
			return response;
		});

	ofxGgmlTextRequest request;
	request.prompt = "hello";
	request.settings.stream = true;

	std::string streamed;
	const auto result = backend.generate(
		request,
		[&](const std::string & chunk) {
			streamed += chunk;
			return true;
		});

	OFXGGML_REQUIRE(result.success);
	OFXGGML_REQUIRE(result.text == "stream hello");
	OFXGGML_REQUIRE(streamed == "stream hello");
}

OFXGGML_TEST(llama_server_backend_reports_stream_cancel) {
	ofxGgmlLlamaServerTextBackend backend(
		"http://127.0.0.1:8080",
		[](const ofxGgmlTextServerRequest & request) {
			ofxGgmlTextServerResponse response;
			response.started = true;
			response.status = 200;
			response.text = "partial";
			if (request.onChunk && !request.onChunk("partial")) {
				response.cancelled = true;
				response.error = "llama-server request cancelled";
			}
			return response;
		});

	ofxGgmlTextRequest request;
	request.prompt = "hello";
	request.settings.stream = true;

	const auto result = backend.generate(
		request,
		[](const std::string &) {
			return false;
		});

	OFXGGML_REQUIRE(!result.success);
	OFXGGML_REQUIRE(result.text == "partial");
	OFXGGML_REQUIRE(result.error.find("cancelled") != std::string::npos);
}

OFXGGML_TEST(llama_server_backend_exposes_transport_cancel_probe) {
	ofxGgmlLlamaServerTextBackend backend(
		"http://127.0.0.1:8080",
		[](const ofxGgmlTextServerRequest & request) {
			ofxGgmlTextServerResponse response;
			response.started = true;
			response.status = 200;
			if (request.onChunk) {
				request.onChunk("partial");
			}
			response.text = "partial";
			if (request.shouldCancel && request.shouldCancel()) {
				response.cancelled = true;
				response.error = "llama-server request cancelled";
			}
			return response;
		});

	ofxGgmlTextRequest request;
	request.prompt = "hello";
	request.settings.stream = true;

	bool receivedText = false;
	const auto result = backend.generate(
		request,
		[&](const std::string & chunk) {
			if (chunk == "partial") {
				receivedText = true;
				return true;
			}
			return false;
		});

	OFXGGML_REQUIRE(receivedText);
	OFXGGML_REQUIRE(!result.success);
	OFXGGML_REQUIRE(result.text == "partial");
	OFXGGML_REQUIRE(result.error.find("cancelled") != std::string::npos);
}
