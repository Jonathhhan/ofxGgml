#include "test_harness.h"
#include "../src/ofxGgml.h"

#include <string>

OFXGGML_TEST(server_normalizes_openai_endpoint_urls) {
	ofxGgml::HttpRequest captured;
	auto transport = [&](const ofxGgml::HttpRequest & request) {
		captured = request;
		ofxGgml::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = "{\"data\":[]}";
		return response;
	};
	ofxGgml::Server server(
		"http://localhost:8001/v1/chat/completions", transport);
	ofxGgml::Server defaultServer("", transport);

	OFXGGML_REQUIRE(server.getBaseUrl() == "http://localhost:8001");
	OFXGGML_REQUIRE(defaultServer.getBaseUrl() == "http://127.0.0.1:8080");
	OFXGGML_REQUIRE(server.inspect());
	OFXGGML_REQUIRE(captured.url == "http://localhost:8001/v1/models");
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
	ofxGgml::HttpRequest captured;
	ofxGgml::Server server("http://localhost:8001", [&](const ofxGgml::HttpRequest & httpRequest) {
		captured = httpRequest;
		ofxGgml::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = "{\"choices\":[{\"message\":{\"content\":\"ok\"}}]}";
		return response;
	});
	ofxGgml::ChatRequest request;
	request.systemPrompt = "Be brief";
	request.messages.push_back({ ofxGgml::ChatRole::User, "Hello \"world\"" });
	request.options.model = "local/qwen";
	request.options.maxTokens = 42;
	request.options.temperature = 0.25f;
	request.options.seed = 7;
	request.options.stopSequences = { "</s>" };

	OFXGGML_REQUIRE(server.chat(request));
	const std::string & body = captured.body;
	OFXGGML_REQUIRE(captured.method == ofxGgml::HttpMethod::Post);
	OFXGGML_REQUIRE(captured.url == "http://localhost:8001/v1/chat/completions");
	OFXGGML_REQUIRE(body.find("\"model\":\"local/qwen\"") != std::string::npos);
	OFXGGML_REQUIRE(body.find("\"role\":\"system\"") != std::string::npos);
	OFXGGML_REQUIRE(body.find("Hello \\\"world\\\"") != std::string::npos);
	OFXGGML_REQUIRE(body.find("\"max_tokens\":42") != std::string::npos);
	OFXGGML_REQUIRE(body.find("\"seed\":7") != std::string::npos);
	OFXGGML_REQUIRE(body.find("\"stop\":[\"</s>\"]") != std::string::npos);
}

OFXGGML_TEST(server_adds_bearer_auth_without_exposing_it_in_the_body) {
	ofxGgml::HttpRequest captured;
	ofxGgml::Server server("https://router.huggingface.co/v1", [&](const ofxGgml::HttpRequest & request) {
		captured = request;
		ofxGgml::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = "{\"choices\":[{\"message\":{\"content\":\"ok\"}}]}";
		return response;
	});
	server.setBearerToken("hf_test_token");
	ofxGgml::ChatRequest request;
	request.messages.push_back({ ofxGgml::ChatRole::User, "hello" });
	request.options.model = "org/model:provider";

	OFXGGML_REQUIRE(server.chat(request));
	OFXGGML_REQUIRE(server.hasBearerToken());
	OFXGGML_REQUIRE(captured.url == "https://router.huggingface.co/v1/chat/completions");
	OFXGGML_REQUIRE(captured.headers.size() == 1);
	OFXGGML_REQUIRE(captured.headers[0].first == "Authorization");
	OFXGGML_REQUIRE(captured.headers[0].second == "Bearer hf_test_token");
	OFXGGML_REQUIRE(captured.body.find("hf_test_token") == std::string::npos);
}

OFXGGML_TEST(server_extracts_chat_response_text) {
	std::string responseBody =
		"{\"choices\":[{\"message\":{\"content\":\"Hello\\nthere\"}}]}";
	ofxGgml::Server server("http://localhost:8001", [&](const ofxGgml::HttpRequest &) {
		ofxGgml::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = responseBody;
		return response;
	});
	ofxGgml::ChatRequest request;
	request.messages.push_back({ ofxGgml::ChatRole::User, "Hello" });

	OFXGGML_REQUIRE(server.chat(request).text == "Hello\nthere");
	responseBody = "{\"choices\":[{\"text\":\"completion\"}]}";
	OFXGGML_REQUIRE(server.chat(request).text == "completion");
}

OFXGGML_TEST(server_extracts_openai_tool_calls) {
	ofxGgml::Server server("http://localhost:8001", [](const ofxGgml::HttpRequest &) {
		ofxGgml::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = R"({"choices":[{"message":{"tool_calls":[{"function":{"arguments":"{\"query\":\"Grüße\"}","name":"search_documents"},"type":"function","id":"call-7"}]}}]})";
		return response;
	});
	ofxGgml::ChatRequest request;
	request.messages.push_back({ ofxGgml::ChatRole::User, "Search" });
	request.tools.push_back({ "search_documents", "Search documents", "{}" });

	const auto result = server.chat(request);
	OFXGGML_REQUIRE(result);
	const auto & calls = result.toolCalls;
	OFXGGML_REQUIRE(calls.size() == 1);
	OFXGGML_REQUIRE(calls[0].id == "call-7");
	OFXGGML_REQUIRE(calls[0].name == "search_documents");
	OFXGGML_REQUIRE(calls[0].argumentsJson.find("Grüße") != std::string::npos);
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

OFXGGML_TEST(server_rejects_streaming_tools_before_transport) {
	int calls = 0;
	ofxGgml::Server server("http://localhost:8001", [&](const ofxGgml::HttpRequest &) {
		++calls;
		return ofxGgml::HttpResponse{};
	});
	ofxGgml::ChatRequest request;
	request.messages.push_back({ ofxGgml::ChatRole::User, "Hello" });
	request.tools.push_back({ "search_documents", "Search documents", "{}" });
	request.options.stream = true;
	const auto result = server.chat(request);
	OFXGGML_REQUIRE(!result);
	OFXGGML_REQUIRE(result.error.find("not supported") != std::string::npos);
	OFXGGML_REQUIRE(calls == 0);
}
