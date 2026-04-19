#include "inference/ofxGgmlAceStepBridge.h"

#include "ofJson.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef OFXGGML_HEADLESS_STUBS
#ifdef _WIN32
#define CURL_STATICLIB
#endif
#include <curl/curl.h>
#endif

namespace {

std::string trimCopy(const std::string & text) {
	const auto begin = std::find_if_not(
		text.begin(),
		text.end(),
		[](unsigned char ch) { return std::isspace(ch) != 0; });
	const auto end = std::find_if_not(
		text.rbegin(),
		text.rend(),
		[](unsigned char ch) { return std::isspace(ch) != 0; }).base();
	if (begin >= end) {
		return {};
	}
	return std::string(begin, end);
}

std::string defaultOutputDir() {
	const std::filesystem::path base =
		std::filesystem::current_path() / "generated" / "music";
	return base.lexically_normal().string();
}

std::string sanitizeFileStem(const std::string & text) {
	std::string stem;
	stem.reserve(text.size());
	bool lastWasDash = false;
	for (unsigned char ch : text) {
		if (std::isalnum(ch) != 0) {
			stem.push_back(static_cast<char>(std::tolower(ch)));
			lastWasDash = false;
		} else if (!lastWasDash) {
			stem.push_back('-');
			lastWasDash = true;
		}
	}
	while (!stem.empty() && stem.front() == '-') {
		stem.erase(stem.begin());
	}
	while (!stem.empty() && stem.back() == '-') {
		stem.pop_back();
	}
	return stem.empty() ? std::string("acestep-track") : stem;
}

std::string safeLyrics(const ofxGgmlAceStepRequest & request) {
	const std::string lyrics = trimCopy(request.lyrics);
	if (!lyrics.empty()) {
		return lyrics;
	}
	return request.instrumentalOnly ? std::string("[Instrumental]") : std::string();
}

std::string normalizePrefix(const ofxGgmlAceStepRequest & request) {
	const std::string prefix = trimCopy(request.outputPrefix);
	if (!prefix.empty()) {
		return sanitizeFileStem(prefix);
	}
	const std::string caption = trimCopy(request.caption);
	return caption.empty()
		? std::string("acestep-track")
		: sanitizeFileStem(caption);
}

std::string detectAudioExtension(const std::string & contentType, bool wavRequested) {
	if (contentType.find("wav") != std::string::npos || wavRequested) {
		return ".wav";
	}
	if (contentType.find("mpeg") != std::string::npos ||
		contentType.find("mp3") != std::string::npos) {
		return ".mp3";
	}
	return wavRequested ? ".wav" : ".mp3";
}

std::string summarizeRequestCore(
	const std::string & caption,
	const std::string & lyrics,
	int bpm,
	float durationSeconds,
	const std::string & keyscale,
	const std::string & timesignature) {
	std::ostringstream summary;
	if (!caption.empty()) {
		summary << "Caption: " << caption;
	}
	if (!lyrics.empty()) {
		if (summary.tellp() > 0) {
			summary << "\n";
		}
		summary << "Lyrics: " << lyrics;
	}
	if (bpm > 0 || durationSeconds > 0.0f || !keyscale.empty() || !timesignature.empty()) {
		if (summary.tellp() > 0) {
			summary << "\n";
		}
		summary << "Meta:";
		if (bpm > 0) {
			summary << " " << bpm << " BPM";
		}
		if (durationSeconds > 0.0f) {
			summary << " | " << durationSeconds << " s";
		}
		if (!keyscale.empty()) {
			summary << " | " << keyscale;
		}
		if (!timesignature.empty()) {
			summary << " | " << timesignature << "/4";
		}
	}
	return summary.str();
}

#ifndef OFXGGML_HEADLESS_STUBS
size_t curlWriteToString(void * contents, size_t size, size_t nmemb, void * userp) {
	if (!contents || !userp) {
		return 0;
	}
	const size_t totalSize = size * nmemb;
	static_cast<std::string *>(userp)->append(
		static_cast<const char *>(contents),
		totalSize);
	return totalSize;
}

size_t curlWriteToVector(void * contents, size_t size, size_t nmemb, void * userp) {
	if (!contents || !userp) {
		return 0;
	}
	const size_t totalSize = size * nmemb;
	auto * output = static_cast<std::vector<unsigned char> *>(userp);
	const auto * bytes = static_cast<const unsigned char *>(contents);
	output->insert(output->end(), bytes, bytes + totalSize);
	return totalSize;
}

ofxGgmlAceStepHealthResult performHealthRequest(const std::string & url) {
	ofxGgmlAceStepHealthResult result;
	result.usedServerUrl = url;
	const auto start = std::chrono::steady_clock::now();

	CURL * curl = curl_easy_init();
	if (!curl) {
		result.error = "failed to initialize curl for AceStep health check";
		return result;
	}

	std::string responseBody;
	long httpCode = 0;
	CURLcode performCode = CURLE_OK;
	curl_slist * headers = nullptr;

	try {
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToString);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
		headers = curl_slist_append(headers, "Accept: application/json");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		performCode = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	} catch (...) {
		performCode = CURLE_ABORTED_BY_CALLBACK;
	}

	if (headers) {
		curl_slist_free_all(headers);
	}
	curl_easy_cleanup(curl);

	result.elapsedMs = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - start).count();

	if (performCode != CURLE_OK) {
		result.error = std::string("AceStep health request failed: ") +
			curl_easy_strerror(performCode);
		return result;
	}
	if (httpCode < 200 || httpCode >= 300) {
		result.error = "AceStep health check returned HTTP " + std::to_string(httpCode);
		if (!trimCopy(responseBody).empty()) {
			result.error += ": " + trimCopy(responseBody);
		}
		return result;
	}

	const ofJson parsed = ofJson::parse(responseBody, nullptr, false);
	if (parsed.is_object() && parsed.contains("status") && parsed["status"].is_string()) {
		result.status = parsed["status"].get<std::string>();
	}
	result.success = result.status == "ok" || trimCopy(responseBody) == "{\"status\":\"ok\"}";
	if (!result.success && result.error.empty()) {
		result.error = "AceStep health check returned an unexpected response.";
	}
	return result;
}

ofxGgmlAceStepPropsResult performPropsRequest(const std::string & url) {
	ofxGgmlAceStepPropsResult result;
	result.usedServerUrl = url;
	const auto start = std::chrono::steady_clock::now();

	CURL * curl = curl_easy_init();
	if (!curl) {
		result.error = "failed to initialize curl for AceStep props request";
		return result;
	}

	std::string responseBody;
	long httpCode = 0;
	CURLcode performCode = CURLE_OK;
	curl_slist * headers = nullptr;

	try {
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToString);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
		headers = curl_slist_append(headers, "Accept: application/json");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		performCode = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	} catch (...) {
		performCode = CURLE_ABORTED_BY_CALLBACK;
	}

	if (headers) {
		curl_slist_free_all(headers);
	}
	curl_easy_cleanup(curl);

	result.elapsedMs = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - start).count();
	result.rawJson = responseBody;

	if (performCode != CURLE_OK) {
		result.error = std::string("AceStep props request failed: ") +
			curl_easy_strerror(performCode);
		return result;
	}
	if (httpCode < 200 || httpCode >= 300) {
		result.error = "AceStep props returned HTTP " + std::to_string(httpCode);
		if (!trimCopy(responseBody).empty()) {
			result.error += ": " + trimCopy(responseBody);
		}
		return result;
	}

	const ofJson parsed = ofJson::parse(responseBody, nullptr, false);
	if (parsed.is_discarded()) {
		result.error = "AceStep props returned invalid JSON";
		return result;
	}
	if (parsed.contains("status") && parsed["status"].is_object()) {
		const ofJson & status = parsed["status"];
		if (status.contains("lm") && status["lm"].is_string()) {
			result.lmStatus = status["lm"].get<std::string>();
		}
		if (status.contains("synth") && status["synth"].is_string()) {
			result.synthStatus = status["synth"].get<std::string>();
		}
	}
	if (parsed.contains("cli") && parsed["cli"].is_object()) {
		const ofJson & cli = parsed["cli"];
		if (cli.contains("max_batch") && cli["max_batch"].is_number_integer()) {
			result.maxBatch = cli["max_batch"].get<int>();
		}
		if (cli.contains("mp3_bitrate") && cli["mp3_bitrate"].is_number_integer()) {
			result.mp3Bitrate = cli["mp3_bitrate"].get<int>();
		}
	}
	result.success = true;
	return result;
}

ofxGgmlAceStepGenerateResult performGenerateRequest(
	const std::string & lmUrl,
	const std::string & synthUrl,
	const ofxGgmlAceStepRequest & request,
	const std::string & requestBody) {
	ofxGgmlAceStepGenerateResult result;
	result.usedServerUrl = lmUrl;
	result.requestJson = requestBody;
	const auto start = std::chrono::steady_clock::now();

	CURL * curl = curl_easy_init();
	if (!curl) {
		result.error = "failed to initialize curl for AceStep generation";
		return result;
	}

	long httpCode = 0;
	curl_slist * headers = nullptr;
	CURLcode performCode = CURLE_OK;

	headers = curl_slist_append(headers, "Accept: application/json");
	headers = curl_slist_append(headers, "Content-Type: application/json");

	std::string lmResponseBody;
	try {
		curl_easy_setopt(curl, CURLOPT_URL, lmUrl.c_str());
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody.c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(requestBody.size()));
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToString);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &lmResponseBody);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 240L);
		performCode = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	} catch (...) {
		performCode = CURLE_ABORTED_BY_CALLBACK;
	}

	if (performCode != CURLE_OK) {
		result.error = std::string("AceStep /lm request failed: ") +
			curl_easy_strerror(performCode);
		if (headers) {
			curl_slist_free_all(headers);
		}
		curl_easy_cleanup(curl);
		return result;
	}
	if (httpCode < 200 || httpCode >= 300) {
		result.error = "AceStep /lm returned HTTP " + std::to_string(httpCode);
		if (!trimCopy(lmResponseBody).empty()) {
			result.error += ": " + trimCopy(lmResponseBody);
		}
		if (headers) {
			curl_slist_free_all(headers);
		}
		curl_easy_cleanup(curl);
		return result;
	}

	result.enrichedRequestsJson = lmResponseBody;

	std::vector<unsigned char> synthResponseBytes;
	std::string synthContentType;
	curl_easy_reset(curl);
	httpCode = 0;
	performCode = CURLE_OK;
	try {
		curl_easy_setopt(curl, CURLOPT_URL, synthUrl.c_str());
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, lmResponseBody.c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(lmResponseBody.size()));
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToVector);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &synthResponseBytes);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
		performCode = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
		char * responseContentType = nullptr;
		curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &responseContentType);
		if (responseContentType) {
			synthContentType = responseContentType;
		}
	} catch (...) {
		performCode = CURLE_ABORTED_BY_CALLBACK;
	}

	if (headers) {
		curl_slist_free_all(headers);
	}
	curl_easy_cleanup(curl);

	result.elapsedMs = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - start).count();

	if (performCode != CURLE_OK) {
		result.error = std::string("AceStep /synth request failed: ") +
			curl_easy_strerror(performCode);
		return result;
	}
	if (httpCode < 200 || httpCode >= 300) {
		result.error = "AceStep /synth returned HTTP " + std::to_string(httpCode);
		if (!synthResponseBytes.empty()) {
			result.error += ": " + trimCopy(std::string(
				synthResponseBytes.begin(),
				synthResponseBytes.end()));
		}
		return result;
	}
	if (synthResponseBytes.empty()) {
		result.error = "AceStep /synth returned empty audio output";
		return result;
	}

	std::error_code ec;
	const std::filesystem::path outputDir(
		trimCopy(request.outputDir).empty()
			? defaultOutputDir()
			: trimCopy(request.outputDir));
	std::filesystem::create_directories(outputDir, ec);
	const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	const std::string fileName =
		normalizePrefix(request) + "-" + std::to_string(nowMs) +
		detectAudioExtension(synthContentType, request.wavOutput);
	const std::filesystem::path outputPath = outputDir / fileName;
	std::ofstream file(outputPath, std::ios::binary);
	if (!file) {
		result.error = "AceStep generated audio could not be written to disk.";
		return result;
	}
	file.write(
		reinterpret_cast<const char *>(synthResponseBytes.data()),
		static_cast<std::streamsize>(synthResponseBytes.size()));
	file.close();
	if (!file.good()) {
		result.error = "AceStep generated audio write did not complete successfully.";
		return result;
	}

	ofxGgmlGeneratedMusicTrack track;
	track.path = outputPath.lexically_normal().string();
	track.label = trimCopy(request.caption).empty()
		? std::string("AceStep track")
		: trimCopy(request.caption);
	track.durationSeconds = std::max(0.0f, request.durationSeconds);
	result.tracks.push_back(track);
	result.commandOutput =
		"/lm -> /synth completed; audio saved to " + track.path;
	result.success = true;
	return result;
}

ofxGgmlAceStepUnderstandResult performUnderstandRequest(
	const std::string & url,
	const ofxGgmlAceStepUnderstandRequest & request) {
	ofxGgmlAceStepUnderstandResult result;
	result.usedServerUrl = url;
	const auto start = std::chrono::steady_clock::now();

	CURL * curl = curl_easy_init();
	if (!curl) {
		result.error = "failed to initialize curl for AceStep understand request";
		return result;
	}

	std::string responseBody;
	long httpCode = 0;
	CURLcode performCode = CURLE_OK;
	curl_mime * mime = nullptr;
	curl_slist * headers = nullptr;

	try {
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToString);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
		headers = curl_slist_append(headers, "Accept: application/json");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

		mime = curl_mime_init(curl);
		curl_mimepart * audioPart = curl_mime_addpart(mime);
		curl_mime_name(audioPart, "audio");
		curl_mime_filedata(audioPart, request.audioPath.c_str());

		if (request.includeRequestTemplate) {
			curl_mimepart * requestPart = curl_mime_addpart(mime);
			const std::string requestJson =
				ofxGgmlAceStepBridge::buildRequestJson(request.requestTemplate).dump();
			curl_mime_name(requestPart, "request");
			curl_mime_data(
				requestPart,
				requestJson.c_str(),
				CURL_ZERO_TERMINATED);
			curl_mime_type(requestPart, "application/json");
		}

		curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
		performCode = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	} catch (...) {
		performCode = CURLE_ABORTED_BY_CALLBACK;
	}

	if (mime) {
		curl_mime_free(mime);
	}
	if (headers) {
		curl_slist_free_all(headers);
	}
	curl_easy_cleanup(curl);

	result.elapsedMs = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - start).count();
	result.rawJson = responseBody;

	if (performCode != CURLE_OK) {
		result.error = std::string("AceStep /understand request failed: ") +
			curl_easy_strerror(performCode);
		return result;
	}
	if (httpCode < 200 || httpCode >= 300) {
		result.error = "AceStep /understand returned HTTP " + std::to_string(httpCode);
		if (!trimCopy(responseBody).empty()) {
			result.error += ": " + trimCopy(responseBody);
		}
		return result;
	}

	const ofJson parsed = ofJson::parse(responseBody, nullptr, false);
	if (parsed.is_discarded() || !parsed.is_object()) {
		result.error = "AceStep /understand returned invalid JSON";
		return result;
	}

	if (parsed.contains("caption") && parsed["caption"].is_string()) {
		result.caption = trimCopy(parsed["caption"].get<std::string>());
	}
	if (parsed.contains("lyrics") && parsed["lyrics"].is_string()) {
		result.lyrics = trimCopy(parsed["lyrics"].get<std::string>());
	}
	if (parsed.contains("bpm") && parsed["bpm"].is_number_integer()) {
		result.bpm = parsed["bpm"].get<int>();
	}
	if (parsed.contains("duration") && parsed["duration"].is_number()) {
		result.durationSeconds = parsed["duration"].get<float>();
	}
	if (parsed.contains("keyscale") && parsed["keyscale"].is_string()) {
		result.keyscale = trimCopy(parsed["keyscale"].get<std::string>());
	}
	if (parsed.contains("timesignature") && parsed["timesignature"].is_string()) {
		result.timesignature = trimCopy(parsed["timesignature"].get<std::string>());
	}
	if (parsed.contains("vocal_language") && parsed["vocal_language"].is_string()) {
		result.vocalLanguage = trimCopy(parsed["vocal_language"].get<std::string>());
	}
	if (parsed.contains("audio_codes") && parsed["audio_codes"].is_string()) {
		result.audioCodes = trimCopy(parsed["audio_codes"].get<std::string>());
	}
	result.summary = summarizeRequestCore(
		result.caption,
		result.lyrics,
		result.bpm,
		result.durationSeconds,
		result.keyscale,
		result.timesignature);
	result.success = true;
	return result;
}
#endif

} // namespace

ofxGgmlAceStepBridge::ofxGgmlAceStepBridge(std::string serverUrl)
	: m_serverUrl(std::move(serverUrl)) {
}

void ofxGgmlAceStepBridge::setServerUrl(const std::string & serverUrl) {
	m_serverUrl = trimCopy(serverUrl);
}

const std::string & ofxGgmlAceStepBridge::getServerUrl() const {
	return m_serverUrl;
}

std::string ofxGgmlAceStepBridge::normalizeServerUrl(
	const std::string & serverUrl,
	const std::string & endpoint) {
	std::string normalized = trimCopy(serverUrl);
	if (normalized.empty()) {
		normalized = "http://127.0.0.1:8085";
	}
	while (!normalized.empty() && normalized.back() == '/') {
		normalized.pop_back();
	}
	if (endpoint.empty()) {
		return normalized;
	}
	if (!endpoint.empty() && endpoint.front() == '/') {
		return normalized + endpoint;
	}
	return normalized + "/" + endpoint;
}

ofJson ofxGgmlAceStepBridge::buildRequestJson(const ofxGgmlAceStepRequest & request) {
	ofJson json;
	json["caption"] = trimCopy(request.caption);
	json["lyrics"] = safeLyrics(request);
	json["bpm"] = std::max(0, request.bpm);
	json["duration"] = std::max(0.0f, request.durationSeconds);
	json["keyscale"] = trimCopy(request.keyscale);
	json["timesignature"] = trimCopy(request.timesignature);
	json["vocal_language"] = trimCopy(request.vocalLanguage);
	json["seed"] = request.seed;
	json["batch_size"] = std::clamp(request.batchSize, 1, 9);
	json["lm_temperature"] = std::clamp(request.lmTemperature, 0.0f, 2.0f);
	json["lm_cfg_scale"] = std::max(0.0f, request.lmCfgScale);
	json["lm_top_p"] = std::clamp(request.lmTopP, 0.0f, 1.0f);
	json["lm_top_k"] = std::max(0, request.lmTopK);
	json["lm_negative_prompt"] = trimCopy(request.lmNegativePrompt);
	json["use_cot_caption"] = request.useCotCaption;
	json["audio_codes"] = trimCopy(request.audioCodes);
	json["inference_steps"] = std::max(0, request.inferenceSteps);
	json["guidance_scale"] = std::max(0.0f, request.guidanceScale);
	json["shift"] = std::max(0.0f, request.shift);
	json["audio_cover_strength"] =
		std::clamp(request.audioCoverStrength, 0.0f, 1.0f);
	json["repainting_start"] = request.repaintingStart;
	json["repainting_end"] = request.repaintingEnd;
	json["lego"] = trimCopy(request.lego);
	return json;
}

std::string ofxGgmlAceStepBridge::summarizeRequestJson(const ofJson & requestJson) {
	if (!requestJson.is_object()) {
		return {};
	}

	const std::string caption =
		requestJson.value("caption", std::string());
	const std::string lyrics =
		requestJson.value("lyrics", std::string());
	const int bpm =
		requestJson.value("bpm", 0);
	const float durationSeconds =
		requestJson.value("duration", 0.0f);
	const std::string keyscale =
		requestJson.value("keyscale", std::string());
	const std::string timesignature =
		requestJson.value("timesignature", std::string());
	return summarizeRequestCore(
		trimCopy(caption),
		trimCopy(lyrics),
		bpm,
		durationSeconds,
		trimCopy(keyscale),
		trimCopy(timesignature));
}

std::string ofxGgmlAceStepBridge::summarizeUnderstandResult(
	const ofxGgmlAceStepUnderstandResult & result) {
	return summarizeRequestCore(
		trimCopy(result.caption),
		trimCopy(result.lyrics),
		result.bpm,
		result.durationSeconds,
		trimCopy(result.keyscale),
		trimCopy(result.timesignature));
}

ofxGgmlAceStepHealthResult ofxGgmlAceStepBridge::healthCheck(
	const std::string & serverUrl) const {
	const std::string url = normalizeServerUrl(
		serverUrl.empty() ? m_serverUrl : serverUrl,
		"/health");
#ifdef OFXGGML_HEADLESS_STUBS
	ofxGgmlAceStepHealthResult result;
	result.usedServerUrl = url;
	result.error = "AceStep health requests require openFrameworks runtime";
	return result;
#else
	return performHealthRequest(url);
#endif
}

ofxGgmlAceStepPropsResult ofxGgmlAceStepBridge::fetchProps(
	const std::string & serverUrl) const {
	const std::string url = normalizeServerUrl(
		serverUrl.empty() ? m_serverUrl : serverUrl,
		"/props");
#ifdef OFXGGML_HEADLESS_STUBS
	ofxGgmlAceStepPropsResult result;
	result.usedServerUrl = url;
	result.error = "AceStep props requests require openFrameworks runtime";
	return result;
#else
	return performPropsRequest(url);
#endif
}

ofxGgmlAceStepGenerateResult ofxGgmlAceStepBridge::generate(
	const ofxGgmlAceStepRequest & request,
	const std::string & serverUrl) const {
	if (trimCopy(request.caption).empty()) {
		ofxGgmlAceStepGenerateResult result;
		result.error = "AceStep generation requires a caption / prompt.";
		return result;
	}

	const std::string baseUrl =
		normalizeServerUrl(serverUrl.empty() ? m_serverUrl : serverUrl);
	const std::string requestBody = buildRequestJson(request).dump();
#ifdef OFXGGML_HEADLESS_STUBS
	ofxGgmlAceStepGenerateResult result;
	result.usedServerUrl = baseUrl;
	result.requestJson = requestBody;
	result.error = "AceStep generation requires openFrameworks runtime";
	return result;
#else
	return performGenerateRequest(
		normalizeServerUrl(baseUrl, "/lm"),
		normalizeServerUrl(baseUrl, request.wavOutput ? "/synth?wav=1" : "/synth"),
		request,
		requestBody);
#endif
}

ofxGgmlAceStepUnderstandResult ofxGgmlAceStepBridge::understandAudio(
	const ofxGgmlAceStepUnderstandRequest & request,
	const std::string & serverUrl) const {
	if (trimCopy(request.audioPath).empty()) {
		ofxGgmlAceStepUnderstandResult result;
		result.error = "AceStep understand requires an audio file path.";
		return result;
	}

	const std::string baseUrl =
		normalizeServerUrl(serverUrl.empty() ? m_serverUrl : serverUrl);
#ifdef OFXGGML_HEADLESS_STUBS
	ofxGgmlAceStepUnderstandResult result;
	result.usedServerUrl = baseUrl;
	result.error = "AceStep understand requests require openFrameworks runtime";
	return result;
#else
	return performUnderstandRequest(
		normalizeServerUrl(baseUrl, "/understand"),
		request);
#endif
}

std::shared_ptr<ofxGgmlMusicGenerationBackend>
ofxGgmlAceStepBridge::createMusicGenerationBackend(
	const std::string & serverUrl) const {
	const std::string configuredUrl = serverUrl.empty() ? m_serverUrl : serverUrl;
	return createMusicGenerationBridgeBackend(
		[configuredUrl, bridge = *this](const ofxGgmlMusicGenerationRequest & request) {
			ofxGgmlAceStepRequest aceRequest;
			aceRequest.caption = request.prompt;
			aceRequest.durationSeconds =
				static_cast<float>(std::max(0, request.durationSeconds));
			aceRequest.seed = request.seed;
			aceRequest.instrumentalOnly = request.instrumentalOnly;
			aceRequest.lmNegativePrompt = request.negativePrompt;
			aceRequest.outputDir = request.outputDir;
			aceRequest.outputPrefix = request.outputPrefix;
			aceRequest.wavOutput = false;

			ofxGgmlMusicGenerationResult result;
			const ofxGgmlAceStepGenerateResult aceResult =
				bridge.generate(aceRequest, configuredUrl);
			result.success = aceResult.success;
			result.elapsedMs = aceResult.elapsedMs;
			result.backendName = "AceStep";
			result.generatedPrompt = aceRequest.caption;
			result.normalizedCommand = aceResult.usedServerUrl + " /lm -> /synth";
			result.commandOutput = aceResult.commandOutput;
			result.error = aceResult.error;
			result.tracks = aceResult.tracks;
			return result;
		});
}
