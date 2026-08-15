#include "test_harness.h"
#include "../src/ofxGgml.h"

OFXGGML_TEST(chat_session_keeps_successful_history) {
	int requestCount = 0;
	ofxGgml::Server server("http://localhost:8001", [&](const ofxGgml::HttpRequest & request) {
		++requestCount;
		ofxGgml::HttpResponse response;
		response.started = true;
		response.status = 200;
		response.body = requestCount == 1
			? "{\"choices\":[{\"message\":{\"content\":\"First answer\"}}]}"
			: "{\"choices\":[{\"message\":{\"content\":\"Second answer\"}}]}";
		OFXGGML_REQUIRE(request.body.find("Be concise") != std::string::npos);
		return response;
	});

	ofxGgml::ChatSession session(server);
	session.setSystemPrompt("Be concise");
	const auto first = session.send("First question");
	const auto second = session.send("Second question");

	OFXGGML_REQUIRE(first);
	OFXGGML_REQUIRE(second);
	OFXGGML_REQUIRE(session.getMessages().size() == 4);
	OFXGGML_REQUIRE(session.getMessages()[1].content == "First answer");
	OFXGGML_REQUIRE(session.getMessages()[3].content == "Second answer");
}

OFXGGML_TEST(chat_session_rolls_back_failed_user_message) {
	ofxGgml::Server server("http://localhost:8001", [](const ofxGgml::HttpRequest &) {
		ofxGgml::HttpResponse response;
		response.started = true;
		response.status = 500;
		return response;
	});
	ofxGgml::ChatSession session(server);

	const auto result = session.send("This fails");
	OFXGGML_REQUIRE(!result);
	OFXGGML_REQUIRE(session.getMessages().empty());
}

OFXGGML_TEST(chat_session_rejects_empty_messages_without_transport) {
	int calls = 0;
	ofxGgml::Server server("http://localhost:8001", [&](const ofxGgml::HttpRequest &) {
		++calls;
		return ofxGgml::HttpResponse{};
	});
	ofxGgml::ChatSession session(server);

	const auto result = session.send("");
	OFXGGML_REQUIRE(!result);
	OFXGGML_REQUIRE(result.error == "message is empty");
	OFXGGML_REQUIRE(calls == 0);
}
