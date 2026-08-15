#include "test_harness.h"
#include "../src/ofxGgml.h"

#include <string>

OFXGGML_TEST(server_normalizes_openai_endpoint_urls) {
	OFXGGML_REQUIRE(
		ofxGgml::Server::normalizeBaseUrl("") == "http://127.0.0.1:8080");
	OFXGGML_REQUIRE(
		ofxGgml::Server::normalizeBaseUrl("http://localhost:8001/v1") ==
		"http://localhost:8001");
	OFXGGML_REQUIRE(
		ofxGgml::Server::normalizeBaseUrl(
			"http://localhost:8001/v1/chat/completions") ==
		"http://localhost:8001");
	OFXGGML_REQUIRE(
		ofxGgml::Server::modelsUrl("http://localhost:8001/") ==
		"http://localhost:8001/v1/models");
}

OFXGGML_TEST(server_inspects_models_endpoint) {
	ofxGgml::HttpRequest captured;
	ofxGgml::Server server("http://localhost:8001", [&](const ofxGgml::HttpRequest & request) {
		captured = request;
		ofxGgml::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = "{\"data\":[{\"id\":\"local/qwen\"},{\"id\":\"embed\"}]}";
		return response;
	});

	const auto status = server.inspect();
	OFXGGML_REQUIRE(status);
	OFXGGML_REQUIRE(captured.method == ofxGgml::HttpMethod::Get);
	OFXGGML_REQUIRE(captured.url == "http://localhost:8001/v1/models");
	OFXGGML_REQUIRE(status.models.size() == 2);
	OFXGGML_REQUIRE(status.models[0] == "local/qwen");
}

OFXGGML_TEST(server_builds_chat_completions_body) {
	ofxGgml::ChatRequest request;
	request.systemPrompt = "Be brief";
	request.messages.push_back({ ofxGgml::ChatRole::User, "Hello \"world\"" });
	request.options.model = "local/qwen";
	request.options.maxTokens = 42;
	request.options.temperature = 0.25f;
	request.options.seed = 7;
	request.options.stopSequences = { "</s>" };

	const std::string body = ofxGgml::Server::buildChatBody(request);
	OFXGGML_REQUIRE(body.find("\"model\":\"local/qwen\"") != std::string::npos);
	OFXGGML_REQUIRE(body.find("\"role\":\"system\"") != std::string::npos);
	OFXGGML_REQUIRE(body.find("Hello \\\"world\\\"") != std::string::npos);
	OFXGGML_REQUIRE(body.find("\"max_tokens\":42") != std::string::npos);
	OFXGGML_REQUIRE(body.find("\"seed\":7") != std::string::npos);
	OFXGGML_REQUIRE(body.find("\"stop\":[\"</s>\"]") != std::string::npos);
}

OFXGGML_TEST(server_extracts_chat_response_text) {
	OFXGGML_REQUIRE(
		ofxGgml::Server::extractChatText(
			"{\"choices\":[{\"message\":{\"content\":\"Hello\\nthere\"}}]}") ==
		"Hello\nthere");
	OFXGGML_REQUIRE(
		ofxGgml::Server::extractChatText(
			"{\"choices\":[{\"text\":\"completion\"}]}") == "completion");
}

OFXGGML_TEST(server_reports_http_failures) {
	ofxGgml::Server server("http://localhost:8001", [](const ofxGgml::HttpRequest &) {
		ofxGgml::HttpResponse response;
		response.started = true;
		response.status = 503;
		response.error = "unavailable";
		return response;
	});
	ofxGgml::ChatRequest request;
	request.messages.push_back({ ofxGgml::ChatRole::User, "Hello" });

	const auto result = server.chat(request);
	OFXGGML_REQUIRE(!result);
	OFXGGML_REQUIRE(result.httpStatus == 503);
	OFXGGML_REQUIRE(result.error.find("HTTP 503") != std::string::npos);
}
