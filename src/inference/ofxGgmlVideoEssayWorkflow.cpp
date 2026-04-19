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
		if (end > pos) {
			try {
				uniqueIndices.insert(std::stoi(text.substr(pos, end - pos)));
			} catch (...) {
			}
		}
		pos = end;
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

ofxGgmlVideoEssayResult ofxGgmlVideoEssayWorkflow::run(
	const ofxGgmlVideoEssayRequest & request) const {
	ofxGgmlVideoEssayResult result;
	const uint64_t startTimeMs = ofGetElapsedTimeMillis();

	if (trimCopy(request.modelPath).empty()) {
		result.error = "Video essay workflow requires a configured text model path.";
		return result;
	}
	if (trimCopy(request.topic).empty()) {
		result.error = "Video essay workflow topic is empty.";
		return result;
	}
	if (!request.useCrawler && request.sourceUrls.empty()) {
		result.error = "Video essay workflow needs loaded source URLs or a crawler URL.";
		return result;
	}
	if (request.useCrawler && trimCopy(request.crawlerRequest.startUrl).empty()) {
		result.error = "Video essay workflow crawler URL is empty.";
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
	result.success = true;
	result.backendName = !result.citationResult.backendName.empty()
		? result.citationResult.backendName
		: "VideoEssayWorkflow";
	result.elapsedMs = static_cast<float>(ofGetElapsedTimeMillis() - startTimeMs);
	return result;
}
