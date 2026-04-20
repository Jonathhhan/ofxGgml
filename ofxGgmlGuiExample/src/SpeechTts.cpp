#include "ofApp.h"

#include "utils/AudioHelpers.h"
#include "utils/ImGuiHelpers.h"
#include "utils/PathHelpers.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <unordered_set>
#include <vector>

namespace {

struct PiperVoiceOption {
	std::string label;
	std::string modelPath;
};

struct TtsPreviewUiLabels {
	const char * comboLabel;
	const char * pauseLabel;
	const char * resumeLabel;
	const char * playLabel;
	const char * restartLabel;
	const char * stopLabel;
	const char * filePrefix;
	const char * pausedStatus;
	const char * playingStatus;
	const char * restartedStatus;
	const char * stoppedStatus;
};

std::string normalizedTtsBackendId(const ofxGgmlTtsModelProfile & profile) {
	const std::string backendId = trim(profile.backendId);
	return backendId.empty() ? std::string("chatllm") : backendId;
}

bool usesPiperTtsBackend(const ofxGgmlTtsModelProfile & profile) {
	return normalizedTtsBackendId(profile) == "piper";
}

bool usesChatLlmTtsBackend(const ofxGgmlTtsModelProfile & profile) {
	return normalizedTtsBackendId(profile) == "chatllm";
}

bool pathLooksLikeChatLlmExecutable(const std::string & value) {
	return ofxGgmlPiperTtsAdapters::looksLikeChatLlmExecutable(value);
}

std::string summarizeArtifactLabel(const std::string & path, int fallbackIndex) {
	std::string label = ofFilePath::getBaseName(path);
	return label.empty() ? ("Audio " + std::to_string(fallbackIndex + 1)) : label;
}

std::vector<PiperVoiceOption> discoverPiperVoices(const std::string & preferredModelPath) {
	std::vector<PiperVoiceOption> voices;
	std::vector<std::filesystem::path> roots;
	std::unordered_set<std::string> seenRoots;
	std::unordered_set<std::string> seenModels;

	const auto appendRoot = [&](const std::filesystem::path & root) {
		if (root.empty()) {
			return;
		}
		std::error_code ec;
		const std::filesystem::path normalized =
			std::filesystem::weakly_canonical(root, ec);
		const std::string key = (ec ? root : normalized).lexically_normal().string();
		if (!key.empty() && seenRoots.insert(ofToLower(key)).second) {
			roots.push_back(ec ? root : normalized);
		}
	};

	const std::string trimmedPreferred = trim(preferredModelPath);
	if (!trimmedPreferred.empty()) {
		appendRoot(std::filesystem::path(trimmedPreferred).parent_path());
	}
	appendRoot(std::filesystem::path(ofToDataPath("models", true)));
	appendRoot(std::filesystem::path(ofToDataPath("models/piper", true)));
	appendRoot(std::filesystem::path(ofToDataPath("models/piper-voices", true)));

	for (const auto & root : roots) {
		std::error_code existsEc;
		if (!std::filesystem::exists(root, existsEc) || existsEc) {
			continue;
		}

		std::error_code iterEc;
		for (std::filesystem::recursive_directory_iterator it(root, iterEc), end;
			!iterEc && it != end;
			it.increment(iterEc)) {
			if (iterEc || !it->is_regular_file()) {
				continue;
			}
			const std::filesystem::path modelPath = it->path();
			if (ofToLower(modelPath.extension().string()) != ".onnx") {
				continue;
			}
			const std::string configPath = modelPath.string() + ".json";
			std::error_code configEc;
			if (!std::filesystem::exists(configPath, configEc) || configEc) {
				continue;
			}

			const std::string normalizedModel =
				modelPath.lexically_normal().string();
			if (!seenModels.insert(ofToLower(normalizedModel)).second) {
				continue;
			}

			PiperVoiceOption option;
			option.modelPath = normalizedModel;
			option.label = modelPath.stem().string();
			const std::string relativeLabel = std::filesystem::relative(modelPath, root, configEc).string();
			if (!configEc && !trim(relativeLabel).empty()) {
				option.label = relativeLabel;
			}
			voices.push_back(option);
		}
	}

	std::sort(
		voices.begin(),
		voices.end(),
		[](const PiperVoiceOption & a, const PiperVoiceOption & b) {
			return ofToLower(a.label) < ofToLower(b.label);
		});
	return voices;
}

template <typename EnsureLoadedFn, typename StopPlaybackFn>
void drawTtsPreviewControls(
	const bool generating,
	TtsPreviewState & previewState,
	const TtsPreviewUiLabels & labels,
	EnsureLoadedFn && ensureLoaded,
	StopPlaybackFn && stopPlayback) {
	auto & audioFiles = previewState.audioFiles;
	if (audioFiles.empty()) {
		return;
	}

	previewState.selectedAudioIndex = std::clamp(
		previewState.selectedAudioIndex,
		0,
		std::max(0, static_cast<int>(audioFiles.size()) - 1));
	if (audioFiles.size() > 1) {
		const std::string previewLabel = summarizeArtifactLabel(
			audioFiles[static_cast<size_t>(previewState.selectedAudioIndex)].path,
			previewState.selectedAudioIndex);
		if (ImGui::BeginCombo(labels.comboLabel, previewLabel.c_str())) {
			for (int i = 0; i < static_cast<int>(audioFiles.size()); ++i) {
				const std::string itemLabel =
					summarizeArtifactLabel(audioFiles[static_cast<size_t>(i)].path, i);
				const bool selected = (previewState.selectedAudioIndex == i);
				if (ImGui::Selectable(itemLabel.c_str(), selected)) {
					previewState.selectedAudioIndex = i;
					ensureLoaded(previewState.selectedAudioIndex, false);
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}

	const bool canPreviewAudio = !generating && !audioFiles.empty();
	ImGui::BeginDisabled(!canPreviewAudio);
	const bool audioLoaded = previewState.isAudioLoaded();
	const bool audioPlaying = previewState.isAudioPlaying();
	const char * playPauseLabel = audioPlaying
		? labels.pauseLabel
		: (previewState.isPlaybackPaused() ? labels.resumeLabel : labels.playLabel);
	if (ImGui::SmallButton(playPauseLabel)) {
		if (!audioLoaded) {
			ensureLoaded(previewState.selectedAudioIndex, true);
		} else if (audioPlaying) {
			previewState.pausePlayback();
			previewState.statusMessage = labels.pausedStatus;
		} else {
			previewState.resumePlayback();
			previewState.statusMessage = labels.playingStatus;
		}
	}
	ImGui::SameLine();
	if (ImGui::SmallButton(labels.restartLabel)) {
		if (ensureLoaded(previewState.selectedAudioIndex, false)) {
			previewState.restartPlayback();
			previewState.statusMessage = labels.restartedStatus;
		}
	}
	ImGui::SameLine();
	if (ImGui::SmallButton(labels.stopLabel)) {
		stopPlayback(false);
		previewState.statusMessage = labels.stoppedStatus;
	}
	ImGui::EndDisabled();
	ImGui::TextDisabled(
		"%s %s",
		labels.filePrefix,
		audioFiles[static_cast<size_t>(previewState.selectedAudioIndex)].path.c_str());
}

void mixPreviewStateIntoOutput(TtsPreviewState & previewState, ofSoundBuffer & output) {
	std::lock_guard<std::mutex> lock(previewState.playbackMutex);
	if (!previewState.playbackLoaded ||
		previewState.playbackPaused ||
		!previewState.playbackActive ||
		previewState.playbackChannels <= 0 ||
		previewState.playbackSampleRate <= 0 ||
		previewState.playbackSamples.empty()) {
		return;
	}

	const size_t outputChannels = std::max<size_t>(1, output.getNumChannels());
	const size_t outputFrames = output.getNumFrames();
	const size_t sourceChannels = static_cast<size_t>(previewState.playbackChannels);
	const size_t sourceFrames =
		previewState.playbackSamples.size() / sourceChannels;
	if (sourceFrames == 0) {
		previewState.playbackLoaded = false;
		previewState.playbackActive = false;
		previewState.playbackPaused = false;
		return;
	}

	const double outputSampleRate = static_cast<double>(
		std::max<size_t>(1, static_cast<size_t>(output.getSampleRate())));
	const double sourceToOutputStep =
		static_cast<double>(previewState.playbackSampleRate) /
		outputSampleRate;
	double position = previewState.playbackPositionFrames;

	for (size_t frame = 0; frame < outputFrames; ++frame) {
		if (position >= static_cast<double>(sourceFrames)) {
			previewState.playbackPositionFrames = static_cast<double>(sourceFrames);
			previewState.playbackActive = false;
			previewState.playbackPaused = false;
			return;
		}

		const size_t sourceFrame0 = static_cast<size_t>(position);
		const size_t sourceFrame1 = std::min(sourceFrame0 + 1, sourceFrames - 1);
		const float interpolation =
			static_cast<float>(position - static_cast<double>(sourceFrame0));
		for (size_t channel = 0; channel < outputChannels; ++channel) {
			const size_t sourceChannel =
				(sourceChannels == 1) ? 0 : std::min(channel, sourceChannels - 1);
			const float sample0 =
				previewState.playbackSamples[sourceFrame0 * sourceChannels + sourceChannel];
			const float sample1 =
				previewState.playbackSamples[sourceFrame1 * sourceChannels + sourceChannel];
			const float mixedSample =
				sample0 + ((sample1 - sample0) * interpolation);
			output[frame * outputChannels + channel] += mixedSample;
		}
		position += sourceToOutputStep;
	}

	previewState.playbackPositionFrames = position;
	if (position >= static_cast<double>(sourceFrames)) {
		previewState.playbackPositionFrames = static_cast<double>(sourceFrames);
		previewState.playbackActive = false;
		previewState.playbackPaused = false;
	}
}

} // namespace

ofxGgmlLiveSpeechSettings ofApp::makeLiveSpeechSettings() const {
	ofxGgmlLiveSpeechSettings settings;
	if (!speechProfiles.empty()) {
		const int clampedIndex = std::clamp(
			selectedSpeechProfileIndex,
			0,
			std::max(0, static_cast<int>(speechProfiles.size()) - 1));
		settings.profile = speechProfiles[static_cast<size_t>(clampedIndex)];
	}
	settings.executable = trim(speechExecutable);
	settings.modelPath = trim(speechModelPath);
	settings.serverUrl = trim(speechServerUrl);
	settings.serverModel = trim(speechServerModel);
	settings.prompt = trim(speechPrompt);
	settings.languageHint = trim(speechLanguageHint);
	settings.task = static_cast<ofxGgmlSpeechTask>(std::clamp(speechTaskIndex, 0, 1));
	settings.sampleRate = speechInputSampleRate;
	settings.intervalSeconds = speechLiveIntervalSeconds;
	settings.windowSeconds = speechLiveWindowSeconds;
	settings.overlapSeconds = speechLiveOverlapSeconds;
	settings.enabled = speechLiveTranscriptionEnabled;
	settings.returnTimestamps = false;
	return settings;
}

void ofApp::applyLiveSpeechTranscriberSettings() {
	speechLiveTranscriber.setSettings(makeLiveSpeechSettings());
	speechLiveTranscriber.setLogCallback(
		[this](ofLogLevel level, const std::string & message) {
			logWithLevel(level, message);
		});
}

void ofApp::audioIn(ofSoundBuffer & input) {
	if (!speechRecording) {
		return;
	}

	const size_t channels = std::max<size_t>(1, input.getNumChannels());
	const size_t frames = input.getNumFrames();
	if (milkdropPreviewFeedMicWhileRecording) {
		std::lock_guard<std::mutex> previewLock(milkdropPreviewAudioMutex);
		milkdropPreviewAudioSamples.assign(input.getBuffer().begin(), input.getBuffer().end());
		milkdropPreviewAudioFrames = static_cast<int>(frames);
		milkdropPreviewAudioChannels = static_cast<int>(channels);
	}
	std::vector<float> monoSamples;
	monoSamples.reserve(frames);
	{
		std::lock_guard<std::mutex> lock(speechRecordMutex);
		speechRecordedSamples.reserve(speechRecordedSamples.size() + frames);
		for (size_t frame = 0; frame < frames; ++frame) {
			float mono = 0.0f;
			for (size_t channel = 0; channel < channels; ++channel) {
				mono += input[frame * channels + channel];
			}
			mono /= static_cast<float>(channels);
			speechRecordedSamples.push_back(mono);
			monoSamples.push_back(mono);
		}
	}
	if (!monoSamples.empty()) {
		speechLiveTranscriber.appendMonoSamples(monoSamples);
	}
}

void ofApp::audioOut(ofSoundBuffer & output) {
	std::fill(output.getBuffer().begin(), output.getBuffer().end(), 0.0f);
	mixPreviewStateIntoOutput(ttsPanelPreview, output);
	mixPreviewStateIntoOutput(chatTtsPreview, output);
	mixPreviewStateIntoOutput(summarizeTtsPreview, output);
	mixPreviewStateIntoOutput(translateTtsPreview, output);
	mixPreviewStateIntoOutput(videoEssayTtsPreview, output);
	for (float & sample : output.getBuffer()) {
		sample = std::clamp(sample, -1.0f, 1.0f);
	}
}

bool ofApp::ensureSpeechInputStreamReady() {
	const bool configMatches =
		speechInputStreamConfigured &&
		speechInputStreamConfigSampleRate == speechInputSampleRate &&
		speechInputStreamConfigChannels == speechInputChannels &&
		speechInputStreamConfigBufferSize == speechInputBufferSize;
	if (configMatches && speechInputStream.getSoundStream() != nullptr) {
		return true;
	}

	if (speechInputStream.getSoundStream() != nullptr) {
		speechInputStream.stop();
		speechInputStream.close();
	}

	ofSoundStreamSettings settings;
	settings.setInListener(this);
	settings.sampleRate = speechInputSampleRate;
	settings.numInputChannels = speechInputChannels;
	settings.numOutputChannels = 0;
	settings.bufferSize = speechInputBufferSize;
	settings.numBuffers = 4;
	if (!speechInputStream.setup(settings)) {
		speechInputStreamConfigured = false;
		return false;
	}

	speechInputStreamConfigured = true;
	speechInputStreamConfigSampleRate = speechInputSampleRate;
	speechInputStreamConfigChannels = speechInputChannels;
	speechInputStreamConfigBufferSize = speechInputBufferSize;
	return true;
}

bool ofApp::ensureTtsOutputStreamReady() {
	if (ttsOutputStreamConfigured && ttsOutputStream.getSoundStream() != nullptr) {
		return true;
	}

	if (ttsOutputStream.getSoundStream() != nullptr) {
		ttsOutputStream.stop();
		ttsOutputStream.close();
	}

	ofSoundStreamSettings settings;
	settings.setOutListener(this);
	settings.sampleRate = ttsOutputSampleRate;
	settings.numInputChannels = 0;
	settings.numOutputChannels = ttsOutputChannels;
	settings.bufferSize = ttsOutputBufferSize;
	settings.numBuffers = 4;
	if (!ttsOutputStream.setup(settings)) {
		ttsOutputStreamConfigured = false;
		return false;
	}

	ttsOutputStreamConfigured = true;
	return true;
}

bool ofApp::startSpeechRecording() {
	if (speechRecording) {
		return true;
	}

	try {
		applyLiveSpeechTranscriberSettings();
		{
			std::lock_guard<std::mutex> lock(speechRecordMutex);
			speechRecordedSamples.clear();
			speechRecordedTempPath.clear();
		}
		if (!ensureSpeechInputStreamReady()) {
			logWithLevel(OF_LOG_ERROR, "Failed to open microphone input stream for speech mode.");
			return false;
		}
		speechRecording = true;
		speechRecordingStartTime = ofGetElapsedTimef();
		if (speechLiveTranscriptionEnabled) {
			speechLiveTranscriber.beginCapture(true);
		}
		logWithLevel(OF_LOG_NOTICE, "Started microphone recording for speech mode.");
		return true;
	} catch (const std::exception & e) {
		logWithLevel(OF_LOG_ERROR, std::string("Failed to start microphone recording: ") + e.what());
		return false;
	} catch (...) {
		logWithLevel(OF_LOG_ERROR, "Failed to start microphone recording.");
		return false;
	}
}

void ofApp::stopSpeechRecording(bool keepBufferedAudio) {
	if (speechRecording) {
		speechRecording = false;
		speechRecordingStartTime = 0.0f;
	}
	if (!keepBufferedAudio) {
		std::lock_guard<std::mutex> lock(speechRecordMutex);
		speechRecordedSamples.clear();
		speechRecordedTempPath.clear();
	}
	applyLiveSpeechTranscriberSettings();
	speechLiveTranscriber.stopCapture(keepBufferedAudio);
	if (!speechLiveTranscriptionEnabled) {
		speechLiveTranscriber.reset(!keepBufferedAudio);
	}
}

std::string ofApp::flushSpeechRecordingToTempWav() {
	std::vector<float> recorded;
	{
		std::lock_guard<std::mutex> lock(speechRecordMutex);
		recorded = speechRecordedSamples;
	}
	if (recorded.empty()) {
		return {};
	}
	if (speechRecordedTempPath.empty()) {
		speechRecordedTempPath = makeTempMicRecordingPath();
	}
	if (!writeMonoWavFile(speechRecordedTempPath, recorded, speechInputSampleRate)) {
		logWithLevel(OF_LOG_ERROR, "Failed to write microphone recording to WAV.");
		return {};
	}
	return speechRecordedTempPath;
}

bool ofApp::ensureTtsProfilesLoaded() {
	if (ttsProfiles.empty()) {
		ttsProfiles = ofxGgmlTtsInference::defaultProfiles();
		selectedTtsProfileIndex = 0;
	}
	if (ttsProfiles.empty()) {
		selectedTtsProfileIndex = 0;
		return false;
	}
	selectedTtsProfileIndex = std::clamp(
		selectedTtsProfileIndex,
		0,
		std::max(0, static_cast<int>(ttsProfiles.size()) - 1));
	return true;
}

ofxGgmlTtsModelProfile ofApp::getSelectedTtsProfile() const {
	if (ttsProfiles.empty()) {
		return {};
	}
	const int clampedIndex = std::clamp(
		selectedTtsProfileIndex,
		0,
		std::max(0, static_cast<int>(ttsProfiles.size()) - 1));
	return ttsProfiles[static_cast<size_t>(clampedIndex)];
}

void ofApp::applyTtsProfileDefaults(
	const ofxGgmlTtsModelProfile & profile,
	bool onlyWhenEmpty) {
	const std::string suggestedPath =
		suggestedModelPath(profile.modelPath, profile.modelFileHint);
	if (!suggestedPath.empty() &&
		(!onlyWhenEmpty || trim(ttsModelPath).empty())) {
		copyStringToBuffer(ttsModelPath, sizeof(ttsModelPath), suggestedPath);
	}
	const std::string suggestedSpeakerPath =
		suggestedModelPath(profile.speakerPath, profile.speakerFileHint);
	if (!suggestedSpeakerPath.empty() &&
		(!onlyWhenEmpty || trim(ttsSpeakerPath).empty())) {
		copyStringToBuffer(ttsSpeakerPath, sizeof(ttsSpeakerPath), suggestedSpeakerPath);
	}
	if (trim(ttsOutputPath).empty()) {
		copyStringToBuffer(
			ttsOutputPath,
			sizeof(ttsOutputPath),
			ofToDataPath("generated/tts_output.wav", true));
	}
}

std::string ofApp::resolveConfiguredTtsExecutable(
	const ofxGgmlTtsModelProfile & profile) const {
	if (usesPiperTtsBackend(profile)) {
		return ofxGgmlPiperTtsAdapters::resolvePiperExecutable(trim(ttsExecutablePath));
	}
	return ofxGgmlChatLlmTtsAdapters::resolveChatLlmExecutable(trim(ttsExecutablePath));
}

std::shared_ptr<ofxGgmlTtsBackend> ofApp::createConfiguredTtsBackend(
	const ofxGgmlTtsModelProfile & profile,
	const std::string & executableHint) const {
	const std::string configuredHint =
		executableHint.empty() ? trim(ttsExecutablePath) : executableHint;
	if (usesPiperTtsBackend(profile)) {
		ofxGgmlPiperTtsAdapters::RuntimeOptions runtimeOptions;
		runtimeOptions.executablePath = configuredHint;
		return ofxGgmlPiperTtsAdapters::createBackend(runtimeOptions, "Piper TTS");
	}

	ofxGgmlChatLlmTtsAdapters::RuntimeOptions runtimeOptions;
	runtimeOptions.executablePath = configuredHint;
	return ofxGgmlChatLlmTtsAdapters::createBackend(runtimeOptions, "ChatLLM TTS");
}

void ofApp::drawSpeechPanel() {
	drawPanelHeader("Speech", "audio transcription and translation via Whisper backends");
	const float compactModeFieldWidth = std::min(320.0f, ImGui::GetContentRegionAvail().x);

	if (speechProfiles.empty()) {
		speechProfiles = ofxGgmlSpeechInference::defaultProfiles();
	}
	selectedSpeechProfileIndex = std::clamp(
		selectedSpeechProfileIndex,
		0,
		std::max(0, static_cast<int>(speechProfiles.size()) - 1));

	if (!speechProfiles.empty()) {
		std::vector<const char *> profileNames;
		profileNames.reserve(speechProfiles.size());
		for (const auto & profile : speechProfiles) {
			profileNames.push_back(profile.name.c_str());
		}
		ImGui::SetNextItemWidth(260);
		if (ImGui::Combo(
			"Speech profile",
			&selectedSpeechProfileIndex,
			profileNames.data(),
			static_cast<int>(profileNames.size()))) {
			const auto & profile = speechProfiles[static_cast<size_t>(selectedSpeechProfileIndex)];
			if (!profile.executable.empty()) {
				copyStringToBuffer(speechExecutable, sizeof(speechExecutable), profile.executable);
			}
			const std::string suggestedPath =
				suggestedModelPath(profile.modelPath, profile.modelFileHint);
			if (!suggestedPath.empty()) {
				copyStringToBuffer(speechModelPath, sizeof(speechModelPath), suggestedPath);
			}
			if (!profile.supportsTranslate &&
				speechTaskIndex == static_cast<int>(ofxGgmlSpeechTask::Translate)) {
				speechTaskIndex = static_cast<int>(ofxGgmlSpeechTask::Transcribe);
			}
			autoSaveSession();
		}
	}

	if (hasDeferredSpeechAudioPath) {
		copyStringToBuffer(
			speechAudioPath,
			sizeof(speechAudioPath),
			deferredSpeechAudioPath);
		hasDeferredSpeechAudioPath = false;
		deferredSpeechAudioPath.clear();
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Audio path", speechAudioPath, sizeof(speechAudioPath));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse audio...", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select audio file", false);
		if (result.bSuccess) {
			copyStringToBuffer(speechAudioPath, sizeof(speechAudioPath), result.getPath());
			autoSaveSession();
		}
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Server URL", speechServerUrl, sizeof(speechServerUrl));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Server model", speechServerModel, sizeof(speechServerModel));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	if (!speechServerStatusMessage.empty()) {
		ImGui::TextDisabled("%s", speechServerStatusMessage.c_str());
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Executable", speechExecutable, sizeof(speechExecutable));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Model path", speechModelPath, sizeof(speechModelPath));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse model...", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select Whisper model", false);
		if (result.bSuccess) {
			copyStringToBuffer(speechModelPath, sizeof(speechModelPath), result.getPath());
			autoSaveSession();
		}
	}

	static const char * speechTaskLabels[] = {"Transcribe", "Translate"};
	ImGui::SetNextItemWidth(180);
	ImGui::Combo("Speech task", &speechTaskIndex, speechTaskLabels, 2);
	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Language hint", speechLanguageHint, sizeof(speechLanguageHint));
	ImGui::Checkbox("Return timestamps", &speechReturnTimestamps);
	ImGui::InputTextMultiline(
		"Speech prompt",
		speechPrompt,
		sizeof(speechPrompt),
		ImVec2(-1, 80));

	const bool liveToggleChanged =
		ImGui::Checkbox("Live mic transcription", &speechLiveTranscriptionEnabled);
	if (liveToggleChanged) {
		applyLiveSpeechTranscriberSettings();
		if (!speechLiveTranscriptionEnabled) {
			speechLiveTranscriber.reset(false);
		} else if (speechRecording) {
			speechLiveTranscriber.beginCapture(false);
		}
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(180);
	ImGui::SliderFloat("Live interval", &speechLiveIntervalSeconds, 0.5f, 5.0f, "%.2f s");
	ImGui::SetNextItemWidth(180);
	ImGui::SliderFloat("Live window", &speechLiveWindowSeconds, 2.0f, 30.0f, "%.1f s");
	ImGui::SetNextItemWidth(180);
	ImGui::SliderFloat("Live overlap", &speechLiveOverlapSeconds, 0.0f, 3.0f, "%.2f s");
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		applyLiveSpeechTranscriberSettings();
		autoSaveSession();
	}

	const bool hasBufferedMicAudio = [&]() {
		std::lock_guard<std::mutex> lock(speechRecordMutex);
		return !speechRecordedSamples.empty();
	}();
	if (!speechRecording) {
		if (ImGui::Button("Start Mic Recording", ImVec2(160, 0))) {
			startSpeechRecording();
		}
	} else {
		if (ImGui::Button("Stop Mic", ImVec2(160, 0))) {
			stopSpeechRecording(true);
		}
		ImGui::SameLine();
		ImGui::TextDisabled(
			"Recording: %.1f s",
			std::max(0.0f, ofGetElapsedTimef() - speechRecordingStartTime));
	}
	if (hasBufferedMicAudio) {
		ImGui::SameLine();
		if (ImGui::Button("Use Recording", ImVec2(140, 0))) {
			const std::string tempPath = flushSpeechRecordingToTempWav();
			if (!tempPath.empty()) {
				deferredSpeechAudioPath = tempPath;
				hasDeferredSpeechAudioPath = true;
				autoSaveSession();
			}
		}
	}

	const bool canRunSpeech =
		!generating.load() &&
		(std::strlen(speechAudioPath) > 0 || hasBufferedMicAudio);
	ImGui::BeginDisabled(!canRunSpeech);
	if (ImGui::Button("Run Speech", ImVec2(140, 0))) {
		if (std::strlen(speechAudioPath) == 0) {
			const std::string tempPath = flushSpeechRecordingToTempWav();
			if (!tempPath.empty()) {
				copyStringToBuffer(speechAudioPath, sizeof(speechAudioPath), tempPath);
			}
		}
		runSpeechInference();
	}
	ImGui::EndDisabled();

	const ofxGgmlLiveSpeechSnapshot liveSnapshot = speechLiveTranscriber.getSnapshot();
	if (speechLiveTranscriptionEnabled) {
		ImGui::Separator();
		ImGui::TextDisabled(
			"Live status: %s | buffered %.1f s%s",
			liveSnapshot.status.empty() ? "idle" : liveSnapshot.status.c_str(),
			static_cast<float>(liveSnapshot.bufferedSeconds),
			liveSnapshot.busy ? " | transcribing" : "");
		if (!liveSnapshot.detectedLanguage.empty()) {
			ImGui::TextDisabled("Detected language: %s", liveSnapshot.detectedLanguage.c_str());
		}
		ImGui::BeginChild("##SpeechLiveTranscript", ImVec2(0, 120), true);
		if (liveSnapshot.transcript.empty()) {
			ImGui::TextDisabled("Live transcript will appear here while recording.");
		} else {
			ImGui::TextWrapped("%s", liveSnapshot.transcript.c_str());
		}
		ImGui::EndChild();
	}

	ImGui::Separator();
	if (!speechDetectedLanguage.empty()) {
		ImGui::TextDisabled("Detected language: %s", speechDetectedLanguage.c_str());
	}
	if (!speechTranscriptPath.empty()) {
		ImGui::TextDisabled("Transcript file: %s", speechTranscriptPath.c_str());
	}
	if (!speechSrtPath.empty()) {
		ImGui::TextDisabled("SRT file: %s", speechSrtPath.c_str());
	}
	if (speechSegmentCount > 0) {
		ImGui::TextDisabled("Segments: %d", speechSegmentCount);
	}
	ImGui::BeginChild("##SpeechOut", ImVec2(0, 0), true);
	if (generating.load() && activeGenerationMode == AiMode::Speech) {
		std::string partial;
		{
			std::lock_guard<std::mutex> lock(streamMutex);
			partial = streamingOutput;
		}
		if (partial.empty()) {
			ImGui::TextDisabled("Transcribing...");
		} else {
			ImGui::TextWrapped("%s", partial.c_str());
		}
	} else if (speechOutput.empty()) {
		ImGui::TextDisabled("Speech transcription appears here.");
	} else {
		ImGui::TextWrapped("%s", speechOutput.c_str());
	}
	ImGui::EndChild();
}

void ofApp::drawTtsPanel() {
	drawPanelHeader("TTS", "local text-to-speech via Piper and chatllm.cpp backends");
	const float compactModeFieldWidth = std::min(320.0f, ImGui::GetContentRegionAvail().x);

	const bool loadedTtsProfiles = ensureTtsProfilesLoaded();
	if (loadedTtsProfiles && !ttsProfiles.empty()) {
		applyTtsProfileDefaults(getSelectedTtsProfile(), true);
	}

	const ofxGgmlTtsModelProfile activeProfile = getSelectedTtsProfile();
	const bool activeProfileUsesPiper = usesPiperTtsBackend(activeProfile);
	const bool activeProfileUsesChatLlm = usesChatLlmTtsBackend(activeProfile);
	const std::vector<PiperVoiceOption> availablePiperVoices =
		activeProfileUsesPiper ? discoverPiperVoices(ttsModelPath) : std::vector<PiperVoiceOption>{};
	if (!ttsProfiles.empty()) {
		std::vector<const char *> profileNames;
		profileNames.reserve(ttsProfiles.size());
		for (const auto & profile : ttsProfiles) {
			profileNames.push_back(profile.name.c_str());
		}
		ImGui::SetNextItemWidth(280);
		if (ImGui::Combo(
			"TTS profile",
			&selectedTtsProfileIndex,
			profileNames.data(),
			static_cast<int>(profileNames.size()))) {
			const ofxGgmlTtsModelProfile nextProfile = getSelectedTtsProfile();
			if (!nextProfile.supportsVoiceCloning &&
				ttsTaskIndex == static_cast<int>(ofxGgmlTtsTask::CloneVoice)) {
				ttsTaskIndex = static_cast<int>(ofxGgmlTtsTask::Synthesize);
			}
			if (usesPiperTtsBackend(nextProfile) &&
				ttsTaskIndex == static_cast<int>(ofxGgmlTtsTask::ContinueSpeech)) {
				ttsTaskIndex = static_cast<int>(ofxGgmlTtsTask::Synthesize);
			}
			const std::string configuredExecutable = trim(ttsExecutablePath);
			if (usesPiperTtsBackend(nextProfile) &&
				pathLooksLikeChatLlmExecutable(configuredExecutable)) {
				copyStringToBuffer(ttsExecutablePath, sizeof(ttsExecutablePath), "");
			}
			if (usesChatLlmTtsBackend(nextProfile) &&
				ofxGgmlPiperTtsAdapters::looksLikePythonLauncher(configuredExecutable)) {
				copyStringToBuffer(ttsExecutablePath, sizeof(ttsExecutablePath), "");
			}
			applyTtsProfileDefaults(getSelectedTtsProfile(), false);
			autoSaveSession();
		}
		if (!activeProfile.modelRepoHint.empty()) {
			ImGui::TextDisabled("Preset repo: %s", activeProfile.modelRepoHint.c_str());
		}
		if (!activeProfile.architecture.empty()) {
			ImGui::TextDisabled("Architecture: %s", activeProfile.architecture.c_str());
		}
		if (!activeProfile.modelFileHint.empty()) {
			ImGui::TextDisabled("Preset file: %s", activeProfile.modelFileHint.c_str());
		}
		if (!activeProfile.speakerFileHint.empty()) {
			ImGui::TextDisabled("Speaker file: %s", activeProfile.speakerFileHint.c_str());
		}
		ImGui::TextDisabled(
			"Capabilities: clone voice %s | streaming %s",
			activeProfile.supportsVoiceCloning
				? (activeProfile.requiresSpeakerProfile
					? "speaker.json required"
					: "supported")
				: "not advertised",
			activeProfile.supportsStreaming ? "supported" : "offline export only");
		if (activeProfileUsesPiper) {
			ImGui::TextDisabled(
				"Piper expects an .onnx voice file with its matching .onnx.json config beside it.");
		} else if (!activeProfile.architecture.empty()) {
			ImGui::TextDisabled(
				"This profile expects a converted chatllm.cpp model artifact, not a raw GGUF or safetensors checkpoint.");
		}
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Executable", ttsExecutablePath, sizeof(ttsExecutablePath));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse exe##Tts", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog(
			activeProfileUsesPiper
				? "Select Piper executable or python launcher"
				: "Select chatllm executable",
			false);
		if (result.bSuccess) {
			copyStringToBuffer(ttsExecutablePath, sizeof(ttsExecutablePath), result.getPath());
			autoSaveSession();
		}
	}
	if (activeProfileUsesChatLlm) {
		ImGui::SameLine();
		if (ImGui::Button("Use local##TtsExe", ImVec2(110, 0))) {
			copyStringToBuffer(
				ttsExecutablePath,
				sizeof(ttsExecutablePath),
				ofxGgmlChatLlmTtsAdapters::preferredLocalExecutablePath());
			autoSaveSession();
		}
	} else if (activeProfileUsesPiper) {
		ImGui::SameLine();
		if (ImGui::Button("Use local##TtsExe", ImVec2(110, 0))) {
			copyStringToBuffer(
				ttsExecutablePath,
				sizeof(ttsExecutablePath),
				ofxGgmlPiperTtsAdapters::preferredLocalExecutablePath());
			autoSaveSession();
		}
	}
	if (activeProfileUsesPiper) {
		ImGui::TextDisabled(
			"Leave Executable blank to auto-discover the addon-local Piper launcher first, then %s on PATH. You can also point this to python or py if Piper is installed as a module.",
			ofxGgmlPiperTtsAdapters::defaultExecutableHint().c_str());
		ImGui::TextDisabled(
			"Preferred local path: %s",
			ofxGgmlPiperTtsAdapters::preferredLocalExecutablePath().c_str());
	} else {
		ImGui::TextDisabled(
			"Leave Executable blank to auto-discover chatllm.cpp. Preferred local path: %s",
			ofxGgmlChatLlmTtsAdapters::preferredLocalExecutablePath().c_str());
	}
	ImGui::TextDisabled(
		"Resolved executable: %s",
		resolveConfiguredTtsExecutable(activeProfile).c_str());

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Model path", ttsModelPath, sizeof(ttsModelPath));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse model##Tts", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog(
			activeProfileUsesPiper
				? "Select Piper voice model (.onnx)"
				: "Select converted chatllm TTS model (.bin/.ggmm)",
			false);
		if (result.bSuccess) {
			copyStringToBuffer(ttsModelPath, sizeof(ttsModelPath), result.getPath());
			autoSaveSession();
		}
	}
	if (activeProfileUsesPiper) {
		ImGui::TextDisabled(
			"Model path should point to a Piper .onnx voice. Keep the matching .onnx.json file next to it.");
		if (!availablePiperVoices.empty()) {
			std::string selectedVoiceLabel = ofFilePath::getBaseName(trim(ttsModelPath));
			for (const auto & voice : availablePiperVoices) {
				if (ofToLower(voice.modelPath) == ofToLower(trim(ttsModelPath))) {
					selectedVoiceLabel = voice.label;
					break;
				}
			}
			if (selectedVoiceLabel.empty()) {
				selectedVoiceLabel = "Select Piper voice";
			}
			if (ImGui::BeginCombo("Installed Piper voices", selectedVoiceLabel.c_str())) {
				for (const auto & voice : availablePiperVoices) {
					const bool selected =
						(ofToLower(voice.modelPath) == ofToLower(trim(ttsModelPath)));
					if (ImGui::Selectable(voice.label.c_str(), selected)) {
						copyStringToBuffer(ttsModelPath, sizeof(ttsModelPath), voice.modelPath);
						autoSaveSession();
					}
					if (selected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
		} else {
			ImGui::TextDisabled("No Piper .onnx voices with matching .onnx.json were found under data/models yet.");
		}
	} else {
		ImGui::TextDisabled(
			"Model path should point to a converted chatllm.cpp artifact such as .bin or .ggmm. Raw .gguf and .safetensors files are not loaded here.");
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText(
		activeProfileUsesPiper ? "Speaker name/id" : "Speaker profile",
		ttsSpeakerPath,
		sizeof(ttsSpeakerPath));
	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Reference audio", ttsSpeakerReferencePath, sizeof(ttsSpeakerReferencePath));
	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Prompt audio", ttsPromptAudioPath, sizeof(ttsPromptAudioPath));
	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Output path", ttsOutputPath, sizeof(ttsOutputPath));
	ImGui::SetNextItemWidth(160);
	ImGui::InputText("Language", ttsLanguage, sizeof(ttsLanguage));

	static const char * ttsTaskLabels[] = {"Synthesize", "Clone Voice", "Continue Speech"};
	ImGui::SetNextItemWidth(200);
	ImGui::Combo("TTS task", &ttsTaskIndex, ttsTaskLabels, 3);
	if (activeProfileUsesPiper && ttsTaskIndex != static_cast<int>(ofxGgmlTtsTask::Synthesize)) {
		ImGui::TextDisabled("Piper is currently wired for plain synthesis only.");
	}
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Seed", &ttsSeed, -1, 999999);
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Max tokens", &ttsMaxTokens, 0, 4096);
	ImGui::SetNextItemWidth(220);
	ImGui::SliderFloat("Temperature", &ttsTemperature, 0.0f, 2.0f, "%.2f");
	ImGui::SetNextItemWidth(220);
	ImGui::SliderFloat("Repetition penalty", &ttsRepetitionPenalty, 1.0f, 3.0f, "%.2f");
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Repetition range", &ttsRepetitionRange, 0, 512);
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Top K", &ttsTopK, 0, 200);
	ImGui::SetNextItemWidth(220);
	ImGui::SliderFloat("Top P", &ttsTopP, 0.0f, 1.0f, "%.2f");
	ImGui::SetNextItemWidth(220);
	ImGui::SliderFloat("Min P", &ttsMinP, 0.0f, 1.0f, "%.2f");
	ImGui::Checkbox("Stream audio", &ttsStreamAudio);
	ImGui::SameLine();
	ImGui::Checkbox("Normalize text", &ttsNormalizeText);
	if (activeProfileUsesPiper) {
		ImGui::TextDisabled("Synthesize works directly with Piper .onnx voice files.");
		ImGui::TextDisabled("Voice cloning is not wired into the Piper adapter yet; use prepared voices here.");
	} else if (activeProfile.supportsVoiceCloning) {
		ImGui::TextDisabled("Voice cloning remains backend-specific. For chatllm, keep using the clone/reference fields below.");
	}

	ImGui::Separator();
	ImGui::TextDisabled("Chat voice");
	ImGui::Checkbox("Use different voice for chat replies", &chatUseCustomTtsVoice);
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	if (chatUseCustomTtsVoice) {
		if (activeProfileUsesPiper && !availablePiperVoices.empty()) {
			std::string selectedChatVoiceLabel = ofFilePath::getBaseName(trim(chatTtsModelPath));
			for (const auto & voice : availablePiperVoices) {
				if (ofToLower(voice.modelPath) == ofToLower(trim(chatTtsModelPath))) {
					selectedChatVoiceLabel = voice.label;
					break;
				}
			}
			if (selectedChatVoiceLabel.empty()) {
				selectedChatVoiceLabel = "Select chat voice";
			}
			if (ImGui::BeginCombo("Chat Piper voice", selectedChatVoiceLabel.c_str())) {
				for (const auto & voice : availablePiperVoices) {
					const bool selected =
						(ofToLower(voice.modelPath) == ofToLower(trim(chatTtsModelPath)));
					if (ImGui::Selectable(voice.label.c_str(), selected)) {
						copyStringToBuffer(chatTtsModelPath, sizeof(chatTtsModelPath), voice.modelPath);
						autoSaveSession();
					}
					if (selected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			if (ImGui::SmallButton("Use current TTS voice for chat")) {
				copyStringToBuffer(chatTtsModelPath, sizeof(chatTtsModelPath), trim(ttsModelPath));
				autoSaveSession();
			}
		}
		ImGui::SetNextItemWidth(compactModeFieldWidth);
		ImGui::InputText("Chat model path", chatTtsModelPath, sizeof(chatTtsModelPath));
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			autoSaveSession();
		}
		ImGui::SetNextItemWidth(compactModeFieldWidth);
		ImGui::InputText("Chat speaker/profile", chatTtsSpeakerPath, sizeof(chatTtsSpeakerPath));
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			autoSaveSession();
		}
	}

	ImGui::InputTextMultiline(
		"TTS text",
		ttsInput,
		sizeof(ttsInput),
		ImVec2(-1, 120));

	const bool canRunTts =
		!generating.load() &&
		std::strlen(ttsInput) > 0 &&
		std::strlen(ttsModelPath) > 0;
	ImGui::BeginDisabled(!canRunTts);
	if (ImGui::Button("Run TTS", ImVec2(140, 0))) {
		runTtsInference();
	}
	ImGui::EndDisabled();

	ImGui::Separator();
	if (!ttsBackendName.empty()) {
		ImGui::TextDisabled(
			"Last backend: %s%s",
			ttsBackendName.c_str(),
			ttsElapsedMs > 0.0f
				? (" in " + ofxGgmlHelpers::formatDurationMs(ttsElapsedMs)).c_str()
				: "");
	}
	if (!ttsResolvedSpeakerPath.empty()) {
		ImGui::TextDisabled("Resolved speaker: %s", ttsResolvedSpeakerPath.c_str());
	}
	if (!ttsMetadata.empty()) {
		for (const auto & entry : ttsMetadata) {
			if (trim(entry.first).empty() || trim(entry.second).empty()) {
				continue;
			}
			ImGui::TextDisabled("%s: %s", entry.first.c_str(), entry.second.c_str());
		}
	}
	if (!ttsAudioFiles.empty()) {
		ImGui::TextDisabled("Generated audio:");
		for (const auto & artifact : ttsAudioFiles) {
			ImGui::BulletText("%s", artifact.path.c_str());
		}
	}
	if (!ttsPanelPreview.statusMessage.empty()) {
		ImGui::TextDisabled("%s", ttsPanelPreview.statusMessage.c_str());
	}
	drawTtsPreviewControls(
		generating.load(),
		ttsPanelPreview,
		{
			"##TtsPanelAudio",
			"Pause TTS audio",
			"Resume TTS audio",
			"Play TTS audio",
			"Restart TTS audio",
			"Stop TTS audio",
			"TTS audio file:",
			"Paused synthesized TTS output.",
			"Playing synthesized TTS output.",
			"Restarted synthesized TTS output.",
			"Stopped synthesized TTS output."
		},
		[this](int artifactIndex, bool autoplay) {
			return ensureTtsPanelAudioLoaded(artifactIndex, autoplay);
		},
		[this](bool clearLoadedPath) {
			stopTtsPanelPlayback(clearLoadedPath);
		});
	ImGui::BeginChild("##TtsOut", ImVec2(0, 0), true);
	if (generating.load() && activeGenerationMode == AiMode::Tts) {
		std::string partial;
		{
			std::lock_guard<std::mutex> lock(streamMutex);
			partial = streamingOutput;
		}
		if (partial.empty()) {
			ImGui::TextDisabled("Synthesizing...");
		} else {
			ImGui::TextWrapped("%s", partial.c_str());
		}
	} else if (ttsOutput.empty()) {
		ImGui::TextDisabled("TTS output appears here.");
	} else {
		ImGui::TextWrapped("%s", ttsOutput.c_str());
	}
	ImGui::EndChild();
}
