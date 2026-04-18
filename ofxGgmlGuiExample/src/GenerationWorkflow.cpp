#include "ofApp.h"

#include "utils/ConsoleHelpers.h"
#include "utils/TextPromptHelpers.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <sstream>
#include <thread>

void ofApp::runInference(
	AiMode mode,
	const std::string & userText,
	const std::string & systemPrompt,
	const std::string & overridePrompt,
	const ofxGgmlRealtimeInfoSettings & realtimeSettings) {
	if (generating.load() || !engineReady) return;
	if (mode == AiMode::Script) {
		lastScriptRequest = userText;
	}

	const int chatLanguageIndexSnapshot = chatLanguageIndex;
	const int selectedLanguageIndexSnapshot = selectedLanguageIndex;
	const int translateSourceLangSnapshot = translateSourceLang;
	const int translateTargetLangSnapshot = translateTargetLang;
	const int selectedScriptFileIndexSnapshot = selectedScriptFileIndex;
	const bool scriptIncludeRepoContextSnapshot = scriptIncludeRepoContext;
	const auto recentScriptTouchedFilesSnapshot = recentScriptTouchedFiles;
	const std::string lastScriptFailureReasonSnapshot = lastScriptFailureReason;
	const std::string scriptBackendLabelSnapshot = [&]() {
		if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
			const std::string serverUrl = trim(textServerUrl);
			return serverUrl.empty()
				? std::string("llama-server")
				: std::string("llama-server @ ") + serverUrl;
		}
		const std::string cliPath = trim(llamaCliCommand);
		return cliPath.empty() ? std::string("llama-completion") : cliPath;
	}();
	const auto chatLanguagesSnapshot = chatLanguages;
	const auto scriptLanguagesSnapshot = scriptLanguages;
	const auto translateLanguagesSnapshot = translateLanguages;

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = mode;
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput.clear();
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	workerThread = std::thread(
		[this,
		 mode,
		 userText,
		 systemPrompt,
		 overridePrompt,
		 realtimeSettings,
		 chatLanguageIndexSnapshot,
		 selectedLanguageIndexSnapshot,
		 translateSourceLangSnapshot,
		 translateTargetLangSnapshot,
		 selectedScriptFileIndexSnapshot,
		 scriptIncludeRepoContextSnapshot,
		 recentScriptTouchedFilesSnapshot,
		 lastScriptFailureReasonSnapshot,
		 scriptBackendLabelSnapshot,
		 chatLanguagesSnapshot,
		 scriptLanguagesSnapshot,
		 translateLanguagesSnapshot]() {
			try {
				const bool preserveLlamaInstructions = (mode == AiMode::Script);
				ofxGgmlRealtimeInfoSettings effectiveRealtimeSettings = realtimeSettings;
				if (liveContextMode == LiveContextMode::Offline) {
					effectiveRealtimeSettings.enabled = false;
					effectiveRealtimeSettings.explicitUrls.clear();
				} else if (
					mode == AiMode::Script &&
					scriptSource.getSourceType() == ofxGgmlScriptSourceType::Internet) {
					effectiveRealtimeSettings.heading = "Context fetched from loaded sources";
					effectiveRealtimeSettings.explicitUrls = scriptSource.getInternetUrls();
					effectiveRealtimeSettings.allowPromptUrlFetch = false;
					effectiveRealtimeSettings.allowDomainProviders = false;
					effectiveRealtimeSettings.allowGenericSearch = false;
					effectiveRealtimeSettings.enabled =
						(liveContextMode == LiveContextMode::LiveContext ||
						 liveContextMode == LiveContextMode::LiveContextStrictCitations);
					effectiveRealtimeSettings.requestCitations =
						(liveContextMode == LiveContextMode::LoadedSourcesOnly ||
						 liveContextMode == LiveContextMode::LiveContextStrictCitations);
				}

				auto buildPromptForCurrentMode = [&](const std::string & text) {
					switch (mode) {
					case AiMode::Chat: {
						ofxGgmlChatAssistantRequest request;
						request.userText = text;
						request.systemPrompt = systemPrompt;
						if (chatLanguageIndexSnapshot > 0 &&
							chatLanguageIndexSnapshot <
								static_cast<int>(chatLanguagesSnapshot.size())) {
							request.responseLanguage =
								chatLanguagesSnapshot[static_cast<size_t>(chatLanguageIndexSnapshot)]
									.name;
						}
						return chatAssistant.preparePrompt(request).prompt;
					}
					case AiMode::Script: {
						ofxGgmlCodeAssistantRequest request;
						request.action = ofxGgmlCodeAssistantAction::Ask;
						request.userInput = text;
						request.lastTask = lastScriptRequest;
						request.lastOutput = scriptOutput;
						if (selectedLanguageIndexSnapshot >= 0 &&
							selectedLanguageIndexSnapshot <
								static_cast<int>(scriptLanguagesSnapshot.size())) {
							request.language = scriptLanguagesSnapshot
								[static_cast<size_t>(selectedLanguageIndexSnapshot)];
						}

						ofxGgmlCodeAssistantContext context;
						context.scriptSource = &scriptSource;
						context.projectMemory = &scriptProjectMemory;
						context.focusedFileIndex = selectedScriptFileIndexSnapshot;
						context.includeRepoContext = scriptIncludeRepoContextSnapshot;
						context.maxRepoFiles = kMaxScriptContextFiles;
						context.maxFocusedFileChars = kMaxFocusedFileSnippetChars;
						context.activeMode = "Script";
						context.selectedBackend = scriptBackendLabelSnapshot;
						context.recentTouchedFiles = recentScriptTouchedFilesSnapshot;
						context.lastFailureReason = lastScriptFailureReasonSnapshot;
						return scriptAssistant.preparePrompt(request, context).prompt;
					}
					case AiMode::Summarize: {
						ofxGgmlTextAssistantRequest request;
						request.task = ofxGgmlTextTask::Summarize;
						request.inputText = text;
						return textAssistant.preparePrompt(request).prompt;
					}
					case AiMode::Write: {
						ofxGgmlTextAssistantRequest request;
						request.task = ofxGgmlTextTask::Rewrite;
						request.inputText = text;
						return textAssistant.preparePrompt(request).prompt;
					}
					case AiMode::Translate: {
						ofxGgmlTextAssistantRequest request;
						request.task = ofxGgmlTextTask::Translate;
						request.inputText = text;
						if (translateSourceLangSnapshot >= 0 &&
							translateSourceLangSnapshot <
								static_cast<int>(translateLanguagesSnapshot.size())) {
							request.sourceLanguage = translateLanguagesSnapshot
								[static_cast<size_t>(translateSourceLangSnapshot)]
									.name;
						}
						if (translateTargetLangSnapshot >= 0 &&
							translateTargetLangSnapshot <
								static_cast<int>(translateLanguagesSnapshot.size())) {
							request.targetLanguage = translateLanguagesSnapshot
								[static_cast<size_t>(translateTargetLangSnapshot)]
									.name;
						}
						return textAssistant.preparePrompt(request).prompt;
					}
					case AiMode::Custom: {
						ofxGgmlTextAssistantRequest request;
						request.task = ofxGgmlTextTask::Custom;
						request.inputText = text;
						request.systemPrompt = systemPrompt;
						return textAssistant.preparePrompt(request).prompt;
					}
					case AiMode::MilkDrop: {
						ofxGgmlMilkDropRequest request;
						request.prompt = text;
						return milkdropGenerator.preparePrompt(request).prompt;
					}
					default:
						return text;
					}
				};

				std::string prompt =
					overridePrompt.empty() ? buildPromptForCurrentMode(userText) : overridePrompt;
				if (effectiveRealtimeSettings.enabled ||
					!effectiveRealtimeSettings.explicitUrls.empty()) {
					prompt = ofxGgmlInference::buildPromptWithRealtimeInfo(
						prompt,
						userText,
						effectiveRealtimeSettings);
				}
				std::string result;
				std::string error;

				if (shouldLog(OF_LOG_VERBOSE)) {
					logWithLevel(OF_LOG_VERBOSE, "=== Generation started ===");
					logWithLevel(
						OF_LOG_VERBOSE,
						std::string("Mode: ") + modeLabels[static_cast<int>(mode)]);
					logWithLevel(
						OF_LOG_VERBOSE,
						"Prompt (" + ofToString(prompt.size()) + " chars):\n" + prompt);
				}

				bool promptTrimmed = false;
				const size_t estimatedTokens = prompt.size() / 3;
				const size_t maxCtxTokens = static_cast<size_t>(contextSize);
				if (estimatedTokens > maxCtxTokens) {
					prompt = clampPromptToContext(prompt, maxCtxTokens, promptTrimmed);
					if (promptTrimmed) {
						logWithLevel(
							OF_LOG_WARNING,
							"Prompt exceeded context budget (~" +
								std::to_string(estimatedTokens) + " tokens > " +
								std::to_string(maxCtxTokens) +
								"); trimmed automatically to fit.");
					}
				}

				const std::string trimmedPrompt = trim(prompt);
				std::string latestRawPartial;

				auto cleanPartialForDisplay = [&](const std::string & rawPartial) {
					std::string cleaned = rawPartial;
					if (mode == AiMode::Script) {
						(void)trimmedPrompt;
					} else {
						cleaned = cleanChatOutput(cleaned);
					}
					return cleaned;
				};

				auto streamCallback = [&](const std::string & partialRaw) {
					if (cancelRequested.load()) {
						return false;
					}
					latestRawPartial = partialRaw;
					const std::string partial = cleanPartialForDisplay(partialRaw);
					{
						std::lock_guard<std::mutex> lock(streamMutex);
						streamingOutput = partial;
					}
					return true;
				};

				bool success = runRealInference(
					mode,
					prompt,
					result,
					error,
					streamCallback,
					preserveLlamaInstructions);

				if (cancelRequested.load()) {
					result = "[Generation cancelled]";
				} else if (!success) {
					const std::string streamed = cleanPartialForDisplay(latestRawPartial);
					if (!streamed.empty()) {
						logWithLevel(
							OF_LOG_WARNING,
							"Inference reported failure but produced partial output (" +
								ofToString(streamed.size()) +
								" chars), using it.");
						result = streamed;
					} else {
						logWithLevel(OF_LOG_ERROR, "Inference error: " + error);
						result = "[Error] " + error;
					}
				} else if (shouldLog(OF_LOG_VERBOSE)) {
					logWithLevel(
						OF_LOG_VERBOSE,
						"Output (" + ofToString(result.size()) + " chars):\n" + result);
				}

				if (shouldLog(OF_LOG_VERBOSE)) {
					logWithLevel(OF_LOG_VERBOSE, "=== Generation finished ===");
				}

				bool likelyCutoff = isLikelyCutoffOutput(result, static_cast<int>(mode));

				if (stopAtNaturalBoundary && result.rfind("[Error]", 0) != 0) {
					if (mode == AiMode::Script) {
						if (!result.empty() && result.back() != '\n') {
							size_t cut = result.find_last_of('\n');
							if (cut != std::string::npos && cut > result.size() / 2) {
								result = trim(result.substr(0, cut));
							}
						}
					} else {
						size_t best = std::string::npos;
						for (size_t i = 0; i < result.size(); i++) {
							const char c = result[i];
							if (c == '.' || c == '!' || c == '?') {
								if (i + 1 == result.size() ||
									std::isspace(
										static_cast<unsigned char>(result[i + 1])) ||
									result[i + 1] == '"' || result[i + 1] == '\'') {
									best = i + 1;
								}
							}
						}
						if (best != std::string::npos && best > result.size() / 2) {
							result = trim(result.substr(0, best));
						}
					}
				}

				if (mode == AiMode::Script && autoContinueCutoff && likelyCutoff &&
					result.rfind("[Error]", 0) != 0 && !cancelRequested.load()) {
					const size_t tailChars = std::min<size_t>(result.size(), 600);
					const std::string tail = result.substr(result.size() - tailChars);
					ofxGgmlCodeAssistantRequest continuationRequest;
					continuationRequest.action =
						ofxGgmlCodeAssistantAction::ContinueCutoff;
					continuationRequest.userInput = tail;
					continuationRequest.lastOutput = tail;
					if (selectedLanguageIndexSnapshot >= 0 &&
						selectedLanguageIndexSnapshot <
							static_cast<int>(scriptLanguagesSnapshot.size())) {
						continuationRequest.language = scriptLanguagesSnapshot
							[static_cast<size_t>(selectedLanguageIndexSnapshot)];
					}
					std::string continuationPrompt =
						scriptAssistant.preparePrompt(continuationRequest, {}).prompt;
					bool contTrimmed = false;
					const size_t contEstimatedTokens = continuationPrompt.size() / 3;
					const size_t contMaxCtxTokens = static_cast<size_t>(contextSize);
					if (contEstimatedTokens > contMaxCtxTokens) {
						continuationPrompt = clampPromptToContext(
							continuationPrompt,
							contMaxCtxTokens,
							contTrimmed);
					}

					std::string continuationOut;
					std::string continuationErr;
					if (runRealInference(
							mode,
							continuationPrompt,
							continuationOut,
							continuationErr,
							nullptr,
							preserveLlamaInstructions) &&
						!continuationOut.empty()) {
						if (stopAtNaturalBoundary && continuationOut.back() != '\n') {
							size_t cut = continuationOut.find_last_of('\n');
							if (cut != std::string::npos &&
								cut > continuationOut.size() / 2) {
								continuationOut = trim(
									continuationOut.substr(0, cut));
							}
						}
						result += "\n" + continuationOut;
						likelyCutoff = isLikelyCutoffOutput(
							continuationOut,
							static_cast<int>(mode));
						logWithLevel(
							OF_LOG_NOTICE,
							"Auto-continued Script output after cutoff detection.");
					} else if (!continuationErr.empty()) {
						logWithLevel(
							OF_LOG_WARNING,
							"Auto-continue failed: " + continuationErr);
					}
				}

				{
					std::lock_guard<std::mutex> lock(outputMutex);
					if (!cancelRequested.load()) {
						pendingOutput = result;
						pendingRole = "assistant";
						pendingMode = mode;
						if (mode == AiMode::Script) {
							lastScriptOutputLikelyCutoff = likelyCutoff;
							const size_t tailChars =
								std::min<size_t>(result.size(), 600);
							lastScriptOutputTail =
								result.substr(result.size() - tailChars);
						}
					}
				}

				{
					std::lock_guard<std::mutex> lock(streamMutex);
					streamingOutput.clear();
				}

			} catch (const std::exception & e) {
				logWithLevel(
					OF_LOG_ERROR,
					std::string("Exception in worker thread: ") + e.what());
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingOutput =
					std::string("[Error] Internal exception: ") + e.what();
				pendingRole = "assistant";
				pendingMode = mode;
			} catch (...) {
				logWithLevel(OF_LOG_ERROR, "Unknown exception in worker thread");
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingOutput = "[Error] Unknown internal exception occurred.";
				pendingRole = "assistant";
				pendingMode = mode;
			}

			generating.store(false);
		});
}

void ofApp::stopGeneration() {
	if (generating.load()) {
		cancelRequested.store(true);
		killInferenceProcess();
	}
	if (workerThread.joinable()) {
		workerThread.join();
	}
	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput.clear();
	}
	generating.store(false);
}

void ofApp::applyPendingOutput() {
	std::lock_guard<std::mutex> lock(outputMutex);
	const bool hasPendingTextOutput = !pendingOutput.empty();
	if (!hasPendingTextOutput && !pendingImageSearchDirty) {
		return;
	}

	if (hasPendingTextOutput) {
		switch (pendingMode) {
		case AiMode::Chat:
			chatMessages.push_back({"assistant", pendingOutput, ofGetElapsedTimef()});
			fprintf(
				stderr,
				"%s\n",
				formatConsoleLogLine("ChatWindow", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::Script:
			scriptOutput = pendingOutput;
			scriptMessages.push_back({"assistant", pendingOutput, ofGetElapsedTimef()});
			if (pendingOutput.rfind("[Error]", 0) != 0) {
				scriptProjectMemory.addInteraction(lastScriptRequest, pendingOutput);
				lastScriptFailureReason.clear();
				const auto structured =
					ofxGgmlCodeAssistant::parseStructuredResult(pendingOutput);
				std::vector<std::string> touchedFiles;
				touchedFiles.reserve(
					structured.filesToTouch.size() +
					structured.patchOperations.size());
				for (const auto & fileIntent : structured.filesToTouch) {
					if (!fileIntent.filePath.empty()) {
						touchedFiles.push_back(fileIntent.filePath);
					}
				}
				for (const auto & patchOperation : structured.patchOperations) {
					if (!patchOperation.filePath.empty()) {
						touchedFiles.push_back(patchOperation.filePath);
					}
				}
				std::sort(touchedFiles.begin(), touchedFiles.end());
				touchedFiles.erase(
					std::unique(touchedFiles.begin(), touchedFiles.end()),
					touchedFiles.end());
				recentScriptTouchedFiles = touchedFiles;
				if (!structured.verificationCommands.empty()) {
					cachedScriptVerificationCommands =
						structured.verificationCommands;
				}
			} else {
				lastScriptFailureReason = pendingOutput;
			}
			fprintf(
				stderr,
				"%s\n",
				formatConsoleLogLine("Script", "AI", pendingOutput).c_str());
			break;
		case AiMode::Summarize:
			summarizeOutput = pendingOutput;
			fprintf(
				stderr,
				"%s\n",
				formatConsoleLogLine("Summarize", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::Write:
			writeOutput = pendingOutput;
			fprintf(
				stderr,
				"%s\n",
				formatConsoleLogLine("Write", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::Translate:
			translateOutput = pendingOutput;
			fprintf(
				stderr,
				"%s\n",
				formatConsoleLogLine("Translate", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::Custom:
			customOutput = pendingOutput;
			fprintf(
				stderr,
				"%s\n",
				formatConsoleLogLine("Custom", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::Vision:
			visionOutput = pendingOutput;
			visionSampledVideoFrames = pendingVisionSampledVideoFrames;
			montageSummary = pendingMontageSummary;
			montageEditorBrief = pendingMontageEditorBrief;
			montageEdlText = pendingMontageEdlText;
			montageSrtText = pendingMontageSrtText;
			montageVttText = pendingMontageVttText;
			montagePreviewBundle = pendingMontagePreviewBundle;
			montageSubtitleTrack = pendingMontageSubtitleTrack;
			montageSourceSubtitleTrack = pendingMontageSourceSubtitleTrack;
			montagePreviewTimelineSeconds = 0.0;
			montagePreviewTimelinePlaying = false;
			montagePreviewTimelineLastTickTime = 0.0f;
			montagePreviewSubtitleSlavePath.clear();
#if OFXGGML_HAS_OFXVLC4
			montageVlcPreviewLoadedSubtitlePath.clear();
			montageVlcPreviewError.clear();
#endif
			montagePreviewStatusMessage =
				montagePreviewBundle.montageTrack.cues.empty() &&
					montagePreviewBundle.sourceTrack.cues.empty()
					? std::string()
					: ofxGgmlMontagePreviewBridge::summarizeBundle(
						montagePreviewBundle);
			selectedMontageCueIndex = montageSubtitleTrack.cues.empty()
				? -1
				: std::clamp(
					selectedMontageCueIndex,
					0,
					static_cast<int>(montageSubtitleTrack.cues.size()) - 1);
			videoPlanSummary = pendingVideoPlanSummary;
			videoEditPlanSummary = pendingVideoEditPlanSummary;
			if (!pendingVideoPlanJson.empty()) {
				copyStringToBuffer(
					videoPlanJson,
					sizeof(videoPlanJson),
					pendingVideoPlanJson);
			}
			if (!pendingVideoEditPlanJson.empty()) {
				copyStringToBuffer(
					videoEditPlanJson,
					sizeof(videoEditPlanJson),
					pendingVideoEditPlanJson);
				resetVideoEditWorkflowState();
			}
			fprintf(
				stderr,
				"%s\n",
				formatConsoleLogLine("Vision", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::Speech:
			speechOutput = pendingOutput;
			speechDetectedLanguage = pendingSpeechDetectedLanguage;
			speechTranscriptPath = pendingSpeechTranscriptPath;
			speechSrtPath = pendingSpeechSrtPath;
			speechSegmentCount = pendingSpeechSegmentCount;
			fprintf(
				stderr,
				"%s\n",
				formatConsoleLogLine("Speech", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::Tts:
			ttsOutput = pendingOutput;
			ttsBackendName = pendingTtsBackendName;
			ttsElapsedMs = pendingTtsElapsedMs;
			ttsResolvedSpeakerPath = pendingTtsResolvedSpeakerPath;
			ttsAudioFiles = pendingTtsAudioFiles;
			ttsMetadata = pendingTtsMetadata;
			fprintf(
				stderr,
				"%s\n",
				formatConsoleLogLine("TTS", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::Diffusion:
			diffusionOutput = pendingOutput;
			diffusionBackendName = pendingDiffusionBackendName;
			diffusionElapsedMs = pendingDiffusionElapsedMs;
			diffusionGeneratedImages = pendingDiffusionImages;
			diffusionMetadata = pendingDiffusionMetadata;
			fprintf(
				stderr,
				"%s\n",
				formatConsoleLogLine("Diffusion", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::Clip:
			clipOutput = pendingOutput;
			clipBackendName = pendingClipBackendName;
			clipElapsedMs = pendingClipElapsedMs;
			clipEmbeddingDimension = pendingClipEmbeddingDimension;
			clipHits = pendingClipHits;
			fprintf(
				stderr,
				"%s\n",
				formatConsoleLogLine("CLIP", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::MilkDrop:
			milkdropOutput = pendingOutput;
			milkdropValidation = pendingMilkDropValidation;
			milkdropVariants = pendingMilkDropVariants;
			milkdropSelectedVariantIndex = milkdropVariants.empty() ? -1 : 0;
			fprintf(
				stderr,
				"%s\n",
				formatConsoleLogLine("MilkDrop", "AI", pendingOutput, true).c_str());
#if OFXGGML_HAS_OFXPROJECTM
			if (milkdropAutoPreview) {
				loadMilkDropPresetIntoPreview(milkdropOutput);
			}
#endif
			break;
		}
	}

	if (pendingImageSearchDirty) {
		imageSearchOutput = pendingImageSearchOutput;
		imageSearchBackendName = pendingImageSearchBackendName;
		imageSearchElapsedMs = pendingImageSearchElapsedMs;
		imageSearchResults = pendingImageSearchResults;
		if (imageSearchResults.empty()) {
			selectedImageSearchResultIndex = -1;
			imageSearchPreviewImage.clear();
			imageSearchPreviewLoadedPath.clear();
			imageSearchPreviewError.clear();
			imageSearchPreviewSourceUrl.clear();
		} else {
			selectedImageSearchResultIndex = std::clamp(
				selectedImageSearchResultIndex,
				0,
				static_cast<int>(imageSearchResults.size()) - 1);
		}
		fprintf(
			stderr,
			"%s\n",
			formatConsoleLogLine("Image Search", "AI", imageSearchOutput, true).c_str());
	}
	if (pendingCitationDirty) {
		citationOutput = pendingCitationOutput;
		citationBackendName = pendingCitationBackendName;
		citationElapsedMs = pendingCitationElapsedMs;
		citationResults = pendingCitationResults;
		fprintf(
			stderr,
			"%s\n",
			formatConsoleLogLine("Citations", "AI", citationOutput, true).c_str());
	}

	pendingOutput.clear();
	pendingSpeechDetectedLanguage.clear();
	pendingSpeechTranscriptPath.clear();
	pendingSpeechSrtPath.clear();
	pendingSpeechSegmentCount = 0;
	pendingTtsBackendName.clear();
	pendingTtsElapsedMs = 0.0f;
	pendingTtsResolvedSpeakerPath.clear();
	pendingTtsAudioFiles.clear();
	pendingTtsMetadata.clear();
	pendingMilkDropValidation = {};
	pendingMilkDropVariants.clear();
	pendingMontageSummary.clear();
	pendingMontageEditorBrief.clear();
	pendingMontageEdlText.clear();
	pendingMontageSrtText.clear();
	pendingMontageVttText.clear();
	pendingMontagePreviewBundle = {};
	pendingMontageSubtitleTrack = {};
	pendingMontageSourceSubtitleTrack = {};
	pendingVideoPlanJson.clear();
	pendingVideoPlanSummary.clear();
	pendingVideoEditPlanJson.clear();
	pendingVideoEditPlanSummary.clear();
	pendingVisionSampledVideoFrames.clear();
	pendingDiffusionBackendName.clear();
	pendingDiffusionElapsedMs = 0.0f;
	pendingDiffusionImages.clear();
	pendingDiffusionMetadata.clear();
	pendingImageSearchOutput.clear();
	pendingImageSearchBackendName.clear();
	pendingImageSearchElapsedMs = 0.0f;
	pendingImageSearchResults.clear();
	pendingImageSearchDirty = false;
	pendingCitationOutput.clear();
	pendingCitationBackendName.clear();
	pendingCitationElapsedMs = 0.0f;
	pendingCitationResults.clear();
	pendingCitationDirty = false;
	pendingClipBackendName.clear();
	pendingClipElapsedMs = 0.0f;
	pendingClipEmbeddingDimension = 0;
	pendingClipHits.clear();
}

void ofApp::drawPerformanceWindow() {
	performancePanel.draw(
		showPerformance,
		ggml,
		devices,
		lastComputeMs,
		lastNodeCount,
		lastBackendUsed,
		selectedBackendIndex,
		backendNames,
		numThreads,
		contextSize,
		batchSize,
		textInferenceBackend,
		detectedModelLayers,
		gpuLayers,
		seed,
		maxTokens,
		temperature,
		topP,
		topK,
		minP,
		repeatPenalty,
		devices);
}

void ofApp::copyToClipboard(const std::string & text) {
	ImGui::SetClipboardText(text.c_str());
}

void ofApp::drawStatusBar() {
	statusBar.draw(
		engineStatus,
		modelPresets,
		selectedModelIndex,
		activeMode,
		modeLabels,
		chatLanguageIndex,
		chatLanguages,
		selectedLanguageIndex,
		scriptLanguages,
		maxTokens,
		temperature,
		topP,
		topK,
		minP,
		liveContextMode,
		gpuLayers,
		detectedModelLayers,
		generating,
		generationStartTime,
		streamingOutput,
		streamMutex,
		lastComputeMs);
}

void ofApp::drawDeviceInfoWindow() {
	deviceInfoPanel.draw(showDeviceInfo, ggml, devices);
}

void ofApp::drawLogWindow() {
	logPanel.draw(showLog, logMessages, logMutex);
}

void ofApp::runHierarchicalReview(const std::string & overrideQuery) {
	const std::string effectiveReviewQuery = !trim(overrideQuery).empty()
		? trim(overrideQuery)
		: (std::strlen(scriptInput) > 0
			? std::string(scriptInput)
			: ofxGgmlCodeReview::defaultReviewQuery());
	runInference(AiMode::Script, effectiveReviewQuery);
}

void ofApp::exportChatHistory(const std::string & path) {
	std::ofstream out(path);
	if (!out.is_open()) return;

	out << "# Chat Export\n\n";
	for (const auto & msg : chatMessages) {
		if (msg.role == "user") {
			out << "**User:** " << msg.text << "\n\n";
		} else if (msg.role == "assistant") {
			out << "**Assistant:** " << msg.text << "\n\n";
		} else {
			out << "**" << msg.role << ":** " << msg.text << "\n\n";
		}
	}
}
