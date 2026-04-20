#include "ofApp.h"

#include "ofJson.h"
#include "utils/BackendHelpers.h"
#include "utils/ImGuiHelpers.h"
#include "utils/PathHelpers.h"

#include <algorithm>
#include <fstream>

std::string ofApp::escapeSessionText(const std::string & text) const {
	return text;
}

std::string ofApp::unescapeSessionText(const std::string & text) const {
	return text;
}

bool ofApp::saveSession(const std::string & path) {
	ofJson session;
	session["format"] = "ofxGgmlGuiSession";
	session["version"] = 2;

	session["settings"] = {
		{"activeMode", static_cast<int>(activeMode)},
		{"selectedModelIndex", selectedModelIndex},
		{"selectedVideoRenderPresetIndex", selectedVideoRenderPresetIndex},
		{"selectedLanguageIndex", selectedLanguageIndex},
		{"scriptAgentModeIndex", scriptAgentModeIndex},
		{"translateSourceLang", translateSourceLang},
		{"translateTargetLang", translateTargetLang},
		{"chatLanguageIndex", chatLanguageIndex},
		{"maxTokens", maxTokens},
		{"temperature", temperature},
		{"topP", topP},
		{"topK", topK},
		{"minP", minP},
		{"repeatPenalty", repeatPenalty},
		{"contextSize", contextSize},
		{"batchSize", batchSize},
		{"gpuLayers", gpuLayers},
		{"seed", seed},
		{"numThreads", numThreads},
		{"selectedBackendIndex", selectedBackendIndex},
		{"themeIndex", themeIndex},
		{"mirostatMode", mirostatMode},
		{"mirostatTau", mirostatTau},
		{"mirostatEta", mirostatEta},
		{"textInferenceBackend", static_cast<int>(textInferenceBackend)},
		{"useModeTokenBudgets", useModeTokenBudgets},
		{"autoContinueCutoff", autoContinueCutoff},
		{"usePromptCache", usePromptCache},
		{"logLevel", static_cast<int>(logLevel)},
		{"liveContextMode", static_cast<int>(liveContextMode)},
		{"liveContextAllowPromptUrls", liveContextAllowPromptUrls},
		{"liveContextAllowDomainProviders", liveContextAllowDomainProviders},
		{"liveContextAllowGenericSearch", liveContextAllowGenericSearch},
		{"scriptIncludeRepoContext", scriptIncludeRepoContext},
		{"selectedVisionProfileIndex", selectedVisionProfileIndex},
		{"selectedSpeechProfileIndex", selectedSpeechProfileIndex},
		{"selectedTtsProfileIndex", selectedTtsProfileIndex},
		{"selectedDiffusionProfileIndex", selectedDiffusionProfileIndex},
		{"citationUseCrawler", citationUseCrawler},
		{"citationMaxResults", citationMaxResults},
		{"videoEssayCitationCount", videoEssayCitationCount},
		{"videoEssayToneIndex", videoEssayToneIndex},
		{"videoEssayAudienceIndex", videoEssayAudienceIndex},
		{"longVideoChunkCount", longVideoChunkCount},
		{"longVideoStructureIndex", longVideoStructureIndex},
		{"longVideoPacingIndex", longVideoPacingIndex},
		{"longVideoWidth", longVideoWidth},
		{"longVideoHeight", longVideoHeight},
		{"longVideoFps", longVideoFps},
		{"longVideoFramesPerChunk", longVideoFramesPerChunk},
		{"longVideoSeed", longVideoSeed},
		{"longVideoRenderPresetIndex", longVideoRenderPresetIndex},
		{"longVideoRenderModeIndex", longVideoRenderModeIndex},
		{"longVideoRenderSelectedChunkIndex", longVideoRenderSelectedChunkIndex}
	};
	session["settings"]["modeMaxTokens"] = ofJson::array();
	for (int i = 0; i < kModeCount; ++i) {
		session["settings"]["modeMaxTokens"].push_back(modeMaxTokens[static_cast<size_t>(i)]);
	}
	session["settings"]["modeTextBackendIndices"] = ofJson::array();
	for (int i = 0; i < kModeCount; ++i) {
		session["settings"]["modeTextBackendIndices"].push_back(modeTextBackendIndices[static_cast<size_t>(i)]);
	}
	if (selectedBackendIndex >= 0 &&
		selectedBackendIndex < static_cast<int>(backendNames.size())) {
		session["settings"]["selectedBackendName"] =
			backendNames[static_cast<size_t>(selectedBackendIndex)];
	}

	const ofxGgmlScriptSourceType savedScriptSourceType =
		deferredScriptSourceRestorePending ? deferredScriptSourceType : scriptSource.getSourceType();
	const std::string savedScriptSourcePath =
		deferredScriptSourceRestorePending ? deferredScriptSourcePath : scriptSource.getLocalFolderPath();
	std::string savedScriptSourceInternetUrls = deferredScriptSourceRestorePending
		? deferredScriptSourceInternetUrls
		: std::string();
	if (!deferredScriptSourceRestorePending) {
		const auto internetUrls = scriptSource.getInternetUrls();
		for (size_t i = 0; i < internetUrls.size(); ++i) {
			if (i > 0) {
				savedScriptSourceInternetUrls += "\n";
			}
			savedScriptSourceInternetUrls += internetUrls[i];
		}
	}
	session["scriptSource"] = {
		{"type", static_cast<int>(savedScriptSourceType)},
		{"path", savedScriptSourcePath},
		{"github", std::string(scriptSourceGitHub)},
		{"branch", std::string(scriptSourceBranch)},
		{"internetUrls", savedScriptSourceInternetUrls},
		{"projectMemoryEnabled", scriptProjectMemory.isEnabled()},
		{"projectMemoryText", scriptProjectMemory.getMemoryText()}
	};

	session["buffers"] = {
		{"chatInput", std::string(chatInput)},
		{"easyPrimaryInput", std::string(easyPrimaryInput)},
		{"easySecondaryInput", std::string(easySecondaryInput)},
		{"scriptInput", std::string(scriptInput)},
		{"summarizeInput", std::string(summarizeInput)},
		{"writeInput", std::string(writeInput)},
		{"translateInput", std::string(translateInput)},
		{"voiceTranslatorAudioPath", std::string(voiceTranslatorAudioPath)},
		{"videoEssayTopic", std::string(videoEssayTopic)},
		{"videoEssaySeedUrl", std::string(videoEssaySeedUrl)},
		{"videoEssaySourceVideoPath", std::string(videoEssaySourceVideoPath)},
		{"longVideoConcept", std::string(longVideoConcept)},
		{"longVideoStyle", std::string(longVideoStyle)},
		{"longVideoNegativeStyle", std::string(longVideoNegativeStyle)},
		{"longVideoContinuityGoal", std::string(longVideoContinuityGoal)},
		{"longVideoRenderSourceImagePath", std::string(longVideoRenderSourceImagePath)},
		{"longVideoRenderEndImagePath", std::string(longVideoRenderEndImagePath)},
		{"longVideoRenderOutputDir", std::string(longVideoRenderOutputDir)},
		{"longVideoRenderOutputPrefix", std::string(longVideoRenderOutputPrefix)},
		{"customInput", std::string(customInput)},
		{"customSystemPrompt", std::string(customSystemPrompt)},
		{"sourceUrlsInput", std::string(sourceUrlsInput)},
		{"citationTopic", std::string(citationTopic)},
		{"citationSeedUrl", std::string(citationSeedUrl)},
		{"textServerUrl", std::string(textServerUrl)},
		{"textServerModel", std::string(textServerModel)},
		{"customModelPath", std::string(customModelPath)},
		{"customVideoRenderModelPath", std::string(customVideoRenderModelPath)},
		{"visionPrompt", std::string(visionPrompt)},
		{"visionImagePath", std::string(visionImagePath)},
		{"visionVideoPath", std::string(visionVideoPath)},
		{"visionModelPath", std::string(visionModelPath)},
		{"visionServerUrl", std::string(visionServerUrl)},
		{"videoSidecarUrl", std::string(videoSidecarUrl)},
		{"videoSidecarModel", std::string(videoSidecarModel)},
		{"visionSystemPrompt", std::string(visionSystemPrompt)},
		{"videoPlanJson", std::string(videoPlanJson)},
		{"videoEditPlanJson", std::string(videoEditPlanJson)},
		{"montageSubtitlePath", std::string(montageSubtitlePath)},
		{"montageGoal", std::string(montageGoal)},
		{"montageEdlTitle", std::string(montageEdlTitle)},
		{"montageReelName", std::string(montageReelName)},
		{"montageClipPaths", std::string(montageClipPaths)},
		{"montageRenderAudioPath", std::string(montageRenderAudioPath)},
		{"videoEditGoal", std::string(videoEditGoal)},
		{"speechAudioPath", std::string(speechAudioPath)},
		{"speechExecutable", std::string(speechExecutable)},
		{"speechModelPath", std::string(speechModelPath)},
		{"speechServerUrl", std::string(speechServerUrl)},
		{"speechServerModel", std::string(speechServerModel)},
		{"speechPrompt", std::string(speechPrompt)},
		{"speechLanguageHint", std::string(speechLanguageHint)},
		{"ttsInput", std::string(ttsInput)},
		{"ttsExecutablePath", std::string(ttsExecutablePath)},
		{"ttsModelPath", std::string(ttsModelPath)},
		{"ttsSpeakerPath", std::string(ttsSpeakerPath)},
		{"chatTtsModelPath", std::string(chatTtsModelPath)},
		{"chatTtsSpeakerPath", std::string(chatTtsSpeakerPath)},
		{"ttsSpeakerReferencePath", std::string(ttsSpeakerReferencePath)},
		{"ttsOutputPath", std::string(ttsOutputPath)},
		{"ttsPromptAudioPath", std::string(ttsPromptAudioPath)},
		{"ttsLanguage", std::string(ttsLanguage)},
		{"diffusionPrompt", std::string(diffusionPrompt)},
		{"diffusionInstruction", std::string(diffusionInstruction)},
		{"diffusionNegativePrompt", std::string(diffusionNegativePrompt)},
		{"diffusionRankingPrompt", std::string(diffusionRankingPrompt)},
		{"diffusionModelPath", std::string(diffusionModelPath)},
		{"diffusionVaePath", std::string(diffusionVaePath)},
		{"diffusionInitImagePath", std::string(diffusionInitImagePath)},
		{"diffusionMaskImagePath", std::string(diffusionMaskImagePath)},
		{"diffusionOutputDir", std::string(diffusionOutputDir)},
		{"diffusionOutputPrefix", std::string(diffusionOutputPrefix)},
		{"diffusionSampler", std::string(diffusionSampler)},
		{"imageSearchPrompt", std::string(imageSearchPrompt)},
		{"clipPrompt", std::string(clipPrompt)},
		{"clipModelPath", std::string(clipModelPath)},
		{"clipImagePaths", std::string(clipImagePaths)},
		{"musicToImageDescription", std::string(musicToImageDescription)},
		{"musicToImageLyrics", std::string(musicToImageLyrics)},
		{"musicToImageStyle", std::string(musicToImageStyle)},
		{"imageToMusicDescription", std::string(imageToMusicDescription)},
		{"imageToMusicSceneNotes", std::string(imageToMusicSceneNotes)},
		{"imageToMusicStyle", std::string(imageToMusicStyle)},
		{"imageToMusicInstrumentation", std::string(imageToMusicInstrumentation)},
		{"imageToMusicAbcTitle", std::string(imageToMusicAbcTitle)},
		{"imageToMusicAbcKey", std::string(imageToMusicAbcKey)},
		{"imageToMusicAbcOutputPath", std::string(imageToMusicAbcOutputPath)},
		{"aceStepServerUrl", std::string(aceStepServerUrl)},
		{"aceStepPrompt", std::string(aceStepPrompt)},
		{"aceStepLyrics", std::string(aceStepLyrics)},
		{"aceStepAudioPath", std::string(aceStepAudioPath)},
		{"aceStepOutputDir", std::string(aceStepOutputDir)},
		{"aceStepOutputPrefix", std::string(aceStepOutputPrefix)},
		{"aceStepKeyscale", std::string(aceStepKeyscale)},
		{"aceStepTimesignature", std::string(aceStepTimesignature)},
		{"milkdropPrompt", std::string(milkdropPrompt)},
		{"milkdropPresetPath", std::string(milkdropPresetPath)}
	};

	session["indices"] = {
		{"easyActionIndex", easyActionIndex},
		{"easyCitationCount", easyCitationCount},
		{"visionTaskIndex", visionTaskIndex},
		{"videoTaskIndex", videoTaskIndex},
		{"visionVideoMaxFrames", visionVideoMaxFrames},
		{"videoPlanBeatCount", videoPlanBeatCount},
		{"videoPlanSceneCount", videoPlanSceneCount},
		{"videoPlanGenerationMode", videoPlanGenerationMode},
		{"videoEditPresetIndex", videoEditPresetIndex},
		{"videoEditClipCount", videoEditClipCount},
		{"selectedVideoPlanSceneIndex", selectedVideoPlanSceneIndex},
		{"selectedVideoEssaySceneIndex", selectedVideoEssaySceneIndex},
		{"montageMaxClips", montageMaxClips},
		{"montageFps", montageFps},
		{"speechTaskIndex", speechTaskIndex},
		{"ttsTaskIndex", ttsTaskIndex},
		{"ttsSeed", ttsSeed},
		{"ttsMaxTokens", ttsMaxTokens},
		{"diffusionTaskIndex", diffusionTaskIndex},
		{"diffusionSelectionModeIndex", diffusionSelectionModeIndex},
		{"diffusionWidth", diffusionWidth},
		{"diffusionHeight", diffusionHeight},
		{"diffusionSteps", diffusionSteps},
		{"diffusionBatchCount", diffusionBatchCount},
		{"diffusionSeed", diffusionSeed},
		{"imageSearchMaxResults", imageSearchMaxResults},
		{"clipTopK", clipTopK},
		{"clipVerbosity", clipVerbosity},
		{"imageToMusicDurationSeconds", imageToMusicDurationSeconds},
		{"imageToMusicAbcBars", imageToMusicAbcBars},
		{"aceStepBpm", aceStepBpm},
		{"aceStepDurationSeconds", aceStepDurationSeconds},
		{"aceStepSeed", aceStepSeed},
		{"aceStepSelectedTrackIndex", aceStepSelectedTrackIndex},
		{"musicVideoSectionCount", musicVideoSectionCount},
		{"musicVideoStructureIndex", musicVideoStructureIndex},
		{"milkdropCategoryIndex", milkdropCategoryIndex},
		{"milkdropVariantCount", milkdropVariantCount}
	};

	session["floats"] = {
		{"videoPlanDurationSeconds", videoPlanDurationSeconds},
		{"musicVideoCutIntensity", musicVideoCutIntensity},
		{"videoEditTargetDurationSeconds", videoEditTargetDurationSeconds},
		{"montageMinScore", montageMinScore},
		{"montageTargetDurationSeconds", montageTargetDurationSeconds},
		{"montageMinSpacingSeconds", montageMinSpacingSeconds},
		{"montagePreRollSeconds", montagePreRollSeconds},
		{"montagePostRollSeconds", montagePostRollSeconds},
		{"ttsTemperature", ttsTemperature},
		{"ttsRepetitionPenalty", ttsRepetitionPenalty},
		{"ttsTopP", ttsTopP},
		{"ttsMinP", ttsMinP},
		{"diffusionCfgScale", diffusionCfgScale},
		{"diffusionStrength", diffusionStrength},
		{"milkdropRandomness", milkdropRandomness},
		{"milkdropPreviewBeatSensitivity", milkdropPreviewBeatSensitivity},
		{"milkdropPreviewPresetDuration", milkdropPreviewPresetDuration},
		{"videoEssayTargetDurationSeconds", videoEssayTargetDurationSeconds},
		{"easyTargetDurationSeconds", easyTargetDurationSeconds},
		{"longVideoTargetDurationSeconds", longVideoTargetDurationSeconds}
	};

	session["bools"] = {
		{"chatSpeakReplies", chatSpeakReplies},
		{"chatUseCustomTtsVoice", chatUseCustomTtsVoice},
		{"summarizeSpeakOutput", summarizeSpeakOutput},
		{"easyUseCrawler", easyUseCrawler},
		{"voiceTranslatorSpeakOutput", voiceTranslatorSpeakOutput},
		{"videoEssayUseCrawler", videoEssayUseCrawler},
		{"videoEssayIncludeCounterpoints", videoEssayIncludeCounterpoints},
		{"longVideoUsePromptInheritance", longVideoUsePromptInheritance},
		{"longVideoFavorLoopableEnding", longVideoFavorLoopableEnding},
		{"musicToImageIncludeLyrics", musicToImageIncludeLyrics},
		{"imageToMusicInstrumentalOnly", imageToMusicInstrumentalOnly},
		{"aceStepUseWav", aceStepUseWav},
		{"aceStepInstrumentalOnly", aceStepInstrumentalOnly},
		{"videoPlanMultiScene", videoPlanMultiScene},
		{"videoPlanUseForGeneration", videoPlanUseForGeneration},
		{"montagePreserveChronology", montagePreserveChronology},
		{"montageSubtitlePlaybackEnabled", montageSubtitlePlaybackEnabled},
		{"videoEditUseCurrentAnalysis", videoEditUseCurrentAnalysis},
		{"speechReturnTimestamps", speechReturnTimestamps},
		{"speechLiveTranscriptionEnabled", speechLiveTranscriptionEnabled},
		{"ttsStreamAudio", ttsStreamAudio},
		{"ttsNormalizeText", ttsNormalizeText},
		{"diffusionNormalizeClipEmbeddings", diffusionNormalizeClipEmbeddings},
		{"diffusionSaveMetadata", diffusionSaveMetadata},
		{"clipNormalizeEmbeddings", clipNormalizeEmbeddings},
		{"milkdropAutoPreview", milkdropAutoPreview},
		{"milkdropPreviewFeedMicWhileRecording", milkdropPreviewFeedMicWhileRecording}
	};

	session["montagePreview"] = {
		{"timingModeIndex", montagePreviewTimingModeIndex},
		{"subtitleSlavePath", montagePreviewSubtitleSlavePath}
	};
	session["videoEditWorkflow"] = {
		{"activeStepIndex", videoEditWorkflowActiveStepIndex}
	};
	session["videoEditWorkflow"]["completedStepIndices"] = ofJson::array();
	for (const int stepIndex : videoEditWorkflowCompletedStepIndices) {
		session["videoEditWorkflow"]["completedStepIndices"].push_back(stepIndex);
	}

	session["liveSpeech"] = {
		{"intervalSeconds", speechLiveIntervalSeconds},
		{"windowSeconds", speechLiveWindowSeconds},
		{"overlapSeconds", speechLiveOverlapSeconds}
	};

	session["outputs"] = {
		{"chatLastAssistantReply", chatLastAssistantReply},
		{"chatTtsStatusMessage", chatTtsPreview.statusMessage},
		{"summarizeTtsStatusMessage", summarizeTtsPreview.statusMessage},
		{"easyOutput", easyOutput},
		{"scriptOutput", scriptOutput},
		{"summarizeOutput", summarizeOutput},
		{"writeOutput", writeOutput},
		{"translateOutput", translateOutput},
		{"voiceTranslatorStatus", voiceTranslatorStatus},
		{"voiceTranslatorTranscript", voiceTranslatorTranscript},
		{"videoEssayStatus", videoEssayStatus},
		{"videoEssayOutline", videoEssayOutline},
		{"videoEssayScript", videoEssayScript},
		{"videoEssaySrtText", videoEssaySrtText},
		{"videoEssayVisualConcept", videoEssayVisualConcept},
		{"videoEssayScenePlanJson", videoEssayScenePlanJson},
		{"videoEssayScenePlanSummary", videoEssayScenePlanSummary},
		{"videoEssayScenePlanningError", videoEssayScenePlanningError},
		{"videoEssayEditPlanJson", videoEssayEditPlanJson},
		{"videoEssayEditPlanSummary", videoEssayEditPlanSummary},
		{"videoEssayEditPlanningError", videoEssayEditPlanningError},
		{"videoEssayEditorBrief", videoEssayEditorBrief},
		{"videoEssayVlcPreviewStatusMessage", videoEssayVlcPreviewStatusMessage},
		{"videoEssayLastRenderedVideoPath", videoEssayLastRenderedVideoPath},
		{"longVideoStatus", longVideoStatus},
		{"longVideoContinuityBible", longVideoContinuityBible},
		{"longVideoManifestJson", longVideoManifestJson},
		{"longVideoRenderStatus", longVideoRenderStatus},
		{"longVideoRenderOutputDirectory", longVideoRenderOutputDirectory},
		{"longVideoRenderMetadataPath", longVideoRenderMetadataPath},
		{"longVideoRenderManifestPath", longVideoRenderManifestPath},
		{"longVideoRenderManifestJson", longVideoRenderManifestJson},
		{"customOutput", customOutput},
		{"citationOutput", citationOutput},
		{"visionOutput", visionOutput},
		{"montageSummary", montageSummary},
		{"montageEditorBrief", montageEditorBrief},
		{"montageEdlText", montageEdlText},
		{"montageSrtText", montageSrtText},
		{"montageVttText", montageVttText},
		{"montageClipPlaylistManifestPath", montageClipPlaylistManifestPath},
		{"montageClipPlaylistStatusMessage", montageClipPlaylistStatusMessage},
		{"montageClipRenderOutputPath", montageClipRenderOutputPath},
		{"videoPlanSummary", videoPlanSummary},
		{"videoEditPlanSummary", videoEditPlanSummary},
		{"speechOutput", speechOutput},
		{"speechDetectedLanguage", speechDetectedLanguage},
		{"speechTranscriptPath", speechTranscriptPath},
		{"speechSrtPath", speechSrtPath},
		{"speechSegmentCount", speechSegmentCount},
		{"ttsOutput", ttsOutput},
		{"diffusionOutput", diffusionOutput},
		{"musicToImagePromptOutput", musicToImagePromptOutput},
		{"musicToImageStatus", musicToImageStatus},
		{"musicVideoSectionSummary", musicVideoSectionSummary},
		{"imageToMusicPromptOutput", imageToMusicPromptOutput},
		{"imageToMusicNotationOutput", imageToMusicNotationOutput},
		{"imageToMusicStatus", imageToMusicStatus},
		{"imageToMusicSavedNotationPath", imageToMusicSavedNotationPath},
		{"aceStepStatus", aceStepStatus},
		{"aceStepGeneratedRequestJson", aceStepGeneratedRequestJson},
		{"aceStepUnderstoodSummary", aceStepUnderstoodSummary},
		{"aceStepUnderstoodCaption", aceStepUnderstoodCaption},
		{"aceStepUnderstoodLyrics", aceStepUnderstoodLyrics},
		{"aceStepUsedServerUrl", aceStepUsedServerUrl},
		{"imageSearchOutput", imageSearchOutput},
		{"clipOutput", clipOutput},
		{"milkdropOutput", milkdropOutput},
		{"milkdropSavedPresetPath", milkdropSavedPresetPath},
		{"milkdropPreviewStatus", milkdropPreviewStatus}
	};

	session["chatMessages"] = ofJson::array();
	for (const auto & msg : chatMessages) {
		session["chatMessages"].push_back({
			{"role", msg.role},
			{"text", msg.text},
			{"timestamp", msg.timestamp}
		});
	}

	std::ofstream out(path);
	if (!out.is_open()) {
		return false;
	}
	out << session.dump(2);
	return true;
}

bool ofApp::loadSession(const std::string & path) {
	std::ifstream in(path);
	if (!in.is_open()) {
		return false;
	}

	ofJson session;
	try {
		in >> session;
	} catch (...) {
		logWithLevel(OF_LOG_WARNING, "Failed to parse session file: " + path);
		return false;
	}
	if (!session.is_object()) {
		return false;
	}

	auto getString = [](const ofJson & node, const char * key, const std::string & fallback = std::string()) {
		return node.contains(key) && node[key].is_string()
			? node[key].get<std::string>()
			: fallback;
	};
	auto getInt = [](const ofJson & node, const char * key, int fallback = 0) {
		return node.contains(key) && node[key].is_number_integer()
			? node[key].get<int>()
			: fallback;
	};
	auto getFloat = [](const ofJson & node, const char * key, float fallback = 0.0f) {
		return node.contains(key) && node[key].is_number()
			? node[key].get<float>()
			: fallback;
	};
	auto getBool = [](const ofJson & node, const char * key, bool fallback = false) {
		return node.contains(key) && node[key].is_boolean()
			? node[key].get<bool>()
			: fallback;
	};
	auto copyJsonString = [this, &getString](char * buffer, size_t bufferSize, const ofJson & node, const char * key) {
		copyStringToBuffer(buffer, bufferSize, getString(node, key));
	};

	const ofJson settings = session.value("settings", ofJson::object());
	const ofJson scriptSourceJson = session.value("scriptSource", ofJson::object());
	const ofJson buffers = session.value("buffers", ofJson::object());
	const ofJson indices = session.value("indices", ofJson::object());
	const ofJson floats = session.value("floats", ofJson::object());
	const ofJson bools = session.value("bools", ofJson::object());
	const ofJson montagePreview = session.value("montagePreview", ofJson::object());
	const ofJson videoEditWorkflow = session.value("videoEditWorkflow", ofJson::object());
	const ofJson liveSpeech = session.value("liveSpeech", ofJson::object());
	const ofJson outputs = session.value("outputs", ofJson::object());

	activeMode = static_cast<AiMode>(std::clamp(getInt(settings, "activeMode", static_cast<int>(AiMode::Chat)), 0, kModeCount - 1));
	selectedModelIndex = std::clamp(getInt(settings, "selectedModelIndex", 0), 0, std::max(0, static_cast<int>(modelPresets.size()) - 1));
	selectedVideoRenderPresetIndex = std::clamp(
		getInt(settings, "selectedVideoRenderPresetIndex", 0),
		0,
		std::max(0, static_cast<int>(videoRenderPresets.size()) - 1));
	selectedLanguageIndex = std::clamp(getInt(settings, "selectedLanguageIndex", 0), 0, std::max(0, static_cast<int>(scriptLanguages.size()) - 1));
	scriptAgentModeIndex = std::clamp(getInt(settings, "scriptAgentModeIndex", scriptAgentModeIndex), 0, 1);
	translateSourceLang = std::clamp(getInt(settings, "translateSourceLang", 0), 0, std::max(0, static_cast<int>(translateLanguages.size()) - 1));
	translateTargetLang = std::clamp(getInt(settings, "translateTargetLang", 1), 0, std::max(0, static_cast<int>(translateLanguages.size()) - 1));
	chatLanguageIndex = std::clamp(getInt(settings, "chatLanguageIndex", 0), 0, std::max(0, static_cast<int>(chatLanguages.size()) - 1));
	maxTokens = std::clamp(getInt(settings, "maxTokens", maxTokens), 32, 4096);
	temperature = std::clamp(getFloat(settings, "temperature", temperature), 0.0f, 2.0f);
	topP = std::clamp(getFloat(settings, "topP", topP), 0.0f, 1.0f);
	topK = std::clamp(getInt(settings, "topK", topK), 0, 200);
	minP = std::clamp(getFloat(settings, "minP", minP), 0.0f, 1.0f);
	repeatPenalty = std::clamp(getFloat(settings, "repeatPenalty", repeatPenalty), 1.0f, 2.0f);
	contextSize = std::clamp(getInt(settings, "contextSize", contextSize), 256, 16384);
	batchSize = std::clamp(getInt(settings, "batchSize", batchSize), 32, 4096);
	gpuLayers = std::clamp(getInt(settings, "gpuLayers", gpuLayers), 0, 999);
	seed = getInt(settings, "seed", seed);
	numThreads = std::clamp(getInt(settings, "numThreads", numThreads), 1, 128);
	selectedBackendIndex = std::clamp(getInt(settings, "selectedBackendIndex", selectedBackendIndex), 0, std::max(0, static_cast<int>(backendNames.size()) - 1));
	const std::string selectedBackendName = getString(settings, "selectedBackendName");
	if (!selectedBackendName.empty()) {
		for (int i = 0; i < static_cast<int>(backendNames.size()); ++i) {
			if (backendNames[static_cast<size_t>(i)] == selectedBackendName) {
				selectedBackendIndex = i;
				break;
			}
		}
	}
	themeIndex = std::clamp(getInt(settings, "themeIndex", themeIndex), 0, 2);
	mirostatMode = std::clamp(getInt(settings, "mirostatMode", mirostatMode), 0, 2);
	mirostatTau = std::clamp(getFloat(settings, "mirostatTau", mirostatTau), 0.0f, 10.0f);
	mirostatEta = std::clamp(getFloat(settings, "mirostatEta", mirostatEta), 0.0f, 1.0f);
	textInferenceBackend = clampTextInferenceBackend(getInt(settings, "textInferenceBackend", static_cast<int>(textInferenceBackend)));
	useModeTokenBudgets = getBool(settings, "useModeTokenBudgets", useModeTokenBudgets);
	autoContinueCutoff = getBool(settings, "autoContinueCutoff", autoContinueCutoff);
	usePromptCache = getBool(settings, "usePromptCache", usePromptCache);
	logLevel = static_cast<ofLogLevel>(std::clamp(getInt(settings, "logLevel", static_cast<int>(logLevel)), static_cast<int>(OF_LOG_VERBOSE), static_cast<int>(OF_LOG_SILENT)));
	liveContextMode = static_cast<LiveContextMode>(std::clamp(getInt(settings, "liveContextMode", static_cast<int>(liveContextMode)), 0, 3));
	liveContextAllowPromptUrls = getBool(settings, "liveContextAllowPromptUrls", liveContextAllowPromptUrls);
	liveContextAllowDomainProviders = getBool(settings, "liveContextAllowDomainProviders", liveContextAllowDomainProviders);
	liveContextAllowGenericSearch = getBool(settings, "liveContextAllowGenericSearch", liveContextAllowGenericSearch);
	scriptIncludeRepoContext = getBool(settings, "scriptIncludeRepoContext", scriptIncludeRepoContext);
	selectedVisionProfileIndex = std::max(0, getInt(settings, "selectedVisionProfileIndex", selectedVisionProfileIndex));
	selectedSpeechProfileIndex = std::max(0, getInt(settings, "selectedSpeechProfileIndex", selectedSpeechProfileIndex));
	selectedTtsProfileIndex = std::max(0, getInt(settings, "selectedTtsProfileIndex", selectedTtsProfileIndex));
	selectedDiffusionProfileIndex = std::max(0, getInt(settings, "selectedDiffusionProfileIndex", selectedDiffusionProfileIndex));
	videoEssayCitationCount = std::clamp(
		getInt(settings, "videoEssayCitationCount", videoEssayCitationCount),
		2,
		12);
	videoEssayToneIndex = std::clamp(
		getInt(settings, "videoEssayToneIndex", videoEssayToneIndex),
		0,
		3);
	videoEssayAudienceIndex = std::clamp(
		getInt(settings, "videoEssayAudienceIndex", videoEssayAudienceIndex),
		0,
		3);
	longVideoChunkCount = std::clamp(
		getInt(settings, "longVideoChunkCount", longVideoChunkCount),
		1,
		16);
	longVideoStructureIndex = std::clamp(
		getInt(settings, "longVideoStructureIndex", longVideoStructureIndex),
		0,
		3);
	longVideoPacingIndex = std::clamp(
		getInt(settings, "longVideoPacingIndex", longVideoPacingIndex),
		0,
		2);
	longVideoWidth = std::clamp(getInt(settings, "longVideoWidth", longVideoWidth), 128, 1920);
	longVideoHeight = std::clamp(getInt(settings, "longVideoHeight", longVideoHeight), 128, 1920);
	longVideoFps = std::clamp(getInt(settings, "longVideoFps", longVideoFps), 1, 60);
	longVideoFramesPerChunk = std::clamp(
		getInt(settings, "longVideoFramesPerChunk", longVideoFramesPerChunk),
		8,
		240);
	longVideoSeed = getInt(settings, "longVideoSeed", longVideoSeed);
	longVideoRenderPresetIndex = std::clamp(
		getInt(settings, "longVideoRenderPresetIndex", longVideoRenderPresetIndex),
		0,
		4);
	longVideoRenderModeIndex = std::clamp(
		getInt(settings, "longVideoRenderModeIndex", longVideoRenderModeIndex),
		0,
		3);
	longVideoRenderSelectedChunkIndex = std::max(
		0,
		getInt(settings, "longVideoRenderSelectedChunkIndex", longVideoRenderSelectedChunkIndex));
	if (settings.contains("modeMaxTokens") && settings["modeMaxTokens"].is_array()) {
		for (size_t i = 0; i < std::min<size_t>(settings["modeMaxTokens"].size(), kModeCount); ++i) {
			modeMaxTokens[i] = std::clamp(settings["modeMaxTokens"][i].get<int>(), 32, 4096);
		}
	}
	if (settings.contains("modeTextBackendIndices") && settings["modeTextBackendIndices"].is_array()) {
		for (size_t i = 0; i < std::min<size_t>(settings["modeTextBackendIndices"].size(), kModeCount); ++i) {
			modeTextBackendIndices[i] = std::clamp(settings["modeTextBackendIndices"][i].get<int>(), 0, 1);
		}
	}

	copyJsonString(chatInput, sizeof(chatInput), buffers, "chatInput");
	copyJsonString(easyPrimaryInput, sizeof(easyPrimaryInput), buffers, "easyPrimaryInput");
	copyJsonString(easySecondaryInput, sizeof(easySecondaryInput), buffers, "easySecondaryInput");
	copyJsonString(scriptInput, sizeof(scriptInput), buffers, "scriptInput");
	copyJsonString(summarizeInput, sizeof(summarizeInput), buffers, "summarizeInput");
	copyJsonString(writeInput, sizeof(writeInput), buffers, "writeInput");
	copyJsonString(translateInput, sizeof(translateInput), buffers, "translateInput");
	copyJsonString(
		voiceTranslatorAudioPath,
		sizeof(voiceTranslatorAudioPath),
		buffers,
		"voiceTranslatorAudioPath");
	copyJsonString(videoEssayTopic, sizeof(videoEssayTopic), buffers, "videoEssayTopic");
	copyJsonString(videoEssaySeedUrl, sizeof(videoEssaySeedUrl), buffers, "videoEssaySeedUrl");
	copyJsonString(
		videoEssaySourceVideoPath,
		sizeof(videoEssaySourceVideoPath),
		buffers,
		"videoEssaySourceVideoPath");
	copyJsonString(longVideoConcept, sizeof(longVideoConcept), buffers, "longVideoConcept");
	copyJsonString(longVideoStyle, sizeof(longVideoStyle), buffers, "longVideoStyle");
	copyJsonString(
		longVideoNegativeStyle,
		sizeof(longVideoNegativeStyle),
		buffers,
		"longVideoNegativeStyle");
	copyJsonString(
		longVideoContinuityGoal,
		sizeof(longVideoContinuityGoal),
		buffers,
		"longVideoContinuityGoal");
	copyJsonString(
		longVideoRenderSourceImagePath,
		sizeof(longVideoRenderSourceImagePath),
		buffers,
		"longVideoRenderSourceImagePath");
	copyJsonString(
		longVideoRenderEndImagePath,
		sizeof(longVideoRenderEndImagePath),
		buffers,
		"longVideoRenderEndImagePath");
	copyJsonString(
		longVideoRenderOutputDir,
		sizeof(longVideoRenderOutputDir),
		buffers,
		"longVideoRenderOutputDir");
	copyJsonString(
		longVideoRenderOutputPrefix,
		sizeof(longVideoRenderOutputPrefix),
		buffers,
		"longVideoRenderOutputPrefix");
	copyJsonString(customInput, sizeof(customInput), buffers, "customInput");
	copyJsonString(customSystemPrompt, sizeof(customSystemPrompt), buffers, "customSystemPrompt");
	copyJsonString(sourceUrlsInput, sizeof(sourceUrlsInput), buffers, "sourceUrlsInput");
	copyJsonString(citationTopic, sizeof(citationTopic), buffers, "citationTopic");
	copyJsonString(citationSeedUrl, sizeof(citationSeedUrl), buffers, "citationSeedUrl");
	copyJsonString(textServerUrl, sizeof(textServerUrl), buffers, "textServerUrl");
	copyJsonString(textServerModel, sizeof(textServerModel), buffers, "textServerModel");
	copyJsonString(customModelPath, sizeof(customModelPath), buffers, "customModelPath");
	copyJsonString(
		customVideoRenderModelPath,
		sizeof(customVideoRenderModelPath),
		buffers,
		"customVideoRenderModelPath");
	copyJsonString(visionPrompt, sizeof(visionPrompt), buffers, "visionPrompt");
	copyJsonString(visionImagePath, sizeof(visionImagePath), buffers, "visionImagePath");
	copyJsonString(visionVideoPath, sizeof(visionVideoPath), buffers, "visionVideoPath");
	copyJsonString(visionModelPath, sizeof(visionModelPath), buffers, "visionModelPath");
	copyJsonString(visionServerUrl, sizeof(visionServerUrl), buffers, "visionServerUrl");
	copyJsonString(videoSidecarUrl, sizeof(videoSidecarUrl), buffers, "videoSidecarUrl");
	copyJsonString(videoSidecarModel, sizeof(videoSidecarModel), buffers, "videoSidecarModel");
	copyJsonString(visionSystemPrompt, sizeof(visionSystemPrompt), buffers, "visionSystemPrompt");
	copyJsonString(videoPlanJson, sizeof(videoPlanJson), buffers, "videoPlanJson");
	copyJsonString(videoEditPlanJson, sizeof(videoEditPlanJson), buffers, "videoEditPlanJson");
	copyJsonString(montageSubtitlePath, sizeof(montageSubtitlePath), buffers, "montageSubtitlePath");
	copyJsonString(montageGoal, sizeof(montageGoal), buffers, "montageGoal");
	copyJsonString(montageEdlTitle, sizeof(montageEdlTitle), buffers, "montageEdlTitle");
	copyJsonString(montageReelName, sizeof(montageReelName), buffers, "montageReelName");
	copyJsonString(montageClipPaths, sizeof(montageClipPaths), buffers, "montageClipPaths");
	copyJsonString(montageRenderAudioPath, sizeof(montageRenderAudioPath), buffers, "montageRenderAudioPath");
	copyJsonString(videoEditGoal, sizeof(videoEditGoal), buffers, "videoEditGoal");
	copyJsonString(speechAudioPath, sizeof(speechAudioPath), buffers, "speechAudioPath");
	copyJsonString(speechExecutable, sizeof(speechExecutable), buffers, "speechExecutable");
	copyJsonString(speechModelPath, sizeof(speechModelPath), buffers, "speechModelPath");
	copyJsonString(speechServerUrl, sizeof(speechServerUrl), buffers, "speechServerUrl");
	copyJsonString(speechServerModel, sizeof(speechServerModel), buffers, "speechServerModel");
	copyJsonString(speechPrompt, sizeof(speechPrompt), buffers, "speechPrompt");
	copyJsonString(speechLanguageHint, sizeof(speechLanguageHint), buffers, "speechLanguageHint");
	copyJsonString(ttsInput, sizeof(ttsInput), buffers, "ttsInput");
	copyJsonString(ttsExecutablePath, sizeof(ttsExecutablePath), buffers, "ttsExecutablePath");
	copyJsonString(ttsModelPath, sizeof(ttsModelPath), buffers, "ttsModelPath");
	copyJsonString(ttsSpeakerPath, sizeof(ttsSpeakerPath), buffers, "ttsSpeakerPath");
	copyJsonString(chatTtsModelPath, sizeof(chatTtsModelPath), buffers, "chatTtsModelPath");
	copyJsonString(chatTtsSpeakerPath, sizeof(chatTtsSpeakerPath), buffers, "chatTtsSpeakerPath");
	copyJsonString(ttsSpeakerReferencePath, sizeof(ttsSpeakerReferencePath), buffers, "ttsSpeakerReferencePath");
	copyJsonString(ttsOutputPath, sizeof(ttsOutputPath), buffers, "ttsOutputPath");
	copyJsonString(ttsPromptAudioPath, sizeof(ttsPromptAudioPath), buffers, "ttsPromptAudioPath");
	copyJsonString(ttsLanguage, sizeof(ttsLanguage), buffers, "ttsLanguage");
	copyJsonString(diffusionPrompt, sizeof(diffusionPrompt), buffers, "diffusionPrompt");
	copyJsonString(diffusionInstruction, sizeof(diffusionInstruction), buffers, "diffusionInstruction");
	copyJsonString(diffusionNegativePrompt, sizeof(diffusionNegativePrompt), buffers, "diffusionNegativePrompt");
	copyJsonString(diffusionRankingPrompt, sizeof(diffusionRankingPrompt), buffers, "diffusionRankingPrompt");
	copyJsonString(diffusionModelPath, sizeof(diffusionModelPath), buffers, "diffusionModelPath");
	copyJsonString(diffusionVaePath, sizeof(diffusionVaePath), buffers, "diffusionVaePath");
	copyJsonString(diffusionInitImagePath, sizeof(diffusionInitImagePath), buffers, "diffusionInitImagePath");
	copyJsonString(diffusionMaskImagePath, sizeof(diffusionMaskImagePath), buffers, "diffusionMaskImagePath");
	copyJsonString(diffusionOutputDir, sizeof(diffusionOutputDir), buffers, "diffusionOutputDir");
	copyJsonString(diffusionOutputPrefix, sizeof(diffusionOutputPrefix), buffers, "diffusionOutputPrefix");
	copyJsonString(diffusionSampler, sizeof(diffusionSampler), buffers, "diffusionSampler");
	copyJsonString(imageSearchPrompt, sizeof(imageSearchPrompt), buffers, "imageSearchPrompt");
	copyJsonString(clipPrompt, sizeof(clipPrompt), buffers, "clipPrompt");
	copyJsonString(clipModelPath, sizeof(clipModelPath), buffers, "clipModelPath");
	copyJsonString(clipImagePaths, sizeof(clipImagePaths), buffers, "clipImagePaths");
	copyJsonString(musicToImageDescription, sizeof(musicToImageDescription), buffers, "musicToImageDescription");
	copyJsonString(musicToImageLyrics, sizeof(musicToImageLyrics), buffers, "musicToImageLyrics");
	copyJsonString(musicToImageStyle, sizeof(musicToImageStyle), buffers, "musicToImageStyle");
	copyJsonString(imageToMusicDescription, sizeof(imageToMusicDescription), buffers, "imageToMusicDescription");
	copyJsonString(imageToMusicSceneNotes, sizeof(imageToMusicSceneNotes), buffers, "imageToMusicSceneNotes");
	copyJsonString(imageToMusicStyle, sizeof(imageToMusicStyle), buffers, "imageToMusicStyle");
	copyJsonString(imageToMusicInstrumentation, sizeof(imageToMusicInstrumentation), buffers, "imageToMusicInstrumentation");
	copyJsonString(imageToMusicAbcTitle, sizeof(imageToMusicAbcTitle), buffers, "imageToMusicAbcTitle");
	copyJsonString(imageToMusicAbcKey, sizeof(imageToMusicAbcKey), buffers, "imageToMusicAbcKey");
	copyJsonString(imageToMusicAbcOutputPath, sizeof(imageToMusicAbcOutputPath), buffers, "imageToMusicAbcOutputPath");
	copyJsonString(aceStepServerUrl, sizeof(aceStepServerUrl), buffers, "aceStepServerUrl");
	copyJsonString(aceStepPrompt, sizeof(aceStepPrompt), buffers, "aceStepPrompt");
	copyJsonString(aceStepLyrics, sizeof(aceStepLyrics), buffers, "aceStepLyrics");
	copyJsonString(aceStepAudioPath, sizeof(aceStepAudioPath), buffers, "aceStepAudioPath");
	copyJsonString(aceStepOutputDir, sizeof(aceStepOutputDir), buffers, "aceStepOutputDir");
	copyJsonString(aceStepOutputPrefix, sizeof(aceStepOutputPrefix), buffers, "aceStepOutputPrefix");
	copyJsonString(aceStepKeyscale, sizeof(aceStepKeyscale), buffers, "aceStepKeyscale");
	copyJsonString(aceStepTimesignature, sizeof(aceStepTimesignature), buffers, "aceStepTimesignature");
	copyJsonString(milkdropPrompt, sizeof(milkdropPrompt), buffers, "milkdropPrompt");
	copyJsonString(milkdropPresetPath, sizeof(milkdropPresetPath), buffers, "milkdropPresetPath");

	visionTaskIndex = std::clamp(getInt(indices, "visionTaskIndex", visionTaskIndex), 0, 2);
	videoTaskIndex = std::clamp(getInt(indices, "videoTaskIndex", videoTaskIndex), 0, 4);
	visionVideoMaxFrames = std::clamp(getInt(indices, "visionVideoMaxFrames", visionVideoMaxFrames), 1, 12);
	easyActionIndex = std::clamp(getInt(indices, "easyActionIndex", easyActionIndex), 0, 5);
	easyCitationCount = std::clamp(getInt(indices, "easyCitationCount", easyCitationCount), 1, 12);
	videoPlanBeatCount = std::clamp(getInt(indices, "videoPlanBeatCount", videoPlanBeatCount), 1, 12);
	videoPlanSceneCount = std::clamp(getInt(indices, "videoPlanSceneCount", videoPlanSceneCount), 1, 8);
	videoPlanGenerationMode = std::clamp(getInt(indices, "videoPlanGenerationMode", videoPlanGenerationMode), 0, 1);
	videoEditPresetIndex = std::clamp(getInt(indices, "videoEditPresetIndex", videoEditPresetIndex), 0, 5);
	videoEditClipCount = std::clamp(getInt(indices, "videoEditClipCount", videoEditClipCount), 1, 12);
	selectedVideoPlanSceneIndex = std::max(0, getInt(indices, "selectedVideoPlanSceneIndex", selectedVideoPlanSceneIndex));
	selectedVideoEssaySceneIndex = std::max(0, getInt(indices, "selectedVideoEssaySceneIndex", selectedVideoEssaySceneIndex));
	montageMaxClips = std::clamp(getInt(indices, "montageMaxClips", montageMaxClips), 1, 24);
	montageFps = std::clamp(getInt(indices, "montageFps", montageFps), 12, 60);
	speechTaskIndex = std::clamp(getInt(indices, "speechTaskIndex", speechTaskIndex), 0, 1);
	ttsTaskIndex = std::clamp(getInt(indices, "ttsTaskIndex", ttsTaskIndex), 0, 2);
	ttsSeed = getInt(indices, "ttsSeed", ttsSeed);
	ttsMaxTokens = std::max(0, getInt(indices, "ttsMaxTokens", ttsMaxTokens));
	diffusionTaskIndex = std::clamp(getInt(indices, "diffusionTaskIndex", diffusionTaskIndex), 0, 6);
	diffusionSelectionModeIndex = std::clamp(getInt(indices, "diffusionSelectionModeIndex", diffusionSelectionModeIndex), 0, 2);
	diffusionWidth = clampSupportedDiffusionImageSize(getInt(indices, "diffusionWidth", diffusionWidth));
	diffusionHeight = clampSupportedDiffusionImageSize(getInt(indices, "diffusionHeight", diffusionHeight));
	diffusionSteps = std::clamp(getInt(indices, "diffusionSteps", diffusionSteps), 1, 200);
	diffusionBatchCount = std::clamp(getInt(indices, "diffusionBatchCount", diffusionBatchCount), 1, 16);
	diffusionSeed = getInt(indices, "diffusionSeed", diffusionSeed);
	imageSearchMaxResults = std::clamp(getInt(indices, "imageSearchMaxResults", imageSearchMaxResults), 1, 32);
	clipTopK = std::clamp(getInt(indices, "clipTopK", clipTopK), 0, 16);
	clipVerbosity = std::clamp(getInt(indices, "clipVerbosity", clipVerbosity), 0, 2);
	imageToMusicDurationSeconds = std::clamp(
		getInt(indices, "imageToMusicDurationSeconds", imageToMusicDurationSeconds),
		8,
		90);
	imageToMusicAbcBars = std::clamp(
		getInt(indices, "imageToMusicAbcBars", imageToMusicAbcBars),
		8,
		32);
	aceStepBpm = std::max(0, getInt(indices, "aceStepBpm", aceStepBpm));
	aceStepDurationSeconds = std::clamp(
		getInt(indices, "aceStepDurationSeconds", aceStepDurationSeconds),
		8,
		180);
	aceStepSeed = getInt(indices, "aceStepSeed", aceStepSeed);
	aceStepSelectedTrackIndex = std::max(
		0,
		getInt(indices, "aceStepSelectedTrackIndex", aceStepSelectedTrackIndex));
	musicVideoSectionCount = std::clamp(
		getInt(indices, "musicVideoSectionCount", musicVideoSectionCount),
		2,
		8);
	musicVideoStructureIndex = std::clamp(
		getInt(indices, "musicVideoStructureIndex", musicVideoStructureIndex),
		0,
		4);
	milkdropCategoryIndex = std::max(0, getInt(indices, "milkdropCategoryIndex", milkdropCategoryIndex));
	milkdropVariantCount = std::clamp(
		getInt(indices, "milkdropVariantCount", milkdropVariantCount),
		1,
		6);
	citationMaxResults = std::clamp(getInt(settings, "citationMaxResults", citationMaxResults), 1, 12);

	videoPlanDurationSeconds = std::clamp(getFloat(floats, "videoPlanDurationSeconds", videoPlanDurationSeconds), 1.0f, 30.0f);
	musicVideoCutIntensity = std::clamp(
		getFloat(floats, "musicVideoCutIntensity", musicVideoCutIntensity),
		0.0f,
		1.0f);
	videoEditTargetDurationSeconds = std::clamp(getFloat(floats, "videoEditTargetDurationSeconds", videoEditTargetDurationSeconds), 1.0f, 120.0f);
	montageMinScore = std::clamp(getFloat(floats, "montageMinScore", montageMinScore), 0.0f, 1.0f);
	montageTargetDurationSeconds = std::clamp(getFloat(floats, "montageTargetDurationSeconds", montageTargetDurationSeconds), 1.0f, 120.0f);
	montageMinSpacingSeconds = std::clamp(getFloat(floats, "montageMinSpacingSeconds", montageMinSpacingSeconds), 0.0f, 15.0f);
	montagePreRollSeconds = std::clamp(getFloat(floats, "montagePreRollSeconds", montagePreRollSeconds), 0.0f, 5.0f);
	montagePostRollSeconds = std::clamp(getFloat(floats, "montagePostRollSeconds", montagePostRollSeconds), 0.0f, 5.0f);
	ttsTemperature = std::clamp(getFloat(floats, "ttsTemperature", ttsTemperature), 0.0f, 2.0f);
	ttsRepetitionPenalty = std::clamp(getFloat(floats, "ttsRepetitionPenalty", ttsRepetitionPenalty), 1.0f, 3.0f);
	ttsTopP = std::clamp(getFloat(floats, "ttsTopP", ttsTopP), 0.0f, 1.0f);
	ttsMinP = std::clamp(getFloat(floats, "ttsMinP", ttsMinP), 0.0f, 1.0f);
	diffusionCfgScale = std::clamp(getFloat(floats, "diffusionCfgScale", diffusionCfgScale), 0.0f, 30.0f);
	diffusionStrength = std::clamp(getFloat(floats, "diffusionStrength", diffusionStrength), 0.0f, 1.0f);
	milkdropRandomness = std::clamp(getFloat(floats, "milkdropRandomness", milkdropRandomness), 0.0f, 1.0f);
	milkdropPreviewBeatSensitivity = std::clamp(
		getFloat(floats, "milkdropPreviewBeatSensitivity", milkdropPreviewBeatSensitivity),
		0.2f,
		3.0f);
	milkdropPreviewPresetDuration = std::clamp(
		getFloat(floats, "milkdropPreviewPresetDuration", milkdropPreviewPresetDuration),
		4.0f,
		60.0f);
	videoEssayTargetDurationSeconds = std::clamp(
		getFloat(floats, "videoEssayTargetDurationSeconds", videoEssayTargetDurationSeconds),
		30.0f,
		360.0f);
	longVideoTargetDurationSeconds = std::clamp(
		getFloat(floats, "longVideoTargetDurationSeconds", longVideoTargetDurationSeconds),
		8.0f,
		360.0f);
	easyTargetDurationSeconds = std::clamp(
		getFloat(floats, "easyTargetDurationSeconds", easyTargetDurationSeconds),
		30.0f,
		360.0f);

	chatSpeakReplies = getBool(bools, "chatSpeakReplies", chatSpeakReplies);
	chatUseCustomTtsVoice = getBool(bools, "chatUseCustomTtsVoice", chatUseCustomTtsVoice);
	summarizeSpeakOutput = getBool(bools, "summarizeSpeakOutput", summarizeSpeakOutput);
	easyUseCrawler = getBool(bools, "easyUseCrawler", easyUseCrawler);
	voiceTranslatorSpeakOutput = getBool(
		bools,
		"voiceTranslatorSpeakOutput",
		voiceTranslatorSpeakOutput);
	videoEssayUseCrawler = getBool(bools, "videoEssayUseCrawler", videoEssayUseCrawler);
	videoEssayIncludeCounterpoints = getBool(
		bools,
		"videoEssayIncludeCounterpoints",
		videoEssayIncludeCounterpoints);
	longVideoUsePromptInheritance = getBool(
		bools,
		"longVideoUsePromptInheritance",
		longVideoUsePromptInheritance);
	longVideoFavorLoopableEnding = getBool(
		bools,
		"longVideoFavorLoopableEnding",
		longVideoFavorLoopableEnding);
	musicToImageIncludeLyrics = getBool(bools, "musicToImageIncludeLyrics", musicToImageIncludeLyrics);
	imageToMusicInstrumentalOnly = getBool(
		bools,
		"imageToMusicInstrumentalOnly",
		imageToMusicInstrumentalOnly);
	aceStepUseWav = getBool(bools, "aceStepUseWav", aceStepUseWav);
	aceStepInstrumentalOnly = getBool(
		bools,
		"aceStepInstrumentalOnly",
		aceStepInstrumentalOnly);
	videoPlanMultiScene = getBool(bools, "videoPlanMultiScene", videoPlanMultiScene);
	videoPlanUseForGeneration = getBool(bools, "videoPlanUseForGeneration", videoPlanUseForGeneration);
	montagePreserveChronology = getBool(bools, "montagePreserveChronology", montagePreserveChronology);
	montageSubtitlePlaybackEnabled = getBool(bools, "montageSubtitlePlaybackEnabled", montageSubtitlePlaybackEnabled);
	montagePreviewTimingModeIndex = std::clamp(
		getInt(montagePreview, "timingModeIndex", montagePreviewTimingModeIndex),
		0,
		1);
	montagePreviewSubtitleSlavePath = getString(montagePreview, "subtitleSlavePath");
	montagePreviewTimelineSeconds = 0.0;
	montagePreviewTimelinePlaying = false;
	montagePreviewTimelineLastTickTime = 0.0f;
	videoEditWorkflowActiveStepIndex = getInt(videoEditWorkflow, "activeStepIndex", -1);
	videoEditWorkflowCompletedStepIndices.clear();
	if (videoEditWorkflow.contains("completedStepIndices") &&
		videoEditWorkflow["completedStepIndices"].is_array()) {
		for (const auto & item : videoEditWorkflow["completedStepIndices"]) {
			if (item.is_number_integer()) {
				videoEditWorkflowCompletedStepIndices.push_back(item.get<int>());
			}
		}
		std::sort(
			videoEditWorkflowCompletedStepIndices.begin(),
			videoEditWorkflowCompletedStepIndices.end());
		videoEditWorkflowCompletedStepIndices.erase(
			std::unique(
				videoEditWorkflowCompletedStepIndices.begin(),
				videoEditWorkflowCompletedStepIndices.end()),
			videoEditWorkflowCompletedStepIndices.end());
	}
	videoEditUseCurrentAnalysis = getBool(bools, "videoEditUseCurrentAnalysis", videoEditUseCurrentAnalysis);
	speechReturnTimestamps = getBool(bools, "speechReturnTimestamps", speechReturnTimestamps);
	speechLiveTranscriptionEnabled = getBool(bools, "speechLiveTranscriptionEnabled", speechLiveTranscriptionEnabled);
	ttsStreamAudio = getBool(bools, "ttsStreamAudio", ttsStreamAudio);
	ttsNormalizeText = getBool(bools, "ttsNormalizeText", ttsNormalizeText);
	diffusionNormalizeClipEmbeddings = getBool(bools, "diffusionNormalizeClipEmbeddings", diffusionNormalizeClipEmbeddings);
	diffusionSaveMetadata = getBool(bools, "diffusionSaveMetadata", diffusionSaveMetadata);
	clipNormalizeEmbeddings = getBool(bools, "clipNormalizeEmbeddings", clipNormalizeEmbeddings);
	milkdropAutoPreview = getBool(bools, "milkdropAutoPreview", milkdropAutoPreview);
	milkdropPreviewFeedMicWhileRecording = getBool(
		bools,
		"milkdropPreviewFeedMicWhileRecording",
		milkdropPreviewFeedMicWhileRecording);
	citationUseCrawler = getBool(settings, "citationUseCrawler", citationUseCrawler);

	speechLiveIntervalSeconds = std::clamp(getFloat(liveSpeech, "intervalSeconds", speechLiveIntervalSeconds), 0.5f, 5.0f);
	speechLiveWindowSeconds = std::clamp(getFloat(liveSpeech, "windowSeconds", speechLiveWindowSeconds), 2.0f, 30.0f);
	speechLiveOverlapSeconds = std::clamp(getFloat(liveSpeech, "overlapSeconds", speechLiveOverlapSeconds), 0.0f, 3.0f);

	scriptOutput = getString(outputs, "scriptOutput");
	summarizeOutput = getString(outputs, "summarizeOutput");
	writeOutput = getString(outputs, "writeOutput");
	translateOutput = getString(outputs, "translateOutput");
	voiceTranslatorStatus = getString(outputs, "voiceTranslatorStatus");
	voiceTranslatorTranscript = getString(outputs, "voiceTranslatorTranscript");
	easyOutput = getString(outputs, "easyOutput");
	videoEssayStatus = getString(outputs, "videoEssayStatus");
	videoEssayOutline = getString(outputs, "videoEssayOutline");
	videoEssayScript = getString(outputs, "videoEssayScript");
	videoEssaySrtText = getString(outputs, "videoEssaySrtText");
	videoEssayVisualConcept = getString(outputs, "videoEssayVisualConcept");
	videoEssayScenePlanJson = getString(outputs, "videoEssayScenePlanJson");
	videoEssayScenePlanSummary = getString(outputs, "videoEssayScenePlanSummary");
	videoEssayScenePlanningError = getString(outputs, "videoEssayScenePlanningError");
	videoEssayEditPlanJson = getString(outputs, "videoEssayEditPlanJson");
	videoEssayEditPlanSummary = getString(outputs, "videoEssayEditPlanSummary");
	videoEssayEditPlanningError = getString(outputs, "videoEssayEditPlanningError");
	videoEssayEditorBrief = getString(outputs, "videoEssayEditorBrief");
	videoEssayVlcPreviewStatusMessage = getString(outputs, "videoEssayVlcPreviewStatusMessage");
	videoEssayLastRenderedVideoPath = getString(outputs, "videoEssayLastRenderedVideoPath");
	longVideoStatus = getString(outputs, "longVideoStatus");
	longVideoContinuityBible = getString(outputs, "longVideoContinuityBible");
	longVideoManifestJson = getString(outputs, "longVideoManifestJson");
	longVideoRenderStatus = getString(outputs, "longVideoRenderStatus");
	longVideoRenderOutputDirectory = getString(outputs, "longVideoRenderOutputDirectory");
	longVideoRenderMetadataPath = getString(outputs, "longVideoRenderMetadataPath");
	longVideoRenderManifestPath = getString(outputs, "longVideoRenderManifestPath");
	longVideoRenderManifestJson = getString(outputs, "longVideoRenderManifestJson");
	customOutput = getString(outputs, "customOutput");
	citationOutput = getString(outputs, "citationOutput");
	visionOutput = getString(outputs, "visionOutput");
	montageSummary = getString(outputs, "montageSummary");
	montageEditorBrief = getString(outputs, "montageEditorBrief");
	montageEdlText = getString(outputs, "montageEdlText");
	montageSrtText = getString(outputs, "montageSrtText");
	montageVttText = getString(outputs, "montageVttText");
	montageClipPlaylistManifestPath = getString(outputs, "montageClipPlaylistManifestPath");
	montageClipPlaylistStatusMessage = getString(outputs, "montageClipPlaylistStatusMessage");
	montageClipRenderOutputPath = getString(outputs, "montageClipRenderOutputPath");
	videoPlanSummary = getString(outputs, "videoPlanSummary");
	videoEditPlanSummary = getString(outputs, "videoEditPlanSummary");
	speechOutput = getString(outputs, "speechOutput");
	speechDetectedLanguage = getString(outputs, "speechDetectedLanguage");
	speechTranscriptPath = getString(outputs, "speechTranscriptPath");
	speechSrtPath = getString(outputs, "speechSrtPath");
	speechSegmentCount = getInt(outputs, "speechSegmentCount", speechSegmentCount);
	chatLastAssistantReply = getString(outputs, "chatLastAssistantReply");
	chatTtsPreview.statusMessage = getString(outputs, "chatTtsStatusMessage");
	summarizeTtsPreview.statusMessage = getString(outputs, "summarizeTtsStatusMessage");
	ttsOutput = getString(outputs, "ttsOutput");
	diffusionOutput = getString(outputs, "diffusionOutput");
	musicToImagePromptOutput = getString(outputs, "musicToImagePromptOutput");
	musicToImageStatus = getString(outputs, "musicToImageStatus");
	musicVideoSectionSummary = getString(outputs, "musicVideoSectionSummary");
	imageToMusicPromptOutput = getString(outputs, "imageToMusicPromptOutput");
	imageToMusicNotationOutput = getString(outputs, "imageToMusicNotationOutput");
	imageToMusicStatus = getString(outputs, "imageToMusicStatus");
	imageToMusicSavedNotationPath = getString(outputs, "imageToMusicSavedNotationPath");
	aceStepStatus = getString(outputs, "aceStepStatus");
	aceStepGeneratedRequestJson = getString(outputs, "aceStepGeneratedRequestJson");
	aceStepUnderstoodSummary = getString(outputs, "aceStepUnderstoodSummary");
	aceStepUnderstoodCaption = getString(outputs, "aceStepUnderstoodCaption");
	aceStepUnderstoodLyrics = getString(outputs, "aceStepUnderstoodLyrics");
	aceStepUsedServerUrl = getString(outputs, "aceStepUsedServerUrl");
	imageSearchOutput = getString(outputs, "imageSearchOutput");
	clipOutput = getString(outputs, "clipOutput");
	milkdropOutput = getString(outputs, "milkdropOutput");
	milkdropSavedPresetPath = getString(outputs, "milkdropSavedPresetPath");
	milkdropPreviewStatus = getString(outputs, "milkdropPreviewStatus");

	chatMessages.clear();
	if (session.contains("chatMessages") && session["chatMessages"].is_array()) {
		for (const auto & entry : session["chatMessages"]) {
			Message msg;
			msg.role = getString(entry, "role");
			msg.text = getString(entry, "text");
			msg.timestamp = getFloat(entry, "timestamp", 0.0f);
			chatMessages.push_back(std::move(msg));
		}
	}

	deferredScriptSourceRestorePending = false;
	deferredScriptSourceType =
		static_cast<ofxGgmlScriptSourceType>(std::clamp(getInt(scriptSourceJson, "type", 0), 0, 3));
	deferredScriptSourcePath = getString(scriptSourceJson, "path");
	deferredScriptSourceInternetUrls = getString(scriptSourceJson, "internetUrls");
	copyStringToBuffer(scriptSourceGitHub, sizeof(scriptSourceGitHub), getString(scriptSourceJson, "github"));
	copyStringToBuffer(scriptSourceBranch, sizeof(scriptSourceBranch), getString(scriptSourceJson, "branch", "main"));
	scriptProjectMemory.setEnabled(getBool(scriptSourceJson, "projectMemoryEnabled", scriptProjectMemory.isEnabled()));
	scriptProjectMemory.setMemoryText(getString(scriptSourceJson, "projectMemoryText"));
	if (deferredScriptSourceType != ofxGgmlScriptSourceType::None) {
		deferredScriptSourceRestorePending = true;
	}

	applyTheme(themeIndex);
	applyLogLevel(logLevel);
	applyLiveSpeechTranscriberSettings();
	rebuildMontageSubtitleTrackFromText();
	return true;
}

void ofApp::autoSaveSession() {
	if (!lastSessionPath.empty()) {
		saveSession(lastSessionPath);
	}
}

void ofApp::clearDeferredScriptSourceRestore() {
	deferredScriptSourceRestorePending = false;
	deferredScriptSourceType = ofxGgmlScriptSourceType::None;
	deferredScriptSourcePath.clear();
	deferredScriptSourceInternetUrls.clear();
}

bool ofApp::restoreDeferredScriptSourceIfNeeded() {
	if (!deferredScriptSourceRestorePending) {
		return true;
	}

	bool success = true;
	switch (deferredScriptSourceType) {
	case ofxGgmlScriptSourceType::LocalFolder:
		if (!scriptLanguages.empty() &&
			selectedLanguageIndex >= 0 &&
			selectedLanguageIndex < static_cast<int>(scriptLanguages.size())) {
			scriptSource.setPreferredExtension(
				scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExtension);
		}
		success = !deferredScriptSourcePath.empty() &&
			scriptSource.setLocalFolder(deferredScriptSourcePath);
		break;
	case ofxGgmlScriptSourceType::GitHubRepo: {
		scriptSource.setGitHubMode();
		const std::string ownerRepo = trim(scriptSourceGitHub);
		const std::string branch = trim(scriptSourceBranch);
		success = ownerRepo.empty() ||
			scriptSource.setGitHubRepoFromInput(ownerRepo, branch);
		break;
	}
	case ofxGgmlScriptSourceType::Internet:
		scriptSource.setInternetUrls(
			splitStoredScriptSourceUrls(deferredScriptSourceInternetUrls));
		success = true;
		break;
	default:
		scriptSource.clear();
		success = true;
		break;
	}

	selectedScriptFileIndex = -1;
	if (success) {
		clearDeferredScriptSourceRestore();
	} else {
		logWithLevel(
			OF_LOG_WARNING,
			"Saved script source could not be restored. You can re-select it from the Script panel.");
	}
	return success;
}

void ofApp::autoLoadSession() {
	std::error_code ec;
	if (!lastSessionPath.empty() &&
		std::filesystem::exists(lastSessionPath, ec) &&
		!ec) {
		loadSession(lastSessionPath);
	}
}
