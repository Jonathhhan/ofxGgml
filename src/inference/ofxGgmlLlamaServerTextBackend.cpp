#include "ofxGgmlLlamaServerTextBackend.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <sstream>
#include <utility>

#if __has_include("ofMain.h")
#include "ofMain.h"
#define OFXGGML_HAS_OF_HTTP_RUNTIME 1
#endif

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

std::string roleLabel(ofxGgmlTextRole role) {
	switch (role) {
	case ofxGgmlTextRole::System: return "system";
	case ofxGgmlTextRole::User: return "user";
	case ofxGgmlTextRole::Assistant: return "assistant";
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

bool appendDecodedJsonChar(const std::string & value, std::size_t & index, std::string & out) {
	if (index >= value.size()) {
		return false;
	}
	const char c = value[index++];
	if (c != '\\') {
		out.push_back(c);
		return true;
	}
	if (index >= value.size()) {
		return false;
	}
	const char escaped = value[index++];
	switch (escaped) {
	case '"': out.push_back('"'); return true;
	case '\\': out.push_back('\\'); return true;
	case '/': out.push_back('/'); return true;
	case 'b': out.push_back('\b'); return true;
	case 'f': out.push_back('\f'); return true;
	case 'n': out.push_back('\n'); return true;
	case 'r': out.push_back('\r'); return true;
	case 't': out.push_back('\t'); return true;
	case 'u':
		if (index + 4 <= value.size()) {
			index += 4;
		}
		return true;
	default:
		out.push_back(escaped);
		return true;
	}
}

std::string extractJsonStringField(const std::string & json, const std::string & key) {
	const std::string quotedKey = "\"" + key + "\"";
	std::size_t searchFrom = 0;
	while (true) {
		const std::size_t keyPos = json.find(quotedKey, searchFrom);
		if (keyPos == std::string::npos) {
			return {};
		}
		const std::size_t colon = json.find(':', keyPos + quotedKey.size());
		if (colon == std::string::npos) {
			return {};
		}
		std::size_t valueStart = colon + 1;
		while (valueStart < json.size() &&
			std::isspace(static_cast<unsigned char>(json[valueStart]))) {
			++valueStart;
		}
		if (valueStart >= json.size() || json[valueStart] != '"') {
			searchFrom = valueStart;
			continue;
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
}

} // namespace

ofxGgmlLlamaServerTextBackend::ofxGgmlLlamaServerTextBackend(
	std::string serverUrl,
	ofxGgmlTextServerRunner runner,
	std::string displayName)
	: m_serverUrl(std::move(serverUrl))
	, m_runner(runner ? std::move(runner) : ofxGgmlLlamaServerTextBackend::runRequest)
	, m_displayName(std::move(displayName)) {
}

void ofxGgmlLlamaServerTextBackend::setServerUrl(std::string serverUrl) {
	m_serverUrl = std::move(serverUrl);
}

const std::string & ofxGgmlLlamaServerTextBackend::getServerUrl() const {
	return m_serverUrl;
}

void ofxGgmlLlamaServerTextBackend::setRequestRunner(
	ofxGgmlTextServerRunner runner) {
	m_runner = runner ? std::move(runner) : ofxGgmlLlamaServerTextBackend::runRequest;
}

bool ofxGgmlLlamaServerTextBackend::hasRequestRunner() const {
	return static_cast<bool>(m_runner);
}

std::string ofxGgmlLlamaServerTextBackend::backendName() const {
	return m_displayName.empty() ? "llama-server" : m_displayName;
}

ofxGgmlTextResult ofxGgmlLlamaServerTextBackend::generate(
	const ofxGgmlTextRequest & request,
	ofxGgmlTextChunkCallback onChunk) const {
	ofxGgmlTextResult result;
	result.backendName = backendName();

	const std::string prompt = composePrompt(request);
	if (prompt.empty()) {
		result.error = "prompt is empty";
		return result;
	}

	const std::string configuredUrl = request.settings.serverUrl.empty()
		? m_serverUrl
		: request.settings.serverUrl;
	const std::string requestUrl = normalizeServerUrl(configuredUrl);
	if (requestUrl.empty()) {
		result.error = "server URL is empty";
		return result;
	}

	const auto started = std::chrono::steady_clock::now();
	ofxGgmlTextServerRequest serverRequest;
	serverRequest.url = requestUrl;
	serverRequest.body = buildRequestBody(
		request,
		prompt,
		request.settings.serverModel);
	const ofxGgmlTextServerResponse response = m_runner(serverRequest);
	result.elapsedMs = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - started).count();
	result.rawOutput = response.body;
	result.metadata.push_back({ "serverUrl", requestUrl });
	if (!request.settings.serverModel.empty()) {
		result.metadata.push_back({ "serverModel", request.settings.serverModel });
	}

	if (!response.started) {
		result.error = response.error.empty()
			? "llama-server request did not start"
			: response.error;
		result.error += " (" + requestUrl + ")";
		return result;
	}
	if (response.status <= 0) {
		result.error = "llama-server is not reachable at " + requestUrl;
		if (!response.error.empty()) {
			result.error += ": " + response.error;
		}
		return result;
	}
	if (response.status < 200 || response.status >= 300) {
		result.error = "llama-server request failed with HTTP " +
			std::to_string(response.status) + " at " + requestUrl;
		if (!response.error.empty()) {
			result.error += ": " + response.error;
		} else if (!response.body.empty()) {
			result.error += ": " + response.body;
		}
		return result;
	}

	result.text = extractTextFromResponse(response.body);
	if (result.text.empty()) {
		result.error = "llama-server returned empty output";
		return result;
	}
	result.success = true;
	result.finishReason = "stop";
	if (onChunk) {
		onChunk(result.text);
	}
	return result;
}

std::string ofxGgmlLlamaServerTextBackend::normalizeServerUrl(
	const std::string & serverUrl) {
	std::string normalized = trimCopy(serverUrl);
	if (normalized.empty()) {
		normalized = "http://127.0.0.1:8080";
	}
	if (endsWith(normalized, "/v1/chat/completions") ||
		endsWith(normalized, "/chat/completions")) {
		return normalized;
	}
	normalized = stripTrailingSlash(normalized);
	if (endsWith(normalized, "/v1")) {
		return normalized + "/chat/completions";
	}
	return normalized + "/v1/chat/completions";
}

std::string ofxGgmlLlamaServerTextBackend::composePrompt(
	const ofxGgmlTextRequest & request) {
	if (!request.prompt.empty()) {
		return request.prompt;
	}
	std::ostringstream prompt;
	if (!request.systemPrompt.empty()) {
		prompt << request.systemPrompt << "\n";
	}
	for (const auto & message : request.messages) {
		if (!message.content.empty()) {
			prompt << message.content << "\n";
		}
	}
	return trimCopy(prompt.str());
}

std::string ofxGgmlLlamaServerTextBackend::buildRequestBody(
	const ofxGgmlTextRequest & request,
	const std::string & prompt,
	const std::string & serverModel) {
	std::ostringstream body;
	body << "{";
	if (!serverModel.empty()) {
		body << "\"model\":\"" << escapeJson(serverModel) << "\",";
	}
	body << "\"messages\":[";
	bool needsComma = false;
	auto appendMessage = [&](const std::string & role, const std::string & content) {
		if (content.empty()) {
			return;
		}
		if (needsComma) {
			body << ",";
		}
		body << "{\"role\":\"" << role << "\",\"content\":\"" <<
			escapeJson(content) << "\"}";
		needsComma = true;
	};
	appendMessage("system", request.systemPrompt);
	if (!request.messages.empty()) {
		for (const auto & message : request.messages) {
			appendMessage(roleLabel(message.role), message.content);
		}
	} else {
		appendMessage("user", prompt);
	}
	body << "],";
	body << "\"max_tokens\":" << std::max(1, request.settings.maxTokens) << ",";
	body << "\"temperature\":" << std::max(0.0f, request.settings.temperature) << ",";
	body << "\"top_p\":" << std::clamp(request.settings.topP, 0.0f, 1.0f) << ",";
	body << "\"stream\":false";
	if (request.settings.topK > 0) {
		body << ",\"top_k\":" << request.settings.topK;
	}
	if (request.settings.seed >= 0) {
		body << ",\"seed\":" << request.settings.seed;
	}
	if (!request.settings.stopSequences.empty()) {
		body << ",\"stop\":[";
		for (std::size_t i = 0; i < request.settings.stopSequences.size(); ++i) {
			if (i > 0) {
				body << ",";
			}
			body << "\"" << escapeJson(request.settings.stopSequences[i]) << "\"";
		}
		body << "]";
	}
	body << "}";
	return body.str();
}

std::string ofxGgmlLlamaServerTextBackend::extractTextFromResponse(
	const std::string & responseBody) {
	for (const std::string & key : { "content", "text", "response" }) {
		const std::string value = extractJsonStringField(responseBody, key);
		if (!trimCopy(value).empty()) {
			return value;
		}
	}
	return {};
}

ofxGgmlTextServerResponse ofxGgmlLlamaServerTextBackend::runRequest(
	const ofxGgmlTextServerRequest & request) {
	ofxGgmlTextServerResponse result;
	if (request.url.empty()) {
		result.error = "server URL is empty";
		return result;
	}
#if defined(OFXGGML_HAS_OF_HTTP_RUNTIME)
	ofHttpRequest httpRequest(request.url, "llama-server-text");
	httpRequest.method = ofHttpRequest::POST;
	httpRequest.body = request.body;
	httpRequest.contentType = request.contentType;
	httpRequest.headers["Accept"] = "application/json";
	httpRequest.headers["Content-Type"] = request.contentType;
	httpRequest.timeoutSeconds = request.timeoutSeconds;

	ofURLFileLoader loader;
	const ofHttpResponse response = loader.handleRequest(httpRequest);
	result.started = true;
	result.status = response.status;
	result.body = response.data.getText();
	result.error = response.error;
	return result;
#else
	result.error = "llama-server requests require openFrameworks HTTP runtime";
	return result;
#endif
}
