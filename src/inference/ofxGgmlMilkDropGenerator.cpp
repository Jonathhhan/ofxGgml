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

bool isCommentLine(const std::string & line) {
	return line.rfind("//", 0) == 0 || line.rfind(';', 0) == 0;
}

std::string buildRepairInstruction(
	const std::string & userInstruction,
	const ofxGgmlMilkDropValidation & validation) {
	std::ostringstream out;
	if (!trimCopy(userInstruction).empty()) {
		out << trimCopy(userInstruction) << "\n";
	} else {
		out << "Repair this MilkDrop / projectM preset so it is valid and conservative.\n";
	}
	if (!validation.issues.empty()) {
		out << "Fix these detected issues:\n";
		for (const auto & issue : validation.issues) {
			out << "- ";
			if (!issue.severity.empty()) {
				out << "[" << issue.severity << "] ";
			}
			if (issue.line > 0) {
				out << "line " << issue.line << ": ";
			}
			out << issue.message;
			if (!issue.suggestion.empty()) {
				out << " Suggestion: " << issue.suggestion;
			}
			out << "\n";
		}
	}
	out << "Preserve the creative style where possible, but prefer a working preset over risky syntax.";
	return out.str();
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
	result.validation = validatePreset(result.presetText);
	if (result.presetText.empty()) {
		result.success = false;
		result.error = "Generated preset text was empty after sanitization.";
	} else if (!result.validation.valid) {
		result.success = false;
		result.error = "Generated preset did not pass basic MilkDrop validation.";
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

ofxGgmlMilkDropResult ofxGgmlMilkDropGenerator::repairPreset(
	const std::string & modelPath,
	const std::string & presetText,
	const std::string & category,
	float randomness,
	const std::string & repairInstruction,
	const ofxGgmlInferenceSettings & settings,
	std::function<bool(const std::string &)> onChunk) const {
	const ofxGgmlMilkDropValidation validation = validatePreset(presetText);
	return editPreset(
		modelPath,
		presetText,
		buildRepairInstruction(repairInstruction, validation),
		category,
		randomness,
		settings,
		std::move(onChunk));
}

ofxGgmlMilkDropVariantResult ofxGgmlMilkDropGenerator::generateVariants(
	const std::string & modelPath,
	const ofxGgmlMilkDropRequest & request,
	int variantCount,
	const ofxGgmlInferenceSettings & settings) const {
	ofxGgmlMilkDropVariantResult result;
	const int clampedCount = std::clamp(variantCount, 1, 8);
	result.variants.reserve(static_cast<size_t>(clampedCount));

	for (int i = 0; i < clampedCount; ++i) {
		ofxGgmlMilkDropRequest variantRequest = request;
		std::ostringstream prompt;
		prompt
			<< request.prompt << "\n"
			<< "Generate variant " << (i + 1) << " of " << clampedCount
			<< ". Make it clearly distinct in rhythm, motion, palette, or geometry while staying in the same category.";
		variantRequest.prompt = prompt.str();

		ofxGgmlMilkDropVariant variant;
		variant.label = "Variant " + ofToString(i + 1);
		const ofxGgmlMilkDropResult variantResult = generatePreset(
			modelPath,
			variantRequest,
			settings,
			nullptr);
		variant.success = variantResult.success;
		variant.inference = variantResult.inference;
		variant.presetText = variantResult.presetText;
		variant.error = variantResult.error;
		variant.validation = variantResult.validation;
		result.variants.push_back(std::move(variant));
	}

	result.success = !result.variants.empty() &&
		std::all_of(
			result.variants.begin(),
			result.variants.end(),
			[](const ofxGgmlMilkDropVariant & variant) { return variant.success; });
	if (!result.success) {
		const auto failed = std::find_if(
			result.variants.begin(),
			result.variants.end(),
			[](const ofxGgmlMilkDropVariant & variant) { return !variant.success; });
		result.error = failed != result.variants.end() && !failed->error.empty()
			? failed->error
			: "One or more MilkDrop variants failed to generate.";
	}
	return result;
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

ofxGgmlMilkDropValidation ofxGgmlMilkDropGenerator::validatePreset(const std::string & rawText) {
	ofxGgmlMilkDropValidation validation;
	validation.sanitizedPresetText = sanitizePresetText(rawText);

	if (validation.sanitizedPresetText.empty()) {
		validation.issues.push_back({
			"error",
			0,
			"Preset text is empty after sanitization.",
			"Generate or repair the preset again."
		});
		return validation;
	}

	std::istringstream stream(validation.sanitizedPresetText);
	std::string line;
	int lineNumber = 0;
	int parenBalance = 0;
	bool firstMeaningfulLineSeen = false;

	while (std::getline(stream, line)) {
		++lineNumber;
		const std::string trimmed = trimCopy(line);
		if (trimmed.empty()) {
			continue;
		}

		if (!firstMeaningfulLineSeen) {
			firstMeaningfulLineSeen = true;
			validation.hasPresetHeader = trimmed.rfind("[preset", 0) == 0;
			if (!validation.hasPresetHeader) {
				validation.issues.push_back({
					"error",
					lineNumber,
					"Preset does not start with a [preset..] section header.",
					"Start the preset with [preset00]."
				});
			}
		}

		if (trimmed.front() == '[' && trimmed.back() == ']') {
			continue;
		}
		if (isCommentLine(trimmed)) {
			continue;
		}

		const auto equalsPos = trimmed.find('=');
		if (equalsPos == std::string::npos ||
			equalsPos == 0 ||
			equalsPos + 1 >= trimmed.size()) {
			validation.issues.push_back({
				"error",
				lineNumber,
				"Line is not in conservative key=value form.",
				"Rewrite this line as a supported assignment."
			});
			continue;
		}

		++validation.assignmentCount;
		parenBalance += static_cast<int>(std::count(trimmed.begin(), trimmed.end(), '('));
		parenBalance -= static_cast<int>(std::count(trimmed.begin(), trimmed.end(), ')'));
	}

	if (!validation.hasPresetHeader) {
		validation.issues.push_back({
			"error",
			0,
			"Missing preset header.",
			"Add [preset00] at the top."
		});
	}
	if (validation.assignmentCount < 3) {
		validation.issues.push_back({
			"warning",
			0,
			"Preset has very few assignments and may be too sparse to render well.",
			"Add a few conservative parameters such as zoom, decay, or fRating."
		});
	}
	if (parenBalance != 0) {
		validation.issues.push_back({
			"error",
			0,
			"Parentheses appear to be unbalanced.",
			"Repair the formula syntax so opening and closing parentheses match."
		});
	}
	if (validation.sanitizedPresetText.find("fRating=") == std::string::npos) {
		validation.issues.push_back({
			"warning",
			0,
			"Preset is missing fRating.",
			"Add a conservative fRating value for compatibility."
		});
	}

	validation.valid = std::none_of(
		validation.issues.begin(),
		validation.issues.end(),
		[](const ofxGgmlMilkDropValidationIssue & issue) {
			return issue.severity == "error";
		});
	return validation;
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
