#include "ofApp.h"

#include "utils/AudioHelpers.h"
#include "utils/ImGuiHelpers.h"
#include "utils/PathHelpers.h"

#include <algorithm>
#include <exception>
#include <vector>

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

std::string ofApp::resolveConfiguredTtsExecutable() const {
	return ofxGgmlChatLlmTtsAdapters::resolveChatLlmExecutable(trim(ttsExecutablePath));
}

std::shared_ptr<ofxGgmlTtsBackend> ofApp::createConfiguredTtsBackend(
	const std::string & executableHint) const {
	ofxGgmlChatLlmTtsAdapters::RuntimeOptions runtimeOptions;
	runtimeOptions.executablePath =
		executableHint.empty() ? trim(ttsExecutablePath) : executableHint;
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
	drawPanelHeader("TTS", "local text-to-speech via chatllm.cpp-backed OuteTTS models");
	const float compactModeFieldWidth = std::min(320.0f, ImGui::GetContentRegionAvail().x);

	const bool loadedTtsProfiles = ensureTtsProfilesLoaded();
	if (loadedTtsProfiles && !ttsProfiles.empty()) {
		applyTtsProfileDefaults(getSelectedTtsProfile(), true);
	}
	if (trim(ttsExecutablePath).empty()) {
		copyStringToBuffer(
			ttsExecutablePath,
			sizeof(ttsExecutablePath),
			ofxGgmlChatLlmTtsAdapters::preferredLocalExecutablePath());
	}

	const ofxGgmlTtsModelProfile activeProfile = getSelectedTtsProfile();
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
			applyTtsProfileDefaults(getSelectedTtsProfile(), false);
			autoSaveSession();
		}
		if (!activeProfile.modelRepoHint.empty()) {
			ImGui::TextDisabled("Recommended repo: %s", activeProfile.modelRepoHint.c_str());
		}
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Executable", ttsExecutablePath, sizeof(ttsExecutablePath));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse exe##Tts", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select chatllm executable", false);
		if (result.bSuccess) {
			copyStringToBuffer(ttsExecutablePath, sizeof(ttsExecutablePath), result.getPath());
			autoSaveSession();
		}
	}
	ImGui::TextDisabled("Resolved executable: %s", resolveConfiguredTtsExecutable().c_str());

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Model path", ttsModelPath, sizeof(ttsModelPath));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse model##Tts", ImVec2(110, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select TTS model", false);
		if (result.bSuccess) {
			copyStringToBuffer(ttsModelPath, sizeof(ttsModelPath), result.getPath());
			autoSaveSession();
		}
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Speaker profile", ttsSpeakerPath, sizeof(ttsSpeakerPath));
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
	if (!ttsAudioFiles.empty()) {
		ImGui::TextDisabled("Generated audio:");
		for (const auto & artifact : ttsAudioFiles) {
			ImGui::BulletText("%s", artifact.path.c_str());
		}
	}
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
