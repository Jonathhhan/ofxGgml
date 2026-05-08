#include "catch2.hpp"

#include "../src/ofxGgmlFoundation.h"
#include "../src/ofxGgmlInference.h"
#include "../src/ofxGgmlCreative.h"

class CapturingHttpTransport final : public ofxGgmlHttpTransport {
public:
	ofxGgmlHttpResponse send(const ofxGgmlHttpRequest & request) override {
		lastRequest = request;
		return {200, responseBody, ""};
	}

	ofxGgmlHttpRequest lastRequest;
	std::string responseBody = R"({"choices":[{"message":{"content":"server says hi"}}]})";
};

TEST_CASE("Fresh foundation runtime has openFrameworks-style lifecycle", "[foundation]") {
	ofxGgmlApp ai;
	REQUIRE_FALSE(ai.isReady());

	ofxGgmlRuntimeSettings settings;
	settings.threads = 2;
	settings.modelDirectory = "models";

	REQUIRE(ai.setup(settings));
	REQUIRE(ai.isReady());
	REQUIRE(ai.runtime().getBackendName() == "CPU");
	REQUIRE(ai.runtime().listDevices().size() == 1);

	ai.close();
	REQUIRE_FALSE(ai.isReady());
}

TEST_CASE("Fresh foundation text client can use a deterministic backend", "[foundation][text]") {
	ofxGgmlApp ai;
	REQUIRE(ai.setup());

	auto response = ai.generate("hello from a sketch");
	REQUIRE(response.ok);
	REQUIRE(response.text == "hello from a sketch");
	REQUIRE(response.error.empty());
}

TEST_CASE("Fresh foundation chat uses the latest message", "[foundation][text]") {
	ofxGgmlTextClient text;
	REQUIRE(text.setup());

	auto response = text.chat({
		{"system", "Be brief."},
		{"user", "describe this addon"}
	});

	REQUIRE(response.ok);
	REQUIRE(response.text == "describe this addon");
}

TEST_CASE("Clean inference session keeps model catalog separate from runtime", "[inference]") {
	ofxGgmlSession ai;
	REQUIRE(ai.setup());
	REQUIRE(ai.isReady());

	ofxGgmlModelSpec model;
	model.id = "tiny";
	model.name = "Tiny local model";
	model.path = "models/tiny.gguf";
	model.role = ofxGgmlModelRole::Text;
	model.contextTokens = 2048;
	ai.addModel(model);

	const auto found = ai.findModel("tiny");
	REQUIRE(found.has_value());
	REQUIRE(found->path == "models/tiny.gguf");
	REQUIRE(ai.models().findByRole(ofxGgmlModelRole::Text).size() == 1);
}

TEST_CASE("Clean inference session can swap text backend styles", "[inference][text]") {
	ofxGgmlSession ai;
	REQUIRE(ai.setup());
	REQUIRE(ai.useEchoBackend());

	auto echo = ai.generate("same text back");
	REQUIRE(echo.ok);
	REQUIRE(echo.text == "same text back");

	ofxGgmlLlamaServerSettings server;
	server.baseUrl = "http://127.0.0.1:8080";
	REQUIRE(ai.useLlamaServer(server));

	auto serverResponse = ai.generate("hello server");
	REQUIRE_FALSE(serverResponse.ok);
	REQUIRE(serverResponse.error.find("HTTP transport") != std::string::npos);
}

TEST_CASE("Clean llama-server backend serializes chat requests through transport", "[inference][llama-server]") {
	ofxGgmlSession ai;
	REQUIRE(ai.setup());

	auto transport = std::make_shared<CapturingHttpTransport>();
	ofxGgmlLlamaServerSettings server;
	server.baseUrl = "http://127.0.0.1:8080/";
	server.model = "local-model";
	REQUIRE(ai.useLlamaServer(server, transport));

	ofxGgmlTextRequest request;
	request.messages = {
		{"system", "Be brief."},
		{"user", "Say hi."}
	};
	request.maxTokens = 32;
	request.temperature = 0.2f;

	const auto response = ai.generate(request);
	REQUIRE(response.ok);
	REQUIRE(response.text == "server says hi");
	REQUIRE(transport->lastRequest.method == "POST");
	REQUIRE(transport->lastRequest.url == "http://127.0.0.1:8080/v1/chat/completions");
	REQUIRE(transport->lastRequest.body.find("\"model\":\"local-model\"") != std::string::npos);
	REQUIRE(transport->lastRequest.body.find("\"content\":\"Say hi.\"") != std::string::npos);
}

TEST_CASE("Clean inference session can own a small conversation loop", "[inference][chat]") {
	ofxGgmlSession ai;
	REQUIRE(ai.setup());

	ofxGgmlConversation conversation;
	conversation.setSystemPrompt("Reply plainly.");

	const auto response = ai.chat(conversation, "hello");
	REQUIRE(response.ok);
	REQUIRE(response.text == "hello");
	REQUIRE(conversation.size() == 3);
	REQUIRE(conversation.messages()[0].role == "system");
	REQUIRE(conversation.messages()[1].role == "user");
	REQUIRE(conversation.messages()[2].role == "assistant");
}

TEST_CASE("Clean inference session exposes an update-driven task queue", "[inference][tasks]") {
	ofxGgmlSession ai;
	REQUIRE(ai.setup());

	const auto first = ai.submit("first");
	const auto second = ai.submit("second");
	REQUIRE(first != second);
	REQUIRE(ai.tasks().pendingCount() == 2);

	REQUIRE(ai.update());
	REQUIRE(ai.tasks().pendingCount() == 1);
	REQUIRE(ai.tasks().completedCount() == 1);

	auto completed = ai.popCompleted();
	REQUIRE(completed.has_value());
	REQUIRE(completed->id == first);
	REQUIRE(completed->response.ok);
	REQUIRE(completed->response.text == "first");

	REQUIRE(ai.update(4));
	completed = ai.popCompleted();
	REQUIRE(completed.has_value());
	REQUIRE(completed->id == second);
	REQUIRE(completed->response.text == "second");
	REQUIRE(ai.tasks().empty());
}

TEST_CASE("Creative layer registers opt-in capabilities without pulling implementations", "[creative]") {
	ofxGgmlCreativeSession creative;
	REQUIRE(creative.setup());
	REQUIRE(creative.isReady());

	creative.addCapability({
		"vision-describe",
		"Vision Describe",
		ofxGgmlCreativeCapabilityKind::Vision,
		false,
		"Adapter slot for image-to-text backends."
	});

	const auto capability = creative.findCapability("vision-describe");
	REQUIRE(capability.has_value());
	REQUIRE(capability->kind == ofxGgmlCreativeCapabilityKind::Vision);
	REQUIRE_FALSE(capability->configured);
	REQUIRE(creative.findCapabilities(ofxGgmlCreativeCapabilityKind::Vision).size() == 1);
}
