#include "ofApp.h"

#include "utils/ConsoleHelpers.h"
#include "utils/ImGuiHelpers.h"
#include "utils/PathHelpers.h"
#include "utils/SpeechHelpers.h"
#include "utils/TextPromptHelpers.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <sstream>
#include <thread>

namespace {

std::string summarizeStructuredScriptResult(
	const ofxGgmlCodeAssistantStructuredResult & structured) {
	std::ostringstream out;

	const std::string goal = trim(structured.goalSummary);
	const std::string approach = trim(structured.approachSummary);
	if (!goal.empty()) {
		out << goal << "\n";
	}
	if (!approach.empty()) {
		if (!goal.empty()) {
			out << "\n";
		}
		out << approach << "\n";
	}

	if (!structured.reviewFindings.empty()) {
		out << "\nFindings:\n";
		for (const auto & finding : structured.reviewFindings) {
			out << "- [P" << finding.priority << "] " << finding.title;
			if (!trim(finding.filePath).empty()) {
				out << " (" << finding.filePath;
				if (finding.line > 0) {
					out << ":" << finding.line;
				}
				out << ")";
			}
			out << "\n";
			if (!trim(finding.description).empty()) {
				out << "  " << trim(finding.description) << "\n";
			}
			if (!trim(finding.fixSuggestion).empty()) {
				out << "  Fix: " << trim(finding.fixSuggestion) << "\n";
			}
		}
	} else if (!structured.steps.empty()) {
		out << "\nPlan:\n";
		for (const auto & step : structured.steps) {
			if (!trim(step).empty()) {
				out << "- " << trim(step) << "\n";
			}
		}
	}

	if (!structured.verificationCommands.empty()) {
		out << "\nVerification:\n";
		for (const auto & command : structured.verificationCommands) {
			out << "- " << trim(command.label);
			if (!trim(command.executable).empty()) {
				out << ": " << trim(command.executable);
				for (const auto & arg : command.arguments) {
					out << " " << arg;
				}
			}
			if (!trim(command.expectedOutcome).empty()) {
				out << "\n  Expect: " << trim(command.expectedOutcome);
			}
			out << "\n";
		}
	}

	if (!structured.questions.empty()) {
		out << "\nOpen questions:\n";
		for (const auto & question : structured.questions) {
			if (!trim(question).empty()) {
				out << "- " << trim(question) << "\n";
			}
		}
	}

	if (!trim(structured.riskAssessment.level).empty() ||
		!structured.riskAssessment.reasons.empty()) {
		out << "\nRisk: ";
		if (!trim(structured.riskAssessment.level).empty()) {
			out << trim(structured.riskAssessment.level);
		} else {
			out << "unspecified";
		}
		out << "\n";
		for (const auto & reason : structured.riskAssessment.reasons) {
			if (!trim(reason).empty()) {
				out << "- " << trim(reason) << "\n";
			}
		}
	}

	return trim(out.str());
}

bool hasApprovalControlledToolCall(
	const std::vector<ofxGgmlCodeAssistantToolCall> & toolCalls) {
	return std::any_of(
		toolCalls.begin(),
		toolCalls.end(),
		[](const ofxGgmlCodeAssistantToolCall & toolCall) {
			return toolCall.requiresApproval;
		});
}

std::string buildScriptToolExecutionSummary(
	const ofxGgmlCodingAgentResult & result) {
	if (result.readOnly) {
		return {};
	}

	const bool hasExecutionDetails =
		hasApprovalControlledToolCall(result.assistantResult.proposedToolCalls) ||
		result.appliedChanges ||
		!result.applyResult.messages.empty() ||
		result.verificationAttempted ||
		!trim(result.verificationResult.summary).empty();
	if (!hasExecutionDetails) {
		return {};
	}

	std::ostringstream out;
	out << "Tool execution\n";
	if (!trim(result.summary).empty()) {
		out << trim(result.summary) << "\n";
	}
	if (!result.applyResult.touchedFiles.empty()) {
		out << "Touched files: ";
		for (size_t i = 0; i < result.applyResult.touchedFiles.size(); ++i) {
			if (i > 0) {
				out << ", ";
			}
			out << result.applyResult.touchedFiles[i];
		}
		out << "\n";
	}
	for (const auto & message : result.applyResult.messages) {
		const std::string trimmed = trim(message);
		if (!trimmed.empty()) {
			out << trimmed << "\n";
		}
	}
	if (!trim(result.verificationResult.summary).empty()) {
		out << trim(result.verificationResult.summary) << "\n";
	}
	return trim(out.str());
}

} // namespace

ofApp::InferenceModePromptSnapshot ofApp::makeInferenceModePromptSnapshot() const {
	InferenceModePromptSnapshot snapshot;
	snapshot.chatLanguageIndex = chatLanguageIndex;
	snapshot.translateSourceLangIndex = translateSourceLang;
	snapshot.translateTargetLangIndex = translateTargetLang;
	snapshot.chatLanguages = chatLanguages;
	snapshot.translateLanguages = translateLanguages;
	snapshot.script.selectedLanguageIndex = selectedLanguageIndex;
	snapshot.script.focusedFileIndex = selectedScriptFileIndex;
	snapshot.script.includeRepoContext = scriptIncludeRepoContext;
	snapshot.script.languages = scriptLanguages;
	snapshot.script.recentTouchedFiles = recentScriptTouchedFiles;
	snapshot.script.lastTask = lastScriptRequest;
	snapshot.script.lastOutput = scriptOutput;
	snapshot.script.lastFailureReason = lastScriptFailureReason;
	snapshot.script.backendLabel = [&]() {
		if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
			const std::string serverUrl = trim(textServerUrl);
			return serverUrl.empty()
				? std::string("llama-server")
				: std::string("llama-server @ ") + serverUrl;
		}
		const std::string cliPath = trim(getLlamaCliCommand());
		return cliPath.empty() ? std::string("llama-completion") : cliPath;
	}();
	return snapshot;
}

ofxGgmlTextAssistantRequest ofApp::buildTextAssistantRequestForMode(
	AiMode mode,
	const std::string & inputText,
	const std::string & systemPrompt,
	const InferenceModePromptSnapshot & snapshot) const {
	ofxGgmlTextAssistantRequest request;
	request.inputText = inputText;
	switch (mode) {
	case AiMode::Summarize:
		request.task = ofxGgmlTextTask::Summarize;
		break;
	case AiMode::Write:
		request.task = ofxGgmlTextTask::Rewrite;
		break;
	case AiMode::Translate:
		request.task = ofxGgmlTextTask::Translate;
		if (snapshot.translateSourceLangIndex >= 0 &&
			snapshot.translateSourceLangIndex <
				static_cast<int>(snapshot.translateLanguages.size())) {
			request.sourceLanguage = snapshot.translateLanguages
				[static_cast<size_t>(snapshot.translateSourceLangIndex)]
					.name;
		}
		if (snapshot.translateTargetLangIndex >= 0 &&
			snapshot.translateTargetLangIndex <
				static_cast<int>(snapshot.translateLanguages.size())) {
			request.targetLanguage = snapshot.translateLanguages
				[static_cast<size_t>(snapshot.translateTargetLangIndex)]
					.name;
		}
		break;
	case AiMode::Custom:
	case AiMode::VideoEssay:
		request.task = ofxGgmlTextTask::Custom;
		request.systemPrompt = systemPrompt;
		break;
	default:
		request.task = ofxGgmlTextTask::Custom;
		break;
	}
	return request;
}

ofxGgmlCodeAssistantRequest ofApp::buildScriptAssistantRequest(
	const std::string & inputText,
	const InferenceModePromptSnapshot & snapshot) const {
	ofxGgmlCodeAssistantRequest request;
	request.action = ofxGgmlCodeAssistantAction::Ask;
	request.userInput = inputText;
	request.lastTask = snapshot.script.lastTask;
	request.lastOutput = snapshot.script.lastOutput;
	if (snapshot.script.selectedLanguageIndex >= 0 &&
		snapshot.script.selectedLanguageIndex <
			static_cast<int>(snapshot.script.languages.size())) {
		request.language = snapshot.script.languages
			[static_cast<size_t>(snapshot.script.selectedLanguageIndex)];
	}
	if (scriptSource.getSourceType() == ofxGgmlScriptSourceType::Internet) {
		request.webUrls = scriptSource.getInternetUrls();
	}
	return request;
}

ofxGgmlCodeAssistantContext ofApp::buildScriptAssistantContext(
	const InferenceModePromptSnapshot & snapshot) {
	ofxGgmlCodeAssistantContext context;
	context.scriptSource = &scriptSource;
	context.projectMemory = &scriptProjectMemory;
	context.focusedFileIndex = snapshot.script.focusedFileIndex;
	context.includeRepoContext = snapshot.script.includeRepoContext;
	context.maxRepoFiles = kDefaultMaxScriptContextFiles;
	context.maxFocusedFileChars = kDefaultMaxFocusedFileSnippetChars;
	context.activeMode = "Script";
	context.selectedBackend = snapshot.script.backendLabel;
	context.recentTouchedFiles = snapshot.script.recentTouchedFiles;
	context.lastFailureReason = snapshot.script.lastFailureReason;
	return context;
}

std::string ofApp::buildPromptForMode(
	AiMode mode,
	const std::string & inputText,
	const std::string & systemPrompt,
	const InferenceModePromptSnapshot & snapshot) {
	switch (mode) {
	case AiMode::Chat: {
		ofxGgmlChatAssistantRequest request;
		request.userText = inputText;
		request.systemPrompt = systemPrompt;
		if (snapshot.chatLanguageIndex >= 0 &&
			snapshot.chatLanguageIndex <
				static_cast<int>(snapshot.chatLanguages.size())) {
			request.responseLanguage = snapshot.chatLanguages
				[static_cast<size_t>(snapshot.chatLanguageIndex)]
					.name;
		}
		return chatAssistant.preparePrompt(request).prompt;
	}
	case AiMode::Script:
		return scriptAssistant
			.preparePrompt(
				buildScriptAssistantRequest(inputText, snapshot),
				buildScriptAssistantContext(snapshot))
			.prompt;
	case AiMode::Summarize:
	case AiMode::Write:
	case AiMode::Translate:
	case AiMode::Custom:
	case AiMode::VideoEssay:
		return textAssistant.preparePrompt(
			buildTextAssistantRequestForMode(
				mode,
				inputText,
				systemPrompt,
				snapshot))
			.prompt;
	case AiMode::MilkDrop: {
		ofxGgmlMilkDropRequest request;
		request.prompt = inputText;
		return milkdropGenerator.preparePrompt(request).prompt;
	}
	default:
		return inputText;
	}
}

std::string ofApp::buildScriptContinuationPrompt(
	const std::string & partialOutput,
	const InferenceModePromptSnapshot & snapshot) const {
	ofxGgmlCodeAssistantRequest request;
	request.action = ofxGgmlCodeAssistantAction::ContinueCutoff;
	request.userInput = partialOutput;
	request.lastOutput = partialOutput;
	if (snapshot.script.selectedLanguageIndex >= 0 &&
		snapshot.script.selectedLanguageIndex <
			static_cast<int>(snapshot.script.languages.size())) {
		request.language = snapshot.script.languages
			[static_cast<size_t>(snapshot.script.selectedLanguageIndex)];
	}
	return scriptAssistant.preparePrompt(request, {}).prompt;
}

void ofApp::runPreparedTextRequest(
	AiMode mode,
	const ofxGgmlTextAssistantRequest & request,
	const ofxGgmlRealtimeInfoSettings & realtimeSettings) {
	runInference(
		mode,
		request.inputText,
		request.systemPrompt,
		textAssistant.preparePrompt(request).prompt,
		realtimeSettings);
}

void ofApp::runScriptAssistantRequest(
	const ofxGgmlCodeAssistantRequest & request,
	const std::string & requestLabel,
	bool clearInputAfter,
	const ofxGgmlRealtimeInfoSettings &,
	const ofxGgmlCodeAssistantContext * contextOverride,
	bool forcePlanMode) {
	if (generating.load() || !engineReady) return;

	lastScriptRequest = request.userInput;
	const InferenceModePromptSnapshot promptSnapshot = makeInferenceModePromptSnapshot();
	const ofxGgmlCodeAssistantContext assistantContext =
		contextOverride != nullptr
			? *contextOverride
			: buildScriptAssistantContext(promptSnapshot);

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::Script;
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput.clear();
	}

	if (clearInputAfter) {
		std::memset(scriptInput, 0, sizeof(scriptInput));
	}

	{
		std::lock_guard<std::mutex> approvalLock(scriptAssistantApprovalMutex);
		scriptAssistantApprovalPending = false;
		scriptAssistantApprovalDecisionReady = false;
		scriptAssistantApprovalDecisionApproved = false;
		scriptAssistantApprovalDecisionRequestId = 0;
		scriptAssistantPendingApprovalToolCall = {};
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	workerThread = std::thread(
		[this, request, requestLabel, assistantContext, forcePlanMode]() {
			try {
				const std::string modelPath = getSelectedModelPath();
				std::string localError;
				const bool preferredServerBackend =
					(textInferenceBackend == TextInferenceBackend::LlamaServer);
				bool useServerBackend = preferredServerBackend;

				auto prepareCliBackend = [&](std::string * cliError = nullptr) -> bool {
					std::string backendError;
					if (modelPath.empty()) {
						backendError = "No model preset selected.";
					} else if (!std::filesystem::exists(modelPath)) {
						backendError = "Model file not found: " + modelPath;
					}
					if (!backendError.empty()) {
						if (cliError != nullptr) {
							*cliError = backendError;
						}
						return false;
					}
					if (!isLlamaCliReady()) {
						probeLlamaCli();
						if (!isLlamaCliReady()) {
							backendError =
								"Optional CLI fallback is not installed. Build it with scripts/build-llama-cli.sh if you want a local non-server fallback.";
							if (cliError != nullptr) {
								*cliError = backendError;
							}
							return false;
						}
					}
					const std::string cliCommand = getLlamaCliCommand();
					scriptAssistant.getInference().setCompletionExecutable(cliCommand);
					scriptAssistant.getInference().probeCompletionCapabilities(true);
					scriptCodingAgent.setCompletionExecutable(cliCommand);
					return true;
				};

				if (useServerBackend && !ensureTextServerReady(false, true)) {
					const std::string serverError = !textServerStatusMessage.empty()
						? textServerStatusMessage
						: "Server-backed inference is not ready.";
					std::string cliError;
					if (prepareCliBackend(&cliError)) {
						useServerBackend = false;
						if (shouldLog(OF_LOG_NOTICE)) {
							logWithLevel(
								OF_LOG_NOTICE,
								"Server-backed Script assistant is unavailable; falling back to local llama-completion for this request.");
						}
					} else {
						localError = serverError;
						if (!cliError.empty()) {
							localError += " CLI fallback unavailable: " + cliError;
						}
					}
				} else if (!useServerBackend && !prepareCliBackend(&localError)) {
					// localError already populated
				}

				if (!localError.empty()) {
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingOutput = "[Error] " + localError;
					pendingRole = "assistant";
					pendingMode = AiMode::Script;
					pendingScriptAssistantSessionDirty = false;
					pendingScriptAssistantEvents.clear();
					pendingScriptAssistantToolCalls.clear();
					return;
				}

				ofxGgmlInferenceSettings inferenceSettings;
				inferenceSettings.maxTokens = std::clamp(maxTokens, 1, 8192);
				inferenceSettings.temperature = std::isfinite(temperature)
					? std::clamp(temperature, 0.0f, 2.0f)
					: kDefaultInferenceTemp;
				inferenceSettings.topP = std::isfinite(topP)
					? std::clamp(topP, 0.0f, 1.0f)
					: kDefaultInferenceTopP;
				inferenceSettings.topK = std::clamp(topK, 0, 200);
				inferenceSettings.minP = std::isfinite(minP)
					? std::clamp(minP, 0.0f, 1.0f)
					: 0.0f;
				inferenceSettings.repeatPenalty = std::isfinite(repeatPenalty)
					? std::clamp(repeatPenalty, 1.0f, 2.0f)
					: kDefaultInferenceRepeatPenalty;
				inferenceSettings.contextSize = std::clamp(contextSize, 256, 16384);
				inferenceSettings.batchSize = std::clamp(batchSize, 32, 4096);
				inferenceSettings.threads = std::clamp(numThreads, 1, 128);
				inferenceSettings.gpuLayers = std::clamp(
					gpuLayers,
					0,
					detectedModelLayers > 0 ? detectedModelLayers : 128);
				inferenceSettings.seed = seed;
				inferenceSettings.simpleIo = true;
				inferenceSettings.singleTurn = true;
				inferenceSettings.autoProbeCliCapabilities = true;
				inferenceSettings.trimPromptToContext = true;
				inferenceSettings.allowBatchFallback = true;
				inferenceSettings.autoContinueCutoff = autoContinueCutoff;
				inferenceSettings.stopAtNaturalBoundary = stopAtNaturalBoundary;
				inferenceSettings.autoPromptCache = usePromptCache;
				inferenceSettings.promptCachePath =
					usePromptCache ? promptCachePathFor(modelPath, AiMode::Script) : std::string();
				inferenceSettings.mirostat = mirostatMode;
				inferenceSettings.mirostatTau = mirostatTau;
				inferenceSettings.mirostatEta = mirostatEta;
				inferenceSettings.useServerBackend = useServerBackend;
				if (useServerBackend) {
					inferenceSettings.serverUrl = effectiveTextServerUrl(textServerUrl);
					inferenceSettings.serverModel = trim(textServerModel);
				}
				if (!useServerBackend &&
					!backendNames.empty() &&
					selectedBackendIndex >= 0 &&
					selectedBackendIndex < static_cast<int>(backendNames.size())) {
					const std::string & selected =
						backendNames[static_cast<size_t>(selectedBackendIndex)];
					if (selected != "CPU") {
						inferenceSettings.device = selected;
					}
				}
				if (!useServerBackend && inferenceSettings.device.empty()) {
					const std::string backend = ggml.getBackendName();
					if (!backend.empty() && backend != "CPU" && backend != "none") {
						inferenceSettings.device = backend;
					}
				}
				if (!useServerBackend &&
					inferenceSettings.gpuLayers == 0 &&
					inferenceSettings.device != "CPU") {
					inferenceSettings.gpuLayers =
						detectedModelLayers > 0 ? detectedModelLayers : 999;
				}

				std::vector<ofxGgmlCodeAssistantEvent> assistantEvents;
				auto eventCallback =
					[&](const ofxGgmlCodeAssistantEvent & event) -> bool {
					assistantEvents.push_back(event);
					return !cancelRequested.load();
				};
				auto approvalCallback =
					[&](const ofxGgmlCodeAssistantToolCall & toolCall) -> bool {
					uint64_t requestId = 0;
					{
						std::lock_guard<std::mutex> approvalLock(
							scriptAssistantApprovalMutex);
						requestId = ++scriptAssistantApprovalRequestId;
						scriptAssistantPendingApprovalToolCall = toolCall;
						scriptAssistantApprovalPending = true;
						scriptAssistantApprovalDecisionReady = false;
						scriptAssistantApprovalDecisionApproved = false;
						scriptAssistantApprovalDecisionRequestId = 0;
					}
					scriptAssistantApprovalCv.notify_all();

					std::unique_lock<std::mutex> approvalLock(
						scriptAssistantApprovalMutex);
					scriptAssistantApprovalCv.wait(
						approvalLock,
						[&]() {
							return cancelRequested.load() ||
								(scriptAssistantApprovalDecisionReady &&
								 scriptAssistantApprovalDecisionRequestId == requestId);
						});

					const bool approved = !cancelRequested.load() &&
						scriptAssistantApprovalDecisionReady &&
						scriptAssistantApprovalDecisionRequestId == requestId &&
						scriptAssistantApprovalDecisionApproved;
					scriptAssistantApprovalPending = false;
					scriptAssistantApprovalDecisionReady = false;
					scriptAssistantApprovalDecisionApproved = false;
					scriptAssistantApprovalDecisionRequestId = 0;
					scriptAssistantPendingApprovalToolCall = {};
					return approved;
				};
				auto onChunk = [&](const std::string & chunk) -> bool {
					if (cancelRequested.load()) {
						return false;
					}
					std::lock_guard<std::mutex> lock(streamMutex);
					streamingOutput = chunk;
					return true;
				};

				ofxGgmlCodeAssistantSession localSessionCopy;
				{
					std::lock_guard<std::mutex> lock(outputMutex);
					localSessionCopy = scriptAssistantSession;
				}
				scriptCodingAgent.getSession() = localSessionCopy;

				ofxGgmlCodeAssistantRequest effectiveRequest = request;
				effectiveRequest.labelOverride = requestLabel;
				ofxGgmlCodingAgentRequest agentRequest;
				agentRequest.assistantRequest = effectiveRequest;
				agentRequest.taskLabel = requestLabel;

				ofxGgmlCodingAgentSettings agentSettings;
				agentSettings.mode =
					(forcePlanMode || scriptAgentModeIndex != 0)
						? ofxGgmlCodingAgentMode::Plan
						: ofxGgmlCodingAgentMode::Build;
				agentSettings.inferenceSettings = inferenceSettings;
				agentSettings.autoApply =
					(agentSettings.mode == ofxGgmlCodingAgentMode::Build);
				agentSettings.autoVerify =
					(agentSettings.mode == ofxGgmlCodingAgentMode::Build);
				agentSettings.requireStructuredResult = true;
				agentSettings.preferUnifiedDiff =
					(agentSettings.mode == ofxGgmlCodingAgentMode::Build);

				const auto agentResult = scriptCodingAgent.run(
					modelPath,
					agentRequest,
					assistantContext,
					agentSettings,
					approvalCallback,
					eventCallback,
					onChunk);
				localSessionCopy = scriptCodingAgent.getSession();

				std::string result = agentResult.assistantResult.inference.text;
				if (trim(result).empty() &&
					agentResult.assistantResult.inference.success) {
					result = summarizeStructuredScriptResult(
						agentResult.assistantResult.structured);
					if (trim(result).empty()) {
						result = trim(agentResult.summary);
					}
				}
				if (cancelRequested.load()) {
					result = "[Generation cancelled]";
				} else if (!agentResult.assistantResult.inference.success) {
					std::string streamedSnapshot;
					{
						std::lock_guard<std::mutex> lock(streamMutex);
						streamedSnapshot = streamingOutput;
					}
					if (!trim(streamedSnapshot).empty()) {
						result = streamedSnapshot;
					} else {
						const std::string assistantError = !agentResult.error.empty()
							? agentResult.error
							: (agentResult.assistantResult.inference.error.empty()
								? "Inference failed."
								: agentResult.assistantResult.inference.error);
						result = "[Error] " + assistantError;
					}
				}
				if (!cancelRequested.load() &&
					agentResult.assistantResult.inference.success) {
					const std::string toolExecutionSummary =
						buildScriptToolExecutionSummary(agentResult);
					if (!toolExecutionSummary.empty()) {
						if (!trim(result).empty()) {
							result += "\n\n";
						}
						result += toolExecutionSummary;
					}
				}

				const bool likelyCutoff =
					isLikelyCutoffOutput(result, static_cast<int>(AiMode::Script));

				{
					std::lock_guard<std::mutex> lock(outputMutex);
					if (!cancelRequested.load()) {
						pendingOutput = result;
						pendingRole = "assistant";
						pendingMode = AiMode::Script;
						pendingScriptAssistantSession = localSessionCopy;
						pendingScriptAssistantSessionDirty = true;
						pendingScriptAssistantEvents = std::move(assistantEvents);
						pendingScriptAssistantToolCalls =
							agentResult.assistantResult.proposedToolCalls;
						lastScriptOutputLikelyCutoff = likelyCutoff;
						const size_t tailChars = std::min<size_t>(result.size(), 600);
						lastScriptOutputTail =
							result.substr(result.size() - tailChars);
					}
				}

				{
					std::lock_guard<std::mutex> lock(streamMutex);
					streamingOutput.clear();
				}
				{
					std::lock_guard<std::mutex> approvalLock(scriptAssistantApprovalMutex);
					scriptAssistantApprovalPending = false;
					scriptAssistantApprovalDecisionReady = false;
					scriptAssistantApprovalDecisionApproved = false;
					scriptAssistantApprovalDecisionRequestId = 0;
					scriptAssistantPendingApprovalToolCall = {};
				}
				scriptAssistantApprovalCv.notify_all();
			} catch (const std::exception & e) {
				logWithLevel(
					OF_LOG_ERROR,
					std::string("Exception in Script assistant worker: ") + e.what());
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingOutput =
					std::string("[Error] Internal exception: ") + e.what();
				pendingRole = "assistant";
				pendingMode = AiMode::Script;
				pendingScriptAssistantSessionDirty = false;
				pendingScriptAssistantEvents.clear();
				pendingScriptAssistantToolCalls.clear();
				{
					std::lock_guard<std::mutex> approvalLock(scriptAssistantApprovalMutex);
						scriptAssistantApprovalPending = false;
						scriptAssistantApprovalDecisionReady = false;
						scriptAssistantApprovalDecisionApproved = false;
						scriptAssistantApprovalDecisionRequestId = 0;
						scriptAssistantPendingApprovalToolCall = {};
				}
				scriptAssistantApprovalCv.notify_all();
			} catch (...) {
				logWithLevel(OF_LOG_ERROR, "Unknown exception in Script assistant worker");
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingOutput = "[Error] Unknown internal exception occurred.";
				pendingRole = "assistant";
				pendingMode = AiMode::Script;
				pendingScriptAssistantSessionDirty = false;
				pendingScriptAssistantEvents.clear();
				pendingScriptAssistantToolCalls.clear();
				{
					std::lock_guard<std::mutex> approvalLock(scriptAssistantApprovalMutex);
						scriptAssistantApprovalPending = false;
						scriptAssistantApprovalDecisionReady = false;
						scriptAssistantApprovalDecisionApproved = false;
						scriptAssistantApprovalDecisionRequestId = 0;
						scriptAssistantPendingApprovalToolCall = {};
				}
				scriptAssistantApprovalCv.notify_all();
			}

			generating.store(false);
		});
}

void ofApp::runScriptInlineCompletionRequest(
	const std::string & targetFilePath,
	const std::string & prefix,
	const std::string & suffix,
	const std::string & instruction) {
	if (generating.load() || !engineReady) return;

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::Script;
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput.clear();
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	workerThread = std::thread(
		[this, targetFilePath, prefix, suffix, instruction]() {
			try {
				const std::string modelPath = getSelectedModelPath();
				std::string localError;
				const bool preferredServerBackend =
					(textInferenceBackend == TextInferenceBackend::LlamaServer);
				bool useServerBackend = preferredServerBackend;

				auto prepareCliBackend = [&](std::string * cliError = nullptr) -> bool {
					std::string backendError;
					if (modelPath.empty()) {
						backendError = "No model preset selected.";
					} else if (!std::filesystem::exists(modelPath)) {
						backendError = "Model file not found: " + modelPath;
					}
					if (!backendError.empty()) {
						if (cliError != nullptr) {
							*cliError = backendError;
						}
						return false;
					}
					if (!isLlamaCliReady()) {
						probeLlamaCli();
						if (!isLlamaCliReady()) {
							backendError =
								"Optional CLI fallback is not installed. Build it with scripts/build-llama-cli.sh if you want a local non-server fallback.";
							if (cliError != nullptr) {
								*cliError = backendError;
							}
							return false;
						}
					}
					scriptAssistant.getInference().setCompletionExecutable(getLlamaCliCommand());
					scriptAssistant.getInference().probeCompletionCapabilities(true);
					return true;
				};

				if (useServerBackend && !ensureTextServerReady(false, true)) {
					const std::string serverError = !textServerStatusMessage.empty()
						? textServerStatusMessage
						: "Server-backed inference is not ready.";
					std::string cliError;
					if (prepareCliBackend(&cliError)) {
						useServerBackend = false;
					} else {
						localError = serverError;
						if (!cliError.empty()) {
							localError += " CLI fallback unavailable: " + cliError;
						}
					}
				} else if (!useServerBackend && !prepareCliBackend(&localError)) {
				}

				if (!localError.empty()) {
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingOutput = "[Error] " + localError;
					pendingRole = "assistant";
					pendingMode = AiMode::Script;
					pendingScriptInlineCompletionOutput.clear();
					pendingScriptInlineCompletionTargetPath.clear();
					return;
				}

				ofxGgmlInferenceSettings inferenceSettings;
				inferenceSettings.maxTokens = std::clamp(maxTokens, 1, 8192);
				inferenceSettings.temperature = std::isfinite(temperature)
					? std::clamp(temperature, 0.0f, 2.0f)
					: kDefaultInferenceTemp;
				inferenceSettings.topP = std::isfinite(topP)
					? std::clamp(topP, 0.0f, 1.0f)
					: kDefaultInferenceTopP;
				inferenceSettings.topK = std::clamp(topK, 0, 200);
				inferenceSettings.minP = std::isfinite(minP)
					? std::clamp(minP, 0.0f, 1.0f)
					: 0.0f;
				inferenceSettings.repeatPenalty = std::isfinite(repeatPenalty)
					? std::clamp(repeatPenalty, 1.0f, 2.0f)
					: kDefaultInferenceRepeatPenalty;
				inferenceSettings.contextSize = std::clamp(contextSize, 256, 16384);
				inferenceSettings.batchSize = std::clamp(batchSize, 32, 4096);
				inferenceSettings.threads = std::clamp(numThreads, 1, 128);
				inferenceSettings.gpuLayers = std::clamp(
					gpuLayers,
					0,
					detectedModelLayers > 0 ? detectedModelLayers : 128);
				inferenceSettings.seed = seed;
				inferenceSettings.simpleIo = true;
				inferenceSettings.singleTurn = true;
				inferenceSettings.autoProbeCliCapabilities = true;
				inferenceSettings.trimPromptToContext = true;
				inferenceSettings.allowBatchFallback = true;
				inferenceSettings.autoPromptCache = usePromptCache;
				inferenceSettings.promptCachePath =
					usePromptCache ? promptCachePathFor(modelPath, AiMode::Script) : std::string();
				inferenceSettings.useServerBackend = useServerBackend;
				if (useServerBackend) {
					inferenceSettings.serverUrl = effectiveTextServerUrl(textServerUrl);
					inferenceSettings.serverModel = trim(textServerModel);
				}
				if (!useServerBackend &&
					!backendNames.empty() &&
					selectedBackendIndex >= 0 &&
					selectedBackendIndex < static_cast<int>(backendNames.size())) {
					const std::string & selected =
						backendNames[static_cast<size_t>(selectedBackendIndex)];
					if (selected != "CPU") {
						inferenceSettings.device = selected;
					}
				}

				ofxGgmlCodeAssistantInlineCompletionRequest request;
				if (!scriptLanguages.empty() &&
					selectedLanguageIndex >= 0 &&
					selectedLanguageIndex < static_cast<int>(scriptLanguages.size())) {
					request.language = scriptLanguages[static_cast<size_t>(selectedLanguageIndex)];
				}
				request.filePath = targetFilePath;
				request.prefix = prefix;
				request.suffix = suffix;
				request.instruction = instruction;
				request.maxTokens = std::clamp(maxTokens, 32, 512);
				request.singleLine = false;
				request.useFillInTheMiddle = !suffix.empty();

				auto onChunk = [&](const std::string & chunk) -> bool {
					if (cancelRequested.load()) {
						return false;
					}
					std::lock_guard<std::mutex> lock(streamMutex);
					streamingOutput = chunk;
					return true;
				};

				const auto completionResult = scriptAssistant.runInlineCompletion(
					modelPath,
					request,
					inferenceSettings,
					onChunk);

				std::string completion = completionResult.completion;
				if (cancelRequested.load()) {
					completion = "[Generation cancelled]";
				} else if (!completionResult.inference.success && trim(completion).empty()) {
					completion = "[Error] " + (completionResult.inference.error.empty()
						? std::string("Inline completion failed.")
						: completionResult.inference.error);
				}

				{
					std::lock_guard<std::mutex> lock(outputMutex);
					if (!cancelRequested.load()) {
						pendingOutput = completion;
						pendingRole = "assistant";
						pendingMode = AiMode::Script;
						pendingScriptInlineCompletionOutput = completion;
						pendingScriptInlineCompletionTargetPath = targetFilePath;
					}
				}

				{
					std::lock_guard<std::mutex> lock(streamMutex);
					streamingOutput.clear();
				}
			} catch (const std::exception & e) {
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingOutput =
					std::string("[Error] Inline completion exception: ") + e.what();
				pendingRole = "assistant";
				pendingMode = AiMode::Script;
				pendingScriptInlineCompletionOutput.clear();
				pendingScriptInlineCompletionTargetPath.clear();
			} catch (...) {
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingOutput = "[Error] Unknown inline completion exception occurred.";
				pendingRole = "assistant";
				pendingMode = AiMode::Script;
				pendingScriptInlineCompletionOutput.clear();
				pendingScriptInlineCompletionTargetPath.clear();
			}

			generating.store(false);
		});
}

void ofApp::runInference(
	AiMode mode,
	const std::string & userText,
	const std::string & systemPrompt,
	const std::string & overridePrompt,
	const ofxGgmlRealtimeInfoSettings & realtimeSettings) {
	if (generating.load() || !engineReady) return;
	if (mode == AiMode::Script) {
		ofxGgmlCodeAssistantRequest request =
			buildScriptAssistantRequest(userText, makeInferenceModePromptSnapshot());
		if (!overridePrompt.empty()) {
			request.bodyOverride = overridePrompt;
		}
		runScriptAssistantRequest(request, userText, false, realtimeSettings);
		return;
	}

	const InferenceModePromptSnapshot promptSnapshot = makeInferenceModePromptSnapshot();

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
		 promptSnapshot]() {
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

				std::string prompt =
					overridePrompt.empty()
						? buildPromptForMode(mode, userText, systemPrompt, promptSnapshot)
						: overridePrompt;
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
					std::string continuationPrompt =
						buildScriptContinuationPrompt(tail, promptSnapshot);
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

void ofApp::reapFinishedWorkerThread() {
	if (!generating.load() && workerThread.joinable()) {
		workerThread.join();
	}
}

void ofApp::stopGeneration(bool waitForCompletion) {
	if (generating.load()) {
		cancelRequested.store(true);
		killActiveInferenceProcess();
		scriptAssistantApprovalCv.notify_all();
		generatingStatus = "Cancelling generation...";
	}
	if (waitForCompletion && workerThread.joinable()) {
		workerThread.join();
	}
	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput.clear();
	}
	if (waitForCompletion) {
		generating.store(false);
	} else if (!generating.load()) {
		reapFinishedWorkerThread();
	}
}

void ofApp::applyPendingOutput() {
	std::lock_guard<std::mutex> lock(outputMutex);
	const bool hasPendingTextOutput = !pendingOutput.empty();
	if (!hasPendingTextOutput &&
		!pendingMusicToImageDirty &&
		!pendingImageToMusicDirty &&
		!pendingAceStepDirty &&
		!pendingImageSearchDirty &&
		!pendingCitationDirty &&
		!pendingVideoEssayDirty &&
		!pendingLongVideoDirty &&
		!pendingVoiceTranslatorDirty) {
		return;
	}

	if (hasPendingTextOutput) {
		switch (pendingMode) {
		case AiMode::Chat:
			chatMessages.push_back({"assistant", pendingOutput, ofGetElapsedTimef()});
			chatLastAssistantReply = pendingOutput;
			fprintf(
				stderr,
				"%s\n",
				formatConsoleLogLine("ChatWindow", "AI", pendingOutput, true).c_str());
			if (chatSpeakReplies && !trim(pendingOutput).empty()) {
				speakLatestChatReply(false);
			}
			break;
		case AiMode::Easy:
			easyOutput = pendingOutput;
			fprintf(
				stderr,
				"%s\n",
				formatConsoleLogLine("Easy", "AI", pendingOutput, true).c_str());
			break;
		case AiMode::Script:
			scriptOutput = pendingOutput;
			if (!pendingScriptInlineCompletionOutput.empty()) {
				scriptInlineCompletionOutput = pendingScriptInlineCompletionOutput;
				scriptInlineCompletionTargetPath = pendingScriptInlineCompletionTargetPath;
			}
			if (pendingScriptAssistantSessionDirty) {
				scriptAssistantSession = pendingScriptAssistantSession;
			}
			scriptAssistantEvents = pendingScriptAssistantEvents;
			scriptAssistantToolCalls = pendingScriptAssistantToolCalls;
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
			if (summarizeSpeakOutput && !trim(pendingOutput).empty()) {
				speakLatestSummary(false);
			}
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
			ttsPanelPreview.audioFiles = pendingTtsAudioFiles;
			ttsPanelPreview.selectedAudioIndex = 0;
			ttsPanelPreview.loadedAudioPath.clear();
			ttsPanelPreview.statusMessage = pendingOutput;
			if (!ttsPanelPreview.audioFiles.empty()) {
				ensureTtsPanelAudioLoaded(0, false);
			}
			if (chatTtsPreview.request.pending) {
				chatTtsPreview.audioFiles = pendingTtsAudioFiles;
				chatTtsPreview.selectedAudioIndex = 0;
				chatTtsPreview.loadedAudioPath.clear();
				chatTtsPreview.statusMessage = pendingOutput;
				chatTtsPreview.request.clear();
				if (!chatTtsPreview.audioFiles.empty()) {
					ensureChatTtsAudioLoaded(0, true);
				}
			}
			if (summarizeTtsPreview.request.pending) {
				summarizeTtsPreview.audioFiles = pendingTtsAudioFiles;
				summarizeTtsPreview.selectedAudioIndex = 0;
				summarizeTtsPreview.loadedAudioPath.clear();
				summarizeTtsPreview.statusMessage = pendingOutput;
				summarizeTtsPreview.request.clear();
				if (!summarizeTtsPreview.audioFiles.empty()) {
					ensureSummaryTtsAudioLoaded(0, true);
				}
			}
			if (translateTtsPreview.request.pending) {
				translateTtsPreview.audioFiles = pendingTtsAudioFiles;
				translateTtsPreview.selectedAudioIndex = 0;
				translateTtsPreview.loadedAudioPath.clear();
				translateTtsPreview.statusMessage = pendingOutput;
				translateTtsPreview.request.clear();
				if (!translateTtsPreview.audioFiles.empty()) {
					ensureTranslateTtsAudioLoaded(0, true);
				}
			}
			if (videoEssayTtsPreview.request.pending) {
				videoEssayTtsPreview.audioFiles = pendingTtsAudioFiles;
				videoEssayTtsPreview.selectedAudioIndex = 0;
				videoEssayTtsPreview.loadedAudioPath.clear();
				videoEssayTtsPreview.statusMessage = pendingOutput;
				videoEssayTtsPreview.request.clear();
				if (!videoEssayTtsPreview.audioFiles.empty()) {
					ensureVideoEssayTtsAudioLoaded(0, true);
				}
			}
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

	if (pendingMusicToImageDirty) {
		musicToImagePromptOutput = pendingMusicToImagePromptOutput;
		musicToImageStatus = pendingMusicToImageStatus;
		fprintf(
			stderr,
			"%s\n",
			formatConsoleLogLine("Music to Image", "AI", musicToImageStatus, true).c_str());
	}
	if (pendingImageToMusicDirty) {
		if (!pendingImageToMusicPromptOutput.empty()) {
			imageToMusicPromptOutput = pendingImageToMusicPromptOutput;
		}
		if (!pendingImageToMusicNotationOutput.empty()) {
			imageToMusicNotationOutput = pendingImageToMusicNotationOutput;
		}
		imageToMusicStatus = pendingImageToMusicStatus;
		fprintf(
			stderr,
			"%s\n",
			formatConsoleLogLine("Image to Music", "AI", imageToMusicStatus, true).c_str());
	}
	if (pendingAceStepDirty) {
		aceStepStatus = pendingAceStepStatus;
		aceStepGeneratedRequestJson = pendingAceStepGeneratedRequestJson;
		aceStepUnderstoodSummary = pendingAceStepUnderstoodSummary;
		aceStepUnderstoodCaption = pendingAceStepUnderstoodCaption;
		aceStepUnderstoodLyrics = pendingAceStepUnderstoodLyrics;
		aceStepUsedServerUrl = pendingAceStepUsedServerUrl;
		aceStepGeneratedTracks = pendingAceStepGeneratedTracks;
		aceStepSelectedTrackIndex = std::clamp(
			aceStepSelectedTrackIndex,
			0,
			std::max(0, static_cast<int>(aceStepGeneratedTracks.size()) - 1));
#if OFXGGML_HAS_OFXVLC4
		if (aceStepGeneratedTracks.empty()) {
			closeAceStepVlcPreview();
		} else if (!aceStepVlcLoadedAudioPath.empty() &&
			aceStepSelectedTrackIndex < static_cast<int>(aceStepGeneratedTracks.size()) &&
			trim(aceStepGeneratedTracks[static_cast<size_t>(aceStepSelectedTrackIndex)].path) != aceStepVlcLoadedAudioPath) {
			closeAceStepVlcPreview();
		}
#endif
		fprintf(
			stderr,
			"%s\n",
			formatConsoleLogLine("AceStep", "AI", aceStepStatus, true).c_str());
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
	if (pendingVideoEssayDirty) {
		videoEssayStatus = pendingVideoEssayStatus;
		videoEssayOutline = pendingVideoEssayOutline;
		videoEssayScript = pendingVideoEssayScript;
		videoEssaySrtText = pendingVideoEssaySrtText;
		videoEssayVisualConcept = pendingVideoEssayVisualConcept;
		videoEssayScenePlanJson = pendingVideoEssayScenePlanJson;
		videoEssayScenePlanSummary = pendingVideoEssayScenePlanSummary;
		videoEssayScenePlanningError = pendingVideoEssayScenePlanningError;
		videoEssayEditPlanJson = pendingVideoEssayEditPlanJson;
		videoEssayEditPlanSummary = pendingVideoEssayEditPlanSummary;
		videoEssayEditPlanningError = pendingVideoEssayEditPlanningError;
		videoEssayEditorBrief = pendingVideoEssayEditorBrief;
		videoEssayCitations = pendingVideoEssayCitations;
		videoEssaySections = pendingVideoEssaySections;
		videoEssayVoiceCues = pendingVideoEssayVoiceCues;
		fprintf(
			stderr,
			"%s\n",
			formatConsoleLogLine("Video Essay", "AI", videoEssayStatus, true).c_str());
	}
	if (pendingLongVideoDirty) {
		longVideoStatus = pendingLongVideoStatus;
		longVideoContinuityBible = pendingLongVideoContinuityBible;
		longVideoManifestJson = pendingLongVideoManifestJson;
		longVideoChunks = pendingLongVideoChunks;
		fprintf(
			stderr,
			"%s\n",
			formatConsoleLogLine("Video", "AI", longVideoStatus, true).c_str());
	}
	if (pendingLongVideoRenderDirty) {
		longVideoRenderStatus = pendingLongVideoRenderStatus;
		longVideoRenderOutputDirectory = pendingLongVideoRenderOutputDirectory;
		longVideoRenderMetadataPath = pendingLongVideoRenderMetadataPath;
		longVideoRenderManifestPath = pendingLongVideoRenderManifestPath;
		longVideoRenderManifestJson = pendingLongVideoRenderManifestJson;
		fprintf(
			stderr,
			"%s\n",
			formatConsoleLogLine("Video", "AI", longVideoRenderStatus, true).c_str());
	}
	if (pendingVoiceTranslatorDirty) {
		voiceTranslatorStatus = pendingVoiceTranslatorStatus;
		voiceTranslatorTranscript = pendingVoiceTranslatorTranscript;
			if (!pendingTtsBackendName.empty() || !pendingTtsAudioFiles.empty()) {
				ttsBackendName = pendingTtsBackendName;
				ttsElapsedMs = pendingTtsElapsedMs;
				ttsResolvedSpeakerPath = pendingTtsResolvedSpeakerPath;
				ttsAudioFiles = pendingTtsAudioFiles;
				ttsMetadata = pendingTtsMetadata;
				ttsOutput = pendingVoiceTranslatorStatus;
				translateTtsPreview.audioFiles = pendingTtsAudioFiles;
				translateTtsPreview.selectedAudioIndex = 0;
				translateTtsPreview.loadedAudioPath.clear();
				translateTtsPreview.statusMessage = pendingVoiceTranslatorStatus;
				if (!translateTtsPreview.audioFiles.empty()) {
					ensureTranslateTtsAudioLoaded(0, true);
				}
			}
		fprintf(
			stderr,
			"%s\n",
			formatConsoleLogLine(
				"Voice Translator",
				"AI",
				voiceTranslatorStatus,
				true).c_str());
	}

	pendingOutput.clear();
	pendingScriptInlineCompletionOutput.clear();
	pendingScriptInlineCompletionTargetPath.clear();
	pendingScriptAssistantSession = {};
	pendingScriptAssistantSessionDirty = false;
	pendingScriptAssistantEvents.clear();
	pendingScriptAssistantToolCalls.clear();
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
	pendingMusicToImagePromptOutput.clear();
	pendingMusicToImageStatus.clear();
	pendingMusicToImageDirty = false;
	pendingImageToMusicPromptOutput.clear();
	pendingImageToMusicNotationOutput.clear();
	pendingImageToMusicStatus.clear();
	pendingImageToMusicDirty = false;
	pendingAceStepStatus.clear();
	pendingAceStepGeneratedRequestJson.clear();
	pendingAceStepUnderstoodSummary.clear();
	pendingAceStepUnderstoodCaption.clear();
	pendingAceStepUnderstoodLyrics.clear();
	pendingAceStepUsedServerUrl.clear();
	pendingAceStepGeneratedTracks.clear();
	pendingAceStepDirty = false;
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
	pendingVideoEssayStatus.clear();
	pendingVideoEssayOutline.clear();
	pendingVideoEssayScript.clear();
	pendingVideoEssaySrtText.clear();
	pendingVideoEssayVisualConcept.clear();
	pendingVideoEssayScenePlanJson.clear();
	pendingVideoEssayScenePlanSummary.clear();
	pendingVideoEssayScenePlanningError.clear();
	pendingVideoEssayEditPlanJson.clear();
	pendingVideoEssayEditPlanSummary.clear();
	pendingVideoEssayEditPlanningError.clear();
	pendingVideoEssayEditorBrief.clear();
	pendingVideoEssayCitations.clear();
	pendingVideoEssaySections.clear();
	pendingVideoEssayVoiceCues.clear();
	pendingVideoEssayDirty = false;
	pendingLongVideoStatus.clear();
	pendingLongVideoContinuityBible.clear();
	pendingLongVideoManifestJson.clear();
	pendingLongVideoChunks.clear();
	pendingLongVideoDirty = false;
	pendingLongVideoRenderStatus.clear();
	pendingLongVideoRenderOutputDirectory.clear();
	pendingLongVideoRenderMetadataPath.clear();
	pendingLongVideoRenderManifestPath.clear();
	pendingLongVideoRenderManifestJson.clear();
	pendingLongVideoRenderDirty = false;
	pendingVoiceTranslatorStatus.clear();
	pendingVoiceTranslatorTranscript.clear();
	pendingVoiceTranslatorDirty = false;
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
	applyScriptReviewPreset();
	const std::string effectiveReviewQuery = !trim(overrideQuery).empty()
		? trim(overrideQuery)
		: (std::strlen(scriptInput) > 0
			? std::string(scriptInput)
			: ofxGgmlCodeReview::defaultReviewQuery());
	ofxGgmlCodeAssistantRequest request =
		buildScriptAssistantRequest(
			effectiveReviewQuery,
			makeInferenceModePromptSnapshot());
	request.action = ofxGgmlCodeAssistantAction::Review;
	request.requestStructuredResult = true;
	request.labelOverride = "Review all files";
	request.bodyOverride =
		"Review the loaded workspace hierarchically. Focus on concrete bugs, regressions, risks, and missing tests. "
		"Prefer evidence-backed findings with file references and concise fixes.";
	runScriptAssistantRequest(
		request,
		request.labelOverride,
		false,
		{},
		nullptr,
		true);
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
