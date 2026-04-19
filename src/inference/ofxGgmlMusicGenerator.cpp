#include "inference/ofxGgmlMusicGenerator.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

std::string trimCopy(const std::string & text) {
	const auto begin = std::find_if_not(
		text.begin(),
		text.end(),
		[](unsigned char ch) { return std::isspace(ch) != 0; });
	const auto end = std::find_if_not(
		text.rbegin(),
		text.rend(),
		[](unsigned char ch) { return std::isspace(ch) != 0; }).base();
	if (begin >= end) {
		return {};
	}
	return std::string(begin, end);
}

std::string toLowerCopy(const std::string & text) {
	std::string lowered = text;
	std::transform(
		lowered.begin(),
		lowered.end(),
		lowered.begin(),
		[](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});
	return lowered;
}

std::string stripFenceWrappedText(const std::string & text) {
	std::string normalized = trimCopy(text);
	if (normalized.rfind("```", 0) != 0) {
		return normalized;
	}

	const size_t firstNewline = normalized.find('\n');
	if (firstNewline == std::string::npos) {
		return normalized;
	}

	normalized = normalized.substr(firstNewline + 1);
	const size_t closingFence = normalized.rfind("```");
	if (closingFence != std::string::npos) {
		normalized = normalized.substr(0, closingFence);
	}
	return trimCopy(normalized);
}

std::string stripKnownPrefix(
	const std::string & text,
	const std::vector<std::string> & prefixes) {
	const std::string trimmed = trimCopy(text);
	const std::string lowered = toLowerCopy(trimmed);
	for (const auto & prefix : prefixes) {
		if (lowered.rfind(prefix, 0) == 0) {
			return trimCopy(trimmed.substr(prefix.size()));
		}
	}
	return trimmed;
}

std::string removeWrappingQuotes(const std::string & text) {
	std::string trimmed = trimCopy(text);
	if (trimmed.size() >= 2) {
		const char first = trimmed.front();
		const char last = trimmed.back();
		if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
			trimmed = trimCopy(trimmed.substr(1, trimmed.size() - 2));
		}
	}
	return trimmed;
}

bool hasPrefix(const std::string & line, const std::string & prefix) {
	return line.rfind(prefix, 0) == 0;
}

std::string slugify(const std::string & text) {
	std::string slug;
	slug.reserve(text.size());
	bool lastWasDash = false;
	for (unsigned char ch : text) {
		if (std::isalnum(ch) != 0) {
			slug.push_back(static_cast<char>(std::tolower(ch)));
			lastWasDash = false;
		} else if (!lastWasDash) {
			slug.push_back('-');
			lastWasDash = true;
		}
	}
	while (!slug.empty() && slug.front() == '-') {
		slug.erase(slug.begin());
	}
	while (!slug.empty() && slug.back() == '-') {
		slug.pop_back();
	}
	return slug.empty() ? std::string("generated-theme") : slug;
}

} // namespace

ofxGgmlMusicGenerationBridgeBackend::ofxGgmlMusicGenerationBridgeBackend(
	GenerateCallback callback)
	: m_callback(std::move(callback)) {
}

std::string ofxGgmlMusicGenerationBridgeBackend::backendName() const {
	return "Bridge";
}

ofxGgmlMusicGenerationResult ofxGgmlMusicGenerationBridgeBackend::generate(
	const ofxGgmlMusicGenerationRequest & request) const {
	if (!m_callback) {
		ofxGgmlMusicGenerationResult result;
		result.error = "Music bridge backend callback is not configured.";
		return result;
	}
	return m_callback(request);
}

std::shared_ptr<ofxGgmlMusicGenerationBackend>
createMusicGenerationBridgeBackend(
	ofxGgmlMusicGenerationBridgeBackend::GenerateCallback callback) {
	return std::make_shared<ofxGgmlMusicGenerationBridgeBackend>(std::move(callback));
}

void ofxGgmlMusicGenerator::setCompletionExecutable(const std::string & path) {
	m_inference.setCompletionExecutable(path);
}

ofxGgmlInference & ofxGgmlMusicGenerator::getInference() {
	return m_inference;
}

const ofxGgmlInference & ofxGgmlMusicGenerator::getInference() const {
	return m_inference;
}

void ofxGgmlMusicGenerator::setBackend(
	std::shared_ptr<ofxGgmlMusicGenerationBackend> backend) {
	m_backend = std::move(backend);
}

std::shared_ptr<ofxGgmlMusicGenerationBackend> ofxGgmlMusicGenerator::getBackend() const {
	return m_backend;
}

ofxGgmlMusicPromptPreparedPrompt ofxGgmlMusicGenerator::prepareMusicPrompt(
	const ofxGgmlMusicPromptRequest & request) const {
	std::ostringstream prompt;
	prompt
		<< "You write high-quality prompts for text-to-music and soundtrack generation models.\n"
		<< "Return only the final music prompt text. Do not add explanations, bullets, Markdown, or JSON.\n"
		<< "Write one vivid prompt that specifies mood, instrumentation, motion, texture, and production detail.\n"
		<< "Prefer language that works well for models like MusicGen or Udio.\n";

	if (!trimCopy(request.style).empty()) {
		prompt << "Preferred musical style: " << trimCopy(request.style) << "\n";
	}
	if (!trimCopy(request.instrumentation).empty()) {
		prompt << "Instrumentation: " << trimCopy(request.instrumentation) << "\n";
	}
	if (!trimCopy(request.mood).empty()) {
		prompt << "Mood: " << trimCopy(request.mood) << "\n";
	}
	if (request.targetDurationSeconds > 0) {
		prompt << "Target duration: about " << request.targetDurationSeconds << " seconds.\n";
	}
	prompt << "Instrumental only: " << (request.instrumentalOnly ? "yes" : "no") << "\n";
	if (!trimCopy(request.referenceLyrics).empty()) {
		prompt << "Optional lyric / theme cues:\n" << trimCopy(request.referenceLyrics) << "\n";
	}
	prompt << "Creative direction:\n" << trimCopy(request.sourceConcept) << "\n";
	prompt << "Generate the final music prompt now.\n";

	ofxGgmlMusicPromptPreparedPrompt prepared;
	prepared.label = "Generate music prompt.";
	prepared.prompt = prompt.str();
	return prepared;
}

ofxGgmlMusicPromptResult ofxGgmlMusicGenerator::generateMusicPrompt(
	const std::string & modelPath,
	const ofxGgmlMusicPromptRequest & request,
	const ofxGgmlInferenceSettings & settings,
	std::function<bool(const std::string &)> onChunk) const {
	ofxGgmlMusicPromptResult result;
	result.prepared = prepareMusicPrompt(request);
	result.inference = m_inference.generate(
		modelPath,
		result.prepared.prompt,
		settings,
		std::move(onChunk));
	result.success = result.inference.success;
	if (!result.inference.success) {
		result.error = result.inference.error;
		return result;
	}

	result.musicPrompt = sanitizeMusicPrompt(result.inference.text);
	if (result.musicPrompt.empty()) {
		result.success = false;
		result.error = "Generated music prompt was empty after sanitization.";
	}
	return result;
}

ofxGgmlMusicNotationPreparedPrompt ofxGgmlMusicGenerator::prepareAbcNotationPrompt(
	const ofxGgmlMusicNotationRequest & request) const {
	std::ostringstream prompt;
	prompt
		<< "You compose short ABC notation sketches for music prototyping.\n"
		<< "Return only valid ABC notation text. Do not add explanations, Markdown fences, or prose.\n"
		<< "Keep the result concise, musical, and internally consistent.\n"
		<< "Prefer one lead melody line and optional simple accompaniment hints.\n"
		<< "Include at minimum the ABC headers X:, T:, M:, L:, Q:, and K:.\n"
		<< "Title: " << trimCopy(request.title) << "\n"
		<< "Style: " << trimCopy(request.style) << "\n"
		<< "Concept: " << trimCopy(request.sourceConcept) << "\n"
		<< "Meter: " << trimCopy(request.meter) << "\n"
		<< "Key: " << trimCopy(request.key) << "\n"
		<< "Bars: " << std::clamp(request.bars, 4, 64) << "\n"
		<< "Form: " << trimCopy(request.formHint) << "\n"
		<< "Instrumental only: " << (request.instrumentalOnly ? "yes" : "no") << "\n"
		<< "Generate the ABC notation now.\n";

	ofxGgmlMusicNotationPreparedPrompt prepared;
	prepared.label = "Generate ABC music sketch.";
	prepared.prompt = prompt.str();
	return prepared;
}

ofxGgmlMusicNotationResult ofxGgmlMusicGenerator::generateAbcNotation(
	const std::string & modelPath,
	const ofxGgmlMusicNotationRequest & request,
	const ofxGgmlInferenceSettings & settings,
	std::function<bool(const std::string &)> onChunk) const {
	ofxGgmlMusicNotationResult result;
	result.prepared = prepareAbcNotationPrompt(request);
	result.inference = m_inference.generate(
		modelPath,
		result.prepared.prompt,
		settings,
		std::move(onChunk));
	result.success = result.inference.success;
	if (!result.inference.success) {
		result.error = result.inference.error;
		return result;
	}

	result.abcNotation = sanitizeAbcNotation(result.inference.text);
	result.validation = validateAbcNotation(result.abcNotation);
	if (result.abcNotation.empty()) {
		result.success = false;
		result.error = "Generated ABC notation was empty after sanitization.";
	} else if (!result.validation.valid) {
		result.success = false;
		result.error = result.validation.issues.empty()
			? std::string("Generated ABC notation did not validate.")
			: result.validation.issues.front().message;
	}
	return result;
}

ofxGgmlMusicGenerationResult ofxGgmlMusicGenerator::generateAudio(
	const ofxGgmlMusicGenerationRequest & request) const {
	const auto start = std::chrono::steady_clock::now();
	if (!m_backend) {
		ofxGgmlMusicGenerationResult result;
		result.error =
			"No music generation backend is attached. Set a backend before calling generateAudio().";
		return result;
	}

	ofxGgmlMusicGenerationResult result = m_backend->generate(request);
	result.backendName = result.backendName.empty()
		? m_backend->backendName()
		: result.backendName;
	result.generatedPrompt = result.generatedPrompt.empty()
		? request.prompt
		: result.generatedPrompt;
	const auto end = std::chrono::steady_clock::now();
	if (result.elapsedMs <= 0.0f) {
		result.elapsedMs = std::chrono::duration<float, std::milli>(end - start).count();
	}
	return result;
}

std::string ofxGgmlMusicGenerator::sanitizeMusicPrompt(const std::string & rawText) {
	std::string sanitized = stripFenceWrappedText(rawText);
	sanitized = stripKnownPrefix(
		sanitized,
		{"prompt:", "music prompt:", "soundtrack prompt:", "final prompt:"});
	sanitized = removeWrappingQuotes(sanitized);
	return trimCopy(sanitized);
}

std::string ofxGgmlMusicGenerator::sanitizeAbcNotation(const std::string & rawText) {
	std::string sanitized = stripFenceWrappedText(rawText);
	sanitized = stripKnownPrefix(
		sanitized,
		{"abc:", "notation:", "score:", "final notation:"});
	sanitized = trimCopy(sanitized);
	if (sanitized.empty()) {
		return {};
	}

	std::vector<std::string> keptLines;
	std::istringstream input(sanitized);
	std::string line;
	bool bodyStarted = false;
	while (std::getline(input, line)) {
		std::string trimmed = trimCopy(line);
		if (trimmed.empty()) {
			if (!keptLines.empty() && !keptLines.back().empty()) {
				keptLines.push_back({});
			}
			continue;
		}

		const bool isHeader =
			hasPrefix(trimmed, "X:") ||
			hasPrefix(trimmed, "T:") ||
			hasPrefix(trimmed, "M:") ||
			hasPrefix(trimmed, "L:") ||
			hasPrefix(trimmed, "Q:") ||
			hasPrefix(trimmed, "K:") ||
			hasPrefix(trimmed, "C:") ||
			hasPrefix(trimmed, "R:") ||
			hasPrefix(trimmed, "%%");
		const bool looksLikeBody =
			trimmed.find('|') != std::string::npos ||
			trimmed.find('z') != std::string::npos ||
			trimmed.find('A') != std::string::npos ||
			trimmed.find('B') != std::string::npos ||
			trimmed.find('c') != std::string::npos;

		if (isHeader || looksLikeBody || bodyStarted) {
			keptLines.push_back(trimmed);
			bodyStarted = bodyStarted || !isHeader;
		}
	}

	std::ostringstream out;
	for (size_t i = 0; i < keptLines.size(); ++i) {
		out << keptLines[i];
		if (i + 1 < keptLines.size()) {
			out << "\n";
		}
	}
	return trimCopy(out.str());
}

ofxGgmlMusicNotationValidation ofxGgmlMusicGenerator::validateAbcNotation(
	const std::string & rawText) {
	ofxGgmlMusicNotationValidation validation;
	validation.sanitizedNotation = sanitizeAbcNotation(rawText);
	if (validation.sanitizedNotation.empty()) {
		validation.issues.push_back({
			"error",
			0,
			"ABC notation is empty after sanitization.",
			"Generate the music sketch again or provide a richer concept."
		});
		return validation;
	}

	const std::vector<std::string> requiredHeaders = {"X:", "T:", "M:", "L:", "Q:", "K:"};
	for (const auto & header : requiredHeaders) {
		if (validation.sanitizedNotation.find(header) == std::string::npos) {
			validation.issues.push_back({
				"error",
				0,
				"Missing required ABC header " + header,
				"Include all required ABC metadata headers before the note lines."
			});
		}
	}
	if (validation.sanitizedNotation.find('|') == std::string::npos) {
		validation.issues.push_back({
			"warning",
			0,
			"ABC notation does not appear to contain bar separators.",
			"Use bar lines to keep the phrase structure readable."
		});
	}

	validation.valid = std::none_of(
		validation.issues.begin(),
		validation.issues.end(),
		[](const ofxGgmlMusicNotationValidationIssue & issue) {
			return issue.severity == "error";
		});
	return validation;
}

std::string ofxGgmlMusicGenerator::saveAbcNotation(
	const std::string & abcNotation,
	const std::string & outputPath) {
	const std::string sanitized = sanitizeAbcNotation(abcNotation);
	if (sanitized.empty() || trimCopy(outputPath).empty()) {
		return {};
	}

	std::error_code ec;
	const std::filesystem::path path(outputPath);
	if (path.has_parent_path()) {
		std::filesystem::create_directories(path.parent_path(), ec);
	}

	std::ofstream file(path, std::ios::binary);
	if (!file) {
		return {};
	}
	file << sanitized;
	return file.good() ? path.lexically_normal().string() : std::string();
}

std::string ofxGgmlMusicGenerator::makeSuggestedFileName(const std::string & sourceConcept) {
	return slugify(sourceConcept) + ".abc";
}
