#include "ofxGgmlLiveSpeechTranscriber.h"

#include "ofMain.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <sstream>

namespace {

std::string trimLiveSpeechCopy(const std::string & value) {
	size_t start = 0;
	while (start < value.size() &&
		std::isspace(static_cast<unsigned char>(value[start]))) {
		++start;
	}
	size_t end = value.size();
	while (end > start &&
		std::isspace(static_cast<unsigned char>(value[end - 1]))) {
		--end;
	}
	return value.substr(start, end - start);
}

std::string flattenSpeechSegmentText(
	const std::vector<ofxGgmlSpeechSegment> & segments) {
	std::ostringstream out;
	for (const auto & segment : segments) {
		const std::string text = trimLiveSpeechCopy(segment.text);
		if (text.empty()) {
			continue;
		}
		if (out.tellp() > 0) {
			out << ' ';
		}
		out << text;
	}
	return trimLiveSpeechCopy(out.str());
}

std::vector<std::string> splitWhitespaceWords(const std::string & text) {
	std::istringstream input(text);
	std::vector<std::string> words;
	std::string word;
	while (input >> word) {
		words.push_back(word);
	}
	return words;
}

std::string normalizeSpeechMergeWord(std::string word) {
	std::string normalized;
	normalized.reserve(word.size());
	for (const unsigned char c : word) {
		if (std::isalnum(c)) {
			normalized.push_back(static_cast<char>(std::tolower(c)));
		}
	}
	return normalized;
}

std::string joinWords(
	const std::vector<std::string> & words,
	size_t startIndex = 0) {
	if (startIndex >= words.size()) {
		return {};
	}
	std::ostringstream out;
	for (size_t i = startIndex; i < words.size(); ++i) {
		if (i > startIndex) {
			out << ' ';
		}
		out << words[i];
	}
	return out.str();
}

std::string appendSpeechTranscriptWithOverlap(
	const std::string & existingTranscript,
	const std::string & incomingTranscript) {
	const std::string trimmedIncoming = trimLiveSpeechCopy(incomingTranscript);
	if (trimmedIncoming.empty()) {
		return trimLiveSpeechCopy(existingTranscript);
	}
	const std::string trimmedExisting = trimLiveSpeechCopy(existingTranscript);
	if (trimmedExisting.empty()) {
		return trimmedIncoming;
	}

	const std::vector<std::string> existingWords = splitWhitespaceWords(trimmedExisting);
	const std::vector<std::string> incomingWords = splitWhitespaceWords(trimmedIncoming);
	if (existingWords.empty() || incomingWords.empty()) {
		return trimLiveSpeechCopy(trimmedExisting + " " + trimmedIncoming);
	}

	const size_t maxOverlap = std::min<size_t>(
		12,
		std::min(existingWords.size(), incomingWords.size()));
	size_t bestOverlap = 0;
	for (size_t overlap = maxOverlap; overlap > 0; --overlap) {
		bool matches = true;
		for (size_t i = 0; i < overlap; ++i) {
			const std::string existingWord =
				normalizeSpeechMergeWord(existingWords[existingWords.size() - overlap + i]);
			const std::string incomingWord =
				normalizeSpeechMergeWord(incomingWords[i]);
			if (existingWord.empty() || incomingWord.empty() ||
				existingWord != incomingWord) {
				matches = false;
				break;
			}
		}
		if (matches) {
			bestOverlap = overlap;
			break;
		}
	}

	const std::string remainder = joinWords(incomingWords, bestOverlap);
	if (remainder.empty()) {
		return trimmedExisting;
	}
	return trimLiveSpeechCopy(trimmedExisting + " " + remainder);
}

struct SpeechExecutionPlan {
	ofxGgmlSpeechRequest request;
	std::string effectiveExecutable;
};

bool buildSpeechExecutionPlan(
	const ofxGgmlLiveSpeechSettings & settings,
	const std::string & audioPath,
	SpeechExecutionPlan & plan,
	std::string & errorMessage) {
	if (audioPath.empty()) {
		errorMessage = "Select an audio file first.";
		return false;
	}

	std::string effectiveExecutable =
		settings.executable.empty()
			? trimLiveSpeechCopy(settings.profile.executable)
			: trimLiveSpeechCopy(settings.executable);
	if (effectiveExecutable.empty()) {
		effectiveExecutable = "whisper-cli";
	}
	effectiveExecutable =
		ofxGgmlSpeechInference::resolveWhisperCliExecutable(effectiveExecutable);

	std::string effectiveModelPath = trimLiveSpeechCopy(settings.modelPath);
	if (effectiveModelPath.empty()) {
		effectiveModelPath = trimLiveSpeechCopy(settings.profile.modelPath);
	}
	if (effectiveModelPath.empty() &&
		!trimLiveSpeechCopy(settings.profile.modelFileHint).empty()) {
		const std::filesystem::path suggested =
			std::filesystem::path(ofToDataPath("models", true)) /
			trimLiveSpeechCopy(settings.profile.modelFileHint);
		std::error_code ec;
		if (std::filesystem::exists(suggested, ec) && !ec) {
			effectiveModelPath = suggested.string();
		}
	}

	plan.request.task = settings.task;
	plan.request.audioPath = audioPath;
	plan.request.modelPath = effectiveModelPath;
	plan.request.serverUrl = trimLiveSpeechCopy(settings.serverUrl);
	plan.request.serverModel = trimLiveSpeechCopy(settings.serverModel);
	plan.request.languageHint = trimLiveSpeechCopy(settings.languageHint);
	plan.request.prompt = trimLiveSpeechCopy(settings.prompt);
	plan.request.returnTimestamps = settings.returnTimestamps;
	plan.effectiveExecutable = effectiveExecutable;
	return true;
}

ofxGgmlSpeechResult executeSpeechExecutionPlan(
	const SpeechExecutionPlan & plan,
	const std::function<void(const std::string &)> & statusCallback,
	const std::function<void(ofLogLevel, const std::string &)> & logCallback) {
	ofxGgmlSpeechInference localInference;
	ofxGgmlSpeechResult result;
	bool attemptedServer = false;

	if (!plan.request.serverUrl.empty()) {
		attemptedServer = true;
		localInference.setBackend(
			ofxGgmlSpeechInference::createWhisperServerBackend(
				plan.request.serverUrl,
				plan.request.serverModel));
		if (statusCallback) {
			statusCallback("Calling speech server...");
		}
		result = localInference.transcribe(plan.request);
		if (!result.success && !plan.effectiveExecutable.empty() && logCallback) {
			logCallback(
				OF_LOG_WARNING,
				"Speech server failed, falling back to " + plan.effectiveExecutable +
					": " + result.error);
		}
	}

	if (!result.success) {
		localInference.setBackend(
			ofxGgmlSpeechInference::createWhisperCliBackend(
				plan.effectiveExecutable));
		if (statusCallback) {
			statusCallback(
				attemptedServer
					? "Falling back to " + plan.effectiveExecutable + "..."
					: "Running " + plan.effectiveExecutable + "...");
		}
		result = localInference.transcribe(plan.request);
	}

	return result;
}

std::string makeTempMicRecordingPath() {
	std::error_code ec;
	std::filesystem::path base = std::filesystem::temp_directory_path(ec);
	if (ec) {
		base = std::filesystem::current_path();
	}
	const auto now = std::chrono::system_clock::now();
	const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
		now.time_since_epoch()).count();
	const uint64_t nonceHi = static_cast<uint64_t>(std::random_device{}());
	const uint64_t nonceLo = static_cast<uint64_t>(std::random_device{}());
	const uint64_t nonce = (nonceHi << 32) | nonceLo;
	std::ostringstream name;
	name << "ofxggml_mic_" << millis << "_" << std::hex << nonce << ".wav";
	return (base / name.str()).string();
}

bool writeMonoWavFile(
	const std::string & path,
	const std::vector<float> & samples,
	int sampleRate) {
	if (path.empty() || samples.empty() || sampleRate <= 0) {
		return false;
	}
	std::ofstream out(path, std::ios::binary);
	if (!out.is_open()) {
		return false;
	}

	const uint16_t channels = 1;
	const uint16_t bitsPerSample = 16;
	const uint32_t byteRate = static_cast<uint32_t>(sampleRate) * channels * (bitsPerSample / 8);
	const uint16_t blockAlign = channels * (bitsPerSample / 8);
	const uint32_t dataSize = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
	const uint32_t riffSize = 36u + dataSize;

	auto writeU16 = [&](uint16_t value) {
		out.write(reinterpret_cast<const char *>(&value), sizeof(value));
	};
	auto writeU32 = [&](uint32_t value) {
		out.write(reinterpret_cast<const char *>(&value), sizeof(value));
	};

	out.write("RIFF", 4);
	writeU32(riffSize);
	out.write("WAVE", 4);
	out.write("fmt ", 4);
	writeU32(16);
	writeU16(1);
	writeU16(channels);
	writeU32(static_cast<uint32_t>(sampleRate));
	writeU32(byteRate);
	writeU16(blockAlign);
	writeU16(bitsPerSample);
	out.write("data", 4);
	writeU32(dataSize);

	for (float sample : samples) {
		const float clamped = std::clamp(sample, -1.0f, 1.0f);
		const int16_t pcm = static_cast<int16_t>(std::lrint(clamped * 32767.0f));
		out.write(reinterpret_cast<const char *>(&pcm), sizeof(pcm));
	}

	return out.good();
}

} // namespace

ofxGgmlLiveSpeechTranscriber::ofxGgmlLiveSpeechTranscriber() = default;

ofxGgmlLiveSpeechTranscriber::~ofxGgmlLiveSpeechTranscriber() {
	stopCapture(false);
	if (m_future.valid()) {
		m_future.wait();
	}
}

void ofxGgmlLiveSpeechTranscriber::setSettings(
	const ofxGgmlLiveSpeechSettings & settings) {
	std::lock_guard<std::mutex> lock(m_settingsMutex);
	m_settings = settings;
}

ofxGgmlLiveSpeechSettings ofxGgmlLiveSpeechTranscriber::getSettings() const {
	std::lock_guard<std::mutex> lock(m_settingsMutex);
	return m_settings;
}

void ofxGgmlLiveSpeechTranscriber::setLogCallback(
	std::function<void(ofLogLevel, const std::string &)> callback) {
	std::lock_guard<std::mutex> lock(m_stateMutex);
	m_logCallback = std::move(callback);
}

void ofxGgmlLiveSpeechTranscriber::beginCapture(bool clearTranscript) {
	drainFinishedTask();
	++m_generation;
	m_capturing = true;
	m_processedSamples = 0;
	m_nextRunTimeSeconds = ofGetElapsedTimef() + 0.2f;
	{
		std::lock_guard<std::mutex> audioLock(m_audioMutex);
		m_recordedSamples.clear();
	}
	{
		std::lock_guard<std::mutex> stateLock(m_stateMutex);
		if (clearTranscript) {
			m_transcript.clear();
			m_detectedLanguage.clear();
		}
		m_status = getSettings().enabled
			? "Listening for speech..."
			: "Start microphone recording to begin live transcription.";
	}
	if (!m_future.valid() ||
		m_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
		m_busy.store(false, std::memory_order_relaxed);
	}
}

void ofxGgmlLiveSpeechTranscriber::stopCapture(bool keepBufferedAudio) {
	m_capturing = false;
	m_nextRunTimeSeconds = 0.0f;
	if (!keepBufferedAudio) {
		std::lock_guard<std::mutex> lock(m_audioMutex);
		m_recordedSamples.clear();
	}
	const ofxGgmlLiveSpeechSettings settings = getSettings();
	std::lock_guard<std::mutex> stateLock(m_stateMutex);
	if (!settings.enabled) {
		m_status.clear();
	} else if (!m_busy.load(std::memory_order_relaxed)) {
		m_status = "Live transcription paused.";
	}
}

void ofxGgmlLiveSpeechTranscriber::reset(bool clearTranscript) {
	++m_generation;
	m_nextRunTimeSeconds = 0.0f;
	m_processedSamples = 0;
	if (!m_future.valid() ||
		m_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
		m_busy.store(false, std::memory_order_relaxed);
	}
	std::lock_guard<std::mutex> stateLock(m_stateMutex);
	if (clearTranscript) {
		m_transcript.clear();
		m_detectedLanguage.clear();
	}
	m_status = getSettings().enabled
		? (m_capturing
			? "Listening for speech..."
			: "Start microphone recording to begin live transcription.")
		: "";
}

void ofxGgmlLiveSpeechTranscriber::clearTranscript() {
	std::lock_guard<std::mutex> stateLock(m_stateMutex);
	m_transcript.clear();
	m_detectedLanguage.clear();
	const ofxGgmlLiveSpeechSettings settings = getSettings();
	m_status = settings.enabled
		? (m_capturing
			? "Listening for speech..."
			: "Start microphone recording to begin live transcription.")
		: "";
}

void ofxGgmlLiveSpeechTranscriber::appendMonoSamples(
	const std::vector<float> & samples) {
	if (samples.empty()) {
		return;
	}
	std::lock_guard<std::mutex> lock(m_audioMutex);
	m_recordedSamples.insert(
		m_recordedSamples.end(),
		samples.begin(),
		samples.end());
}

void ofxGgmlLiveSpeechTranscriber::appendMonoSamples(
	const float * samples,
	size_t sampleCount) {
	if (!samples || sampleCount == 0) {
		return;
	}
	std::lock_guard<std::mutex> lock(m_audioMutex);
	m_recordedSamples.insert(
		m_recordedSamples.end(),
		samples,
		samples + sampleCount);
}

void ofxGgmlLiveSpeechTranscriber::drainFinishedTask() {
	if (!m_future.valid() ||
		m_future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
		return;
	}
	try {
		m_future.get();
	} catch (const std::exception & e) {
		updateStatus(std::string("Live transcription failed: ") + e.what());
		m_busy.store(false, std::memory_order_relaxed);
	} catch (...) {
		updateStatus("Live transcription failed.");
		m_busy.store(false, std::memory_order_relaxed);
	}
}

void ofxGgmlLiveSpeechTranscriber::updateStatus(const std::string & status) {
	std::lock_guard<std::mutex> lock(m_stateMutex);
	m_status = status;
}

void ofxGgmlLiveSpeechTranscriber::update() {
	drainFinishedTask();

	const ofxGgmlLiveSpeechSettings settings = getSettings();
	if (!settings.enabled || !m_capturing ||
		m_busy.load(std::memory_order_relaxed)) {
		return;
	}

	const float intervalSeconds = std::clamp(settings.intervalSeconds, 0.5f, 5.0f);
	const float windowSeconds = std::clamp(settings.windowSeconds, 2.0f, 30.0f);
	const float overlapSeconds = std::clamp(
		settings.overlapSeconds,
		0.0f,
		std::max(0.0f, windowSeconds - 0.25f));
	const float nowSeconds = ofGetElapsedTimef();
	if (nowSeconds < m_nextRunTimeSeconds) {
		return;
	}

	const size_t sampleRate = static_cast<size_t>(std::max(1, settings.sampleRate));
	const size_t minChunkSamples = static_cast<size_t>(
		std::max(1.0f, std::min(intervalSeconds, 1.0f)) * static_cast<float>(sampleRate));
	const size_t maxWindowSamples = static_cast<size_t>(windowSeconds * static_cast<float>(sampleRate));
	const size_t overlapSamples = static_cast<size_t>(overlapSeconds * static_cast<float>(sampleRate));

	std::vector<float> chunkSamples;
	size_t capturedSampleCount = 0;
	{
		std::lock_guard<std::mutex> lock(m_audioMutex);
		capturedSampleCount = m_recordedSamples.size();
		if (capturedSampleCount <= m_processedSamples ||
			capturedSampleCount < minChunkSamples) {
			return;
		}
		const size_t startSample = m_processedSamples > overlapSamples
			? m_processedSamples - overlapSamples
			: 0;
		const size_t boundedStart = capturedSampleCount > maxWindowSamples
			? std::max(startSample, capturedSampleCount - maxWindowSamples)
			: startSample;
		if (capturedSampleCount <= boundedStart ||
			(capturedSampleCount - boundedStart) < minChunkSamples) {
			return;
		}
		chunkSamples.assign(
			m_recordedSamples.begin() + static_cast<std::ptrdiff_t>(boundedStart),
			m_recordedSamples.begin() + static_cast<std::ptrdiff_t>(capturedSampleCount));
	}

	const std::string chunkPath = makeTempMicRecordingPath();
	if (!writeMonoWavFile(chunkPath, chunkSamples, settings.sampleRate)) {
		updateStatus("Failed to write live transcription chunk.");
		m_nextRunTimeSeconds = nowSeconds + intervalSeconds;
		return;
	}

	const uint64_t generationId = m_generation.load(std::memory_order_relaxed);
	m_processedSamples = capturedSampleCount;
	m_nextRunTimeSeconds = nowSeconds + intervalSeconds;
	m_busy.store(true, std::memory_order_relaxed);
	updateStatus("Transcribing live audio...");

	m_future = std::async(
		std::launch::async,
		&ofxGgmlLiveSpeechTranscriber::runChunkTask,
		this,
		generationId,
		settings,
		chunkPath);
}

void ofxGgmlLiveSpeechTranscriber::runChunkTask(
	uint64_t generationId,
	ofxGgmlLiveSpeechSettings settings,
	std::string chunkPath) {
	auto statusCallback = [this, generationId](const std::string & status) {
		if (m_generation.load(std::memory_order_relaxed) != generationId) {
			return;
		}
		updateStatus(status);
	};

	const auto logCallback = [this](ofLogLevel level, const std::string & message) {
		std::function<void(ofLogLevel, const std::string &)> callback;
		{
			std::lock_guard<std::mutex> lock(m_stateMutex);
			callback = m_logCallback;
		}
		if (callback) {
			callback(level, message);
		}
	};

	SpeechExecutionPlan plan;
	std::string planError;
	if (!buildSpeechExecutionPlan(settings, chunkPath, plan, planError)) {
		if (m_generation.load(std::memory_order_relaxed) == generationId) {
			updateStatus(planError);
		}
		std::error_code removeEc;
		std::filesystem::remove(chunkPath, removeEc);
		m_busy.store(false, std::memory_order_relaxed);
		return;
	}

	const ofxGgmlSpeechResult result = executeSpeechExecutionPlan(
		plan,
		statusCallback,
		logCallback);

	if (m_generation.load(std::memory_order_relaxed) == generationId) {
		std::lock_guard<std::mutex> lock(m_stateMutex);
		if (result.success) {
			std::string text = trimLiveSpeechCopy(result.text);
			if (text.empty()) {
				text = flattenSpeechSegmentText(result.segments);
			}
			if (!text.empty()) {
				m_transcript = appendSpeechTranscriptWithOverlap(
					m_transcript,
					text);
				if (!result.detectedLanguage.empty()) {
					m_detectedLanguage = result.detectedLanguage;
				}
				m_status = "Live transcript updated via " + result.backendName + ".";
			} else {
				m_status = "Listening for speech...";
			}
		} else {
			m_status = "Live transcription failed: " + result.error;
		}
	}

	std::error_code removeEc;
	std::filesystem::remove(chunkPath, removeEc);
	m_busy.store(false, std::memory_order_relaxed);
}

ofxGgmlLiveSpeechSnapshot ofxGgmlLiveSpeechTranscriber::getSnapshot() const {
	ofxGgmlLiveSpeechSnapshot snapshot;
	const ofxGgmlLiveSpeechSettings settings = getSettings();
	snapshot.enabled = settings.enabled;
	snapshot.capturing = m_capturing;
	snapshot.busy = m_busy.load(std::memory_order_relaxed);
	{
		std::lock_guard<std::mutex> stateLock(m_stateMutex);
		snapshot.transcript = m_transcript;
		snapshot.status = m_status;
		snapshot.detectedLanguage = m_detectedLanguage;
	}
	{
		std::lock_guard<std::mutex> audioLock(m_audioMutex);
		if (settings.sampleRate > 0) {
			snapshot.bufferedSeconds =
				static_cast<double>(m_recordedSamples.size()) /
				static_cast<double>(settings.sampleRate);
		}
	}
	return snapshot;
}

bool ofxGgmlLiveSpeechTranscriber::isBusy() const {
	return m_busy.load(std::memory_order_relaxed);
}

bool ofxGgmlLiveSpeechTranscriber::isCapturing() const {
	return m_capturing;
}
