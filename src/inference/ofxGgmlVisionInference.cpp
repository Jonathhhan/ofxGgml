#include "ofxGgmlVisionInference.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

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

std::string extractTextFromOpenAiResponse(const std::string & responseJson) {
	if (trimCopy(responseJson).empty()) {
		return {};
	}

#ifdef OFXGGML_HEADLESS_STUBS
	return responseJson;
#else
	try {
		const ofJson parsed = ofJson::parse(responseJson);
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
			"LFM2.5-VL (llama-server)",
			"LFM2-VL",
			"LiquidAI/LFM2.5-VL-1.6B-GGUF",
			"LFM2.5-VL-1.6B-Q4_K_M.gguf",
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
		return "Describe the image clearly and concretely. Focus on the main subjects, visible text, layout, state, and notable details. Use short sections when that improves readability.";
	case ofxGgmlVisionTask::Ocr:
		return "Extract all readable text from the image. Preserve useful line breaks, keep obvious headings together, and mark uncertain regions briefly instead of inventing characters.";
	case ofxGgmlVisionTask::Ask:
		return "Answer the user's question about the image. Cite visible evidence from the image when helpful.";
	}
	return {};
}

std::string ofxGgmlVisionInference::defaultSystemPromptForTask(ofxGgmlVisionTask task) {
	switch (task) {
	case ofxGgmlVisionTask::Describe:
		return "You are a precise multimodal assistant. Describe only what is visually supported by the image and avoid speculation.";
	case ofxGgmlVisionTask::Ocr:
		return "You are an OCR assistant. Extract text faithfully from the image, preserve reading order when possible, and avoid inventing missing characters.";
	case ofxGgmlVisionTask::Ask:
		return "You are a grounded multimodal assistant. Answer using only information visible in the provided image and say when the image is insufficient.";
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

std::string ofxGgmlVisionInference::buildChatCompletionsJson(
	const ofxGgmlVisionModelProfile & profile,
	const ofxGgmlVisionRequest & request) {
	const ofxGgmlVisionPreparedPrompt prepared = preparePrompt(request);
	std::ostringstream json;
	json << "{";
	json << "\"messages\":[";
	json << "{"
		<< "\"role\":\"system\","
		<< "\"content\":\"" << jsonEscape(prepared.systemPrompt) << "\""
		<< "},";
	json << "{"
		<< "\"role\":\"user\","
		<< "\"content\":[";
	json << "{"
		<< "\"type\":\"text\","
		<< "\"text\":\"" << jsonEscape(prepared.userPrompt) << "\""
		<< "}";

	for (size_t imageIndex = 0; imageIndex < request.images.size(); ++imageIndex) {
		const auto & image = request.images[imageIndex];
		const std::string path = trimCopy(image.path);
		if (path.empty()) {
			continue;
		}
		const std::string mimeType = trimCopy(image.mimeType).empty()
			? detectMimeType(path)
			: trimCopy(image.mimeType);
		const std::string encoded = encodeImageFileBase64(path);
		if (encoded.empty()) {
			continue;
		}
		const std::string imageLabel = trimCopy(image.label);
		if (!imageLabel.empty()) {
			json << ",{"
				<< "\"type\":\"text\","
				<< "\"text\":\""
				<< jsonEscape("Image " + std::to_string(imageIndex + 1) + ": " + imageLabel)
				<< "\""
				<< "}";
		}
		json << ",{"
			<< "\"type\":\"image_url\","
			<< "\"image_url\":{"
			<< "\"url\":\"data:" << jsonEscape(mimeType) << ";base64," << encoded << "\"";
		if (request.task == ofxGgmlVisionTask::Ocr) {
			json << ",\"detail\":\"high\"";
		}
		json << "}"
			<< "}";
	}

	json << "]"
		<< "}";
	json << "],";
	json << "\"max_tokens\":" << std::max(1, request.maxTokens) << ",";
	json << "\"temperature\":" << request.temperature;
	if (!trimCopy(profile.modelPath).empty()) {
		json << ",\"model\":\"" << jsonEscape(profile.modelPath) << "\"";
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
	result.usedServerUrl = normalizeServerUrl(
		trimCopy(profile.serverUrl).empty() ? "http://127.0.0.1:8080" : profile.serverUrl);
	result.requestJson = buildChatCompletionsJson(profile, request);

#ifdef OFXGGML_HEADLESS_STUBS
	result.error = "vision server requests require openFrameworks runtime";
	return result;
#else
	try {
		ofHttpRequest httpRequest(result.usedServerUrl, "vision-request");
		httpRequest.method = ofHttpRequest::POST;
		httpRequest.body = result.requestJson;
		httpRequest.contentType = "application/json";
		httpRequest.headers["Accept"] = "application/json";
		httpRequest.timeoutSeconds = 180;

		ofURLFileLoader loader;
		const ofHttpResponse httpResponse = loader.handleRequest(httpRequest);
		result.responseJson = httpResponse.data.getText();

		if (httpResponse.status < 200 || httpResponse.status >= 300) {
			std::string detail = trimCopy(result.responseJson);
			if (detail.empty()) {
				detail = trimCopy(httpResponse.error);
			}
			result.error = "vision request failed with HTTP " +
				ofToString(httpResponse.status) + ": " + detail;
			return result;
		}

		result.text = trimCopy(extractTextFromOpenAiResponse(result.responseJson));
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
