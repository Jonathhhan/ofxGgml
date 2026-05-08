#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct ofxGgmlTextMessage {
	std::string role;
	std::string content;
};

struct ofxGgmlTextRequest {
	std::string prompt;
	std::vector<ofxGgmlTextMessage> messages;
	int maxTokens = 256;
	float temperature = 0.7f;
};

struct ofxGgmlTextResponse {
	bool ok = false;
	std::string text;
	std::string error;
};

class ofxGgmlTextBackend {
public:
	virtual ~ofxGgmlTextBackend() = default;
	virtual bool setup() { return true; }
	virtual ofxGgmlTextResponse generate(const ofxGgmlTextRequest & request) = 0;
};

class ofxGgmlEchoTextBackend final : public ofxGgmlTextBackend {
public:
	ofxGgmlTextResponse generate(const ofxGgmlTextRequest & request) override {
		std::string text = request.prompt;
		if (text.empty() && !request.messages.empty()) {
			text = request.messages.back().content;
		}
		if (text.empty()) {
			return {false, "", "No prompt or messages provided."};
		}

		const auto tokenLimit = static_cast<std::size_t>(std::max(1, request.maxTokens));
		if (text.size() > tokenLimit) {
			text.resize(tokenLimit);
		}
		return {true, text, ""};
	}
};

class ofxGgmlTextClient {
public:
	bool setup(std::shared_ptr<ofxGgmlTextBackend> backend = std::make_shared<ofxGgmlEchoTextBackend>()) {
		backend_ = std::move(backend);
		return backend_ && backend_->setup();
	}

	bool isReady() const {
		return static_cast<bool>(backend_);
	}

	ofxGgmlTextResponse generate(const std::string & prompt) {
		ofxGgmlTextRequest request;
		request.prompt = prompt;
		return generate(request);
	}

	ofxGgmlTextResponse chat(const std::vector<ofxGgmlTextMessage> & messages) {
		ofxGgmlTextRequest request;
		request.messages = messages;
		return generate(request);
	}

	ofxGgmlTextResponse generate(const ofxGgmlTextRequest & request) {
		if (!backend_) {
			return {false, "", "Text backend has not been configured."};
		}
		return backend_->generate(request);
	}

private:
	std::shared_ptr<ofxGgmlTextBackend> backend_;
};
