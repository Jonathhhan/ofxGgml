#include "test_harness.h"
#include "../src/ofxGgml.h"

OFXGGML_TEST(public_umbrella_header_exposes_v2_api) {
	ofxGgml::Server server("http://localhost:8001/v1");
	ofxGgml::ChatSession chat(server);
	OFXGGML_REQUIRE(server.getBaseUrl() == "http://localhost:8001");
	OFXGGML_REQUIRE(chat.getMessages().empty());
}
