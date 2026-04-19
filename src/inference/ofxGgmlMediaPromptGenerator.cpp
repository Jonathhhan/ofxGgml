#include "inference/ofxGgmlMediaPromptGenerator.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

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

std::string stripPromptPrefix(const std::string & text) {
	const std::string trimmed = trimCopy(text);
	const std::string lowered = toLowerCopy(trimmed);
	static const std::vector<std::string> prefixes = {
		"prompt:",
		"visual prompt:",
		"image prompt:",
		"final prompt:"
	};
	for (const auto & prefixText : prefixes) {
		if (lowered.rfind(prefixText, 0) == 0) {
			return trimCopy(trimmed.substr(prefixText.size()));
		}
	}
	return trimmed;
}

std::string stripMusicPromptPrefix(const std::string & text) {
	const std::string trimmed = trimCopy(text);
	const std::string lowered = toLowerCopy(trimmed);
	static const std::vector<std::string> prefixes = {
		"prompt:",
		"music prompt:",
		"soundtrack prompt:",
		"audio prompt:",
		"final prompt:"
	};
	for (const auto & prefixText : prefixes) {
		if (lowered.rfind(prefixText, 0) == 0) {
			return trimCopy(trimmed.substr(prefixText.size()));
		}
	}
	return trimmed;
}

} // namespace

void ofxGgmlMediaPromptGenerator::setCompletionExecutable(const std::string & path) {
	m_inference.setCompletionExecutable(path);
}

ofxGgmlInference & ofxGgmlMediaPromptGenerator::getInference() {
	return m_inference;
}

const ofxGgmlInference & ofxGgmlMediaPromptGenerator::getInference() const {
	return m_inference;
}

ofxGgmlMusicToImagePreparedPrompt ofxGgmlMediaPromptGenerator::prepareMusicToImagePrompt(
	const ofxGgmlMusicToImageRequest & request) const {
	const std::string musicDescription = trimCopy(request.musicDescription);
	const std::string lyrics = trimCopy(request.lyrics);
	const std::string visualStyle = trimCopy(request.visualStyle);

	std::ostringstream prompt;
	prompt
		<< "You convert music descriptions into a single still-image diffusion prompt.\n"
		<< "Return only the final image prompt text. Do not add explanations, bullets, Markdown, or JSON.\n"
		<< "Write one concise but vivid prompt that works well for text-to-image generation.\n"
		<< "Focus on mood, palette, lighting, subject, environment, composition, and cinematic detail.\n"
		<< "Prefer a coherent still image over abstract music theory language.\n";

	if (!visualStyle.empty()) {
		prompt << "Preferred visual treatment: " << visualStyle << "\n";
	}
	if (!musicDescription.empty()) {
		prompt << "Music description:\n" << musicDescription << "\n";
	}
	if (request.includeLyrics && !lyrics.empty()) {
		prompt << "Lyrics / transcript cues:\n" << lyrics << "\n";
	}

	prompt
		<< "Generate the final visual prompt now.\n";

	ofxGgmlMusicToImagePreparedPrompt prepared;
	prepared.label = "Generate music-inspired visual prompt.";
	prepared.prompt = prompt.str();
	return prepared;
}

ofxGgmlImageToMusicPreparedPrompt ofxGgmlMediaPromptGenerator::prepareImageToMusicPrompt(
	const ofxGgmlImageToMusicRequest & request) const {
	const std::string imageDescription = trimCopy(request.imageDescription);
	const std::string sceneNotes = trimCopy(request.sceneNotes);
	const std::string musicalStyle = trimCopy(request.musicalStyle);
	const std::string instrumentation = trimCopy(request.instrumentation);

	std::ostringstream prompt;
	prompt
		<< "You convert visual descriptions into a single music-generation prompt.\n"
		<< "Return only the final music prompt text. Do not add explanations, bullets, Markdown, or JSON.\n"
		<< "Write one vivid prompt that would work well for soundtrack or text-to-music generation models.\n"
		<< "Focus on mood, pacing, dynamics, instrumentation, genre texture, and production detail.\n"
		<< "Prefer concise prompt language over technical analysis.\n";

	if (!musicalStyle.empty()) {
		prompt << "Preferred musical style: " << musicalStyle << "\n";
	}
	if (!instrumentation.empty()) {
		prompt << "Suggested instrumentation: " << instrumentation << "\n";
	}
	if (request.targetDurationSeconds > 0) {
		prompt << "Target duration: about " << request.targetDurationSeconds << " seconds.\n";
	}
	prompt << "Instrumental only: " << (request.instrumentalOnly ? "yes" : "no") << "\n";
	if (!imageDescription.empty()) {
		prompt << "Visual description:\n" << imageDescription << "\n";
	}
	if (!sceneNotes.empty()) {
		prompt << "Extra scene notes:\n" << sceneNotes << "\n";
	}
	prompt << "Generate the final music prompt now.\n";

	ofxGgmlImageToMusicPreparedPrompt prepared;
	prepared.label = "Generate image-inspired music prompt.";
	prepared.prompt = prompt.str();
	return prepared;
}

ofxGgmlMusicToImageResult ofxGgmlMediaPromptGenerator::generateMusicToImagePrompt(
	const std::string & modelPath,
	const ofxGgmlMusicToImageRequest & request,
	const ofxGgmlInferenceSettings & settings,
	std::function<bool(const std::string &)> onChunk) const {
	ofxGgmlMusicToImageResult result;
	result.prepared = prepareMusicToImagePrompt(request);
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

	result.visualPrompt = sanitizeVisualPrompt(result.inference.text);
	if (result.visualPrompt.empty()) {
		result.success = false;
		result.error = "Generated visual prompt was empty after sanitization.";
	}
	return result;
}

ofxGgmlImageToMusicResult ofxGgmlMediaPromptGenerator::generateImageToMusicPrompt(
	const std::string & modelPath,
	const ofxGgmlImageToMusicRequest & request,
	const ofxGgmlInferenceSettings & settings,
	std::function<bool(const std::string &)> onChunk) const {
	ofxGgmlImageToMusicResult result;
	result.prepared = prepareImageToMusicPrompt(request);
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

std::string ofxGgmlMediaPromptGenerator::sanitizeVisualPrompt(const std::string & rawText) {
	std::string sanitized = stripFenceWrappedText(rawText);
	sanitized = stripPromptPrefix(sanitized);
	sanitized = trimCopy(sanitized);

	if (sanitized.size() >= 2) {
		const char first = sanitized.front();
		const char last = sanitized.back();
		if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
			sanitized = trimCopy(sanitized.substr(1, sanitized.size() - 2));
		}
	}

	return sanitized;
}

std::string ofxGgmlMediaPromptGenerator::sanitizeMusicPrompt(const std::string & rawText) {
	std::string sanitized = stripFenceWrappedText(rawText);
	sanitized = stripMusicPromptPrefix(sanitized);
	sanitized = trimCopy(sanitized);

	if (sanitized.size() >= 2) {
		const char first = sanitized.front();
		const char last = sanitized.back();
		if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
			sanitized = trimCopy(sanitized.substr(1, sanitized.size() - 2));
		}
	}

	return sanitized;
}
