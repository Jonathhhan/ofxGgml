#include "test_harness.h"
#include "../src/ofxGgml.h"

OFXGGML_TEST(tool_registry_is_an_explicit_allowlist) {
	ofxGgml::DocumentIndex index;
	index.addText("guide.md", "Use a separate llama-server process boundary.");
	ofxGgml::ToolRegistry tools;
	OFXGGML_REQUIRE(tools.addDocumentSearch(index));
	OFXGGML_REQUIRE(!tools.addDocumentSearch(index));

	ofxGgml::ToolCall search{ "call-1", "search_documents", "{\"query\":\"process boundary\"}" };
	const auto found = tools.execute(search);
	OFXGGML_REQUIRE(found);
	OFXGGML_REQUIRE(found.content.find("[guide.md#chunk-1]") != std::string::npos);

	ofxGgml::ToolCall unknown{ "call-2", "read_file", "{\"path\":\"secret\"}" };
	const auto rejected = tools.execute(unknown);
	OFXGGML_REQUIRE(!rejected);
	OFXGGML_REQUIRE(rejected.error.find("not allowlisted") != std::string::npos);
}

OFXGGML_TEST(tool_loop_searches_then_returns_grounded_answer) {
	int requests = 0;
	ofxGgml::Server server("https://example.test/v1", [&](const ofxGgml::HttpRequest & request) {
		++requests;
		ofxGgml::HttpResponse response;
		response.started = true;
		response.status = 200;
		if (requests == 1) {
			OFXGGML_REQUIRE(request.body.find("\"tools\"") != std::string::npos);
			response.body = R"({"choices":[{"message":{"role":"assistant","tool_calls":[{"id":"call-1","type":"function","function":{"name":"search_documents","arguments":"{\"query\":\"process boundary\"}"}}]}}]})";
		} else {
			OFXGGML_REQUIRE(request.body.find("\"tool_call_id\":\"call-1\"") != std::string::npos);
			OFXGGML_REQUIRE(request.body.find("guide.md#chunk-1") != std::string::npos);
			response.body = R"({"choices":[{"message":{"role":"assistant","content":"The runtime stays separate [guide.md#chunk-1]."}}]})";
		}
		return response;
	});

	ofxGgml::ChatSession chat(server);
	chat.setSystemPrompt("Use search_documents and cite its results.");
	ofxGgml::DocumentIndex index;
	index.addText("guide.md", "The native runtime stays behind a process boundary.");
	ofxGgml::ToolRegistry tools;
	tools.addDocumentSearch(index);
	ofxGgml::ToolLoop loop(chat, tools);

	const auto result = loop.run("Why is the runtime separate?");
	OFXGGML_REQUIRE(result);
	OFXGGML_REQUIRE(result.modelRequests == 2);
	OFXGGML_REQUIRE(result.steps.size() == 1);
	OFXGGML_REQUIRE(result.text.find("[guide.md#chunk-1]") != std::string::npos);
	OFXGGML_REQUIRE(chat.getMessages().size() == 4);
}
