#include "ofxGgmlServer.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <sstream>
#include <utility>

#if __has_include("ofMain.h")
#include "ofMain.h"
#define OFXGGML_HAS_OF_HTTP_RUNTIME 1
#endif

#if defined(OFXGGML_HAS_OF_HTTP_RUNTIME) && __has_include("curl/curl.h")
#if defined(_WIN32) && !defined(CURL_STATICLIB)
#define CURL_STATICLIB
#endif
#include "curl/curl.h"
#define OFXGGML_HAS_CURL_HTTP_RUNTIME 1
#endif

namespace ofxGgml {
namespace {

std::string trimCopy(const std::string & value) {
	std::size_t first = 0;
	while (first < value.size() &&
		std::isspace(static_cast<unsigned char>(value[first]))) {
		++first;
	}
	std::size_t last = value.size();
	while (last > first &&
		std::isspace(static_cast<unsigned char>(value[last - 1]))) {
		--last;
	}
	return value.substr(first, last - first);
}

bool endsWith(const std::string & value, const std::string & suffix) {
	return value.size() >= suffix.size() &&
		value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string stripTrailingSlash(std::string value) {
	while (!value.empty() && value.back() == '/') {
		value.pop_back();
	}
	return value;
}

const char * roleLabel(ChatRole role) {
	switch (role) {
	case ChatRole::System: return "system";
	case ChatRole::User: return "user";
	case ChatRole::Assistant: return "assistant";
	case ChatRole::Tool: return "tool";
	}
	return "user";
}

std::string escapeJson(const std::string & value) {
	std::ostringstream escaped;
	for (const unsigned char c : value) {
		switch (c) {
		case '\\': escaped << "\\\\"; break;
		case '"': escaped << "\\\""; break;
		case '\b': escaped << "\\b"; break;
		case '\f': escaped << "\\f"; break;
		case '\n': escaped << "\\n"; break;
		case '\r': escaped << "\\r"; break;
		case '\t': escaped << "\\t"; break;
		default:
			if (c < 0x20) {
				const char * hex = "0123456789abcdef";
				escaped << "\\u00" << hex[(c >> 4) & 0x0f] << hex[c & 0x0f];
			} else {
				escaped << static_cast<char>(c);
			}
			break;
		}
	}
	return escaped.str();
}

bool appendDecodedJsonChar(
	const std::string & value,
	std::size_t & index,
	std::string & output) {
	if (index >= value.size()) {
		return false;
	}
	const char c = value[index++];
	if (c != '\\') {
		output.push_back(c);
		return true;
	}
	if (index >= value.size()) {
		return false;
	}
	const char escaped = value[index++];
	switch (escaped) {
	case '"': output.push_back('"'); return true;
	case '\\': output.push_back('\\'); return true;
	case '/': output.push_back('/'); return true;
	case 'b': output.push_back('\b'); return true;
	case 'f': output.push_back('\f'); return true;
	case 'n': output.push_back('\n'); return true;
	case 'r': output.push_back('\r'); return true;
	case 't': output.push_back('\t'); return true;
	case 'u':
		if (index + 4 > value.size()) {
			return false;
		}
		index += 4;
		return true;
	default:
		return false;
	}
}

std::string extractJsonStringAt(
	const std::string & json,
	std::size_t valueStart) {
	while (valueStart < json.size() &&
		std::isspace(static_cast<unsigned char>(json[valueStart]))) {
		++valueStart;
	}
	if (valueStart >= json.size() || json[valueStart] != '"') {
		return {};
	}
	++valueStart;
	std::string decoded;
	while (valueStart < json.size()) {
		if (json[valueStart] == '"') {
			return decoded;
		}
		if (!appendDecodedJsonChar(json, valueStart, decoded)) {
			return {};
		}
	}
	return {};
}

std::string extractJsonStringField(
	const std::string & json,
	const std::string & key,
	std::size_t searchFrom = 0) {
	const std::string quotedKey = "\"" + key + "\"";
	const std::size_t keyPosition = json.find(quotedKey, searchFrom);
	if (keyPosition == std::string::npos) {
		return {};
	}
	const std::size_t colon = json.find(':', keyPosition + quotedKey.size());
	if (colon == std::string::npos) {
		return {};
	}
	return extractJsonStringAt(json, colon + 1);
}

#if defined(OFXGGML_HAS_CURL_HTTP_RUNTIME)
bool processServerSentEventLine(
	const std::string & line,
	HttpResponse & response,
	const ChatChunkCallback & onChunk) {
	const std::string prefix = "data:";
	if (line.compare(0, prefix.size(), prefix) != 0) {
		return true;
	}
	const std::string payload = trimCopy(line.substr(prefix.size()));
	if (payload.empty() || payload == "[DONE]") {
		return true;
	}
	response.body += payload + "\n";
	const std::string text = Server::extractChatText(payload);
	if (text.empty()) {
		return true;
	}
	response.streamedText += text;
	if (onChunk && !onChunk(text)) {
		response.cancelled = true;
		response.error = "request cancelled";
		return false;
	}
	return true;
}

struct CurlState {
	HttpResponse * response = nullptr;
	ChatChunkCallback onChunk;
	std::function<bool()> shouldCancel;
	std::string pending;
};

bool shouldCancel(CurlState & state) {
	if (!state.shouldCancel || !state.shouldCancel()) {
		return false;
	}
	state.response->cancelled = true;
	state.response->error = "request cancelled";
	return true;
}

int curlProgress(void * userData, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
	auto * state = static_cast<CurlState *>(userData);
	return state && shouldCancel(*state) ? 1 : 0;
}

std::size_t curlWrite(
	char * data,
	std::size_t size,
	std::size_t count,
	void * userData) {
	const std::size_t bytes = size * count;
	auto * state = static_cast<CurlState *>(userData);
	if (!state || !state->response || !data || shouldCancel(*state)) {
		return 0;
	}
	state->response->started = true;
	state->pending.append(data, bytes);
	while (true) {
		const std::size_t newline = state->pending.find('\n');
		if (newline == std::string::npos) {
			break;
		}
		std::string line = state->pending.substr(0, newline);
		state->pending.erase(0, newline + 1);
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (!processServerSentEventLine(line, *state->response, state->onChunk)) {
			return 0;
		}
	}
	return bytes;
}

HttpResponse runStreamingRequest(const HttpRequest & request) {
	HttpResponse result;
	CURL * curl = curl_easy_init();
	if (!curl) {
		result.error = "curl initialization failed";
		return result;
	}

	struct curl_slist * headers = nullptr;
	headers = curl_slist_append(headers, "Accept: text/event-stream");
	const std::string contentType = "Content-Type: " + request.contentType;
	headers = curl_slist_append(headers, contentType.c_str());

	CurlState state;
	state.response = &result;
	state.onChunk = request.onChunk;
	state.shouldCancel = request.shouldCancel;

	curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(request.timeoutSeconds));
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "ofxGgml/v2");
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);

	const CURLcode code = curl_easy_perform(curl);
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	result.status = static_cast<int>(status);
	result.started = true;
	if (code != CURLE_OK && !result.cancelled) {
		result.error = curl_easy_strerror(code);
	}
	if (!state.pending.empty() && !result.cancelled) {
		processServerSentEventLine(state.pending, result, request.onChunk);
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return result;
}
#endif

} // namespace

Server::Server(std::string baseUrl, HttpTransport transport)
	: baseUrl(normalizeBaseUrl(baseUrl))
	, transport(transport ? std::move(transport) : Server::runHttpRequest) {
}

void Server::setBaseUrl(std::string baseUrl) {
	this->baseUrl = normalizeBaseUrl(baseUrl);
}

const std::string & Server::getBaseUrl() const {
	return baseUrl;
}

void Server::setTransport(HttpTransport transport) {
	this->transport = transport ? std::move(transport) : Server::runHttpRequest;
}

bool Server::hasTransport() const {
	return static_cast<bool>(transport);
}

ServerStatus Server::inspect() const {
	ServerStatus status;
	HttpRequest request;
	request.method = HttpMethod::Get;
	request.url = modelsUrl(baseUrl);
	request.timeoutSeconds = 10;
	const HttpResponse response = transport(request);
	status.httpStatus = response.status;
	if (!response.started) {
		status.error = response.error.empty() ? "request did not start" : response.error;
		return status;
	}
	if (response.status < 200 || response.status >= 300) {
		status.error = "model endpoint returned HTTP " + std::to_string(response.status);
		if (!response.error.empty()) {
			status.error += ": " + response.error;
		}
		return status;
	}
	status.reachable = true;
	status.models = extractModelIds(response.body);
	return status;
}

ChatResult Server::chat(
	const ChatRequest & request,
	ChatChunkCallback onChunk) const {
	ChatResult result;
	if (request.messages.empty()) {
		result.error = "chat request has no messages";
		return result;
	}

	HttpRequest httpRequest;
	httpRequest.method = HttpMethod::Post;
	httpRequest.url = chatCompletionsUrl(baseUrl);
	httpRequest.body = buildChatBody(request);
	httpRequest.stream = request.options.stream;
	httpRequest.onChunk = onChunk;

	const auto startedAt = std::chrono::steady_clock::now();
	const HttpResponse response = transport(httpRequest);
	result.elapsedMs = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - startedAt).count();
	result.httpStatus = response.status;
	result.rawResponse = response.body;
	result.cancelled = response.cancelled;

	if (!response.started) {
		result.error = response.error.empty() ? "request did not start" : response.error;
		return result;
	}
	if (response.cancelled) {
		result.text = response.streamedText;
		result.error = response.error.empty() ? "request cancelled" : response.error;
		return result;
	}
	if (response.status <= 0) {
		result.error = "server is not reachable at " + httpRequest.url;
		if (!response.error.empty()) {
			result.error += ": " + response.error;
		}
		return result;
	}
	if (response.status < 200 || response.status >= 300) {
		result.error = "chat endpoint returned HTTP " + std::to_string(response.status);
		if (!response.error.empty()) {
			result.error += ": " + response.error;
		}
		return result;
	}

	result.text = request.options.stream
		? response.streamedText
		: extractChatText(response.body);
	if (result.text.empty()) {
		result.error = "chat endpoint returned no text";
		return result;
	}
	result.success = true;
	if (onChunk && !request.options.stream) {
		onChunk(result.text);
	}
	return result;
}

std::string Server::normalizeBaseUrl(const std::string & baseUrl) {
	std::string normalized = stripTrailingSlash(trimCopy(baseUrl));
	if (normalized.empty()) {
		normalized = "http://127.0.0.1:8080";
	}
	for (const char * suffixValue : { "/v1/chat/completions", "/chat/completions", "/v1/models", "/models" }) {
		const std::string suffix(suffixValue);
		if (endsWith(normalized, suffix)) {
			normalized.erase(normalized.size() - suffix.size());
			break;
		}
	}
	if (endsWith(normalized, "/v1")) {
		normalized.erase(normalized.size() - 3);
	}
	return stripTrailingSlash(normalized);
}

std::string Server::modelsUrl(const std::string & baseUrl) {
	return normalizeBaseUrl(baseUrl) + "/v1/models";
}

std::string Server::chatCompletionsUrl(const std::string & baseUrl) {
	return normalizeBaseUrl(baseUrl) + "/v1/chat/completions";
}

std::string Server::buildChatBody(const ChatRequest & request) {
	std::ostringstream body;
	body << "{";
	if (!request.options.model.empty()) {
		body << "\"model\":\"" << escapeJson(request.options.model) << "\",";
	}
	body << "\"messages\":[";
	bool needsComma = false;
	auto appendMessage = [&](ChatRole role, const std::string & content) {
		if (content.empty()) {
			return;
		}
		if (needsComma) {
			body << ",";
		}
		body << "{\"role\":\"" << roleLabel(role) << "\",\"content\":\""
			 << escapeJson(content) << "\"}";
		needsComma = true;
	};
	appendMessage(ChatRole::System, request.systemPrompt);
	for (const ChatMessage & message : request.messages) {
		appendMessage(message.role, message.content);
	}
	body << "],";
	body << "\"max_tokens\":" << std::max(1, request.options.maxTokens) << ",";
	body << "\"temperature\":" << std::max(0.0f, request.options.temperature) << ",";
	body << "\"top_p\":" << std::clamp(request.options.topP, 0.0f, 1.0f) << ",";
	body << "\"stream\":" << (request.options.stream ? "true" : "false");
	if (request.options.seed >= 0) {
		body << ",\"seed\":" << request.options.seed;
	}
	if (!request.options.stopSequences.empty()) {
		body << ",\"stop\":[";
		for (std::size_t i = 0; i < request.options.stopSequences.size(); ++i) {
			if (i > 0) {
				body << ",";
			}
			body << "\"" << escapeJson(request.options.stopSequences[i]) << "\"";
		}
		body << "]";
	}
	body << "}";
	return body.str();
}

std::string Server::extractChatText(const std::string & responseBody) {
	for (const char * keyValue : { "content", "text", "response" }) {
		const std::string key(keyValue);
		const std::string value = extractJsonStringField(responseBody, key);
		if (!trimCopy(value).empty()) {
			return value;
		}
	}
	return {};
}

std::vector<std::string> Server::extractModelIds(const std::string & responseBody) {
	std::vector<std::string> models;
	const std::string key = "\"id\"";
	std::size_t searchFrom = 0;
	while (true) {
		const std::size_t keyPosition = responseBody.find(key, searchFrom);
		if (keyPosition == std::string::npos) {
			break;
		}
		const std::size_t colon = responseBody.find(':', keyPosition + key.size());
		if (colon == std::string::npos) {
			break;
		}
		const std::string id = extractJsonStringAt(responseBody, colon + 1);
		if (!id.empty()) {
			models.push_back(id);
		}
		searchFrom = colon + 1;
	}
	return models;
}

HttpResponse Server::runHttpRequest(const HttpRequest & request) {
	HttpResponse result;
	if (request.url.empty()) {
		result.error = "request URL is empty";
		return result;
	}
#if defined(OFXGGML_HAS_OF_HTTP_RUNTIME)
#if defined(OFXGGML_HAS_CURL_HTTP_RUNTIME)
	if (request.stream) {
		return runStreamingRequest(request);
	}
#else
	if (request.stream) {
		result.error = "streaming requests require curl";
		return result;
	}
#endif
	ofHttpRequest ofRequest(request.url, "ofxGgml-server");
	ofRequest.method = request.method == HttpMethod::Post
		? ofHttpRequest::POST
		: ofHttpRequest::GET;
	ofRequest.body = request.body;
	ofRequest.contentType = request.contentType;
	ofRequest.headers["Accept"] = "application/json";
	ofRequest.headers["Content-Type"] = request.contentType;
	ofRequest.timeoutSeconds = request.timeoutSeconds;

	ofURLFileLoader loader;
	const ofHttpResponse response = loader.handleRequest(ofRequest);
	result.started = true;
	result.status = response.status;
	result.body = response.data.getText();
	result.error = response.error;
	return result;
#else
	result.error = "HTTP requests require the openFrameworks runtime";
	return result;
#endif
}

} // namespace ofxGgml
