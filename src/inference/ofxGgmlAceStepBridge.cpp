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

std::string aceStepServerRootFromUrl(const std::string & url) {
	std::string normalized = trimCopy(url);
	while (!normalized.empty() && normalized.back() == '/') {
		normalized.pop_back();
	}

	const std::vector<std::string> suffixes = {
		"/lm",
		"/synth",
		"/understand",
		"/health",
		"/props",
		"/job"
	};
	for (const auto & suffix : suffixes) {
		if (normalized.size() > suffix.size() &&
			normalized.compare(
				normalized.size() - suffix.size(),
				suffix.size(),
				suffix) == 0) {
			normalized.erase(normalized.size() - suffix.size());
			break;
		}
	}
	while (!normalized.empty() && normalized.back() == '/') {
		normalized.pop_back();
	}
	return normalized;
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

bool performStringRequest(
	const std::string & url,
	const char * method,
	const std::string * requestBody,
	const std::vector<std::string> & requestHeaders,
	long timeoutSeconds,
	std::string & responseBody,
	long & httpCode,
	std::string & contentType,
	std::string & error) {
	CURL * curl = curl_easy_init();
	if (!curl) {
		error = "failed to initialize curl";
		return false;
	}

	curl_slist * headers = nullptr;
	for (const auto & header : requestHeaders) {
		headers = curl_slist_append(headers, header.c_str());
	}

	CURLcode performCode = CURLE_OK;
	try {
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToString);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
		if (headers) {
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		}
		if (requestBody) {
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody->c_str());
			curl_easy_setopt(
				curl,
				CURLOPT_POSTFIELDSIZE,
				static_cast<long>(requestBody->size()));
		}
		performCode = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
		char * responseContentType = nullptr;
		curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &responseContentType);
		if (responseContentType) {
			contentType = responseContentType;
		}
	} catch (...) {
		performCode = CURLE_ABORTED_BY_CALLBACK;
	}

	if (headers) {
		curl_slist_free_all(headers);
	}
	curl_easy_cleanup(curl);

	if (performCode != CURLE_OK) {
		error = curl_easy_strerror(performCode);
		return false;
	}
	return true;
}

bool performBinaryRequest(
	const std::string & url,
	const char * method,
	const std::string * requestBody,
	const std::vector<std::string> & requestHeaders,
	long timeoutSeconds,
	std::vector<unsigned char> & responseBytes,
	long & httpCode,
	std::string & contentType,
	std::string & error) {
	CURL * curl = curl_easy_init();
	if (!curl) {
		error = "failed to initialize curl";
		return false;
	}

	curl_slist * headers = nullptr;
	for (const auto & header : requestHeaders) {
		headers = curl_slist_append(headers, header.c_str());
	}

	CURLcode performCode = CURLE_OK;
	try {
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToVector);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBytes);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
		if (headers) {
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		}
		if (requestBody) {
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody->c_str());
			curl_easy_setopt(
				curl,
				CURLOPT_POSTFIELDSIZE,
				static_cast<long>(requestBody->size()));
		}
		performCode = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
		char * responseContentType = nullptr;
		curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &responseContentType);
		if (responseContentType) {
			contentType = responseContentType;
		}
	} catch (...) {
		performCode = CURLE_ABORTED_BY_CALLBACK;
	}

	if (headers) {
		curl_slist_free_all(headers);
	}
	curl_easy_cleanup(curl);

	if (performCode != CURLE_OK) {
		error = curl_easy_strerror(performCode);
		return false;
	}
	return true;
}

bool parseAsyncJobId(const std::string & responseBody, std::string & jobId) {
	const ofJson parsed = ofJson::parse(responseBody, nullptr, false);
	if (!parsed.is_object()) {
		return false;
	}
	if (!parsed.contains("id") || !parsed["id"].is_string()) {
		return false;
	}
	jobId = trimCopy(parsed["id"].get<std::string>());
	return !jobId.empty();
}

bool parseJobStatusPayload(
	const std::string & responseBody,
	std::string & status,
	std::string & errorMessage) {
	const ofJson parsed = ofJson::parse(responseBody, nullptr, false);
	if (!parsed.is_object()) {
		return false;
	}
	if (parsed.contains("status") && parsed["status"].is_string()) {
		status = trimCopy(parsed["status"].get<std::string>());
	}
	if (parsed.contains("error") && parsed["error"].is_string()) {
		errorMessage = trimCopy(parsed["error"].get<std::string>());
	}
	return !status.empty() || !errorMessage.empty();
}

std::string extractLikelyJsonPayload(const std::string & responseBody) {
	const std::string trimmed = trimCopy(responseBody);
	if (trimmed.empty()) {
		return {};
	}
	if (trimmed.rfind("```", 0) == 0) {
		const size_t firstNewline = trimmed.find('\n');
		const size_t closingFence = trimmed.rfind("```");
		if (firstNewline != std::string::npos &&
			closingFence != std::string::npos &&
			closingFence > firstNewline) {
			return trimCopy(trimmed.substr(
				firstNewline + 1,
				closingFence - firstNewline - 1));
		}
	}

	const size_t firstObject = trimmed.find('{');
	const size_t firstArray = trimmed.find('[');
	size_t firstJson = std::string::npos;
	if (firstObject == std::string::npos) {
		firstJson = firstArray;
	} else if (firstArray == std::string::npos) {
		firstJson = firstObject;
	} else {
		firstJson = std::min(firstObject, firstArray);
	}
	if (firstJson == std::string::npos) {
		return trimmed;
	}

	const size_t lastObject = trimmed.rfind('}');
	const size_t lastArray = trimmed.rfind(']');
	const size_t lastJson = std::max(
		lastObject == std::string::npos ? size_t(0) : lastObject,
		lastArray == std::string::npos ? size_t(0) : lastArray);
	if (lastJson < firstJson) {
		return trimmed.substr(firstJson);
	}
	return trimCopy(trimmed.substr(firstJson, lastJson - firstJson + 1));
}

ofJson parseAceStepJsonPayload(const std::string & responseBody) {
	ofJson parsed = ofJson::parse(responseBody, nullptr, false);
	if (!parsed.is_discarded()) {
		return parsed;
	}
	const std::string extracted = extractLikelyJsonPayload(responseBody);
	if (extracted != trimCopy(responseBody)) {
		parsed = ofJson::parse(extracted, nullptr, false);
		if (!parsed.is_discarded()) {
			return parsed;
		}
	}
	return ofJson();
}

bool buildSynthRequestJsonValue(
	const ofJson & parsed,
	ofJson & synthRequest,
	std::string & error) {
	if (parsed.is_discarded() || parsed.is_null()) {
		error = "AceStep /lm result returned invalid JSON";
		return false;
	}
	if (parsed.is_array()) {
		if (parsed.empty() || !parsed.front().is_object()) {
			error = "AceStep /lm result did not contain a usable request payload";
			return false;
		}
		synthRequest = parsed.front();
		return true;
	}
	if (parsed.is_object()) {
		static const std::vector<std::string> nestedKeys = {
			"result",
			"request",
			"requests",
			"data",
			"payload"
		};
		for (const auto & key : nestedKeys) {
			if (!parsed.contains(key)) {
				continue;
			}
			const ofJson & nested = parsed[key];
			if (nested.is_object()) {
				synthRequest = nested;
				return true;
			}
			if (nested.is_array()) {
				if (nested.empty() || !nested.front().is_object()) {
					error = "AceStep /lm result did not contain a usable request payload";
					return false;
				}
				synthRequest = nested.front();
				return true;
			}
			if (nested.is_string()) {
				const ofJson reparsed = parseAceStepJsonPayload(
					trimCopy(nested.get<std::string>()));
				if (!reparsed.is_discarded() &&
					buildSynthRequestJsonValue(reparsed, synthRequest, error)) {
					return true;
				}
			}
		}
		synthRequest = parsed;
		return true;
	}
	if (parsed.is_string()) {
		const ofJson reparsed = parseAceStepJsonPayload(
			trimCopy(parsed.get<std::string>()));
		if (!reparsed.is_discarded() &&
			buildSynthRequestJsonValue(reparsed, synthRequest, error)) {
			return true;
		}
	}
	error = "AceStep /lm result did not contain a usable request payload";
	return false;
}

std::string buildSynthRequestBodyFromLmResult(
	const std::string & lmResponseBody,
	std::string & error) {
	const ofJson parsed = parseAceStepJsonPayload(lmResponseBody);
	ofJson synthRequest;
	if (!buildSynthRequestJsonValue(parsed, synthRequest, error)) {
		if (!trimCopy(lmResponseBody).empty()) {
			error += ": " + trimCopy(extractLikelyJsonPayload(lmResponseBody));
		}
		return {};
	}
	return synthRequest.dump();
}

bool fetchAceStepJobStringResult(
	const std::string & baseUrl,
	const std::string & jobId,
	long timeoutSeconds,
	std::string & resultBody,
	std::string & error) {
	const std::string statusUrl = baseUrl + "/job?id=" + jobId;
	const std::vector<std::string> headers = { "Accept: application/json" };
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::seconds(timeoutSeconds);
	while (std::chrono::steady_clock::now() < deadline) {
		std::string statusBody;
		long httpCode = 0;
		std::string contentType;
		std::string requestError;
		if (!performStringRequest(
				statusUrl,
				"GET",
				nullptr,
				headers,
				30L,
				statusBody,
				httpCode,
				contentType,
				requestError)) {
			error = "AceStep /job status request failed: " + requestError;
			return false;
		}
		if (httpCode < 200 || httpCode >= 300) {
			error = "AceStep /job status returned HTTP " + std::to_string(httpCode);
			if (!trimCopy(statusBody).empty()) {
				error += ": " + trimCopy(statusBody);
			}
			return false;
		}

		std::string status;
		std::string statusError;
		if (!parseJobStatusPayload(statusBody, status, statusError)) {
			error = "AceStep /job status returned invalid JSON";
			return false;
		}
		if (status == "done") {
			const std::string resultUrl = statusUrl + "&result=1";
			long resultCode = 0;
			std::string resultType;
			std::string resultError;
			if (!performStringRequest(
					resultUrl,
					"GET",
					nullptr,
					headers,
					timeoutSeconds,
					resultBody,
					resultCode,
					resultType,
					resultError)) {
				error = "AceStep /job result request failed: " + resultError;
				return false;
			}
			if (resultCode < 200 || resultCode >= 300) {
				error = "AceStep /job result returned HTTP " + std::to_string(resultCode);
				if (!trimCopy(resultBody).empty()) {
					error += ": " + trimCopy(resultBody);
				}
				return false;
			}
			return true;
		}
		if (status == "failed" || status == "cancelled") {
			error = statusError.empty()
				? "AceStep job " + status + "."
				: "AceStep job " + status + ": " + statusError;
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
	}
	error = "AceStep job timed out before producing a result.";
	return false;
}

bool fetchAceStepJobBinaryResult(
	const std::string & baseUrl,
	const std::string & jobId,
	long timeoutSeconds,
	std::vector<unsigned char> & resultBytes,
	std::string & contentType,
	std::string & error) {
	const std::string statusUrl = baseUrl + "/job?id=" + jobId;
	const std::vector<std::string> statusHeaders = { "Accept: application/json" };
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::seconds(timeoutSeconds);
	while (std::chrono::steady_clock::now() < deadline) {
		std::string statusBody;
		long httpCode = 0;
		std::string statusType;
		std::string requestError;
		if (!performStringRequest(
				statusUrl,
				"GET",
				nullptr,
				statusHeaders,
				30L,
				statusBody,
				httpCode,
				statusType,
				requestError)) {
			error = "AceStep /job status request failed: " + requestError;
			return false;
		}
		if (httpCode < 200 || httpCode >= 300) {
			error = "AceStep /job status returned HTTP " + std::to_string(httpCode);
			if (!trimCopy(statusBody).empty()) {
				error += ": " + trimCopy(statusBody);
			}
			return false;
		}

		std::string status;
		std::string statusError;
		if (!parseJobStatusPayload(statusBody, status, statusError)) {
			error = "AceStep /job status returned invalid JSON";
			return false;
		}
		if (status == "done") {
			const std::string resultUrl = statusUrl + "&result=1";
			long resultCode = 0;
			std::string resultError;
			const std::vector<std::string> resultHeaders = { "Accept: */*" };
			if (!performBinaryRequest(
					resultUrl,
					"GET",
					nullptr,
					resultHeaders,
					timeoutSeconds,
					resultBytes,
					resultCode,
					contentType,
					resultError)) {
				error = "AceStep /job result request failed: " + resultError;
				return false;
			}
			if (resultCode < 200 || resultCode >= 300) {
				error = "AceStep /job result returned HTTP " + std::to_string(resultCode);
				if (!resultBytes.empty()) {
					error += ": " + trimCopy(std::string(
						resultBytes.begin(),
						resultBytes.end()));
				}
				return false;
			}
			return true;
		}
		if (status == "failed" || status == "cancelled") {
			error = statusError.empty()
				? "AceStep job " + status + "."
				: "AceStep job " + status + ": " + statusError;
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
	}
	error = "AceStep job timed out before producing a result.";
	return false;
}

ofxGgmlAceStepHealthResult performHealthRequest(
	const std::string & url,
	long timeoutMs) {
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
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, std::min<long>(timeoutMs, 500L));
		curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, std::max<long>(timeoutMs, 500L));
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

	std::string lmResponseBody;
	long httpCode = 0;
	std::string contentType;
	std::string requestError;
	const std::vector<std::string> headers = {
		"Accept: application/json",
		"Content-Type: application/json"
	};
	const std::string baseUrl = aceStepServerRootFromUrl(lmUrl);
	if (!performStringRequest(
			lmUrl,
			"POST",
			&requestBody,
			headers,
			240L,
			lmResponseBody,
			httpCode,
			contentType,
			requestError)) {
		result.error = "AceStep /lm request failed: " + requestError;
		return result;
	}
	if (httpCode < 200 || httpCode >= 300) {
		result.error = "AceStep /lm returned HTTP " + std::to_string(httpCode);
		if (!trimCopy(lmResponseBody).empty()) {
			result.error += ": " + trimCopy(lmResponseBody);
		}
		return result;
	}
	std::string lmJobId;
	if (parseAsyncJobId(lmResponseBody, lmJobId)) {
		std::string jobError;
		if (!fetchAceStepJobStringResult(
				baseUrl,
				lmJobId,
				600L,
				lmResponseBody,
				jobError)) {
			result.error = jobError;
			return result;
		}
	}

	result.enrichedRequestsJson = lmResponseBody;
	std::string synthRequestError;
	const std::string synthRequestBody =
		buildSynthRequestBodyFromLmResult(lmResponseBody, synthRequestError);
	if (synthRequestBody.empty()) {
		result.error = synthRequestError;
		return result;
	}

	std::vector<unsigned char> synthResponseBytes;
	std::string synthContentType;
	httpCode = 0;
	requestError.clear();
	if (!performBinaryRequest(
			synthUrl,
			"POST",
			&synthRequestBody,
			headers,
			600L,
			synthResponseBytes,
			httpCode,
			synthContentType,
			requestError)) {
		result.error = "AceStep /synth request failed: " + requestError;
		return result;
	}

	result.elapsedMs = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - start).count();

	if (httpCode < 200 || httpCode >= 300) {
		result.error = "AceStep /synth returned HTTP " + std::to_string(httpCode);
		if (!synthResponseBytes.empty()) {
			result.error += ": " + trimCopy(std::string(
				synthResponseBytes.begin(),
				synthResponseBytes.end()));
		}
		return result;
	}
	std::string synthEnvelope(
		synthResponseBytes.begin(),
		synthResponseBytes.end());
	std::string synthJobId;
	if (parseAsyncJobId(synthEnvelope, synthJobId)) {
		synthResponseBytes.clear();
		std::string jobError;
		if (!fetchAceStepJobBinaryResult(
				baseUrl,
				synthJobId,
				1200L,
				synthResponseBytes,
				synthContentType,
				jobError)) {
			result.error = jobError;
			return result;
		}
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

	std::string responseBody;
	long httpCode = 0;
	std::string contentType;

	CURL * curl = curl_easy_init();
	if (!curl) {
		result.error = "failed to initialize curl for AceStep understand request";
		return result;
	}

	curl_mime * mime = nullptr;
	curl_slist * headers = nullptr;
	CURLcode performCode = CURLE_OK;
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
		char * responseContentType = nullptr;
		curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &responseContentType);
		if (responseContentType) {
			contentType = responseContentType;
		}
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
	std::string understandJobId;
	if (parseAsyncJobId(responseBody, understandJobId)) {
		std::string jobError;
		const std::string baseUrl = aceStepServerRootFromUrl(url);
		if (!fetchAceStepJobStringResult(
				baseUrl,
				understandJobId,
				1200L,
				responseBody,
				jobError)) {
			result.error = jobError;
			return result;
		}
		result.rawJson = responseBody;
	}

	ofJson parsed = ofJson::parse(responseBody, nullptr, false);
	if (parsed.is_array() && !parsed.empty() && parsed[0].is_object()) {
		parsed = parsed[0];
	}
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
	const std::string & serverUrl,
	long timeoutMs) const {
	const std::string url = normalizeServerUrl(
		serverUrl.empty() ? m_serverUrl : serverUrl,
		"/health");
#ifdef OFXGGML_HEADLESS_STUBS
	ofxGgmlAceStepHealthResult result;
	result.usedServerUrl = url;
	result.error = "AceStep health requests require openFrameworks runtime";
	return result;
#else
	return performHealthRequest(url, timeoutMs);
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
