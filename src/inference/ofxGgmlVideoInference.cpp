#include "ofxGgmlVideoInference.h"
#include "ofxGgmlVisionInference.h"
#ifndef OFXGGML_HEADLESS_STUBS
#include "ofJson.h"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <random>
#include <sstream>
#include <unordered_set>

#ifndef OFXGGML_HEADLESS_STUBS
#include "ofImage.h"
#include "ofVideoPlayer.h"
#endif

namespace {

constexpr int kMaxVideoFrameDimension = 1024;

struct NormalizedVideoRequest {
	ofxGgmlVideoTask task = ofxGgmlVideoTask::Summarize;
	std::string videoPath;
	std::string prompt;
	std::string systemPrompt;
	std::string responseLanguage;
	std::string sidecarUrl;
	std::string sidecarModel;
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
				escaped += hex[(c >> 4) & 0x0F];
				escaped += hex[c & 0x0F];
			} else {
				escaped += static_cast<char>(c);
			}
			break;
		}
	}
	return escaped;
}

std::filesystem::path makeFrameCacheDir(const std::string & videoPath) {
	std::error_code ec;
	std::filesystem::path base =
		std::filesystem::temp_directory_path(ec) / "ofxggml_video_frames";
	std::filesystem::create_directories(base, ec);

	const std::string stem = std::filesystem::path(videoPath).stem().string();
	const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
	std::mt19937_64 rng(static_cast<unsigned long long>(ticks));
	const auto suffix = std::to_string(rng());
	std::filesystem::path dir = base / (stem + "_" + suffix);
	std::filesystem::create_directories(dir, ec);
	return dir;
}

ofxGgmlVisionTask toVisionTask(ofxGgmlVideoTask task) {
	switch (task) {
	case ofxGgmlVideoTask::Summarize: return ofxGgmlVisionTask::Describe;
	case ofxGgmlVideoTask::Ocr: return ofxGgmlVisionTask::Ocr;
	case ofxGgmlVideoTask::Ask: return ofxGgmlVisionTask::Ask;
	case ofxGgmlVideoTask::Action: return ofxGgmlVisionTask::Ask;
	case ofxGgmlVideoTask::Emotion: return ofxGgmlVisionTask::Ask;
	}
	return ofxGgmlVisionTask::Describe;
}

std::string describeFramePosition(size_t index, size_t count) {
	if (count <= 1) {
		return "Only sampled frame";
	}
	if (index == 0) {
		return "Opening frame";
	}
	if (index + 1 == count) {
		return "Closing frame";
	}
	const double ratio = static_cast<double>(index) / static_cast<double>(count - 1);
	if (ratio < 0.34) {
		return "Early frame";
	}
	if (ratio > 0.66) {
		return "Late frame";
	}
	return "Middle frame";
}

NormalizedVideoRequest normalizeVideoRequest(const ofxGgmlVideoRequest & request) {
	NormalizedVideoRequest normalized;
	normalized.task = request.task;
	normalized.videoPath = trimCopy(request.videoPath);
	normalized.prompt = trimCopy(request.prompt);
	if (normalized.prompt.empty()) {
		normalized.prompt = ofxGgmlVideoInference::defaultPromptForTask(request.task);
	}
	normalized.systemPrompt = trimCopy(request.systemPrompt);
	if (normalized.systemPrompt.empty()) {
		normalized.systemPrompt = ofxGgmlVideoInference::defaultSystemPromptForTask(request.task);
	}
	normalized.responseLanguage = trimCopy(request.responseLanguage);
	normalized.sidecarUrl = trimCopy(request.sidecarUrl);
	normalized.sidecarModel = trimCopy(request.sidecarModel);
	return normalized;
}

bool isCompactClassificationTask(ofxGgmlVideoTask task) {
	return task == ofxGgmlVideoTask::Action ||
		task == ofxGgmlVideoTask::Emotion;
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
		std::string key = trimmed;
		std::transform(key.begin(), key.end(), key.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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

} // namespace

ofxGgmlVideoInference::ofxGgmlVideoInference()
	: m_backend(createSampledFramesBackend()) {
}

const char * ofxGgmlVideoInference::taskLabel(ofxGgmlVideoTask task) {
	switch (task) {
	case ofxGgmlVideoTask::Summarize: return "Summarize";
	case ofxGgmlVideoTask::Ocr: return "OCR";
	case ofxGgmlVideoTask::Ask: return "Ask";
	case ofxGgmlVideoTask::Action: return "Action";
	case ofxGgmlVideoTask::Emotion: return "Emotion";
	}
	return "Summarize";
}

std::string ofxGgmlVideoInference::defaultPromptForTask(ofxGgmlVideoTask task) {
	switch (task) {
	case ofxGgmlVideoTask::Summarize:
		return "Summarize the video from the sampled frames. Describe the main actions, scene changes, visible text, and what changes over time. Use a concise timeline when it helps.";
	case ofxGgmlVideoTask::Ocr:
		return "Extract visible on-screen text from the sampled frames. Group the text by timestamp, preserve useful line breaks, and avoid inventing unreadable content.";
	case ofxGgmlVideoTask::Ask:
		return "Answer the user's question about the sampled video frames. Use temporal order when it matters and mention if the answer may depend on unsampled gaps.";
	case ofxGgmlVideoTask::Action:
		return "Identify the primary action in this video clip. Return one compact answer with primary action, evidence, confidence, and uncertainty.";
	case ofxGgmlVideoTask::Emotion:
		return "Identify the dominant emotion in this video clip. Return one compact answer with primary emotion, evidence, confidence, and uncertainty.";
	}
	return {};
}

std::string ofxGgmlVideoInference::defaultSystemPromptForTask(ofxGgmlVideoTask task) {
	switch (task) {
	case ofxGgmlVideoTask::Summarize:
		return "You are a precise video understanding assistant. Infer only what is supported by the sampled frames and mention uncertainty when unseen gaps could matter.";
	case ofxGgmlVideoTask::Ocr:
		return "You are a video OCR assistant. Extract text faithfully from sampled frames, preserve useful timestamp structure, and avoid guessing unreadable characters.";
	case ofxGgmlVideoTask::Ask:
		return "You are a grounded video assistant. Answer only from the sampled frames, use timestamps when helpful, and say when coverage is incomplete.";
	case ofxGgmlVideoTask::Action:
		return "You are a temporal action recognition assistant. Classify only actions supported by the observed clip and explain the evidence briefly.";
	case ofxGgmlVideoTask::Emotion:
		return "You are a multimodal emotion analysis assistant. Infer only emotions supported by visible evidence and keep uncertainty explicit.";
	}
	return {};
}

std::string ofxGgmlVideoInference::formatTimestamp(double seconds) {
	if (!std::isfinite(seconds) || seconds < 0.0) {
		seconds = 0.0;
	}
	const int total = static_cast<int>(std::round(seconds));
	const int hours = total / 3600;
	const int minutes = (total % 3600) / 60;
	const int secs = total % 60;

	std::ostringstream out;
	if (hours > 0) {
		out << hours << ":";
		out << (minutes < 10 ? "0" : "") << minutes << ":";
		out << (secs < 10 ? "0" : "") << secs;
	} else {
		out << minutes << ":";
		out << (secs < 10 ? "0" : "") << secs;
	}
	return out.str();
}

std::vector<double> ofxGgmlVideoInference::buildSampleTimeline(
	double durationSeconds,
	int maxFrames,
	double startSeconds,
	double endSeconds,
	double minFrameSpacingSeconds) {
	std::vector<double> timeline;
	if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0 || maxFrames <= 0) {
		return timeline;
	}

	double start = std::clamp(std::isfinite(startSeconds) ? startSeconds : 0.0, 0.0, durationSeconds);
	double end = durationSeconds;
	if (std::isfinite(endSeconds) && endSeconds > 0.0) {
		end = std::clamp(endSeconds, start, durationSeconds);
	}
	if (end < start) {
		std::swap(start, end);
	}

	const double window = std::max(0.0, end - start);
	if (window <= 0.0) {
		timeline.push_back(start);
		return timeline;
	}

	int count = std::max(1, maxFrames);
	if (minFrameSpacingSeconds > 0.0) {
		const int spacingLimited = static_cast<int>(std::floor(window / minFrameSpacingSeconds)) + 1;
		count = std::min(count, std::max(1, spacingLimited));
	}

	if (count == 1) {
		timeline.push_back(start + window * 0.5);
		return timeline;
	}

	timeline.reserve(static_cast<size_t>(count));
	for (int i = 0; i < count; ++i) {
		const double t = start + (window * static_cast<double>(i) / static_cast<double>(count - 1));
		timeline.push_back(std::clamp(t, start, end));
	}
	return timeline;
}

std::string ofxGgmlVideoInference::buildFrameAwarePrompt(
	const ofxGgmlVideoRequest & request,
	const std::vector<ofxGgmlSampledVideoFrame> & frames) {
	const NormalizedVideoRequest normalized = normalizeVideoRequest(request);
	std::ostringstream prompt;
	prompt << normalized.prompt;
	prompt << "\n\nThese are sampled frames from a video, ordered from earlier to later.";
	prompt << "\nSample count: " << frames.size() << " frame(s).";
	if (request.startSeconds > 0.0 || request.endSeconds > 0.0) {
		prompt << "\nRequested clip window: "
			<< formatTimestamp(request.startSeconds) << " to "
			<< (request.endSeconds > 0.0 ? formatTimestamp(request.endSeconds) : std::string("end"));
	}
	if (request.includeTimestamps && !frames.empty()) {
		prompt << "\nTimeline:";
		for (size_t i = 0; i < frames.size(); ++i) {
			prompt << "\n- Frame " << (i + 1) << " at "
				<< formatTimestamp(frames[i].timestampSeconds)
				<< " [" << describeFramePosition(i, frames.size()) << "]";
			if (!trimCopy(frames[i].label).empty()) {
				prompt << " (" << frames[i].label << ")";
			}
		}
	}
	switch (request.task) {
	case ofxGgmlVideoTask::Summarize:
		prompt << "\nUse the frame order and timestamps to explain what changes over time. Prefer a professional summary with a brief timeline or bullet sequence when useful.";
		break;
	case ofxGgmlVideoTask::Ocr:
		prompt << "\nGroup extracted text by frame and timestamp. Ignore unreadable regions instead of inventing missing words.";
		break;
	case ofxGgmlVideoTask::Ask:
		prompt << "\nAnswer from the sampled evidence only. If the question depends on unseen moments between frames, say so explicitly.";
		break;
	case ofxGgmlVideoTask::Action:
		prompt << "\nReturn exactly four short lines:\n"
			<< "Primary action: <one phrase>\n"
			<< "Evidence: <visible cue from the sampled frames>\n"
			<< "Confidence: <low|medium|high>\n"
			<< "Uncertainty: <what the sampled frames cannot prove>\n"
			<< "Stop after these four lines. Do not repeat any sentence.";
		break;
	case ofxGgmlVideoTask::Emotion:
		prompt << "\nReturn exactly four short lines:\n"
			<< "Primary emotion: <one emotion or uncertain>\n"
			<< "Evidence: <face, posture, gesture, or context cue>\n"
			<< "Confidence: <low|medium|high>\n"
			<< "Uncertainty: <what the sampled frames cannot prove>\n"
			<< "Stop after these four lines. Do not repeat any sentence.";
		break;
	}
	return prompt.str();
}

std::string ofxGgmlVideoInference::normalizeSidecarUrl(const std::string & sidecarUrl) {
	std::string normalized = trimCopy(sidecarUrl);
	if (normalized.empty()) {
		return "http://127.0.0.1:8090/analyze";
	}
	if (normalized.find("/analyze") != std::string::npos) {
		return normalized;
	}
	if (!normalized.empty() && normalized.back() == '/') {
		normalized.pop_back();
	}
	return normalized + "/analyze";
}

std::string ofxGgmlVideoInference::buildTemporalSidecarJson(
	const ofxGgmlVideoRequest & request,
	const std::vector<ofxGgmlSampledVideoFrame> & frames) {
	const NormalizedVideoRequest normalized = normalizeVideoRequest(request);
	std::ostringstream json;
	json << "{";
	json << "\"task\":\"" << jsonEscape(taskLabel(request.task)) << "\",";
	if (!normalized.sidecarModel.empty()) {
		json << "\"model\":\"" << jsonEscape(normalized.sidecarModel) << "\",";
	}
	json << "\"video_path\":\"" << jsonEscape(normalized.videoPath) << "\",";
	json << "\"prompt\":\"" << jsonEscape(normalized.prompt) << "\",";
	json << "\"system_prompt\":\"" << jsonEscape(normalized.systemPrompt) << "\",";
	json << "\"response_language\":\"" << jsonEscape(normalized.responseLanguage) << "\",";
	json << "\"max_tokens\":" << request.maxTokens << ",";
	json << "\"temperature\":" << request.temperature << ",";
	json << "\"sampled_frames\":[";
	for (size_t i = 0; i < frames.size(); ++i) {
		if (i > 0) json << ",";
		json << "{";
		json << "\"path\":\"" << jsonEscape(frames[i].imagePath) << "\",";
		json << "\"label\":\"" << jsonEscape(frames[i].label) << "\",";
		json << "\"timestamp_seconds\":" << frames[i].timestampSeconds;
		json << "}";
	}
	json << "],";
	json << "\"output_schema\":{";
	json << "\"primary_label\":\"string\",";
	json << "\"confidence\":\"number_0_to_1\",";
	json << "\"secondary_labels\":\"string[]\",";
	json << "\"timeline\":\"string[]\",";
	json << "\"evidence\":\"string[]\",";
	json << "\"valence\":\"string_optional\",";
	json << "\"arousal\":\"string_optional\",";
	json << "\"notes\":\"string_optional\"";
	json << "}";
	json << "}";
	return json.str();
}

std::shared_ptr<ofxGgmlVideoBackend> ofxGgmlVideoInference::createSampledFramesBackend() {
	return std::make_shared<ofxGgmlSampledFramesVideoBackend>();
}

void ofxGgmlVideoInference::setBackend(std::shared_ptr<ofxGgmlVideoBackend> backend) {
	m_backend = backend ? std::move(backend) : createSampledFramesBackend();
}

std::shared_ptr<ofxGgmlVideoBackend> ofxGgmlVideoInference::getBackend() const {
	return m_backend;
}

std::string ofxGgmlSampledFramesVideoBackend::backendName() const {
	return "SampledFrames";
}

ofxGgmlVideoBackendSampleResult ofxGgmlSampledFramesVideoBackend::sampleFrames(
	const ofxGgmlVideoRequest & request) const {
	ofxGgmlVideoBackendSampleResult result;
	result.backendName = backendName();

	const NormalizedVideoRequest normalized = normalizeVideoRequest(request);
	if (normalized.videoPath.empty()) {
		result.error = "no video was provided";
		return result;
	}

	std::error_code ec;
	if (!std::filesystem::exists(std::filesystem::path(normalized.videoPath), ec) || ec) {
		result.error = "video file not found: " + normalized.videoPath;
		return result;
	}

#ifdef OFXGGML_HEADLESS_STUBS
	result.error = "video sampling requires openFrameworks video runtime";
	return result;
#else
	ofVideoPlayer player;
	if (!player.load(normalized.videoPath)) {
		result.error = "failed to load video: " + normalized.videoPath;
		return result;
	}

	player.play();
	player.setPaused(true);
	player.update();

	double duration = static_cast<double>(player.getDuration());
	const int totalFrames = player.getTotalNumFrames();
	if ((!std::isfinite(duration) || duration <= 0.0) && totalFrames > 0) {
		duration = std::max(1.0, static_cast<double>(totalFrames) / 30.0);
	}
	if (!std::isfinite(duration) || duration <= 0.0) {
		result.error = "video duration is unavailable";
		return result;
	}

	const std::vector<double> timeline = ofxGgmlVideoInference::buildSampleTimeline(
		duration,
		request.maxFrames,
		request.startSeconds,
		request.endSeconds,
		request.minFrameSpacingSeconds);
	if (timeline.empty()) {
		result.error = "could not determine sampling timeline";
		return result;
	}

	const std::filesystem::path frameDir = makeFrameCacheDir(normalized.videoPath);
	std::vector<ofxGgmlSampledVideoFrame> frames;
	frames.reserve(timeline.size());
	for (size_t i = 0; i < timeline.size(); ++i) {
		const double timestamp = timeline[i];
		if (totalFrames > 0 && duration > 0.0) {
			const int frameIndex = std::clamp(
				static_cast<int>(std::llround((timestamp / duration) * static_cast<double>(totalFrames - 1))),
				0,
				std::max(0, totalFrames - 1));
			player.setFrame(frameIndex);
		} else {
			player.setPosition(static_cast<float>(std::clamp(timestamp / duration, 0.0, 1.0)));
		}
		player.update();

		const ofPixels & pixels = player.getPixels();
		if (!pixels.isAllocated()) {
			result.error = "failed to decode video frame at " + ofxGgmlVideoInference::formatTimestamp(timestamp);
			return result;
		}

		ofImage frameImage;
		frameImage.setFromPixels(pixels);
		const int width = frameImage.getWidth();
		const int height = frameImage.getHeight();
		const int maxDimension = std::max(width, height);
		if (maxDimension > kMaxVideoFrameDimension && width > 0 && height > 0) {
			const float scale =
				static_cast<float>(kMaxVideoFrameDimension) /
				static_cast<float>(maxDimension);
			const int resizedWidth = std::max(1, static_cast<int>(std::round(width * scale)));
			const int resizedHeight = std::max(1, static_cast<int>(std::round(height * scale)));
			frameImage.resize(resizedWidth, resizedHeight);
		}

		std::ostringstream name;
		name << "frame_" << i << ".png";
		const std::filesystem::path framePath = frameDir / name.str();
		if (!ofSaveImage(frameImage.getPixels(), framePath, OF_IMAGE_QUALITY_BEST)) {
			result.error = "failed to save sampled frame image";
			return result;
		}

		ofxGgmlSampledVideoFrame frame;
		frame.imagePath = framePath.string();
		frame.timestampSeconds = timestamp;
		frame.label = describeFramePosition(i, timeline.size()) +
			" at " + ofxGgmlVideoInference::formatTimestamp(timestamp);
		frames.push_back(frame);
	}

	player.stop();
	result.success = true;
	result.sampledFrames = std::move(frames);
	return result;
#endif
}

std::vector<ofxGgmlSampledVideoFrame> ofxGgmlVideoInference::sampleFrames(
	const ofxGgmlVideoRequest & request,
	std::string & error) const {
	const auto backend = m_backend ? m_backend : createSampledFramesBackend();
	const auto sampled = backend->sampleFrames(request);
	error = sampled.error;
	return sampled.sampledFrames;
}

ofxGgmlVideoResult ofxGgmlVideoInference::runServerRequest(
	const ofxGgmlVisionModelProfile & profile,
	const ofxGgmlVideoRequest & request) const {
	const auto backend = m_backend ? m_backend : createSampledFramesBackend();
	const ofxGgmlVideoBackendSampleResult sampled = backend->sampleFrames(request);
	if (!sampled.success) {
		ofxGgmlVideoResult result;
		result.backendName = sampled.backendName;
		result.sampledFrames = sampled.sampledFrames;
		result.error = sampled.error;
		return result;
	}
	ofxGgmlVideoResult result = runServerRequest(profile, request, sampled.sampledFrames);
	result.backendName = sampled.backendName;
	return result;
}

ofxGgmlVideoResult ofxGgmlVideoInference::runServerRequest(
	const ofxGgmlVisionModelProfile & profile,
	const ofxGgmlVideoRequest & request,
	const std::vector<ofxGgmlSampledVideoFrame> & sampledFrames) const {
	ofxGgmlVideoResult result;
	const auto t0 = std::chrono::steady_clock::now();
	const NormalizedVideoRequest normalized = normalizeVideoRequest(request);

	result.backendName = "SampledFrames";
	result.sampledFrames = sampledFrames;
	if (result.sampledFrames.empty()) {
		result.error = "no frames were sampled from the video";
		return result;
	}

	ofxGgmlVisionRequest visionRequest;
	visionRequest.task = toVisionTask(request.task);
	visionRequest.prompt = buildFrameAwarePrompt(request, result.sampledFrames);
	visionRequest.systemPrompt = normalized.systemPrompt;
	visionRequest.responseLanguage = normalized.responseLanguage;
	visionRequest.maxTokens = isCompactClassificationTask(request.task)
		? std::min(std::max(32, request.maxTokens), 96)
		: request.maxTokens;
	visionRequest.temperature = isCompactClassificationTask(request.task)
		? std::min(request.temperature, 0.15f)
		: request.temperature;
	visionRequest.images.reserve(result.sampledFrames.size());
	for (const auto & frame : result.sampledFrames) {
		visionRequest.images.push_back({
			frame.imagePath,
			frame.label,
			"image/png"
		});
	}

	ofxGgmlVisionInference visionInference;
	result.visionResult = visionInference.runServerRequest(profile, visionRequest);
	result.elapsedMs = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - t0).count();
	if (!result.visionResult.success) {
		result.error = result.visionResult.error;
		return result;
	}

	result.success = true;
	result.text = isCompactClassificationTask(request.task)
		? collapseRepeatedSentences(result.visionResult.text)
		: result.visionResult.text;
	return result;
}

ofxGgmlVideoResult ofxGgmlVideoInference::runTemporalSidecarRequest(
	const ofxGgmlVideoRequest & request) const {
	const auto backend = m_backend ? m_backend : createSampledFramesBackend();
	const ofxGgmlVideoBackendSampleResult sampled = backend->sampleFrames(request);
	if (!sampled.success) {
		ofxGgmlVideoResult result;
		result.backendName = "TemporalSidecar+" + sampled.backendName;
		result.sampledFrames = sampled.sampledFrames;
		result.error = sampled.error;
		return result;
	}
	ofxGgmlVideoResult result = runTemporalSidecarRequest(request, sampled.sampledFrames);
	result.backendName = "TemporalSidecar+" + sampled.backendName;
	return result;
}

ofxGgmlVideoResult ofxGgmlVideoInference::runTemporalSidecarRequest(
	const ofxGgmlVideoRequest & request,
	const std::vector<ofxGgmlSampledVideoFrame> & sampledFrames) const {
	ofxGgmlVideoResult result;
	const auto t0 = std::chrono::steady_clock::now();

	result.backendName = "TemporalSidecar+SampledFrames";
	result.sampledFrames = sampledFrames;
	if (result.sampledFrames.empty()) {
		result.error = "no frames were sampled from the video";
		return result;
	}

	result.usedServerUrl = normalizeSidecarUrl(request.sidecarUrl);
	result.requestJson = buildTemporalSidecarJson(request, result.sampledFrames);

#ifdef OFXGGML_HEADLESS_STUBS
	result.error = "temporal sidecar requests require openFrameworks runtime";
	return result;
#else
	try {
		ofHttpRequest httpRequest(result.usedServerUrl, "video-temporal-sidecar");
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
			result.error = "temporal sidecar failed with HTTP " +
				ofToString(httpResponse.status) + ": " + detail;
			return result;
		}

		const ofJson parsed = ofJson::parse(result.responseJson);
		const ofJson payload = parsed.contains("result") ? parsed["result"] : parsed;
		result.structured.analysisType = trimCopy(payload.value("analysis_type", std::string()));
		result.structured.primaryLabel = trimCopy(payload.value("primary_label", std::string()));
		result.structured.confidence = payload.value("confidence", -1.0f);
		result.structured.valence = trimCopy(payload.value("valence", std::string()));
		result.structured.arousal = trimCopy(payload.value("arousal", std::string()));
		result.structured.notes = trimCopy(payload.value("notes", std::string()));
		if (payload.contains("secondary_labels") && payload["secondary_labels"].is_array()) {
			for (const auto & item : payload["secondary_labels"]) {
				if (item.is_string()) {
					result.structured.secondaryLabels.push_back(trimCopy(item.get<std::string>()));
				}
			}
		}
		if (payload.contains("timeline") && payload["timeline"].is_array()) {
			for (const auto & item : payload["timeline"]) {
				if (item.is_string()) {
					result.structured.timeline.push_back(trimCopy(item.get<std::string>()));
				}
			}
		}
		if (payload.contains("evidence") && payload["evidence"].is_array()) {
			for (const auto & item : payload["evidence"]) {
				if (item.is_string()) {
					result.structured.evidence.push_back(trimCopy(item.get<std::string>()));
				}
			}
		}

		std::ostringstream out;
		const std::string title = request.task == ofxGgmlVideoTask::Emotion
			? "Emotion analysis"
			: "Action analysis";
		out << title << "\n";
		if (!result.structured.primaryLabel.empty()) {
			out << "Primary: " << result.structured.primaryLabel;
			if (result.structured.confidence >= 0.0f) {
				out << " (" << ofToString(std::round(result.structured.confidence * 100.0f)) << "%)";
			}
			out << "\n";
		}
		if (!result.structured.secondaryLabels.empty()) {
			out << "Secondary: ";
			for (size_t i = 0; i < result.structured.secondaryLabels.size(); ++i) {
				if (i > 0) out << ", ";
				out << result.structured.secondaryLabels[i];
			}
			out << "\n";
		}
		if (!result.structured.valence.empty() || !result.structured.arousal.empty()) {
			out << "Valence/Arousal: "
				<< (result.structured.valence.empty() ? "-" : result.structured.valence)
				<< " / "
				<< (result.structured.arousal.empty() ? "-" : result.structured.arousal)
				<< "\n";
		}
		if (!result.structured.evidence.empty()) {
			out << "\nEvidence:\n";
			for (const auto & item : result.structured.evidence) {
				out << "- " << item << "\n";
			}
		}
		if (!result.structured.timeline.empty()) {
			out << "\nTimeline:\n";
			for (const auto & item : result.structured.timeline) {
				out << "- " << item << "\n";
			}
		}
		if (!result.structured.notes.empty()) {
			out << "\nNotes:\n" << result.structured.notes;
		}

		result.text = trimCopy(out.str());
		if (result.text.empty()) {
			result.error = "temporal sidecar returned no usable analysis";
			return result;
		}

		result.success = true;
		result.elapsedMs = std::chrono::duration<float, std::milli>(
			std::chrono::steady_clock::now() - t0).count();
		return result;
	} catch (const std::exception & e) {
		result.error = std::string("temporal sidecar parse failed: ") + e.what();
		return result;
	} catch (...) {
		result.error = "temporal sidecar parse failed";
		return result;
	}
#endif
}
