#include "ofxGgmlChatSession.h"

#include "../server/ofxGgmlServer.h"

#include <utility>

namespace ofxGgml {

ChatSession::ChatSession(Server & server)
	: server(server) {
}

void ChatSession::setSystemPrompt(std::string systemPrompt) {
	this->systemPrompt = std::move(systemPrompt);
}

const std::string & ChatSession::getSystemPrompt() const {
	return systemPrompt;
}

void ChatSession::setOptions(ChatOptions options) {
	this->options = std::move(options);
}

const ChatOptions & ChatSession::getOptions() const {
	return options;
}

const std::vector<ChatMessage> & ChatSession::getMessages() const {
	return messages;
}

void ChatSession::clear() {
	messages.clear();
}

ChatResult ChatSession::send(
	const std::string & message,
	ChatChunkCallback onChunk) {
	if (message.empty()) {
		ChatResult result;
		result.error = "message is empty";
		return result;
	}

	messages.push_back({ ChatRole::User, message });
	ChatRequest request;
	request.systemPrompt = systemPrompt;
	request.messages = messages;
	request.options = options;

	ChatResult result = server.get().chat(request, std::move(onChunk));
	if (result) {
		messages.push_back({ ChatRole::Assistant, result.text });
	} else {
		messages.pop_back();
	}
	return result;
}

} // namespace ofxGgml
