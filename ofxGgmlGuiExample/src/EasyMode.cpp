#include "ofApp.h"

#include "utils/ImGuiHelpers.h"
#include "utils/SpeechHelpers.h"

#include <sstream>

namespace {

const char * const kEasyActionLabels[] = {
	"Chat",
	"Summarize",
	"Translate",
	"Find Citations",
	"Video Essay",
	"Coding Agent (Plan)"
};

enum EasyActionIndex {
	EasyActionChat = 0,
	EasyActionSummarize,
	EasyActionTranslate,
	EasyActionFindCitations,
	EasyActionVideoEssay,
	EasyActionCodingAgentPlan
};

std::string joinSourceUris(const std::vector<ofxGgmlPromptSource> & sourcesUsed) {
	std::ostringstream output;
	for (const auto & source : sourcesUsed) {
		const std::string uri = !source.uri.empty() ? source.uri : source.label;
		if (!trim(uri).empty()) {
			output << "- " << uri << "\n";
		}
	}
	return output.str();
}

std::string formatEasyCitationResult(const ofxGgmlCitationSearchResult & result) {
	if (!trim(result.error).empty()) {
		return "[Error] " + result.error;
	}

	std::ostringstream output;
	if (!trim(result.summary).empty()) {
		output << result.summary << "\n\n";
	}
	if (!result.citations.empty()) {
		output << "Citations\n";
		for (size_t i = 0; i < result.citations.size(); ++i) {
			const auto & citation = result.citations[i];
			output << i + 1 << ". \"" << citation.quote << "\"\n";
			if (!trim(citation.note).empty()) {
				output << "   Note: " << citation.note << "\n";
			}
			if (!trim(citation.sourceUri).empty()) {
				output << "   Source: " << citation.sourceUri << "\n";
			} else if (!trim(citation.sourceLabel).empty()) {
				output << "   Source: " << citation.sourceLabel << "\n";
			}
		}
	}
	const std::string sources = joinSourceUris(result.sourcesUsed);
	if (!sources.empty()) {
		output << "\nSources used\n" << sources;
	}
	return trim(output.str());
}

std::string formatEasyVideoEssayResult(const ofxGgmlVideoEssayResult & result) {
	if (!trim(result.error).empty()) {
		return "[Error] " + result.error;
	}

	std::ostringstream output;
	if (!trim(result.outline).empty()) {
		output << "Outline\n" << result.outline << "\n\n";
	}
	if (!trim(result.script).empty()) {
		output << "Script\n" << result.script << "\n\n";
	}
	if (!result.voiceCues.empty()) {
		output << "Voice cues: " << result.voiceCues.size() << "\n";
	}
	if (!trim(result.srtText).empty()) {
		output << "SRT ready\n";
	}
	if (!trim(result.scenePlanSummary).empty()) {
		output << "\nScene plan\n" << result.scenePlanSummary << "\n";
	}
	if (!trim(result.editPlanSummary).empty()) {
		output << "\nEdit plan\n" << result.editPlanSummary << "\n";
	}
	if (!trim(result.editorBrief).empty()) {
		output << "\nEditor brief\n" << result.editorBrief << "\n";
	}
	return trim(output.str());
}

std::string formatEasyCodingAgentResult(const ofxGgmlCodingAgentResult & result) {
	if (!trim(result.error).empty()) {
		return "[Error] " + result.error;
	}

	std::ostringstream output;
	if (!trim(result.summary).empty()) {
		output << result.summary << "\n";
	}

	const auto & structured = result.assistantResult.structured;
	if (!trim(structured.goalSummary).empty()) {
		output << "\nGoal\n" << structured.goalSummary << "\n";
	}
	if (!trim(structured.approachSummary).empty()) {
		output << "\nApproach\n" << structured.approachSummary << "\n";
	}
	if (!structured.steps.empty()) {
		output << "\nSteps\n";
		for (size_t i = 0; i < structured.steps.size(); ++i) {
			output << i + 1 << ". " << structured.steps[i] << "\n";
		}
	}
	if (!trim(result.assistantResult.inference.text).empty()) {
		output << "\nAssistant output\n" << result.assistantResult.inference.text << "\n";
	}
	return trim(output.str());
}

const char * secondaryLabelForEasyAction(const int actionIndex) {
	switch (actionIndex) {
	case EasyActionChat: return "System prompt (optional)";
	case EasyActionTranslate: return "Target language";
	case EasyActionFindCitations: return "Crawler URL or source URL";
	case EasyActionVideoEssay: return "Crawler URL or source URL";
	case EasyActionCodingAgentPlan: return "Focused file path (optional)";
	default: return "Extra context (optional)";
	}
}

const char * primaryLabelForEasyAction(const int actionIndex) {
	switch (actionIndex) {
	case EasyActionChat: return "User text";
	case EasyActionSummarize: return "Text to summarize";
	case EasyActionTranslate: return "Text to translate";
	case EasyActionFindCitations: return "Topic";
	case EasyActionVideoEssay: return "Essay topic";
	case EasyActionCodingAgentPlan: return "Coding task";
	default: return "Input";
	}
}

} // namespace

void ofApp::configureEasyApiFromCurrentUi() {
	ofxGgmlEasyTextConfig textConfig;
	textConfig.modelPath = getSelectedModelPath();
	textConfig.preferServer = (textInferenceBackend == TextInferenceBackend::LlamaServer);
	textConfig.serverUrl = effectiveTextServerUrl(textServerUrl);
	textConfig.serverModel = trim(textServerModel);
	textConfig.settings = buildCurrentTextInferenceSettings(AiMode::Custom);
	easyApi.configureText(textConfig);

	ofxGgmlEasyCrawlerConfig crawlerConfig;
	easyApi.configureWebCrawler(crawlerConfig);
}

void ofApp::runEasyModeExample() {
	if (generating.load() || !engineReady) return;

	const std::string primaryInput = trim(easyPrimaryInput);
	if (primaryInput.empty()) {
		easyOutput = "[Error] Easy mode input is empty.";
		return;
	}

	configureEasyApiFromCurrentUi();
	cancelRequested.store(false);
	generating.store(true);
	activeGenerationMode = AiMode::Easy;
	generatingStatus = "Easy mode is running...";
	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput.clear();
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	const int actionIndex = std::clamp(
		easyActionIndex,
		0,
		static_cast<int>(std::size(kEasyActionLabels)) - 1);
	const std::string secondaryInput = trim(easySecondaryInput);
	const int citationCount = easyCitationCount;
	const bool useCrawler = easyUseCrawler;
	const float targetDuration = easyTargetDurationSeconds;

	workerThread = std::thread([this, actionIndex, primaryInput, secondaryInput, citationCount, useCrawler, targetDuration]() {
		try {
			std::string output;
			switch (actionIndex) {
			case EasyActionChat: {
				const auto result = easyApi.chat(primaryInput, "Auto", secondaryInput);
				output = !trim(result.inference.error).empty()
					? "[Error] " + result.inference.error
					: result.inference.text;
				break;
			}
			case EasyActionSummarize: {
				const auto result = easyApi.summarize(primaryInput);
				output = !trim(result.inference.error).empty()
					? "[Error] " + result.inference.error
					: result.inference.text;
				break;
			}
			case EasyActionTranslate: {
				const auto result = easyApi.translate(
					primaryInput,
					secondaryInput.empty() ? "German" : secondaryInput);
				output = !trim(result.inference.error).empty()
					? "[Error] " + result.inference.error
					: result.inference.text;
				break;
			}
			case EasyActionFindCitations: {
				std::vector<std::string> sourceUrls;
				if (!useCrawler && !secondaryInput.empty()) {
					sourceUrls.push_back(secondaryInput);
				}
				const auto result = easyApi.findCitations(
					primaryInput,
					sourceUrls,
					useCrawler ? secondaryInput : std::string(),
					static_cast<size_t>(std::max(1, citationCount)));
				output = formatEasyCitationResult(result);
				break;
			}
			case EasyActionVideoEssay: {
				ofxGgmlVideoEssayRequest request;
				request.topic = primaryInput;
				request.maxCitations = static_cast<size_t>(std::max(1, citationCount));
				request.useCrawler = useCrawler;
				request.targetDurationSeconds = std::max(30.0f, targetDuration);
				if (useCrawler) {
					request.crawlerRequest.startUrl = secondaryInput;
				} else if (!secondaryInput.empty()) {
					request.sourceUrls.push_back(secondaryInput);
				}
				const auto result = easyApi.planVideoEssay(request);
				output = formatEasyVideoEssayResult(result);
				break;
			}
			case EasyActionCodingAgentPlan:
			default: {
				ofxGgmlCodingAgentRequest request;
				request.taskLabel = "Easy mode coding agent";
				request.assistantRequest.action = ofxGgmlCodeAssistantAction::Ask;
				request.assistantRequest.userInput = primaryInput;
				if (!scriptLanguages.empty()) {
					request.assistantRequest.language =
						scriptLanguages[static_cast<size_t>(std::clamp(
							selectedLanguageIndex,
							0,
							static_cast<int>(scriptLanguages.size()) - 1))];
				}
				if (!secondaryInput.empty()) {
					request.assistantRequest.allowedFiles.push_back(secondaryInput);
				}

				ofxGgmlCodeAssistantContext context =
					buildScriptAssistantContext(makeInferenceModePromptSnapshot());
				context.activeMode = "Easy API";
				context.includeRepoContext = true;
				context.attachScriptSourceDocuments = true;

				ofxGgmlCodingAgentSettings settings;
				settings.mode = ofxGgmlCodingAgentMode::Plan;
				settings.autoApply = false;
				settings.autoVerify = false;
				settings.requireStructuredResult = true;
				const auto result = easyApi.runCodingAgent(request, context, settings);
				output = formatEasyCodingAgentResult(result);
				break;
			}
			}

			{
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingOutput = output;
				pendingRole = "assistant";
				pendingMode = AiMode::Easy;
			}
		} catch (const std::exception & e) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingOutput = std::string("[Error] ") + e.what();
			pendingRole = "assistant";
			pendingMode = AiMode::Easy;
		}

		generating.store(false);
	});
}

void ofApp::drawEasyPanel() {
	drawPanelHeader("Easy API", "high-level examples built on ofxGgmlEasy");
	ImGui::TextWrapped(
		"Use this mode as the shortest path through the addon. It reuses the current model and "
		"backend selection, then exercises the high-level facade instead of the lower-level studio flows.");

	easyActionIndex = std::clamp(
		easyActionIndex,
		0,
		static_cast<int>(std::size(kEasyActionLabels)) - 1);
	if (ImGui::BeginCombo("Action", kEasyActionLabels[easyActionIndex])) {
		for (int i = 0; i < static_cast<int>(std::size(kEasyActionLabels)); ++i) {
			const bool selected = (easyActionIndex == i);
			if (ImGui::Selectable(kEasyActionLabels[i], selected)) {
				easyActionIndex = i;
				autoSaveSession();
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	ImGui::Text("%s", primaryLabelForEasyAction(easyActionIndex));
	ImGui::InputTextMultiline(
		"##EasyPrimaryInput",
		easyPrimaryInput,
		sizeof(easyPrimaryInput),
		ImVec2(-1, 96));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}

	ImGui::InputText(
		secondaryLabelForEasyAction(easyActionIndex),
		easySecondaryInput,
		sizeof(easySecondaryInput));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}

	if (easyActionIndex == EasyActionFindCitations ||
		easyActionIndex == EasyActionVideoEssay) {
		if (ImGui::Checkbox("Use crawler URL", &easyUseCrawler)) {
			autoSaveSession();
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140);
		if (ImGui::InputInt("Citation count", &easyCitationCount)) {
			easyCitationCount = std::clamp(easyCitationCount, 1, 12);
			autoSaveSession();
		}
	}

	if (easyActionIndex == EasyActionVideoEssay) {
		ImGui::SetNextItemWidth(180);
		if (ImGui::InputFloat("Target duration (s)", &easyTargetDurationSeconds, 15.0f, 30.0f, "%.0f")) {
			easyTargetDurationSeconds = std::clamp(easyTargetDurationSeconds, 30.0f, 360.0f);
			autoSaveSession();
		}
	}

	ImGui::TextDisabled("Quick fill");
	if (ImGui::SmallButton("Use Chat")) {
		copyStringToBuffer(easyPrimaryInput, sizeof(easyPrimaryInput), chatInput);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Use Script")) {
		copyStringToBuffer(easyPrimaryInput, sizeof(easyPrimaryInput), scriptInput);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Use Summarize")) {
		copyStringToBuffer(easyPrimaryInput, sizeof(easyPrimaryInput), summarizeInput);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Use Translate")) {
		copyStringToBuffer(easyPrimaryInput, sizeof(easyPrimaryInput), translateInput);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Use Video Essay Topic")) {
		copyStringToBuffer(easyPrimaryInput, sizeof(easyPrimaryInput), videoEssayTopic);
	}

	if (ImGui::Button("Run Easy Example", ImVec2(160, 0))) {
		runEasyModeExample();
	}
	ImGui::SameLine();
	if (ImGui::Button("Copy Output", ImVec2(120, 0))) {
		copyToClipboard(easyOutput);
	}
	ImGui::SameLine();
	if (ImGui::Button("Use in Write", ImVec2(120, 0))) {
		copyStringToBuffer(writeInput, sizeof(writeInput), easyOutput);
		activeMode = AiMode::Write;
	}

	if (generating.load() && activeGenerationMode == AiMode::Easy) {
		ImGui::TextDisabled("%s", generatingStatus.c_str());
	}

	ImGui::BeginChild("##EasyOutput", ImVec2(0, 0), true);
	ImGui::TextWrapped("%s", easyOutput.c_str());
	ImGui::EndChild();
}
