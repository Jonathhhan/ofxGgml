#include "inference/ofxGgmlMilkDropGenerator.h"

#include "ofMain.h"

#include <algorithm>
#include <cctype>
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

std::string joinCategoryDescriptions(
	const std::vector<ofxGgmlMilkDropCategoryOption> & categories) {
	std::ostringstream out;
	for (size_t i = 0; i < categories.size(); ++i) {
		if (i > 0) {
			out << "; ";
		}
		out << categories[i].name << ": " << categories[i].description;
	}
	return out.str();
}

std::string normalizeFenceWrappedText(const std::string & text) {
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

std::string slugify(const std::string & text) {
	std::string slug;
	slug.reserve(text.size());
	bool previousWasDash = false;
	for (const unsigned char ch : text) {
		if (std::isalnum(ch) != 0) {
			slug.push_back(static_cast<char>(std::tolower(ch)));
			previousWasDash = false;
		} else if (!previousWasDash) {
			slug.push_back('-');
			previousWasDash = true;
		}
	}
	while (!slug.empty() && slug.front() == '-') {
		slug.erase(slug.begin());
	}
	while (!slug.empty() && slug.back() == '-') {
		slug.pop_back();
	}
	return slug;
}

} // namespace

void ofxGgmlMilkDropGenerator::setCompletionExecutable(const std::string & path) {
	m_inference.setCompletionExecutable(path);
}

ofxGgmlInference & ofxGgmlMilkDropGenerator::getInference() {
	return m_inference;
}

const ofxGgmlInference & ofxGgmlMilkDropGenerator::getInference() const {
	return m_inference;
}

ofxGgmlMilkDropPreparedPrompt ofxGgmlMilkDropGenerator::preparePrompt(
	const ofxGgmlMilkDropRequest & request) const {
	const std::string category = trimCopy(request.category).empty()
		? "General"
		: trimCopy(request.category);
	const std::string randomnessBand =
		request.randomness >= 0.75f ? "high" :
		request.randomness >= 0.45f ? "medium" : "controlled";

	std::ostringstream prompt;
	prompt
		<< "You are MilkDropLM, a specialist in authoring valid MilkDrop / projectM preset code.\n"
		<< "Return only preset text. Do not add explanations, Markdown fences, JSON, or commentary.\n"
		<< "The preset must start with [preset00] and be immediately usable in projectM.\n"
		<< "Keep the syntax conservative and compatible with common projectM / MilkDrop playback.\n"
		<< "Prefer formulas and variables that are widely supported.\n"
		<< "Write for category: " << category << ".\n"
		<< "Category guidance: " << joinCategoryDescriptions(defaultCategories()) << "\n"
		<< "Randomness level: " << randomnessBand << ".\n"
		<< "Audio-reactive: " << (request.audioReactive ? "yes" : "no") << ".\n"
		<< "Seamless loop feel: " << (request.seamlessLoop ? "yes" : "no") << ".\n";

	if (!trimCopy(request.existingPresetText).empty()) {
		prompt
			<< "You are editing an existing preset. Preserve compatible structure where reasonable.\n"
			<< "Existing preset:\n"
			<< request.existingPresetText << "\n";
	}

	prompt
		<< "Creative direction:\n"
		<< request.prompt << "\n"
		<< "Return the final preset only.\n";

	ofxGgmlMilkDropPreparedPrompt prepared;
	prepared.label = trimCopy(request.existingPresetText).empty()
		? "Generate MilkDrop preset."
		: "Edit MilkDrop preset.";
	prepared.prompt = prompt.str();
	return prepared;
}

ofxGgmlMilkDropResult ofxGgmlMilkDropGenerator::generatePreset(
	const std::string & modelPath,
	const ofxGgmlMilkDropRequest & request,
	const ofxGgmlInferenceSettings & settings,
	std::function<bool(const std::string &)> onChunk) const {
	ofxGgmlMilkDropResult result;
	result.prepared = preparePrompt(request);
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

	result.presetText = sanitizePresetText(result.inference.text);
	if (result.presetText.empty()) {
		result.success = false;
		result.error = "Generated preset text was empty after sanitization.";
	}
	return result;
}

ofxGgmlMilkDropResult ofxGgmlMilkDropGenerator::editPreset(
	const std::string & modelPath,
	const std::string & existingPresetText,
	const std::string & editInstruction,
	const std::string & category,
	float randomness,
	const ofxGgmlInferenceSettings & settings,
	std::function<bool(const std::string &)> onChunk) const {
	ofxGgmlMilkDropRequest request;
	request.prompt = editInstruction;
	request.category = category;
	request.randomness = randomness;
	request.existingPresetText = existingPresetText;
	return generatePreset(
		modelPath,
		request,
		settings,
		std::move(onChunk));
}

std::string ofxGgmlMilkDropGenerator::savePreset(
	const std::string & presetText,
	const std::string & outputPath) const {
	const std::string sanitized = sanitizePresetText(presetText);
	if (sanitized.empty()) {
		return {};
	}

	std::filesystem::path path = outputPath;
	if (path.extension().empty()) {
		path += ".milk";
	}

	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream out(path, std::ios::binary);
	if (!out.is_open()) {
		return {};
	}
	out << sanitized;
	return path.lexically_normal().string();
}

std::vector<ofxGgmlMilkDropCategoryOption> ofxGgmlMilkDropGenerator::defaultCategories() {
	return {
		{"General", "balanced MilkDrop visuals that work well as a default"},
		{"Psychedelic", "colorful, swirling, high-energy hallucination visuals"},
		{"Ambient", "slow, spacious, meditative motion with soft color changes"},
		{"Bass Heavy", "strong beat-driven pulses, zooms, and bass response"},
		{"Geometric", "clean shapes, symmetry, kaleidoscopic geometry, and lines"},
		{"Glitch", "digital distortion, stutter, slicing, and unstable motion"},
		{"Retro", "CRT-era, rave-era, or analog-inspired visual texture"},
		{"Liquid", "fluid, organic, rippling, and wave-like motion"}
	};
}

std::string ofxGgmlMilkDropGenerator::sanitizePresetText(const std::string & rawText) {
	std::string sanitized = normalizeFenceWrappedText(rawText);
	const size_t presetStart = sanitized.find("[preset");
	if (presetStart != std::string::npos) {
		sanitized = sanitized.substr(presetStart);
	}
	return trimCopy(sanitized);
}

std::string ofxGgmlMilkDropGenerator::makeSuggestedFileName(
	const std::string & prompt,
	const std::string & category) {
	std::string base = slugify(prompt);
	if (base.empty()) {
		base = slugify(category);
	}
	if (base.empty()) {
		base = "milkdrop-preset";
	}
	if (base.size() > 48) {
		base.resize(48);
		while (!base.empty() && base.back() == '-') {
			base.pop_back();
		}
	}
	return base + ".milk";
}
