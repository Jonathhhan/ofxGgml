#include "inference/ofxGgmlVideoEssayWorkflow.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <set>
#include <sstream>

namespace {

std::string trimCopy(const std::string & value) {
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

std::vector<std::string> splitLines(const std::string & value) {
	std::vector<std::string> lines;
	std::istringstream input(value);
	std::string line;
	while (std::getline(input, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		lines.push_back(line);
	}
	return lines;
}

std::string joinLines(const std::vector<std::string> & lines) {
	std::ostringstream output;
	for (size_t i = 0; i < lines.size(); ++i) {
		if (i > 0) {
			output << '\n';
		}
		output << lines[i];
	}
	return output.str();
}

std::string sanitizeAssistantText(const std::string & rawText) {
	std::vector<std::string> lines;
	lines.reserve(splitLines(rawText).size());

	bool inFence = false;
	for (const std::string & rawLine : splitLines(rawText)) {
		const std::string trimmed = trimCopy(rawLine);
		if (trimmed.rfind("```", 0) == 0) {
			inFence = !inFence;
			continue;
		}
		if (inFence) {
			lines.push_back(rawLine);
			continue;
		}
		if (trimmed == "Outline:" ||
			trimmed == "Script:" ||
			trimmed == "Narration:" ||
			trimmed == "Voiceover:" ||
			trimmed == "Essay:" ||
			trimmed == "Video Essay Script:") {
			continue;
		}
		lines.push_back(rawLine);
	}

	return trimCopy(joinLines(lines));
}

size_t countWords(const std::string & text) {
	std::istringstream input(text);
	std::string token;
	size_t count = 0;
	while (input >> token) {
		++count;
	}
	return count;
}

double estimateDurationSeconds(const std::string & text) {
	const size_t words = std::max<size_t>(countWords(text), 1);
	return std::max(2.0, static_cast<double>(words) / 2.45);
}

std::string summarizeSectionText(const std::string & text) {
	const std::string trimmed = trimCopy(text);
	if (trimmed.size() <= 180) {
		return trimmed;
	}
	const size_t sentenceEnd = trimmed.find_first_of(".!?");
	if (sentenceEnd != std::string::npos && sentenceEnd + 1 <= 180) {
		return trimCopy(trimmed.substr(0, sentenceEnd + 1));
	}
	return trimCopy(trimmed.substr(0, 177)) + "...";
}

std::vector<int> collectSourceIndices(const std::string & text) {
	std::set<int> uniqueIndices;
	size_t pos = 0;
	while ((pos = text.find("[Source ", pos)) != std::string::npos) {
		pos += 8;
		size_t end = pos;
		while (end < text.size() &&
			std::isdigit(static_cast<unsigned char>(text[end]))) {
			++end;
		}
		const bool hasClosingBracket = end < text.size() && text[end] == ']';
		if (end > pos && hasClosingBracket) {
			try {
				uniqueIndices.insert(std::stoi(text.substr(pos, end - pos)));
			} catch (...) {
			}
		}
		pos = hasClosingBracket ? (end + 1) : end;
	}
	return std::vector<int>(uniqueIndices.begin(), uniqueIndices.end());
}

std::vector<std::string> splitIntoCueChunks(const std::string & text) {
	std::vector<std::string> chunks;
	std::string current;
	size_t currentWords = 0;

	auto flushCurrent = [&]() {
		const std::string trimmed = trimCopy(current);
		if (!trimmed.empty()) {
			chunks.push_back(trimmed);
		}
		current.clear();
		currentWords = 0;
	};

	std::istringstream input(text);
	std::string token;
	while (input >> token) {
		if (!current.empty()) {
			current += ' ';
		}
		current += token;
		++currentWords;
		const bool sentenceBoundary =
			!token.empty() &&
			(token.back() == '.' || token.back() == '!' || token.back() == '?');
		if (currentWords >= 34 || (sentenceBoundary && currentWords >= 16)) {
			flushCurrent();
		}
	}
	flushCurrent();
	return chunks;
}

std::string buildCitationDigest(const ofxGgmlCitationSearchResult & citationResult) {
	std::ostringstream output;
	if (!trimCopy(citationResult.summary).empty()) {
		output << "Citation summary:\n"
			<< trimCopy(citationResult.summary) << "\n\n";
	}
	output << "Available citations:\n";
	for (const auto & item : citationResult.citations) {
		output << "[Source " << item.sourceIndex << "] ";
		if (!item.sourceLabel.empty()) {
			output << item.sourceLabel;
		} else {
			output << "Source " << item.sourceIndex;
		}
		if (!item.sourceUri.empty()) {
			output << " (" << item.sourceUri << ")";
		}
		output << "\nQuote: " << item.quote << "\n";
		if (!item.note.empty()) {
			output << "Note: " << item.note << "\n";
		}
		output << "\n";
	}
	return trimCopy(output.str());
}

std::string formatSrtTime(double seconds) {
	const double safeSeconds = std::max(0.0, seconds);
	const int totalMilliseconds = static_cast<int>(std::llround(safeSeconds * 1000.0));
	const int hours = totalMilliseconds / 3600000;
	const int minutes = (totalMilliseconds / 60000) % 60;
	const int secs = (totalMilliseconds / 1000) % 60;
	const int millis = totalMilliseconds % 1000;
	std::ostringstream output;
	output << std::setfill('0')
		<< std::setw(2) << hours << ':'
		<< std::setw(2) << minutes << ':'
		<< std::setw(2) << secs << ','
		<< std::setw(3) << millis;
	return output.str();
}

} // namespace

ofxGgmlCitationSearch & ofxGgmlVideoEssayWorkflow::getCitationSearch() {
	return m_citationSearch;
}

const ofxGgmlCitationSearch & ofxGgmlVideoEssayWorkflow::getCitationSearch() const {
	return m_citationSearch;
}

ofxGgmlTextAssistant & ofxGgmlVideoEssayWorkflow::getTextAssistant() {
	return m_textAssistant;
}

const ofxGgmlTextAssistant & ofxGgmlVideoEssayWorkflow::getTextAssistant() const {
	return m_textAssistant;
}

ofxGgmlVideoPlanner & ofxGgmlVideoEssayWorkflow::getVideoPlanner() {
	return m_videoPlanner;
}

const ofxGgmlVideoPlanner & ofxGgmlVideoEssayWorkflow::getVideoPlanner() const {
	return m_videoPlanner;
}

std::string ofxGgmlVideoEssayWorkflow::buildOutlinePrompt(
	const ofxGgmlVideoEssayRequest & request,
	const ofxGgmlCitationSearchResult & citationResult) {
	std::ostringstream prompt;
	prompt
		<< "Develop a concise, source-grounded video essay outline.\n"
		<< "Topic: " << trimCopy(request.topic) << "\n"
		<< "Target duration: " << std::max(15.0, request.targetDurationSeconds) << " seconds\n"
		<< "Tone: " << trimCopy(request.tone) << "\n"
		<< "Audience: " << trimCopy(request.audience) << "\n";
	if (request.includeCounterpoints) {
		prompt << "Include one short counterpoint or nuance section when the sources support it.\n";
	}
	prompt
		<< "\nUse only the supplied citations."
		<< " Write 4 to 6 markdown sections beginning with '## '."
		<< " Each section should contain one or two short bullet points and at least one inline citation like [Source 1]."
		<< "\n\n"
		<< buildCitationDigest(citationResult)
		<< "\n\nOutline:\n";
	return prompt.str();
}

std::string ofxGgmlVideoEssayWorkflow::buildScriptPrompt(
	const ofxGgmlVideoEssayRequest & request,
	const ofxGgmlCitationSearchResult & citationResult,
	const std::string & outline) {
	std::ostringstream prompt;
	prompt
		<< "Write a spoken video essay script grounded in the supplied outline and citations.\n"
		<< "Topic: " << trimCopy(request.topic) << "\n"
		<< "Target duration: " << std::max(15.0, request.targetDurationSeconds) << " seconds\n"
		<< "Tone: " << trimCopy(request.tone) << "\n"
		<< "Audience: " << trimCopy(request.audience) << "\n"
		<< "Keep the narration vivid but factual. Do not invent claims.\n"
		<< "Format the script as markdown sections starting with '## '."
		<< " Under each heading, write one or two narration paragraphs with inline citations like [Source 2]."
		<< " Return only the script.\n\n"
		<< "Outline:\n" << trimCopy(outline) << "\n\n"
		<< buildCitationDigest(citationResult)
		<< "\n\nScript:\n";
	return prompt.str();
}

std::string ofxGgmlVideoEssayWorkflow::buildVisualConceptPrompt(
	const ofxGgmlVideoEssayRequest & request,
	const ofxGgmlCitationSearchResult & citationResult,
	const std::vector<ofxGgmlVideoEssaySection> & sections) {
	std::ostringstream prompt;
	prompt
		<< "Develop a coherent visual concept for a narrated video essay.\n"
		<< "Topic: " << trimCopy(request.topic) << "\n"
		<< "Target duration: " << std::max(15.0, request.targetDurationSeconds) << " seconds\n"
		<< "Tone: " << trimCopy(request.tone) << "\n"
		<< "Audience: " << trimCopy(request.audience) << "\n"
		<< "Use the cited sections to shape a grounded documentary-style visual treatment."
		<< " Favor concrete imagery, recurring motifs, editorial pacing, and scene continuity."
		<< " Keep it useful for downstream video planning.\n"
		<< "Return 1 short paragraph followed by 4 to 8 bullet points describing visual motifs, recurring entities,"
		<< " environments, and camera language."
		<< "\n\nSections:\n";

	for (const auto & section : sections) {
		prompt << "- " << (!trimCopy(section.title).empty() ? section.title : ("Section " + std::to_string(section.index + 1)))
			<< " (" << std::fixed << std::setprecision(1) << std::max(1.0, section.estimatedDurationSeconds) << " s)"
			<< ": " << trimCopy(section.summary);
		if (!section.sourceIndices.empty()) {
			prompt << " | cites";
			for (const int sourceIndex : section.sourceIndices) {
				prompt << " [Source " << sourceIndex << "]";
			}
		}
		prompt << "\n";
	}

	prompt << "\n" << buildCitationDigest(citationResult) << "\n\nVisual concept:\n";
	return prompt.str();
}

std::vector<ofxGgmlVideoEssaySection> ofxGgmlVideoEssayWorkflow::parseSectionsFromScript(
	const std::string & script,
	double targetDurationSeconds) {
	std::vector<ofxGgmlVideoEssaySection> sections;
	const std::string sanitized = sanitizeAssistantText(script);
	if (sanitized.empty()) {
		return sections;
	}

	auto flushSection = [&](std::string & title, std::ostringstream & body) {
		const std::string narration = trimCopy(body.str());
		if (narration.empty()) {
			body.str("");
			body.clear();
			return;
		}

		ofxGgmlVideoEssaySection section;
		section.index = static_cast<int>(sections.size());
		section.title = trimCopy(title);
		if (section.title.empty()) {
			section.title = "Section " + std::to_string(section.index + 1);
		}
		section.narrationText = narration;
		section.summary = summarizeSectionText(narration);
		section.sourceIndices = collectSourceIndices(narration);
		section.estimatedDurationSeconds = estimateDurationSeconds(narration);
		sections.push_back(std::move(section));

		body.str("");
		body.clear();
		title.clear();
	};

	std::string currentTitle;
	std::ostringstream currentBody;
	bool sawMarkdownHeading = false;
	for (const std::string & rawLine : splitLines(sanitized)) {
		const std::string trimmed = trimCopy(rawLine);
		if (trimmed.rfind("## ", 0) == 0) {
			sawMarkdownHeading = true;
			flushSection(currentTitle, currentBody);
			currentTitle = trimCopy(trimmed.substr(3));
			continue;
		}

		if (!trimmed.empty()) {
			if (!currentBody.str().empty()) {
				currentBody << "\n";
			}
			currentBody << trimmed;
		} else if (!currentBody.str().empty()) {
			currentBody << "\n";
		}
	}
	flushSection(currentTitle, currentBody);

	if (!sawMarkdownHeading && sections.empty()) {
		std::ostringstream paragraph;
		std::string fallbackTitle;
		for (const std::string & rawLine : splitLines(sanitized)) {
			const std::string trimmed = trimCopy(rawLine);
			if (trimmed.empty()) {
				flushSection(fallbackTitle, paragraph);
				continue;
			}
			if (!paragraph.str().empty()) {
				paragraph << ' ';
			}
			paragraph << trimmed;
		}
		flushSection(fallbackTitle, paragraph);
	}

	if (targetDurationSeconds > 1.0 && !sections.empty()) {
		double totalEstimated = 0.0;
		for (const auto & section : sections) {
			totalEstimated += section.estimatedDurationSeconds;
		}
		if (totalEstimated > 0.0) {
			const double scale = targetDurationSeconds / totalEstimated;
			for (auto & section : sections) {
				section.estimatedDurationSeconds =
					std::max(2.0, section.estimatedDurationSeconds * scale);
			}
		}
	}

	return sections;
}

std::vector<ofxGgmlVideoEssayVoiceCue> ofxGgmlVideoEssayWorkflow::buildVoiceCueSheet(
	const std::vector<ofxGgmlVideoEssaySection> & sections) {
	std::vector<ofxGgmlVideoEssayVoiceCue> cues;
	double currentTime = 0.0;
	for (const auto & section : sections) {
		const auto chunks = splitIntoCueChunks(section.narrationText);
		for (const auto & chunk : chunks) {
			ofxGgmlVideoEssayVoiceCue cue;
			cue.index = static_cast<int>(cues.size());
			cue.sectionIndex = section.index;
			cue.text = trimCopy(chunk);
			cue.startSeconds = currentTime;
			cue.endSeconds = currentTime + estimateDurationSeconds(cue.text);
			currentTime = cue.endSeconds + 0.12;
			cues.push_back(std::move(cue));
		}
	}
	return cues;
}

std::string ofxGgmlVideoEssayWorkflow::buildSrt(
	const std::vector<ofxGgmlVideoEssayVoiceCue> & cues) {
	std::ostringstream output;
	for (size_t i = 0; i < cues.size(); ++i) {
		const auto & cue = cues[i];
		output << i + 1 << "\n"
			<< formatSrtTime(cue.startSeconds) << " --> "
			<< formatSrtTime(cue.endSeconds) << "\n"
			<< cue.text << "\n\n";
	}
	return output.str();
}

std::string ofxGgmlVideoEssayWorkflow::buildEditSourceSummary(
	const std::vector<ofxGgmlVideoEssaySection> & sections) {
	std::ostringstream summary;
	for (const auto & section : sections) {
		if (section.index > 0) {
			summary << "\n";
		}
		summary << section.index + 1 << ". "
			<< (!trimCopy(section.title).empty() ? section.title : ("Section " + std::to_string(section.index + 1)))
			<< " | " << std::fixed << std::setprecision(1)
			<< std::max(1.0, section.estimatedDurationSeconds) << " s";
		if (!trimCopy(section.summary).empty()) {
			summary << " | " << trimCopy(section.summary);
		}
	}
	return summary.str();
}

ofxGgmlVideoEssayValidation ofxGgmlVideoEssayWorkflow::validateRequest(
	const ofxGgmlVideoEssayRequest & request) {
	ofxGgmlVideoEssayValidation validation;
	if (trimCopy(request.modelPath).empty()) {
		validation.ok = false;
		validation.errors.push_back("Video essay workflow requires a configured text model path.");
	}
	if (trimCopy(request.topic).empty()) {
		validation.ok = false;
		validation.errors.push_back("Video essay workflow topic is empty.");
	}
	if (!request.useCrawler && request.sourceUrls.empty()) {
		validation.ok = false;
		validation.errors.push_back("Video essay workflow needs loaded source URLs or a crawler URL.");
	}
	if (request.useCrawler && trimCopy(request.crawlerRequest.startUrl).empty()) {
		validation.ok = false;
		validation.errors.push_back("Video essay workflow crawler URL is empty.");
	}
	if (request.targetDurationSeconds < 20.0) {
		validation.warnings.push_back(
			"Very short target durations can collapse the outline and make narration pacing abrupt.");
	}
	if (request.maxCitations < 3) {
		validation.warnings.push_back(
			"Fewer than three citations can make the essay feel thin or under-sourced.");
	}
	if (trimCopy(request.tone).empty()) {
		validation.warnings.push_back(
			"Tone is empty. The generated narration may sound more generic than intended.");
	}
	if (trimCopy(request.audience).empty()) {
		validation.warnings.push_back(
			"Audience is empty. Consider naming the intended viewer level for better pacing and explanation depth.");
	}
	return validation;
}

std::string ofxGgmlVideoEssayWorkflow::buildWorkflowManifest(
	const ofxGgmlVideoEssayRequest & request,
	const ofxGgmlVideoEssayResult & result) {
	ofJson manifest;
	manifest["topic"] = request.topic;
	manifest["target_duration_seconds"] = request.targetDurationSeconds;
	manifest["tone"] = request.tone;
	manifest["audience"] = request.audience;
	manifest["include_counterpoints"] = request.includeCounterpoints;
	manifest["use_crawler"] = request.useCrawler;
	ofJson sourceUrls = ofJson::array();
	for (const auto & sourceUrl : request.sourceUrls) {
		ofJson sourceUrlJson;
		sourceUrlJson = sourceUrl;
		sourceUrls.push_back(sourceUrlJson);
	}
	manifest["source_urls"] = std::move(sourceUrls);
	manifest["crawler_start_url"] = request.crawlerRequest.startUrl;
	manifest["max_citations"] = static_cast<int>(request.maxCitations);
	manifest["success"] = result.success;
	manifest["backend"] = result.backendName;
	manifest["error"] = result.error;
	manifest["visual_concept"] = result.visualConcept;
	manifest["scene_plan_summary"] = result.scenePlanSummary;
	manifest["edit_plan_summary"] = result.editPlanSummary;
	manifest["editor_brief"] = result.editorBrief;
	manifest["scene_planning_error"] = result.scenePlanningError;
	manifest["edit_planning_error"] = result.editPlanningError;
	manifest["validation"]["ok"] = result.validation.ok;
	ofJson validationErrors = ofJson::array();
	for (const auto & error : result.validation.errors) {
		ofJson errorJson;
		errorJson = error;
		validationErrors.push_back(errorJson);
	}
	manifest["validation"]["errors"] = std::move(validationErrors);
	ofJson validationWarnings = ofJson::array();
	for (const auto & warning : result.validation.warnings) {
		ofJson warningJson;
		warningJson = warning;
		validationWarnings.push_back(warningJson);
	}
	manifest["validation"]["warnings"] = std::move(validationWarnings);

	ofJson sections = ofJson::array();
	for (const auto & section : result.sections) {
		ofJson sourceIndices = ofJson::array();
		for (const int sourceIndex : section.sourceIndices) {
			ofJson sourceIndexJson;
			sourceIndexJson = sourceIndex;
			sourceIndices.push_back(sourceIndexJson);
		}
		ofJson sectionJson;
		sectionJson["index"] = section.index;
		sectionJson["title"] = section.title;
		sectionJson["summary"] = section.summary;
		sectionJson["estimated_duration_seconds"] = section.estimatedDurationSeconds;
		sectionJson["source_indices"] = std::move(sourceIndices);
		sections.push_back(sectionJson);
	}
	manifest["sections"] = std::move(sections);

	ofJson cues = ofJson::array();
	for (const auto & cue : result.voiceCues) {
		ofJson cueJson;
		cueJson["index"] = cue.index;
		cueJson["section_index"] = cue.sectionIndex;
		cueJson["text"] = cue.text;
		cueJson["start_seconds"] = cue.startSeconds;
		cueJson["end_seconds"] = cue.endSeconds;
		cues.push_back(cueJson);
	}
	manifest["voice_cues"] = std::move(cues);
	return manifest.dump(2);
}

ofxGgmlVideoEssayResult ofxGgmlVideoEssayWorkflow::run(
	const ofxGgmlVideoEssayRequest & request) const {
	ofxGgmlVideoEssayResult result;
	const uint64_t startTimeMs = ofGetElapsedTimeMillis();
	result.validation = validateRequest(request);
	if (!result.validation.ok) {
		result.error = result.validation.errors.empty()
			? "Video essay workflow request is invalid."
			: result.validation.errors.front();
		result.workflowManifestJson = buildWorkflowManifest(request, result);
		return result;
	}

	ofxGgmlCitationSearchRequest citationRequest;
	citationRequest.modelPath = request.modelPath;
	citationRequest.topic = request.topic;
	citationRequest.maxCitations = std::max<size_t>(request.maxCitations, 1);
	citationRequest.useCrawler = request.useCrawler;
	citationRequest.crawlerRequest = request.crawlerRequest;
	citationRequest.sourceUrls = request.sourceUrls;
	citationRequest.inferenceSettings = request.inferenceSettings;
	citationRequest.sourceSettings = request.sourceSettings;
	if (citationRequest.sourceSettings.maxSources == 0) {
		citationRequest.sourceSettings.maxSources = 6;
	}
	if (citationRequest.sourceSettings.maxCharsPerSource == 0) {
		citationRequest.sourceSettings.maxCharsPerSource = 2200;
	}
	if (citationRequest.sourceSettings.maxTotalChars == 0) {
		citationRequest.sourceSettings.maxTotalChars = 14000;
	}
	citationRequest.sourceSettings.requestCitations = true;

	result.citationResult = m_citationSearch.search(citationRequest);
	if (!result.citationResult.success) {
		result.error = result.citationResult.error;
		result.elapsedMs = static_cast<float>(ofGetElapsedTimeMillis() - startTimeMs);
		return result;
	}

	ofxGgmlTextAssistantRequest outlineRequest;
	outlineRequest.task = ofxGgmlTextTask::Custom;
	outlineRequest.labelOverride = "Draft cited video essay outline.";
	outlineRequest.systemPrompt =
		"You are a careful documentary researcher and script planner."
		" Stay grounded in the supplied sources and cite them inline.";
	outlineRequest.inputText = buildOutlinePrompt(request, result.citationResult);
	result.outlineResult = m_textAssistant.run(
		request.modelPath,
		outlineRequest,
		request.inferenceSettings);
	if (!result.outlineResult.inference.error.empty()) {
		result.error = result.outlineResult.inference.error;
		result.elapsedMs = static_cast<float>(ofGetElapsedTimeMillis() - startTimeMs);
		return result;
	}
	result.outline = sanitizeAssistantText(result.outlineResult.inference.text);

	ofxGgmlTextAssistantRequest scriptRequest;
	scriptRequest.task = ofxGgmlTextTask::Custom;
	scriptRequest.labelOverride = "Write cited video essay narration.";
	scriptRequest.systemPrompt =
		"You are a documentary narrator."
		" Write clear, spoken prose with natural rhythm while staying anchored to cited facts.";
	scriptRequest.inputText = buildScriptPrompt(request, result.citationResult, result.outline);
	result.scriptResult = m_textAssistant.run(
		request.modelPath,
		scriptRequest,
		request.inferenceSettings);
	if (!result.scriptResult.inference.error.empty()) {
		result.error = result.scriptResult.inference.error;
		result.elapsedMs = static_cast<float>(ofGetElapsedTimeMillis() - startTimeMs);
		return result;
	}
	result.script = sanitizeAssistantText(result.scriptResult.inference.text);
	result.sections = parseSectionsFromScript(result.script, request.targetDurationSeconds);
	result.voiceCues = buildVoiceCueSheet(result.sections);
	result.srtText = buildSrt(result.voiceCues);

	ofxGgmlTextAssistantRequest visualConceptRequest;
	visualConceptRequest.task = ofxGgmlTextTask::Custom;
	visualConceptRequest.labelOverride = "Build a visual concept for the video essay.";
	visualConceptRequest.systemPrompt =
		"You are a documentary creative director."
		" Translate grounded research and spoken structure into a clear visual concept for downstream planning.";
	visualConceptRequest.inputText = buildVisualConceptPrompt(
		request,
		result.citationResult,
		result.sections);
	const ofxGgmlTextAssistantResult visualConceptResult = m_textAssistant.run(
		request.modelPath,
		visualConceptRequest,
		request.inferenceSettings);
	if (!visualConceptResult.inference.error.empty()) {
		result.scenePlanningError = visualConceptResult.inference.error;
	} else {
		result.visualConcept = sanitizeAssistantText(visualConceptResult.inference.text);
	}

	if (!trimCopy(result.visualConcept).empty()) {
		ofxGgmlVideoPlannerRequest sceneRequest;
		sceneRequest.prompt = result.visualConcept;
		sceneRequest.durationSeconds = std::max(15.0, request.targetDurationSeconds);
		sceneRequest.sceneCount = std::clamp(
			static_cast<int>(result.sections.empty() ? 4 : result.sections.size()),
			2,
			8);
		sceneRequest.beatCount = std::clamp(
			static_cast<int>(std::lround(sceneRequest.durationSeconds / 12.0)),
			3,
			12);
		sceneRequest.multiScene = sceneRequest.sceneCount > 1;
		sceneRequest.preferredStyle = "grounded documentary essay, cinematic but factual";
		const ofxGgmlVideoPlannerResult sceneResult = m_videoPlanner.plan(
			request.modelPath,
			sceneRequest,
			request.inferenceSettings,
			m_textAssistant.getInference());
		if (sceneResult.success) {
			result.scenePlan = sceneResult.plan;
			result.scenePlanJson = ofxGgmlVideoPlanner::extractJsonObject(sceneResult.rawText);
			result.scenePlanSummary = ofxGgmlVideoPlanner::summarizePlan(sceneResult.plan);
		} else {
			result.scenePlanningError = sceneResult.error;
		}

		ofxGgmlVideoEditPlannerRequest editRequest;
		editRequest.sourcePrompt = result.visualConcept;
		editRequest.editGoal =
			"Build a video essay edit that supports the narration, preserves chronology, and keeps the sources grounded.";
		editRequest.sourceAnalysis = buildEditSourceSummary(result.sections);
		editRequest.targetDurationSeconds = std::max(15.0, request.targetDurationSeconds);
		editRequest.clipCount = std::clamp(
			static_cast<int>(result.sections.empty() ? 6 : result.sections.size() * 2),
			3,
			12);
		editRequest.preserveChronology = true;
		const ofxGgmlVideoEditPlannerResult editResult = m_videoPlanner.planEdits(
			request.modelPath,
			editRequest,
			request.inferenceSettings,
			m_textAssistant.getInference());
		if (editResult.success) {
			result.editPlan = editResult.plan;
			result.editPlanJson = ofxGgmlVideoPlanner::extractJsonObject(editResult.rawText);
			result.editPlanSummary = ofxGgmlVideoPlanner::summarizeEditPlan(editResult.plan);
			result.editorBrief = ofxGgmlVideoPlanner::buildEditorBrief(editResult.plan);
		} else {
			result.editPlanningError = editResult.error;
		}
	}

	result.success = true;
	result.backendName = !result.citationResult.backendName.empty()
		? result.citationResult.backendName
		: "VideoEssayWorkflow";
	if (result.sections.empty()) {
		result.validation.warnings.push_back(
			"Script parsing did not yield named sections. Downstream scene and edit handoff may be less structured.");
	}
	if (trimCopy(result.visualConcept).empty()) {
		result.validation.warnings.push_back(
			"Visual concept generation returned empty output. Scene/edit planning will be limited to narration structure.");
	}
	result.workflowManifestJson = buildWorkflowManifest(request, result);
	result.elapsedMs = static_cast<float>(ofGetElapsedTimeMillis() - startTimeMs);
	return result;
}
