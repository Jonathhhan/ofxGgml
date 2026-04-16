#include "ofxGgmlVideoInference.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <random>
#include <sstream>

#ifndef OFXGGML_HEADLESS_STUBS
#include "ofImage.h"
#include "ofVideoPlayer.h"
#endif

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
	}
	return ofxGgmlVisionTask::Describe;
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
	}
	return "Summarize";
}

std::string ofxGgmlVideoInference::defaultPromptForTask(ofxGgmlVideoTask task) {
	switch (task) {
	case ofxGgmlVideoTask::Summarize:
		return "Summarize the video from the sampled frames. Describe the main actions, scene changes, visible text, and what happens over time.";
	case ofxGgmlVideoTask::Ocr:
		return "Extract visible on-screen text from the sampled frames. Group the text by timestamp and avoid inventing unreadable content.";
	case ofxGgmlVideoTask::Ask:
		return "Answer the user's question about the sampled video frames. Use temporal order when it matters.";
	}
	return {};
}

std::string ofxGgmlVideoInference::defaultSystemPromptForTask(ofxGgmlVideoTask task) {
	switch (task) {
	case ofxGgmlVideoTask::Summarize:
		return "You are a precise video understanding assistant. Infer only what is supported by the sampled frames and mention uncertainty when the clip may contain unseen gaps.";
	case ofxGgmlVideoTask::Ocr:
		return "You are a video OCR assistant. Extract text faithfully from sampled frames and preserve useful timestamp structure.";
	case ofxGgmlVideoTask::Ask:
		return "You are a grounded video assistant. Answer only from the sampled frames and use timestamps when helpful.";
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
	std::ostringstream prompt;
	const std::string userPrompt = trimCopy(request.prompt).empty()
		? defaultPromptForTask(request.task)
		: trimCopy(request.prompt);

	prompt << userPrompt;
	prompt << "\n\nThese are sampled frames from a video, ordered from earlier to later.";
	if (request.includeTimestamps && !frames.empty()) {
		prompt << "\nTimeline:";
		for (size_t i = 0; i < frames.size(); ++i) {
			prompt << "\n- Frame " << (i + 1) << " at "
				<< formatTimestamp(frames[i].timestampSeconds);
			if (!trimCopy(frames[i].label).empty()) {
				prompt << " (" << frames[i].label << ")";
			}
		}
	}
	prompt << "\nUse the frame order and timestamps to reason about what changes over time.";
	return prompt.str();
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

	const std::string videoPath = trimCopy(request.videoPath);
	if (videoPath.empty()) {
		result.error = "no video was provided";
		return result;
	}

	std::error_code ec;
	if (!std::filesystem::exists(std::filesystem::path(videoPath), ec) || ec) {
		result.error = "video file not found: " + videoPath;
		return result;
	}

#ifdef OFXGGML_HEADLESS_STUBS
	result.error = "video sampling requires openFrameworks video runtime";
	return result;
#else
	ofVideoPlayer player;
	if (!player.load(videoPath)) {
		result.error = "failed to load video: " + videoPath;
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

	const std::filesystem::path frameDir = makeFrameCacheDir(videoPath);
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

		std::ostringstream name;
		name << "frame_" << i << ".png";
		const std::filesystem::path framePath = frameDir / name.str();
		if (!ofSaveImage(pixels, framePath, OF_IMAGE_QUALITY_BEST)) {
			result.error = "failed to save sampled frame image";
			return result;
		}

		ofxGgmlSampledVideoFrame frame;
		frame.imagePath = framePath.string();
		frame.timestampSeconds = timestamp;
		frame.label = "Sample at " + ofxGgmlVideoInference::formatTimestamp(timestamp);
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
	ofxGgmlVideoResult result;
	const auto t0 = std::chrono::steady_clock::now();

	const auto backend = m_backend ? m_backend : createSampledFramesBackend();
	const ofxGgmlVideoBackendSampleResult sampled = backend->sampleFrames(request);
	result.backendName = sampled.backendName;
	result.sampledFrames = sampled.sampledFrames;
	if (!sampled.success) {
		result.error = sampled.error;
		return result;
	}
	if (result.sampledFrames.empty()) {
		result.error = "no frames were sampled from the video";
		return result;
	}

	ofxGgmlVisionRequest visionRequest;
	visionRequest.task = toVisionTask(request.task);
	visionRequest.prompt = buildFrameAwarePrompt(request, result.sampledFrames);
	visionRequest.systemPrompt = trimCopy(request.systemPrompt).empty()
		? defaultSystemPromptForTask(request.task)
		: trimCopy(request.systemPrompt);
	visionRequest.responseLanguage = request.responseLanguage;
	visionRequest.maxTokens = request.maxTokens;
	visionRequest.temperature = request.temperature;
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
	result.text = result.visionResult.text;
	return result;
}
