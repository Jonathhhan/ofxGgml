#include "ofApp.h"

#include "utils/ConsoleHelpers.h"
#include "utils/ImGuiHelpers.h"
#include "utils/TextPromptHelpers.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace {

constexpr float kTextModeDotsAnimationSpeed = 3.0f;
const char * const kTextModeWaitingLabels[] = {
	"generating",
	"generating.",
	"generating..",
	"generating..."
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

void drawGeneratedOutputChild(
	const char * childId,
	const std::string & output,
	const bool isGenerating,
	std::mutex & streamMutex,
	std::string & streamingOutput) {
	ImGui::BeginChild(childId, ImVec2(0, 0), true);
	if (isGenerating) {
		std::string partial;
		{
			std::lock_guard<std::mutex> lock(streamMutex);
			partial = streamingOutput;
		}
		if (partial.empty()) {
			const int dots = static_cast<int>(ImGui::GetTime() * kTextModeDotsAnimationSpeed) % 4;
			ImGui::TextColored(
				ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
				"%s",
				kTextModeWaitingLabels[dots]);
		} else {
			ImGui::TextWrapped("%s", partial.c_str());
		}
	} else {
		ImGui::TextWrapped("%s", output.c_str());
	}
	ImGui::EndChild();
}

template <typename EnsureLoadedFn, typename StopPlaybackFn>
void drawTtsPreviewControls(
	const bool generating,
	std::vector<ofxGgmlTtsAudioArtifact> & audioFiles,
	int & selectedAudioIndex,
	const std::string & loadedAudioPath,
	bool & playbackPaused,
	ofSoundPlayer & player,
	std::string & statusMessage,
	const TtsPreviewUiLabels & labels,
	EnsureLoadedFn && ensureLoaded,
	StopPlaybackFn && stopPlayback) {
	if (!audioFiles.empty()) {
		selectedAudioIndex = std::clamp(
			selectedAudioIndex,
			0,
			std::max(0, static_cast<int>(audioFiles.size()) - 1));
		if (audioFiles.size() > 1) {
			std::string previewLabel =
				ofFilePath::getBaseName(audioFiles[static_cast<size_t>(selectedAudioIndex)].path);
			if (previewLabel.empty()) {
				previewLabel = "Audio " + std::to_string(selectedAudioIndex + 1);
			}
			if (ImGui::BeginCombo(labels.comboLabel, previewLabel.c_str())) {
				for (int i = 0; i < static_cast<int>(audioFiles.size()); ++i) {
					const std::string fileLabel =
						ofFilePath::getBaseName(audioFiles[static_cast<size_t>(i)].path);
					const std::string itemLabel = fileLabel.empty()
						? "Audio " + std::to_string(i + 1)
						: fileLabel;
					const bool selected = (selectedAudioIndex == i);
					if (ImGui::Selectable(itemLabel.c_str(), selected)) {
						selectedAudioIndex = i;
						ensureLoaded(selectedAudioIndex, false);
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
		const bool audioLoaded = player.isLoaded() && !loadedAudioPath.empty();
		const bool audioPlaying = audioLoaded && player.isPlaying();
		const char * playPauseLabel = audioPlaying
			? labels.pauseLabel
			: (playbackPaused ? labels.resumeLabel : labels.playLabel);
		if (ImGui::SmallButton(playPauseLabel)) {
			if (!audioLoaded) {
				ensureLoaded(selectedAudioIndex, true);
			} else if (audioPlaying) {
				player.setPaused(true);
				playbackPaused = true;
				statusMessage = labels.pausedStatus;
			} else {
				if (playbackPaused) {
					player.setPaused(false);
				} else {
					player.play();
				}
				playbackPaused = false;
				statusMessage = labels.playingStatus;
			}
		}
		ImGui::SameLine();
		if (ImGui::SmallButton(labels.restartLabel)) {
			if (ensureLoaded(selectedAudioIndex, false)) {
				player.stop();
				player.play();
				playbackPaused = false;
				statusMessage = labels.restartedStatus;
			}
		}
		ImGui::SameLine();
		if (ImGui::SmallButton(labels.stopLabel)) {
			stopPlayback(false);
			statusMessage = labels.stoppedStatus;
		}
		ImGui::EndDisabled();
		ImGui::TextDisabled(
			"%s %s",
			labels.filePrefix,
			audioFiles[static_cast<size_t>(selectedAudioIndex)].path.c_str());
	}
}

} // namespace

void ofApp::drawChatPanel() {
	drawPanelHeader("Chat", "conversation with the ggml engine");
	if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
		if (!textServerCapabilityHint.empty()) {
			ImGui::TextDisabled("%s", textServerCapabilityHint.c_str());
		} else {
			ImGui::TextDisabled("Server-backed chat is active.");
		}
	}

	if (chatLanguages.empty()) {
		chatLanguages = ofxGgmlChatAssistant::defaultResponseLanguages();
	}
	chatLanguageIndex = std::clamp(
		chatLanguageIndex,
		0,
		std::max(0, static_cast<int>(chatLanguages.size()) - 1));

	ImGui::Text("Response language:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(170);
	const std::string selectedChatLanguage =
		chatLanguages[static_cast<size_t>(chatLanguageIndex)].name;
	if (ImGui::BeginCombo("##ChatLang", selectedChatLanguage.c_str())) {
		for (int i = 0; i < static_cast<int>(chatLanguages.size()); i++) {
			const bool selected = (chatLanguageIndex == i);
			if (ImGui::Selectable(
				chatLanguages[static_cast<size_t>(i)].name.c_str(),
				selected)) {
				chatLanguageIndex = i;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip("Auto lets the model decide. Choose a language to force chat replies.");
	}

	const float inputH = 60.0f;
	ImGui::BeginChild("##ChatHistory", ImVec2(0, -inputH), true);
	for (size_t i = 0; i < chatMessages.size(); i++) {
		auto & msg = chatMessages[i];
		if (msg.role == "user") {
			ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "You:");
		} else if (msg.role == "assistant") {
			ImGui::TextColored(ImVec4(0.6f, 0.7f, 1.0f, 1.0f), "AI:");
		} else {
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "System:");
		}
		ImGui::TextWrapped("%s", msg.text.c_str());
		ImGui::Spacing();
	}

	if (generating.load() && activeGenerationMode == AiMode::Chat) {
		ImGui::TextColored(ImVec4(0.6f, 0.7f, 1.0f, 1.0f), "AI:");
		ImGui::SameLine();
		std::string partial;
		{
			std::lock_guard<std::mutex> lock(streamMutex);
			partial = streamingOutput;
		}
		if (partial.empty()) {
			const int dots = static_cast<int>(ImGui::GetTime() * kTextModeDotsAnimationSpeed) % 4;
			const char * thinking[] = {"thinking", "thinking.", "thinking..", "thinking..."};
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", thinking[dots]);
		} else {
			ImGui::TextWrapped("%s", partial.c_str());
		}
		ImGui::Spacing();
	}

	if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) {
		ImGui::SetScrollHereY(1.0f);
	}
	ImGui::EndChild();

	const float availW = ImGui::GetContentRegionAvail().x;
	ImGui::SetNextItemWidth(availW - 190.0f);
	const bool submitted = ImGui::InputText(
		"##ChatIn",
		chatInput,
		sizeof(chatInput),
		ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::SameLine();
	const bool sendClicked = ImGui::Button("Send", ImVec2(70, 0));
	ImGui::SameLine();
	const bool sendWithSourcesClicked = ImGui::Button("Send with Sources", ImVec2(130, 0));
	if (liveContextMode == LiveContextMode::Offline) {
		ImGui::SameLine();
		ImGui::TextDisabled("(offline)");
	} else if (liveContextMode == LiveContextMode::LoadedSourcesOnly) {
		ImGui::SameLine();
		ImGui::TextDisabled("(loaded sources only)");
	}

	ImGui::InputTextMultiline(
		"Loaded Sources (URLs)",
		sourceUrlsInput,
		sizeof(sourceUrlsInput),
		ImVec2(-1, 48));
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip("Optional source URLs for grounded answers in Chat, Summarize, and Custom.");
	}

	ImGui::Checkbox("Speak chat replies", &chatSpeakReplies);
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip("Automatically synthesize each assistant chat reply through the configured TTS backend.");
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(generating.load() || trim(chatLastAssistantReply).empty());
	if (ImGui::SmallButton("Speak last reply")) {
		speakLatestChatReply(true);
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip("Send the most recent assistant reply into the TTS pipeline and mirror it into the TTS panel.");
	}
	if (!chatTtsPreview.statusMessage.empty()) {
		ImGui::TextDisabled("%s", chatTtsPreview.statusMessage.c_str());
	}
	drawTtsPreviewControls(
		generating.load(),
		chatTtsPreview.audioFiles,
		chatTtsPreview.selectedAudioIndex,
		chatTtsPreview.loadedAudioPath,
		chatTtsPreview.playbackPaused,
		chatTtsPreview.player,
		chatTtsPreview.statusMessage,
		{
			"Chat audio",
			"Pause chat audio",
			"Resume chat audio",
			"Play chat audio",
			"Restart chat audio",
			"Stop chat audio",
			"Chat audio file:",
			"Paused synthesized chat reply.",
			"Playing synthesized chat reply.",
			"Restarted synthesized chat reply.",
			"Stopped synthesized chat reply."
		},
		[this](int artifactIndex, bool autoplay) {
			return ensureChatTtsAudioLoaded(artifactIndex, autoplay);
		},
		[this](bool clearLoadedPath) {
			stopChatTtsPlayback(clearLoadedPath);
		});

	std::string citationSuggestedTopic = trim(chatInput);
	if (citationSuggestedTopic.empty()) {
		for (auto it = chatMessages.rbegin(); it != chatMessages.rend(); ++it) {
			if (it->role == "user" && !trim(it->text).empty()) {
				citationSuggestedTopic = it->text;
				break;
			}
		}
	}
	drawCitationSearchSection("Use Chat Input", citationSuggestedTopic);

	if ((submitted || sendClicked || sendWithSourcesClicked) &&
		std::strlen(chatInput) > 0 &&
		!generating.load()) {
		const std::string userText(chatInput);
		chatMessages.push_back({"user", userText, ofGetElapsedTimef()});
		fprintf(stderr, "%s\n", formatConsoleLogLine("ChatWindow", "You", userText, true).c_str());
		std::memset(chatInput, 0, sizeof(chatInput));
		ofxGgmlRealtimeInfoSettings realtimeSettings = buildLiveContextSettings(
			"",
			"Live context for chat",
			true);
		if (sendWithSourcesClicked) {
			realtimeSettings = buildLiveContextSettings(
				sourceUrlsInput,
				"Loaded sources for this chat reply",
				true);
		}
		runInference(AiMode::Chat, userText, "", "", realtimeSettings);
	}

	if (!chatMessages.empty()) {
		if (ImGui::SmallButton("Summarize Chat")) {
			std::ostringstream transcript;
			size_t included = 0;
			for (auto it = chatMessages.rbegin();
				it != chatMessages.rend() && included < 8;
				++it, ++included) {
				transcript << it->role << ": " << it->text << "\n";
			}
			copyStringToBuffer(
				summarizeInput,
				sizeof(summarizeInput),
				transcript.str());
			activeMode = AiMode::Summarize;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear Chat")) {
			chatMessages.clear();
			chatLastAssistantReply.clear();
			chatTtsPreview.clearPreviewArtifacts();
			stopChatTtsPlayback(true);
			streamingOutput.clear();
		}
	}
}

void ofApp::drawSummarizePanel() {
	drawPanelHeader("Summarize", "condense text into key points");
	if (textInferenceBackend == TextInferenceBackend::LlamaServer &&
		!textServerCapabilityHint.empty()) {
		ImGui::TextDisabled("%s", textServerCapabilityHint.c_str());
	}

	ImGui::Text("Paste text to summarize:");
	ImGui::InputTextMultiline(
		"##SumIn",
		summarizeInput,
		sizeof(summarizeInput),
		ImVec2(-1, 150));

	auto submitTextTask = [&](ofxGgmlTextTask task) {
		ofxGgmlTextAssistantRequest request;
		request.task = task;
		request.inputText = summarizeInput;
		runPreparedTextRequest(AiMode::Summarize, request);
	};

	auto submitSummaryPrompt = [&](const std::string & userText,
		const std::string & instruction,
		const std::string & outputHeading,
		const bool useLoadedSources = false) {
		const auto realtimeSettings = useLoadedSources
			? buildLiveContextSettings(sourceUrlsInput, "Loaded sources for summarization")
			: ofxGgmlRealtimeInfoSettings{};
		runInference(
			AiMode::Summarize,
			userText,
			"",
			buildStructuredTextPrompt(
				"You are a precise analyst who writes crisp, professional summaries.",
				instruction,
				"Material",
				userText,
				outputHeading),
			realtimeSettings);
	};

	const bool hasSummarizeInput = std::strlen(summarizeInput) > 0;
	const bool hasSummarizeUrls = !trim(sourceUrlsInput).empty();
	ImGui::BeginDisabled(generating.load() || (!hasSummarizeInput && !hasSummarizeUrls));
	if (ImGui::Button("Summarize", ImVec2(140, 0))) {
		submitTextTask(ofxGgmlTextTask::Summarize);
	}
	ImGui::SameLine();
	if (ImGui::Button("Key Points", ImVec2(140, 0))) {
		submitTextTask(ofxGgmlTextTask::KeyPoints);
	}
	ImGui::SameLine();
	if (ImGui::Button("TL;DR", ImVec2(140, 0))) {
		submitTextTask(ofxGgmlTextTask::TlDr);
	}
	ImGui::SameLine();
	if (ImGui::Button("Summarize URLs", ImVec2(150, 0))) {
		ofxGgmlTextAssistantRequest request;
		request.task = ofxGgmlTextTask::Summarize;
		request.inputText = hasSummarizeInput
			? std::string(summarizeInput)
			: std::string("Summarize the reference sources.");
		const auto realtimeSettings = buildLiveContextSettings(
			sourceUrlsInput,
			"Loaded sources for summarization");
		runPreparedTextRequest(AiMode::Summarize, request, realtimeSettings);
	}
	ImGui::EndDisabled();

	ImGui::BeginDisabled(generating.load() || (!hasSummarizeInput && !hasSummarizeUrls));
	if (ImGui::Button("Executive Brief", ImVec2(140, 0))) {
		submitSummaryPrompt(
			hasSummarizeInput ? std::string(summarizeInput) : std::string("Use the provided loaded sources."),
			"Write an executive brief with headline, what matters, risks, and recommended next step.",
			"Executive brief");
	}
	ImGui::SameLine();
	if (ImGui::Button("Action Items", ImVec2(140, 0))) {
		submitSummaryPrompt(
			hasSummarizeInput ? std::string(summarizeInput) : std::string("Use the provided loaded sources."),
			"Extract concrete action items with owners or placeholders, dependencies, and urgency.",
			"Action items");
	}
	ImGui::SameLine();
	if (ImGui::Button("Meeting Notes", ImVec2(140, 0))) {
		submitSummaryPrompt(
			hasSummarizeInput ? std::string(summarizeInput) : std::string("Use the provided loaded sources."),
			"Turn the material into professional meeting notes with decisions, blockers, and follow-ups.",
			"Meeting notes");
	}
	ImGui::SameLine();
	if (ImGui::Button("Source Brief", ImVec2(140, 0))) {
		submitSummaryPrompt(
			hasSummarizeInput ? std::string(summarizeInput) : std::string("Use the provided loaded sources."),
			"Summarize the loaded material into a source-backed brief with key claims and supporting context.",
			"Source brief",
			hasSummarizeUrls);
	}
	ImGui::EndDisabled();

	ImGui::Separator();
	ImGui::Text("Summary:");
	if (!summarizeOutput.empty()) {
		ImGui::SameLine();
		if (ImGui::SmallButton("Copy##SumCopy")) {
			copyToClipboard(summarizeOutput);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear##SumClear")) {
			summarizeOutput.clear();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(%d chars)", static_cast<int>(summarizeOutput.size()));
	}
	drawGeneratedOutputChild(
		"##SumOut",
		summarizeOutput,
		generating.load() && activeGenerationMode == AiMode::Summarize,
		streamMutex,
		streamingOutput);

	drawCitationSearchSection("Use Summarize Text", summarizeInput);
}

void ofApp::drawWritePanel() {
	drawPanelHeader("Writing Assistant", "rewrite, expand, polish text");
	if (textInferenceBackend == TextInferenceBackend::LlamaServer &&
		!textServerCapabilityHint.empty()) {
		ImGui::TextDisabled("%s", textServerCapabilityHint.c_str());
	}

	ImGui::Text("Enter your text:");
	ImGui::InputTextMultiline("##WriteIn", writeInput, sizeof(writeInput), ImVec2(-1, 120));

	auto submitTextTask = [&](ofxGgmlTextTask task) {
		ofxGgmlTextAssistantRequest request;
		request.task = task;
		request.inputText = writeInput;
		runPreparedTextRequest(AiMode::Write, request);
	};

	auto submitWritePrompt = [&](const std::string & userText,
		const std::string & instruction,
		const std::string & outputHeading) {
		runInference(
			AiMode::Write,
			userText,
			"",
			buildStructuredTextPrompt(
				"You are a strong professional editor.",
				instruction,
				"Text",
				userText,
				outputHeading));
	};

	ImGui::BeginDisabled(generating.load() || std::strlen(writeInput) == 0);
	if (ImGui::Button("Rewrite", ImVec2(110, 0))) {
		submitTextTask(ofxGgmlTextTask::Rewrite);
	}
	ImGui::SameLine();
	if (ImGui::Button("Expand", ImVec2(110, 0))) {
		submitTextTask(ofxGgmlTextTask::Expand);
	}
	ImGui::SameLine();
	if (ImGui::Button("Make Formal", ImVec2(110, 0))) {
		submitTextTask(ofxGgmlTextTask::MakeFormal);
	}
	ImGui::SameLine();
	if (ImGui::Button("Make Casual", ImVec2(110, 0))) {
		submitTextTask(ofxGgmlTextTask::MakeCasual);
	}
	ImGui::SameLine();
	if (ImGui::Button("Fix Grammar", ImVec2(110, 0))) {
		submitTextTask(ofxGgmlTextTask::FixGrammar);
	}
	ImGui::SameLine();
	if (ImGui::Button("Polish", ImVec2(110, 0))) {
		submitTextTask(ofxGgmlTextTask::Polish);
	}
	ImGui::EndDisabled();

	ImGui::BeginDisabled(generating.load() || std::strlen(writeInput) == 0);
	if (ImGui::Button("Shorten", ImVec2(110, 0))) {
		submitWritePrompt(
			writeInput,
			"Shorten this text while preserving meaning, key details, and professional tone.",
			"Shortened version");
	}
	ImGui::SameLine();
	if (ImGui::Button("Email Reply", ImVec2(110, 0))) {
		submitWritePrompt(
			writeInput,
			"Draft a professional email reply using the provided material. Keep it clear, tactful, and ready to send.",
			"Email reply");
	}
	ImGui::SameLine();
	if (ImGui::Button("Release Notes", ImVec2(110, 0))) {
		submitWritePrompt(
			writeInput,
			"Turn this material into concise release notes with highlights, fixes, and any user-facing caveats.",
			"Release notes");
	}
	ImGui::SameLine();
	if (ImGui::Button("Commit Message", ImVec2(110, 0))) {
		submitWritePrompt(
			writeInput,
			"Write a professional commit message with a short subject and a useful body when justified.",
			"Commit message");
	}
	ImGui::EndDisabled();

	ImGui::Separator();
	ImGui::Text("Output:");
	if (!writeOutput.empty()) {
		ImGui::SameLine();
		if (ImGui::SmallButton("Copy##WriteCopy")) {
			copyToClipboard(writeOutput);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear##WriteClear")) {
			writeOutput.clear();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(%d chars)", static_cast<int>(writeOutput.size()));
	}
	drawGeneratedOutputChild(
		"##WriteOut",
		writeOutput,
		generating.load() && activeGenerationMode == AiMode::Write,
		streamMutex,
		streamingOutput);
}

void ofApp::drawTranslatePanel() {
	drawPanelHeader("Translate", "translate text between languages");
	if (textInferenceBackend == TextInferenceBackend::LlamaServer &&
		!textServerCapabilityHint.empty()) {
		ImGui::TextDisabled("%s", textServerCapabilityHint.c_str());
	}

	if (translateLanguages.empty()) {
		translateLanguages = ofxGgmlTextAssistant::defaultTranslateLanguages();
	}
	if (hasDeferredTranslateInput) {
		copyStringToBuffer(
			translateInput,
			sizeof(translateInput),
			deferredTranslateInput);
		hasDeferredTranslateInput = false;
		deferredTranslateInput.clear();
	}
	if (hasDeferredVoiceTranslatorAudioPath) {
		copyStringToBuffer(
			voiceTranslatorAudioPath,
			sizeof(voiceTranslatorAudioPath),
			deferredVoiceTranslatorAudioPath);
		hasDeferredVoiceTranslatorAudioPath = false;
		deferredVoiceTranslatorAudioPath.clear();
	}
	translateSourceLang = std::clamp(
		translateSourceLang,
		0,
		std::max(0, static_cast<int>(translateLanguages.size()) - 1));
	translateTargetLang = std::clamp(
		translateTargetLang,
		0,
		std::max(0, static_cast<int>(translateLanguages.size()) - 1));

	const auto currentSourceLanguage = [&]() -> std::string {
		if (translateSourceLang < 0 ||
			translateSourceLang >= static_cast<int>(translateLanguages.size())) {
			return "Auto detect";
		}
		return translateLanguages[static_cast<size_t>(translateSourceLang)].name;
	};
	const auto currentTargetLanguage = [&]() -> std::string {
		if (translateTargetLang < 0 ||
			translateTargetLang >= static_cast<int>(translateLanguages.size())) {
			return "English";
		}
		return translateLanguages[static_cast<size_t>(translateTargetLang)].name;
	};
	const auto sourceIsAutoDetect = [&]() -> bool {
		return currentSourceLanguage() == "Auto detect";
	};

	ImGui::TextWrapped(
		"Translate with cleaner output and quicker handoff from other panels. "
		"Use Auto detect for unknown source text, then reuse the result in Write or as a fresh input.");

	ImGui::Text("Source language:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(170);
	if (ImGui::BeginCombo("##SrcLang", currentSourceLanguage().c_str())) {
		for (int i = 0; i < static_cast<int>(translateLanguages.size()); i++) {
			const bool selected = (translateSourceLang == i);
			if (ImGui::Selectable(
				translateLanguages[static_cast<size_t>(i)].name.c_str(),
				selected)) {
				translateSourceLang = i;
				autoSaveSession();
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::Text("Target language:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(170);
	if (ImGui::BeginCombo("##TgtLang", currentTargetLanguage().c_str())) {
		for (int i = 0; i < static_cast<int>(translateLanguages.size()); i++) {
			if (translateLanguages[static_cast<size_t>(i)].name == "Auto detect") {
				continue;
			}
			const bool selected = (translateTargetLang == i);
			if (ImGui::Selectable(
				translateLanguages[static_cast<size_t>(i)].name.c_str(),
				selected)) {
				translateTargetLang = i;
				autoSaveSession();
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::Button("Swap", ImVec2(60, 0))) {
		if (!sourceIsAutoDetect()) {
			std::swap(translateSourceLang, translateTargetLang);
		} else {
			translateSourceLang = translateTargetLang;
			for (int i = 0; i < static_cast<int>(translateLanguages.size()); ++i) {
				if (translateLanguages[static_cast<size_t>(i)].name == "English") {
					translateTargetLang = i;
					break;
				}
			}
		}
		autoSaveSession();
	}

	const bool sameLanguages =
		!sourceIsAutoDetect() &&
		currentSourceLanguage() == currentTargetLanguage();
	if (sameLanguages) {
		ImGui::TextColored(
			ImVec4(0.95f, 0.7f, 0.3f, 1.0f),
			"Source and target are currently the same language.");
	}

	ImGui::Separator();
	ImGui::TextDisabled("Quick sources");
	ImGui::BeginDisabled(trim(writeOutput).empty());
	if (ImGui::SmallButton("Use Write output")) {
		deferredTranslateInput = writeOutput;
		hasDeferredTranslateInput = true;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(customOutput).empty());
	if (ImGui::SmallButton("Use Custom output")) {
		deferredTranslateInput = customOutput;
		hasDeferredTranslateInput = true;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(speechOutput).empty());
	if (ImGui::SmallButton("Use Speech output")) {
		deferredTranslateInput = speechOutput;
		hasDeferredTranslateInput = true;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(translateOutput).empty());
	if (ImGui::SmallButton("Use current translation")) {
		deferredTranslateInput = translateOutput;
		hasDeferredTranslateInput = true;
	}
	ImGui::EndDisabled();

	ImGui::Text("Enter text to translate:");
	ImGui::InputTextMultiline(
		"##TransIn",
		translateInput,
		sizeof(translateInput),
		ImVec2(-1, 120));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}

	auto buildTranslateRequest = [&](const ofxGgmlTextTask task) {
		ofxGgmlTextAssistantRequest request;
		request.task = task;
		request.inputText = translateInput;
		request.sourceLanguage = currentSourceLanguage();
		request.targetLanguage = currentTargetLanguage();
		return request;
	};
	auto runTranslateTask = [&](const ofxGgmlTextTask task) {
		const auto request = buildTranslateRequest(task);
		runPreparedTextRequest(AiMode::Translate, request);
	};

	ImGui::BeginDisabled(generating.load() || std::strlen(translateInput) == 0);
	if (ImGui::Button("Natural", ImVec2(110, 0))) {
		runTranslateTask(ofxGgmlTextTask::Translate);
	}
	ImGui::SameLine();
	if (ImGui::Button("Literal", ImVec2(110, 0))) {
		const auto request = buildTranslateRequest(ofxGgmlTextTask::Translate);
		runInference(
			AiMode::Translate,
			request.inputText,
			"",
			buildStructuredTextPrompt(
				"You are a precise translator.",
				"Translate as literally as possible while preserving formatting and readability."
					" Do not add explanations.",
				"Text",
				request.inputText,
				"Translation"));
	}
	ImGui::SameLine();
	if (ImGui::Button("Detect Language", ImVec2(130, 0))) {
		runTranslateTask(ofxGgmlTextTask::DetectLanguage);
	}
	ImGui::SameLine();
	if (ImGui::Button("Detect + Translate", ImVec2(150, 0))) {
		const auto request = buildTranslateRequest(ofxGgmlTextTask::Translate);
		const std::string prompt = buildStructuredTextPrompt(
			"You are a translation assistant.",
			"Detect the source language first, then translate into the requested target language."
				" Return only the translated text and preserve paragraph breaks, visible emphasis, and list structure.",
			"Text",
			request.inputText,
			"Translation");
		runInference(AiMode::Translate, request.inputText, "", prompt);
	}
	ImGui::EndDisabled();

	ImGui::Separator();
	ImGui::TextDisabled("Voice translator");
	ImGui::TextWrapped(
		"Chain speech transcription, text translation, and TTS into one pass. "
		"Use it for text-to-voice translation or for translating an audio file into spoken output.");
	if (ImGui::Checkbox("Speak translated output", &voiceTranslatorSpeakOutput)) {
		autoSaveSession();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(generating.load() || trim(translateOutput).empty());
	if (ImGui::SmallButton("Speak current translation")) {
		speakTranslatedReply(true);
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip("Send the current translation directly into the configured TTS backend.");
	}
	if (!translateTtsPreview.statusMessage.empty()) {
		ImGui::TextDisabled("%s", translateTtsPreview.statusMessage.c_str());
	}
	drawTtsPreviewControls(
		generating.load(),
		translateTtsPreview.audioFiles,
		translateTtsPreview.selectedAudioIndex,
		translateTtsPreview.loadedAudioPath,
		translateTtsPreview.playbackPaused,
		translateTtsPreview.player,
		translateTtsPreview.statusMessage,
		{
			"Translated audio",
			"Pause translated audio",
			"Resume translated audio",
			"Play translated audio",
			"Restart translated audio",
			"Stop translated audio",
			"Translated audio file:",
			"Paused translated voice output.",
			"Playing translated voice output.",
			"Restarted translated voice output.",
			"Stopped translated voice output."
		},
		[this](int artifactIndex, bool autoplay) {
			return ensureTranslateTtsAudioLoaded(artifactIndex, autoplay);
		},
		[this](bool clearLoadedPath) {
			stopTranslateTtsPlayback(clearLoadedPath);
		});

	ImGui::SetNextItemWidth(-120.0f);
	ImGui::InputText(
		"Voice translator audio",
		voiceTranslatorAudioPath,
		sizeof(voiceTranslatorAudioPath));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Button("Browse##VoiceTranslatorAudio", ImVec2(100, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select audio file", false);
		if (result.bSuccess) {
			copyStringToBuffer(
				voiceTranslatorAudioPath,
				sizeof(voiceTranslatorAudioPath),
				result.getPath());
			autoSaveSession();
		}
	}

	ImGui::BeginDisabled(trim(speechAudioPath).empty());
	if (ImGui::SmallButton("Use Speech audio")) {
		deferredVoiceTranslatorAudioPath = speechAudioPath;
		hasDeferredVoiceTranslatorAudioPath = true;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	const bool hasBufferedMicAudio = [&]() {
		std::lock_guard<std::mutex> lock(speechRecordMutex);
		return !speechRecordedSamples.empty();
	}();
	ImGui::BeginDisabled(!hasBufferedMicAudio);
	if (ImGui::SmallButton("Use Recording")) {
		const std::string tempPath = flushSpeechRecordingToTempWav();
		if (!tempPath.empty()) {
			deferredVoiceTranslatorAudioPath = tempPath;
			hasDeferredVoiceTranslatorAudioPath = true;
			autoSaveSession();
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(trim(speechOutput).empty());
	if (ImGui::SmallButton("Use transcript")) {
		deferredTranslateInput = speechOutput;
		hasDeferredTranslateInput = true;
	}
	ImGui::EndDisabled();

	ImGui::BeginDisabled(
		generating.load() ||
		(std::strlen(translateInput) == 0 && trim(translateOutput).empty()));
	if (ImGui::Button("Translate + Speak", ImVec2(150, 0))) {
		runVoiceTranslatorWorkflow(false);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(generating.load() || trim(voiceTranslatorAudioPath).empty());
	if (ImGui::Button("Audio -> Voice", ImVec2(150, 0))) {
		runVoiceTranslatorWorkflow(true);
	}
	ImGui::EndDisabled();
	if (!voiceTranslatorStatus.empty()) {
		ImGui::TextDisabled("%s", voiceTranslatorStatus.c_str());
	}
	if (!voiceTranslatorTranscript.empty()) {
		ImGui::TextWrapped("Transcript: %s", voiceTranslatorTranscript.c_str());
	}

	ImGui::Separator();
	ImGui::Text("Output:");
	if (!translateOutput.empty()) {
		ImGui::SameLine();
		if (ImGui::SmallButton("Copy##TransCopy")) {
			copyToClipboard(translateOutput);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Use in Write")) {
			copyStringToBuffer(writeInput, sizeof(writeInput), translateOutput);
			activeMode = AiMode::Write;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear##TransClear")) {
			translateOutput.clear();
			voiceTranslatorStatus.clear();
			voiceTranslatorTranscript.clear();
			translateTtsPreview.clearPreviewArtifacts();
			stopTranslateTtsPlayback(true);
		}
	}
	drawGeneratedOutputChild(
		"##TransOut",
		translateOutput,
		generating.load() && activeGenerationMode == AiMode::Translate,
		streamMutex,
		streamingOutput);
}

void ofApp::drawCustomPanel() {
	drawPanelHeader("Custom Prompt", "configure system prompt + user input");
	if (textInferenceBackend == TextInferenceBackend::LlamaServer &&
		!textServerCapabilityHint.empty()) {
		ImGui::TextDisabled("%s", textServerCapabilityHint.c_str());
	}

	if (!promptTemplates.empty()) {
		ImGui::Text("Prompt Template:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(200);
		const char * preview =
			(selectedPromptTemplateIndex >= 0 &&
				selectedPromptTemplateIndex < static_cast<int>(promptTemplates.size()))
			? promptTemplates[static_cast<size_t>(selectedPromptTemplateIndex)].name.c_str()
			: "(select template)";
		if (ImGui::BeginCombo("##PromptTpl", preview)) {
			for (int i = 0; i < static_cast<int>(promptTemplates.size()); i++) {
				const bool selected = (selectedPromptTemplateIndex == i);
				if (ImGui::Selectable(
					promptTemplates[static_cast<size_t>(i)].name.c_str(),
					selected)) {
					selectedPromptTemplateIndex = i;
					const auto & systemPrompt =
						promptTemplates[static_cast<size_t>(i)].systemPrompt;
					copyStringToBuffer(
						customSystemPrompt,
						sizeof(customSystemPrompt),
						systemPrompt);
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}

	ImGui::Text("System prompt:");
	ImGui::InputTextMultiline(
		"##CustSys",
		customSystemPrompt,
		sizeof(customSystemPrompt),
		ImVec2(-1, 60));

	ImGui::Text("Your input:");
	ImGui::InputTextMultiline(
		"##CustIn",
		customInput,
		sizeof(customInput),
		ImVec2(-1, 100));

	ImGui::BeginDisabled(generating.load() || std::strlen(customInput) == 0);
	if (ImGui::Button("Run", ImVec2(100, 0))) {
		ofxGgmlTextAssistantRequest request;
		request.task = ofxGgmlTextTask::Custom;
		request.inputText = customInput;
		request.systemPrompt = customSystemPrompt;
		runPreparedTextRequest(AiMode::Custom, request);
	}
	ImGui::SameLine();
	if (ImGui::Button("Run with Sources", ImVec2(150, 0))) {
		ofxGgmlTextAssistantRequest request;
		request.task = ofxGgmlTextTask::Custom;
		request.inputText = customInput;
		request.systemPrompt = customSystemPrompt;
		const auto realtimeSettings = buildLiveContextSettings(
			sourceUrlsInput,
			"Loaded sources for this custom task");
		runPreparedTextRequest(AiMode::Custom, request, realtimeSettings);
	}
	ImGui::EndDisabled();

	ImGui::BeginDisabled(generating.load() || std::strlen(customInput) == 0);
	if (ImGui::Button("JSON Reply", ImVec2(100, 0))) {
		const std::string customPrompt = buildStructuredTextPrompt(
			std::string(customSystemPrompt) +
				(std::strlen(customSystemPrompt) > 0 ? "\n" : "") +
				"Return valid JSON only.",
			"Answer the user request with valid minified JSON only.",
			"User request",
			customInput,
			"JSON");
		runInference(AiMode::Custom, customInput, customSystemPrompt, customPrompt);
	}
	ImGui::SameLine();
	if (ImGui::Button("Professional Tone", ImVec2(130, 0))) {
		const std::string system = trim(customSystemPrompt).empty()
			? "You are a professional assistant. Be concise, concrete, and businesslike."
			: std::string(customSystemPrompt) +
				"\nPrefer concise, professional, high-signal answers.";
		const auto prompt = buildStructuredTextPrompt(
			system,
			"Answer the user request clearly and professionally.",
			"User request",
			customInput,
			"Answer");
		runInference(AiMode::Custom, customInput, system, prompt);
	}
	ImGui::EndDisabled();

	drawCitationSearchSection("Use Custom Input", customInput);

	ImGui::Separator();
	ImGui::Text("Output:");
	if (!customOutput.empty()) {
		ImGui::SameLine();
		if (ImGui::SmallButton("Copy##CustCopy")) {
			copyToClipboard(customOutput);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear##CustClear")) {
			customOutput.clear();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(%d chars)", static_cast<int>(customOutput.size()));
	}
	drawGeneratedOutputChild(
		"##CustOut",
		customOutput,
		generating.load() && activeGenerationMode == AiMode::Custom,
		streamMutex,
		streamingOutput);
}
