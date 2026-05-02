#include "ofxGgmlVisionInference.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

#ifdef _WIN32
#include "core/ofxGgmlWindowsUtf8.h"
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace {

struct VisionHttpResult {
	int status = -1;
	std::string error;
	std::string body;
};

std::string trimCopy(const std::string & s) {
	size_t start = 0;
	while (start < s.size() &&
		std::isspace(static_cast<unsigned char>(s[start]))) {
		++start;
	}
	size_t end = s.size();
	while (end > start &&
		std::isspace(static_cast<unsigned char>(s[end - 1]))) {
		--end;
	}
	return s.substr(start, end - start);
}

std::string lowerAsciiCopy(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

bool containsSmolVlm2Token(const std::string & text) {
	const std::string lower = lowerAsciiCopy(text);
	return lower.find("smolvlm2") != std::string::npos ||
		lower.find("smol-vlm2") != std::string::npos;
}

std::string collapseRepeatedSentences(const std::string & text) {
	std::string result;
	std::unordered_set<std::string> seen;
	std::string sentence;
	auto flushSentence = [&]() {
		std::string trimmed = trimCopy(sentence);
		sentence.clear();
		if (trimmed.empty()) {
			return;
		}
		const std::string key = lowerAsciiCopy(trimmed);
		if (!seen.insert(key).second) {
			return;
		}
		if (!result.empty()) {
			result.push_back(' ');
		}
		result += trimmed;
	};

	for (char ch : text) {
		sentence.push_back(ch);
		if (ch == '.' || ch == '!' || ch == '?' || ch == '\n') {
			flushSentence();
		}
	}
	flushSentence();
	return result.empty() ? trimCopy(text) : result;
}

bool startsWith(const std::string & text, const std::string & prefix) {
	return text.rfind(prefix, 0) == 0;
}

bool isVisionRecapBoundary(const std::string & lowerSentence) {
	return startsWith(lowerSentence, "to answer the question quickly") ||
		startsWith(lowerSentence, "in summary") ||
		startsWith(lowerSentence, "overall,") ||
		startsWith(lowerSentence, "this professional summary") ||
		startsWith(lowerSentence, "this summary") ||
		lowerSentence.find("provides a clear and concise description") != std::string::npos ||
		lowerSentence.find("making it easily digestible") != std::string::npos;
}

int maxVisionResponseSentences(ofxGgmlVisionTask task) {
	switch (task) {
	case ofxGgmlVisionTask::Describe: return 8;
	case ofxGgmlVisionTask::Ask: return 6;
	case ofxGgmlVisionTask::Ocr: return 0;
	}
	return 8;
}

std::string cleanupVisionResponseText(const std::string & text, ofxGgmlVisionTask task) {
	if (task == ofxGgmlVisionTask::Ocr) {
		return trimCopy(text);
	}

	const std::string collapsed = collapseRepeatedSentences(text);
	std::string result;
	std::string sentence;
	bool wroteAny = false;
	int sentenceCount = 0;
	const int maxSentences = maxVisionResponseSentences(task);
	for (char ch : collapsed) {
		sentence.push_back(ch);
		if (ch != '.' && ch != '!' && ch != '?' && ch != '\n') {
			continue;
		}

		const std::string trimmed = trimCopy(sentence);
		sentence.clear();
		if (trimmed.empty()) {
			continue;
		}
		const std::string lower = lowerAsciiCopy(trimmed);
		const bool startsNegativeInventoryTail =
			lower.find("does not contain any other") != std::string::npos ||
			lower.find("no other objects") != std::string::npos ||
			lower.find("no other scenes") != std::string::npos;
		if (wroteAny && (startsNegativeInventoryTail || isVisionRecapBoundary(lower))) {
			break;
		}
		if (!result.empty()) {
			result.push_back(' ');
		}
		result += trimmed;
		wroteAny = true;
		++sentenceCount;
		if (maxSentences > 0 && sentenceCount >= maxSentences) {
			break;
		}
	}

	const std::string tail = trimCopy(sentence);
	if (!tail.empty() && !isVisionRecapBoundary(lowerAsciiCopy(tail))) {
		if (!result.empty()) {
			result.push_back(' ');
		}
		result += tail;
	}
	return trimCopy(result.empty() ? collapsed : result);
}

bool isSmolVlm2Profile(const ofxGgmlVisionModelProfile & profile) {
	return containsSmolVlm2Token(profile.name) ||
		containsSmolVlm2Token(profile.architecture) ||
		containsSmolVlm2Token(profile.modelRepoHint) ||
		containsSmolVlm2Token(profile.modelFileHint) ||
		containsSmolVlm2Token(std::filesystem::path(profile.modelPath).filename().string());
}

std::string jsonEscape(const std::string & text) {
	std::string escaped;
	escaped.reserve(text.size() + 16);
	for (unsigned char c : text) {
		switch (c) {
		case '\\': escaped += "\\\\"; break;
		case '"': escaped += "\\\""; break;
		case '\b': escaped += "\\b"; break;
		case '\f': escaped += "\\f"; break;
		case '\n': escaped += "\\n"; break;
		case '\r': escaped += "\\r"; break;
		case '\t': escaped += "\\t"; break;
		default:
			if (c < 0x20) {
				static const char * hex = "0123456789ABCDEF";
				escaped += "\\u00";
				escaped.push_back(hex[(c >> 4) & 0xF]);
				escaped.push_back(hex[c & 0xF]);
			} else {
				escaped.push_back(static_cast<char>(c));
			}
		}
	}
	return escaped;
}

std::string base64Encode(const std::string & data) {
	static const char table[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789+/";
	std::string encoded;
	encoded.reserve(((data.size() + 2) / 3) * 4);

	int val = 0;
	int valb = -6;
	for (unsigned char c : data) {
		val = (val << 8) + c;
		valb += 8;
		while (valb >= 0) {
			encoded.push_back(table[(val >> valb) & 0x3F]);
			valb -= 6;
		}
	}
	if (valb > -6) {
		encoded.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
	}
	while (encoded.size() % 4 != 0) {
		encoded.push_back('=');
	}
	return encoded;
}

std::string readFileBinary(const std::string & path) {
	std::ifstream input(path, std::ios::binary);
	if (!input.is_open()) {
		return {};
	}
	std::ostringstream buffer;
	buffer << input.rdbuf();
	return buffer.str();
}

#ifdef _WIN32
std::string winHttpErrorString(DWORD errorCode) {
	return "WinHTTP error " + ofToString(static_cast<unsigned long long>(errorCode));
}

VisionHttpResult winHttpJsonRequest(
	const std::wstring & method,
	const std::string & url,
	const std::string & requestBody,
	int timeoutSeconds) {
	VisionHttpResult result;

	URL_COMPONENTSW components{};
	components.dwStructSize = sizeof(components);
	components.dwSchemeLength = static_cast<DWORD>(-1);
	components.dwHostNameLength = static_cast<DWORD>(-1);
	components.dwUrlPathLength = static_cast<DWORD>(-1);
	components.dwExtraInfoLength = static_cast<DWORD>(-1);

	std::wstring wideUrl = ofxGgmlWideFromUtf8(url);
	if (wideUrl.empty() ||
		!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &components)) {
		result.error = wideUrl.empty() ? "empty URL" : winHttpErrorString(GetLastError());
		return result;
	}

	const bool isHttps = components.nScheme == INTERNET_SCHEME_HTTPS;
	const std::wstring host(
		components.lpszHostName,
		components.dwHostNameLength);
	std::wstring path(
		components.lpszUrlPath ? components.lpszUrlPath : L"/",
		components.dwUrlPathLength > 0 ? components.dwUrlPathLength : 1);
	if (components.dwExtraInfoLength > 0 && components.lpszExtraInfo != nullptr) {
		path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
	}
	if (path.empty()) {
		path = L"/";
	}

	HINTERNET session = WinHttpOpen(
		L"ofxGgmlVision/1.0",
		WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS,
		0);
	if (!session) {
		result.error = winHttpErrorString(GetLastError());
		return result;
	}

	const auto closeHandle = [](HINTERNET handle) {
		if (handle != nullptr) {
			WinHttpCloseHandle(handle);
		}
	};

	HINTERNET connection = nullptr;
	HINTERNET request = nullptr;

	const int timeoutMs = std::max(1, timeoutSeconds) * 1000;
	if (!WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs)) {
		result.error = winHttpErrorString(GetLastError());
		closeHandle(session);
		return result;
	}

	connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
	if (!connection) {
		result.error = winHttpErrorString(GetLastError());
		closeHandle(session);
		return result;
	}

	request = WinHttpOpenRequest(
		connection,
		method.c_str(),
		path.c_str(),
		nullptr,
		WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES,
		isHttps ? WINHTTP_FLAG_SECURE : 0);
	if (!request) {
		result.error = winHttpErrorString(GetLastError());
		closeHandle(connection);
		closeHandle(session);
		return result;
	}

	const std::wstring headers =
		L"Content-Type: application/json\r\nAccept: application/json\r\n";
	LPVOID bodyData = requestBody.empty()
		? WINHTTP_NO_REQUEST_DATA
		: reinterpret_cast<LPVOID>(const_cast<char *>(requestBody.data()));
	const DWORD bodySize = static_cast<DWORD>(requestBody.size());
	if (!WinHttpSendRequest(
			request,
			headers.c_str(),
			static_cast<DWORD>(headers.size()),
			bodyData,
			bodySize,
			bodySize,
			0)) {
		result.error = winHttpErrorString(GetLastError());
		closeHandle(request);
		closeHandle(connection);
		closeHandle(session);
		return result;
	}

	if (!WinHttpReceiveResponse(request, nullptr)) {
		result.error = winHttpErrorString(GetLastError());
		closeHandle(request);
		closeHandle(connection);
		closeHandle(session);
		return result;
	}

	DWORD statusCode = 0;
	DWORD statusCodeSize = sizeof(statusCode);
	if (!WinHttpQueryHeaders(
			request,
			WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX,
			&statusCode,
			&statusCodeSize,
			WINHTTP_NO_HEADER_INDEX)) {
		result.error = winHttpErrorString(GetLastError());
		closeHandle(request);
		closeHandle(connection);
		closeHandle(session);
		return result;
	}
	result.status = static_cast<int>(statusCode);

	for (;;) {
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(request, &available)) {
			result.error = winHttpErrorString(GetLastError());
			result.status = -1;
			break;
		}
		if (available == 0) {
			break;
		}

		std::string chunk(static_cast<size_t>(available), '\0');
		DWORD bytesRead = 0;
		if (!WinHttpReadData(request, chunk.data(), available, &bytesRead)) {
			result.error = winHttpErrorString(GetLastError());
			result.status = -1;
			break;
		}
		chunk.resize(static_cast<size_t>(bytesRead));
		result.body += chunk;
	}

	closeHandle(request);
	closeHandle(connection);
	closeHandle(session);
	return result;
}

VisionHttpResult winHttpPostJson(
	const std::string & url,
	const std::string & requestBody,
	int timeoutSeconds) {
	return winHttpJsonRequest(L"POST", url, requestBody, timeoutSeconds);
}

VisionHttpResult winHttpGetJson(
	const std::string & url,
	int timeoutSeconds) {
	return winHttpJsonRequest(L"GET", url, std::string(), timeoutSeconds);
}
#endif

std::string extractTextFromOpenAiResponse(const std::string & responseJson) {
	if (trimCopy(responseJson).empty()) {
		return {};
	}

#ifdef OFXGGML_HEADLESS_STUBS
	return responseJson;
#else
	try {
		const ofJson parsed = ofJson::parse(responseJson);
		if (parsed.contains("content") && parsed["content"].is_string()) {
			return parsed["content"].get<std::string>();
		}
		if (parsed.contains("output_text") && parsed["output_text"].is_string()) {
			return parsed["output_text"].get<std::string>();
		}
		if (parsed.contains("output") && parsed["output"].is_array()) {
			std::ostringstream joined;
			for (const auto & item : parsed["output"]) {
				if (!item.is_object()) {
					continue;
				}
				if (item.contains("content") && item["content"].is_array()) {
					for (const auto & contentItem : item["content"]) {
						if (!contentItem.is_object()) {
							continue;
						}
						if (contentItem.value("type", std::string()) == "output_text") {
							const std::string text = contentItem.value("text", std::string());
							if (!text.empty()) {
								if (joined.tellp() > 0) {
									joined << "\n";
								}
								joined << text;
							}
						}
					}
				}
			}
			if (joined.tellp() > 0) {
				return joined.str();
			}
		}
		if (!parsed.contains("choices") || !parsed["choices"].is_array() ||
			parsed["choices"].empty()) {
			return {};
		}
		const auto & choice = parsed["choices"][0];
		if (choice.contains("text") && choice["text"].is_string()) {
			return choice["text"].get<std::string>();
		}
		if (!choice.contains("message")) {
			return {};
		}
		const auto & message = choice["message"];
		if (!message.contains("content")) {
			return {};
		}
		const auto & content = message["content"];
		if (content.is_string()) {
			return content.get<std::string>();
		}
		if (content.is_array()) {
			std::ostringstream joined;
			for (const auto & item : content) {
				if (item.is_string()) {
					if (joined.tellp() > 0) {
						joined << "\n";
					}
					joined << item.get<std::string>();
				} else if (item.is_object() &&
					item.value("type", std::string()) == "text") {
					std::string text = item.value("text", std::string());
					if (text.empty() && item.contains("text") && item["text"].is_object()) {
						text = item["text"].value("value", std::string());
					}
					if (!text.empty()) {
						if (joined.tellp() > 0) {
							joined << "\n";
						}
						joined << text;
					}
				}
			}
			return joined.str();
		}
	} catch (...) {
	}
	return {};
#endif
}

} // namespace

std::vector<ofxGgmlVisionModelProfile> ofxGgmlVisionInference::defaultProfiles() {
	return {
		{
			"SmolVLM2 500M Video (llama-server)",
			"SmolVLM2 / mtmd video",
			"ggml-org/SmolVLM2-500M-Video-Instruct-GGUF",
			"SmolVLM2-500M-Video-Instruct-Q8_0.gguf",
			"https://huggingface.co/ggml-org/SmolVLM2-500M-Video-Instruct-GGUF/resolve/main/SmolVLM2-500M-Video-Instruct-Q8_0.gguf",
			"",
			"",
			"http://127.0.0.1:8080",
			true,
			true,
			true
		},
		{
			"LFM2.5-VL (llama-server)",
			"LFM2-VL",
			"LiquidAI/LFM2.5-VL-1.6B-GGUF",
			"LFM2.5-VL-1.6B-Q8_0.gguf",
			"https://huggingface.co/LiquidAI/LFM2.5-VL-1.6B-GGUF/resolve/main/LFM2.5-VL-1.6B-Q8_0.gguf",
			"",
			"",
			"http://127.0.0.1:8080",
			true,
			true,
			false
		},
		{
			"Qwen Vision (llama-server)",
			"Qwen2-VL / Qwen3.5-VL",
			"unsloth/Qwen3.5-4B-GGUF",
			"Qwen3.5-4B-Instruct-Q4_K_M.gguf",
			"",
			"",
			"",
			"http://127.0.0.1:8080",
			true,
			true,
			true
		},
		{
			"GLM OCR (llama-server)",
			"GLM-OCR",
			"ggml-org/GLM-OCR-GGUF",
			"GLM-OCR-Q4_K_M.gguf",
			"",
			"",
			"",
			"http://127.0.0.1:8080",
			true,
			false,
			false
		},
		{
			"Llama 3.2 Vision (EU-restricted / gated)",
			"Llama 3.2 Vision / Llama 3.2 11B Vision Instruct",
			"meta-llama/Llama-3.2-11B-Vision-Instruct",
			"Llama-3.2-11B-Vision-Instruct-Q4_K_M.gguf",
			"",
			"",
			"",
			"http://127.0.0.1:8080",
			true,
			true,
			true
		}
	};
}

const char * ofxGgmlVisionInference::taskLabel(ofxGgmlVisionTask task) {
	switch (task) {
	case ofxGgmlVisionTask::Describe: return "Describe";
	case ofxGgmlVisionTask::Ocr: return "OCR";
	case ofxGgmlVisionTask::Ask: return "Ask";
	}
	return "Describe";
}

std::string ofxGgmlVisionInference::defaultPromptForTask(ofxGgmlVisionTask task) {
	switch (task) {
	case ofxGgmlVisionTask::Describe:
		return "Describe the image clearly and concretely in one concise paragraph. Focus on the main subjects, visible text, layout, state, and notable details. Do not add a recap or second summary.";
	case ofxGgmlVisionTask::Ocr:
		return "Extract all readable text from the image. Preserve useful line breaks, keep obvious headings together, and mark uncertain regions briefly instead of inventing characters.";
	case ofxGgmlVisionTask::Ask:
		return "Answer the user's question about the image directly and concisely. Cite visible evidence from the image when helpful. Do not add a recap or second summary.";
	}
	return {};
}

std::string ofxGgmlVisionInference::defaultSystemPromptForTask(ofxGgmlVisionTask task) {
	switch (task) {
	case ofxGgmlVisionTask::Describe:
		return "You are a precise multimodal assistant. Describe only what is visually supported by the image, avoid speculation, and stop after the complete answer.";
	case ofxGgmlVisionTask::Ocr:
		return "You are an OCR assistant. Extract text faithfully from the image, preserve reading order when possible, and avoid inventing missing characters.";
	case ofxGgmlVisionTask::Ask:
		return "You are a grounded multimodal assistant. Answer using only information visible in the provided image, say when the image is insufficient, and stop after the complete answer.";
	}
	return {};
}

ofxGgmlVisionPreparedPrompt ofxGgmlVisionInference::preparePrompt(
	const ofxGgmlVisionRequest & request) {
	ofxGgmlVisionPreparedPrompt prepared;
	prepared.systemPrompt = trimCopy(request.systemPrompt);
	if (prepared.systemPrompt.empty()) {
		prepared.systemPrompt = defaultSystemPromptForTask(request.task);
	}

	prepared.userPrompt = trimCopy(request.prompt);
	if (prepared.userPrompt.empty()) {
		prepared.userPrompt = defaultPromptForTask(request.task);
	}
	if (!trimCopy(request.responseLanguage).empty() &&
		request.responseLanguage != "Auto") {
		prepared.userPrompt += "\n\nRespond in " + trimCopy(request.responseLanguage) + ".";
	}
	return prepared;
}

std::string ofxGgmlVisionInference::detectMimeType(const std::string & path) {
	std::string ext = std::filesystem::path(path).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (ext == ".png") return "image/png";
	if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
	if (ext == ".webp") return "image/webp";
	if (ext == ".bmp") return "image/bmp";
	if (ext == ".gif") return "image/gif";
	return "application/octet-stream";
}

std::string ofxGgmlVisionInference::encodeImageFileBase64(const std::string & path) {
	const std::string bytes = readFileBinary(path);
	if (bytes.empty()) {
		return {};
	}
	return base64Encode(bytes);
}

std::string ofxGgmlVisionInference::normalizeServerUrl(const std::string & serverUrl) {
	std::string normalized = trimCopy(serverUrl);
	if (normalized.empty()) {
		return "http://127.0.0.1:8080/v1/chat/completions";
	}
	if (normalized.find("/v1/chat/completions") != std::string::npos) {
		return normalized;
	}
	if (!normalized.empty() && normalized.back() == '/') {
		normalized.pop_back();
	}
	if (normalized.find("/v1") == std::string::npos) {
		normalized += "/v1/chat/completions";
	} else {
		normalized += "/chat/completions";
	}
	return normalized;
}

namespace {
std::string visionFallbackServerUrl(const std::string & serverUrl) {
	const std::string normalized = ofxGgmlVisionInference::normalizeServerUrl(serverUrl);
	const std::string v1Suffix = "/v1/chat/completions";
	const std::string compatSuffix = "/chat/completions";
	if (normalized.size() >= v1Suffix.size() &&
		normalized.compare(normalized.size() - v1Suffix.size(), v1Suffix.size(), v1Suffix) == 0) {
		return normalized.substr(0, normalized.size() - v1Suffix.size()) + compatSuffix;
	}
	if (normalized.size() >= compatSuffix.size() &&
		normalized.compare(normalized.size() - compatSuffix.size(), compatSuffix.size(), compatSuffix) == 0) {
		return normalized.substr(0, normalized.size() - compatSuffix.size()) + v1Suffix;
	}
	return std::string();
}

std::string visionServerBaseUrl(const std::string & serverUrl) {
	std::string normalized = ofxGgmlVisionInference::normalizeServerUrl(serverUrl);
	const std::vector<std::string> suffixes = {
		"/v1/chat/completions",
		"/chat/completions"
	};
	for (const auto & suffix : suffixes) {
		if (normalized.size() >= suffix.size() &&
			normalized.compare(normalized.size() - suffix.size(), suffix.size(), suffix) == 0) {
			normalized.erase(normalized.size() - suffix.size());
			break;
		}
	}
	while (!normalized.empty() && normalized.back() == '/') {
		normalized.pop_back();
	}
	return normalized;
}

std::string smolVlm2CompletionUrl(const std::string & serverUrl) {
	return visionServerBaseUrl(serverUrl) + "/completion";
}

std::string smolVlm2PropsUrl(const std::string & serverUrl) {
	return visionServerBaseUrl(serverUrl) + "/props";
}

std::string extractMediaMarkerFromProps(const std::string & responseJson) {
#ifdef OFXGGML_HEADLESS_STUBS
	(void)responseJson;
	return {};
#else
	try {
		const ofJson parsed = ofJson::parse(responseJson);
		if (parsed.contains("media_marker") && parsed["media_marker"].is_string()) {
			return parsed["media_marker"].get<std::string>();
		}
	} catch (...) {
	}
	return {};
#endif
}

std::string smolVlm2ServerPrompt(const ofxGgmlVisionRequest & request) {
	const std::string userPrompt = trimCopy(request.prompt);
	const std::string defaultPrompt =
		trimCopy(ofxGgmlVisionInference::defaultPromptForTask(request.task));
	const bool useProfileDefaultPrompt =
		userPrompt.empty() || userPrompt == defaultPrompt;

	std::string prompt;
	if (useProfileDefaultPrompt) {
		switch (request.task) {
		case ofxGgmlVisionTask::Describe:
			prompt = "Describe the visible image in one concise sentence.";
			break;
		case ofxGgmlVisionTask::Ocr:
			prompt = "Read any visible text in the image. If no text is visible, say so.";
			break;
		case ofxGgmlVisionTask::Ask:
			prompt = defaultPrompt;
			break;
		}
	} else {
		prompt = userPrompt;
	}

	if (!trimCopy(request.responseLanguage).empty() &&
		request.responseLanguage != "Auto") {
		prompt += "\nRespond in " + trimCopy(request.responseLanguage) + ".";
	}
	return prompt;
}

std::string buildSmolVlm2CompletionJson(
	const ofxGgmlVisionRequest & request,
	const std::string & mediaMarker) {
	std::ostringstream prompt;
	size_t encodedImageCount = 0;
	for (const auto & image : request.images) {
		const std::string path = trimCopy(image.path);
		if (path.empty()) {
			continue;
		}
		const std::string encoded = ofxGgmlVisionInference::encodeImageFileBase64(path);
		if (encoded.empty()) {
			continue;
		}
		prompt << mediaMarker << "\n";
		++encodedImageCount;
	}
	prompt << smolVlm2ServerPrompt(request);

	std::ostringstream json;
	json << "{";
	json << "\"prompt\":{";
	json << "\"prompt_string\":\"" << jsonEscape(prompt.str()) << "\",";
	json << "\"multimodal_data\":[";
	size_t imageIndex = 0;
	for (const auto & image : request.images) {
		const std::string path = trimCopy(image.path);
		if (path.empty()) {
			continue;
		}
		const std::string encoded = ofxGgmlVisionInference::encodeImageFileBase64(path);
		if (encoded.empty()) {
			continue;
		}
		if (imageIndex > 0) {
			json << ",";
		}
		json << "\"" << encoded << "\"";
		++imageIndex;
	}
	(void)encodedImageCount;
	json << "]},";
	json << "\"n_predict\":" << std::max(1, request.maxTokens) << ",";
	json << "\"max_tokens\":" << std::max(1, request.maxTokens) << ",";
	json << "\"temperature\":" << request.temperature << ",";
	json << "\"stream\":false";
	json << "}";
	return json.str();
}

std::string cleanupServerCompletionText(std::string text, const std::string & prompt) {
	text = trimCopy(text);
	if (text.empty()) {
		return text;
	}

	const std::string trimmedPrompt = trimCopy(prompt);
	if (!trimmedPrompt.empty() && text.rfind(trimmedPrompt, 0) == 0) {
		text = trimCopy(text.substr(trimmedPrompt.size()));
	}

	const std::vector<std::string> prefixes = {
		"Assistant:",
		"assistant:",
		"ASSISTANT:"
	};
	for (const auto & prefix : prefixes) {
		if (text.rfind(prefix, 0) == 0) {
			text = trimCopy(text.substr(prefix.size()));
			break;
		}
	}
	return text;
}
}

std::string ofxGgmlVisionInference::buildChatCompletionsJson(
	const ofxGgmlVisionModelProfile & profile,
	const ofxGgmlVisionRequest & request) {
	const ofxGgmlVisionPreparedPrompt prepared = preparePrompt(request);
	const bool useSmolVlm2Payload = isSmolVlm2Profile(profile);

	std::ostringstream json;
	json << "{";
	json << "\"messages\":[";
	if (!useSmolVlm2Payload) {
		json << "{"
			<< "\"role\":\"system\","
			<< "\"content\":\"" << jsonEscape(prepared.systemPrompt) << "\""
			<< "},";
	}
	json << "{"
		<< "\"role\":\"user\","
		<< "\"content\":[";

	bool firstContentPart = true;
	auto appendContentComma = [&]() {
		if (!firstContentPart) {
			json << ",";
		}
		firstContentPart = false;
	};

	auto appendTextPart = [&](const std::string & text) {
		const std::string trimmedText = trimCopy(text);
		if (trimmedText.empty()) {
			return;
		}
		appendContentComma();
		json << "{"
			<< "\"type\":\"text\","
			<< "\"text\":\"" << jsonEscape(trimmedText) << "\""
			<< "}";
	};

	auto appendImagePart = [&](const ofxGgmlVisionImageInput & image) {
		const std::string path = trimCopy(image.path);
		if (path.empty()) {
			return;
		}
		const std::string mimeType = trimCopy(image.mimeType).empty()
			? detectMimeType(path)
			: trimCopy(image.mimeType);
		const std::string encoded = encodeImageFileBase64(path);
		if (encoded.empty()) {
			return;
		}
		appendContentComma();
		json << "{"
			<< "\"type\":\"image_url\","
			<< "\"image_url\":{"
			<< "\"url\":\"data:" << jsonEscape(mimeType) << ";base64," << encoded << "\"";
		if (request.task == ofxGgmlVisionTask::Ocr) {
			json << ",\"detail\":\"high\"";
		}
		json << "}"
			<< "}";
	};

	if (!useSmolVlm2Payload) {
		appendTextPart(prepared.userPrompt);
	}

	for (size_t imageIndex = 0; imageIndex < request.images.size(); ++imageIndex) {
		const auto & image = request.images[imageIndex];
		const std::string imageLabel = trimCopy(image.label);
		if (!useSmolVlm2Payload && !imageLabel.empty()) {
			appendTextPart("Image " + std::to_string(imageIndex + 1) + ": " + imageLabel);
		}
		appendImagePart(image);
	}

	if (useSmolVlm2Payload) {
		std::ostringstream prompt;
		if (!trimCopy(prepared.systemPrompt).empty()) {
			prompt << "Instructions:\n" << trimCopy(prepared.systemPrompt) << "\n\n";
		}
		prompt << "Request:\n" << prepared.userPrompt;
		bool wroteImageHeader = false;
		for (size_t imageIndex = 0; imageIndex < request.images.size(); ++imageIndex) {
			const std::string imageLabel = trimCopy(request.images[imageIndex].label);
			if (imageLabel.empty()) {
				continue;
			}
			if (!wroteImageHeader) {
				prompt << "\n\nImages:";
				wroteImageHeader = true;
			}
			prompt << "\n- Image " << (imageIndex + 1) << ": " << imageLabel;
		}
		appendTextPart(prompt.str());
	}

	json << "]"
		<< "}";
	json << "],";
	json << "\"max_tokens\":" << std::max(1, request.maxTokens) << ",";
	json << "\"temperature\":" << request.temperature;
	if (!trimCopy(profile.modelPath).empty()) {
		const std::string modelId = std::filesystem::path(profile.modelPath).filename().string();
		json << ",\"model\":\"" << jsonEscape(modelId.empty() ? profile.modelPath : modelId) << "\"";
	} else if (!trimCopy(profile.modelRepoHint).empty()) {
		json << ",\"model\":\"" << jsonEscape(profile.modelRepoHint) << "\"";
	}
	json << "}";
	return json.str();
}

ofxGgmlVisionResult ofxGgmlVisionInference::runServerRequest(
	const ofxGgmlVisionModelProfile & profile,
	const ofxGgmlVisionRequest & request) const {
	ofxGgmlVisionResult result;
	if (request.images.empty()) {
		result.error = "no image was provided";
		return result;
	}
	for (const auto & image : request.images) {
		if (trimCopy(image.path).empty() ||
			!std::filesystem::exists(std::filesystem::path(image.path))) {
			result.error = "image file not found: " + image.path;
			return result;
		}
	}

	const auto t0 = std::chrono::steady_clock::now();
	const std::string configuredServerUrl =
		trimCopy(profile.serverUrl).empty() ? "http://127.0.0.1:8080" : profile.serverUrl;
	const bool useSmolVlm2Completion = isSmolVlm2Profile(profile);
	result.usedServerUrl = useSmolVlm2Completion
		? smolVlm2CompletionUrl(configuredServerUrl)
		: normalizeServerUrl(configuredServerUrl);

#ifdef OFXGGML_HEADLESS_STUBS
	result.error = "vision server requests require openFrameworks runtime";
	return result;
#else
	try {
		if (useSmolVlm2Completion) {
			const std::string propsUrl = smolVlm2PropsUrl(configuredServerUrl);
			VisionHttpResult propsResponse;
#ifdef _WIN32
			propsResponse = winHttpGetJson(propsUrl, 30);
#else
			ofHttpResponse propsHttp = ofLoadURL(propsUrl);
			propsResponse.status = propsHttp.status;
			propsResponse.error = propsHttp.error;
			propsResponse.body = propsHttp.data.getText();
#endif
			if (propsResponse.status < 200 || propsResponse.status >= 300) {
				std::string detail = trimCopy(propsResponse.body);
				if (detail.empty()) {
					detail = trimCopy(propsResponse.error);
				}
				result.error = "vision server props request failed at " + propsUrl + ": " + detail;
				return result;
			}
			const std::string mediaMarker = extractMediaMarkerFromProps(propsResponse.body);
			if (mediaMarker.empty()) {
				result.error = "vision server did not report a media_marker from " + propsUrl;
				result.responseJson = propsResponse.body;
				return result;
			}
			result.requestJson = buildSmolVlm2CompletionJson(request, mediaMarker);
		} else {
			result.requestJson = buildChatCompletionsJson(profile, request);
		}

		auto performRequest = [&](const std::string & url) {
			VisionHttpResult httpResponse;
#ifdef _WIN32
			httpResponse = winHttpPostJson(url, result.requestJson, 180);
#else
			ofHttpRequest httpRequest(url, "vision-request", false, true, false);
			httpRequest.method = ofHttpRequest::POST;
			httpRequest.body = result.requestJson;
			httpRequest.contentType = "application/json";
			httpRequest.headers["Accept"] = "application/json";
			httpRequest.timeoutSeconds = 180;

			ofURLFileLoader loader;
			ofHttpResponse response = loader.handleRequest(httpRequest);
			httpResponse.status = response.status;
			httpResponse.error = response.error;
			httpResponse.body = response.data.getText();
#endif
			return httpResponse;
		};

		VisionHttpResult httpResponse = performRequest(result.usedServerUrl);
		result.responseJson = httpResponse.body;

		if (!useSmolVlm2Completion && httpResponse.status == 404) {
			const std::string fallbackUrl = visionFallbackServerUrl(result.usedServerUrl);
			if (!fallbackUrl.empty() && fallbackUrl != result.usedServerUrl) {
				ofLogNotice("ofxGgmlVisionInference")
					<< "Vision endpoint returned 404 at " << result.usedServerUrl
					<< ", retrying " << fallbackUrl;
				httpResponse = performRequest(fallbackUrl);
				result.usedServerUrl = fallbackUrl;
				result.responseJson = httpResponse.body;
			}
		}

		if (httpResponse.status < 200 || httpResponse.status >= 300) {
			std::string detail = trimCopy(result.responseJson);
			if (detail.empty()) {
				detail = trimCopy(httpResponse.error);
			}
			if (httpResponse.status < 0) {
				if (detail.empty()) {
					detail = "Could not connect to server";
				}
				result.error =
					"vision request could not reach " + result.usedServerUrl + ": " +
					detail +
					". Vision mode requires a running multimodal llama-server.";
				return result;
			}
			result.error = "vision request failed with HTTP " +
				ofToString(httpResponse.status) + " from " + result.usedServerUrl +
				": " + detail;
			return result;
		}

		result.text = trimCopy(extractTextFromOpenAiResponse(result.responseJson));
		if (useSmolVlm2Completion) {
			result.text = cleanupServerCompletionText(
				result.text,
				smolVlm2ServerPrompt(request));
		}
		result.text = cleanupVisionResponseText(result.text, request.task);
		if (result.text.empty()) {
			result.error = "vision server returned no assistant text";
			return result;
		}
		result.success = true;
		result.elapsedMs = std::chrono::duration<float, std::milli>(
			std::chrono::steady_clock::now() - t0).count();
		return result;
	} catch (const std::exception & e) {
		result.error = std::string("vision request failed: ") + e.what();
		return result;
	}
#endif
}
