#pragma once

#include "ofxGgmlChatTypes.h"

#include <functional>
#include <string>
#include <vector>

namespace ofxGgml {

class Server;
class ToolLoop;

class ChatSession {
public:
	explicit ChatSession(Server & server);

	void setSystemPrompt(std::string systemPrompt);
	const std::string & getSystemPrompt() const;

	void setOptions(ChatOptions options);
	const ChatOptions & getOptions() const;

	const std::vector<ChatMessage> & getMessages() const;
	void clear();

	ChatResult send(
		const std::string & message,
		ChatChunkCallback onChunk = nullptr);

private:
	friend class ToolLoop;

	ChatResult complete(
		std::vector<ChatMessage> newMessages,
		const std::vector<ToolDefinition> & tools,
		ChatChunkCallback onChunk = nullptr);

	std::reference_wrapper<Server> server;
	std::string systemPrompt;
	ChatOptions options;
	std::vector<ChatMessage> messages;
};

} // namespace ofxGgml
