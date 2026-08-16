#include "test_harness.h"
#include "../src/ofxGgml.h"

OFXGGML_TEST(public_umbrella_header_exposes_v2_api) {
	ofxGgml::Server server("http://localhost:8001/v1");
	ofxGgml::ChatSession chat(server);
	ofxGgml::DocumentIndex documents;
	ofxGgml::ToolRegistry tools;
	tools.addDocumentSearch(documents);
	ofxGgml::ToolLoop loop(chat, tools);
	OFXGGML_REQUIRE(server.getBaseUrl() == "http://localhost:8001");
	OFXGGML_REQUIRE(chat.getMessages().empty());
	OFXGGML_REQUIRE(tools.contains("search_documents"));
}
