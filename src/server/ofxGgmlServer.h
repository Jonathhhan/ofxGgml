#pragma once

#include "../chat/ofxGgmlChatTypes.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ofxGgml {

enum class HttpMethod {
	Get,
	Post
};

struct HttpRequest {
	HttpMethod method = HttpMethod::Get;
	std::string url;
	std::string body;
	std::string contentType = "application/json";
	std::vector<std::pair<std::string, std::string>> headers;
	int timeoutSeconds = 180;
	bool stream = false;
	ChatChunkCallback onChunk;
	std::function<bool()> shouldCancel;
};

struct HttpResponse {
	bool started = false;
	bool cancelled = false;
	int status = 0;
	std::string body;
	std::string streamedText;
	std::string error;
};

using HttpTransport = std::function<HttpResponse(const HttpRequest &)>;

struct ServerStatus {
	bool reachable = false;
	int httpStatus = 0;
	std::vector<std::string> models;
	std::string error;

	explicit operator bool() const {
		return reachable;
	}
};

class Server {
public:
	explicit Server(
		std::string baseUrl = "http://127.0.0.1:8080",
		HttpTransport transport = {});

	void setBaseUrl(std::string baseUrl);
	const std::string & getBaseUrl() const;

	void setTransport(HttpTransport transport);
	bool hasTransport() const;
	void setBearerToken(std::string token);
	bool hasBearerToken() const;

	ServerStatus inspect() const;
	ChatResult chat(
		const ChatRequest & request,
		ChatChunkCallback onChunk = nullptr) const;

	static std::string normalizeBaseUrl(const std::string & baseUrl);
	static std::string modelsUrl(const std::string & baseUrl);
	static std::string chatCompletionsUrl(const std::string & baseUrl);
	static std::string buildChatBody(const ChatRequest & request);
	static std::string extractChatText(const std::string & responseBody);
	static std::vector<ToolCall> extractToolCalls(const std::string & responseBody);
	static std::vector<std::string> extractModelIds(const std::string & responseBody);
	static HttpResponse runHttpRequest(const HttpRequest & request);

private:
	std::string baseUrl;
	std::string bearerToken;
	HttpTransport transport;
};

} // namespace ofxGgml
