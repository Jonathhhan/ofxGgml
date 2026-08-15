#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ofxGgml {

enum class ChatRole {
	System,
	User,
	Assistant,
	Tool
};

struct ChatMessage {
	ChatRole role = ChatRole::User;
	std::string content;
};

struct ChatOptions {
	std::string model;
	int maxTokens = 512;
	float temperature = 0.7f;
	float topP = 0.95f;
	int seed = -1;
	bool stream = false;
	std::vector<std::string> stopSequences;
};

struct ChatRequest {
	std::string systemPrompt;
	std::vector<ChatMessage> messages;
	ChatOptions options;
};

struct ChatResult {
	bool success = false;
	bool cancelled = false;
	int httpStatus = 0;
	float elapsedMs = 0.0f;
	std::string text;
	std::string error;
	std::string rawResponse;

	explicit operator bool() const {
		return success;
	}
};

using ChatChunkCallback = std::function<bool(const std::string &)>;

} // namespace ofxGgml
