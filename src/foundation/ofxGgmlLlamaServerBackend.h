#pragma once

#include "ofJson.h"
#include "ofxGgmlHttpTransport.h"
#include "ofxGgmlTextClient.h"

#include <memory>
#include <string>
#include <utility>

struct ofxGgmlLlamaServerSettings {
	std::string baseUrl = "http://127.0.0.1:8080";
	std::string apiKey;
	std::string model;
	int timeoutMs = 60000;
};

class ofxGgmlLlamaServerBackend final : public ofxGgmlTextBackend {
public:
	explicit ofxGgmlLlamaServerBackend(
		ofxGgmlLlamaServerSettings settings,
		std::shared_ptr<ofxGgmlHttpTransport> transport = std::make_shared<ofxGgmlUnavailableHttpTransport>())
		: settings_(std::move(settings))
		, transport_(std::move(transport)) {}

	bool setup() override {
		return !settings_.baseUrl.empty() && static_cast<bool>(transport_);
	}

	ofxGgmlTextResponse generate(const ofxGgmlTextRequest & request) override {
		if (!transport_) {
			return {false, "", "No HTTP transport has been configured."};
		}

		const auto httpResponse = transport_->send(buildHttpRequest(request));
		if (!httpResponse.ok()) {
			return {false, "", httpResponse.error.empty() ? "llama-server request failed." : httpResponse.error};
		}

		return parseTextResponse(httpResponse.body);
	}

	const ofxGgmlLlamaServerSettings & getSettings() const {
		return settings_;
	}

	ofxGgmlHttpRequest buildHttpRequest(const ofxGgmlTextRequest & request) const {
		ofxGgmlHttpRequest http;
		http.method = "POST";
		http.url = trimTrailingSlash(settings_.baseUrl) + "/v1/chat/completions";
		http.timeoutMs = settings_.timeoutMs;
		http.headers["Content-Type"] = "application/json";
		if (!settings_.apiKey.empty()) {
			http.headers["Authorization"] = "Bearer " + settings_.apiKey;
		}
		http.body = buildRequestBody(request);
		return http;
	}

	std::string buildRequestBody(const ofxGgmlTextRequest & request) const {
		ofJson messages = ofJson::array();
		if (!request.messages.empty()) {
			for (const auto & message : request.messages) {
				messages.push_back(ofJson{
					{"role", message.role},
					{"content", message.content}
				});
			}
		} else {
			messages.push_back(ofJson{
				{"role", "user"},
				{"content", request.prompt}
			});
		}

		ofJson body = {
			{"messages", messages},
			{"max_tokens", request.maxTokens},
			{"temperature", request.temperature}
		};
		if (!settings_.model.empty()) {
			body["model"] = settings_.model;
		}
		return body.dump();
	}

	static ofxGgmlTextResponse parseTextResponse(const std::string & responseBody) {
		const auto parsed = ofJson::parse(responseBody, nullptr, false);
		if (parsed.is_discarded()) {
			return {false, "", "Could not parse llama-server response JSON."};
		}

		if (parsed.contains("error")) {
			const auto & error = parsed["error"];
			if (error.is_object()) {
				return {false, "", error.value("message", std::string("llama-server returned an error."))};
			}
			if (error.is_string()) {
				return {false, "", error.get<std::string>()};
			}
			return {false, "", "llama-server returned an error."};
		}

		if (parsed.contains("choices") && parsed["choices"].is_array()) {
			const auto & choice = parsed["choices"][0];
			if (choice.contains("message") && choice["message"].is_object()) {
				const auto & message = choice["message"];
				if (message.contains("content") && message["content"].is_string()) {
					return {true, message["content"].get<std::string>(), ""};
				}
			}
			if (choice.contains("text") && choice["text"].is_string()) {
				return {true, choice["text"].get<std::string>(), ""};
			}
			if (choice.contains("content") && choice["content"].is_string()) {
				return {true, choice["content"].get<std::string>(), ""};
			}
		}

		if (parsed.contains("content") && parsed["content"].is_string()) {
			return {true, parsed["content"].get<std::string>(), ""};
		}
		if (parsed.contains("response") && parsed["response"].is_string()) {
			return {true, parsed["response"].get<std::string>(), ""};
		}

		return {false, "", "llama-server response did not contain generated text."};
	}

private:
	static std::string trimTrailingSlash(std::string value) {
		while (!value.empty() && value.back() == '/') {
			value.pop_back();
		}
		return value;
	}

	ofxGgmlLlamaServerSettings settings_;
	std::shared_ptr<ofxGgmlHttpTransport> transport_;
};
