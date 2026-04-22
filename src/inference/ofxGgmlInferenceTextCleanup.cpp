#include "ofxGgmlInferenceTextCleanup.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <regex>
#include <sstream>
#include <string_view>

namespace {

struct TokenLiteral {
	const char * text;
	size_t len;
};

static constexpr TokenLiteral kRoleLabels[] = {
	{ "user:", 5 },
	{ "user", 4 },
	{ "assistant:", 10 },
	{ "assistant", 9 },
	{ "system:", 7 },
	{ "system", 6 },
	{ "User:", 5 },
	{ "User", 4 },
	{ "Assistant:", 10 },
	{ "Assistant", 9 },
	{ "System:", 7 },
	{ "System", 6 },
	{ "A:", 2 },
	{ "> ", 2 },
};

static constexpr TokenLiteral kTrailingArtifacts[] = {
	{ "> EOF by user", 13 },
	{ "> EOF", 5 },
	{ "EOF", 3 },
	{ "Interrupted by user", 19 },
	{ "[end of text]", 13 },
	{ "<|endoftext|>", 13 },
	{ "<|end_of_text|>", 15 },
	{ "<|eot_id|>", 10 },
	{ "[DONE]", 6 },
};

static constexpr TokenLiteral kInteractivePreambleMarkers[] = {
	{ "Running in interactive mode", 27 },
	{ "Press Ctrl+C to interject", 24 },
	{ "Press Return to return control to the AI", 39 },
	{ "To return control without starting a new line", 43 },
	{ "If you want to submit another line", 34 },
	{ "Not using system message", 24 },
	{ "Using system message", 20 },
	{ "Reverse prompt", 14 },
};

std::string trimCopy(const std::string & s) {
	size_t b = 0;
	while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
		++b;
	}
	size_t e = s.size();
	while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
		--e;
	}
	return s.substr(b, e - b);
}

std::string_view trimView(const std::string & s) {
	size_t b = 0;
	while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
		++b;
	}
	size_t e = s.size();
	while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
		--e;
	}
	return std::string_view(s).substr(b, e - b);
}

bool isRuntimeNoiseLine(std::string_view trimmedLine) {
	if (trimmedLine.empty()) return true;
	if (trimmedLine.rfind("ofxGgml [", 0) == 0) return true;
	if (trimmedLine.find("saving final output to session file") != std::string::npos) return true;
	if (trimmedLine.rfind("main:", 0) == 0 &&
		(trimmedLine.find("session file") != std::string::npos ||
		 trimmedLine.find("prompt_cache_") != std::string::npos)) return true;
	if (trimmedLine.find("warning: no usable GPU found") != std::string::npos) return true;
	if (trimmedLine.find("warning: one possible reason is that llama.cpp was compiled without GPU support") != std::string::npos) return true;
	if (trimmedLine.find("warning: consult docs/build.md for compilation instructions") != std::string::npos) return true;
	if (trimmedLine.find("ggml_cuda_init") != std::string::npos) return true;
	if (trimmedLine.find("ggml_vulkan_init") != std::string::npos) return true;
	if (trimmedLine.find("ggml_metal_init") != std::string::npos) return true;
	if (trimmedLine.find("--gpu-layers option will be ignored") != std::string::npos) return true;
	if (trimmedLine.find("Total VRAM:") != std::string::npos) return true;
	if (trimmedLine.find("compute capability") != std::string::npos) return true;
	if (trimmedLine.find("backend = ") != std::string::npos) return true;
	if (trimmedLine.find("VMM:") != std::string::npos) return true;
	if (trimmedLine.rfind("Device ", 0) == 0) {
		const size_t colon = trimmedLine.find(':');
		if (colon != std::string::npos && colon > 7) {
			bool digitsOnly = true;
			for (size_t i = 7; i < colon; ++i) {
				if (!std::isdigit(static_cast<unsigned char>(trimmedLine[i]))) {
					digitsOnly = false;
					break;
				}
			}
			if (digitsOnly) return true;
		}
	}
	return false;
}

std::string stripLiteralAnsiMarkers(const std::string & text) {
	static const std::regex markerRegex(R"(\[(?:\d{1,3})(?:;\d{1,3})*m)");
	return std::regex_replace(text, markerRegex, "");
}

std::string stripInteractivePreamble(const std::string & text) {
	size_t pos = 0;
	while (pos <= text.size()) {
		const size_t end = text.find('\n', pos);
		const std::string line = (end == std::string::npos)
			? text.substr(pos)
			: text.substr(pos, end - pos);
		const std::string_view trimmed = trimView(line);
		if (!trimmed.empty()) {
			bool isPreamble = false;
			for (const auto & marker : kInteractivePreambleMarkers) {
				if (trimmed.find(marker.text) != std::string_view::npos) {
					isPreamble = true;
					break;
				}
			}
			if (!isPreamble) {
				return trimCopy(text.substr(pos));
			}
		}
		if (end == std::string::npos) {
			break;
		}
		pos = end + 1;
	}
	return {};
}

std::string stripLlamaWarnings(const std::string & text) {
	if (text.empty()) return text;
	std::ostringstream filtered;
	std::istringstream lines(text);
	std::string line;
	while (std::getline(lines, line)) {
		const std::string_view trimmedLine = trimView(line);
		if (isRuntimeNoiseLine(trimmedLine)) {
			continue;
		}
		filtered << line << '\n';
	}
	std::string result = filtered.str();
	if (!result.empty() && result.back() == '\n' &&
		(text.empty() || text.back() != '\n')) {
		result.pop_back();
	}
	return result;
}

std::string stripLeadingRuntimeNoise(const std::string & text) {
	std::ostringstream filtered;
	std::istringstream lines(text);
	std::string line;
	bool skipping = true;
	while (std::getline(lines, line)) {
		const std::string_view trimmedLine = trimView(line);
		if (skipping) {
			if (isRuntimeNoiseLine(trimmedLine)) {
				continue;
			}
			skipping = false;
		}
		filtered << line;
		if (!lines.eof()) {
			filtered << '\n';
		}
	}
	return trimCopy(filtered.str());
}

std::string stripChatTemplateMarkers(const std::string & text) {
	std::string out;
	out.reserve(text.size());
	size_t i = 0;
	while (i < text.size()) {
		if (i + 1 < text.size() && text[i] == '<' && text[i + 1] == '<') {
			out.push_back(text[i]);
			++i;
			continue;
		}
		if (i + 1 < text.size() && text[i] == '<' && text[i + 1] == '|') {
			const size_t end = text.find("|>", i + 2);
			if (end != std::string::npos) {
				i = end + 2;
				continue;
			}
		}
		out.push_back(text[i]);
		++i;
	}
	return out;
}

std::string stripPromptEcho(const std::string & text, const std::string & prompt) {
	const std::string trimmedText = trimCopy(text);
	const std::string trimmedPrompt = trimCopy(prompt);
	if (trimmedText.empty() || trimmedPrompt.empty()) {
		return trimmedText;
	}
	if (trimmedText.rfind(trimmedPrompt, 0) == 0) {
		if (trimmedText.size() == trimmedPrompt.size()) {
			return {};
		}
		const char next = trimmedText[trimmedPrompt.size()];
		const bool looksLikeEchoBoundary =
			std::isspace(static_cast<unsigned char>(next)) ||
			next == ':' ||
			next == '-' ||
			next == '>' ||
			next == '|';
		if (!looksLikeEchoBoundary) {
			return trimmedText;
		}
		return trimCopy(trimmedText.substr(trimmedPrompt.size()));
	}
	return trimmedText;
}

std::string stripLeadingRoleLabels(const std::string & text) {
	std::string out = trimCopy(text);
	bool changed = true;
	while (changed) {
		changed = false;
		for (const auto & label : kRoleLabels) {
			if (out.size() >= label.len &&
				out.compare(0, label.len, label.text, label.len) == 0) {
				if (out.size() > label.len) {
					const char next = out[label.len];
					const bool hasBoundary =
						std::isspace(static_cast<unsigned char>(next)) ||
						next == ':' ||
						next == '-' ||
						next == '>' ||
						next == '|';
					if (!hasBoundary &&
						label.text[label.len - 1] != ':' &&
						label.text[label.len - 1] != ' ') {
						continue;
					}
				}
				out = trimCopy(out.substr(label.len));
				changed = true;
				break;
			}
		}
	}
	return out;
}

bool isStandaloneRoleLabel(std::string_view line) {
	auto trimViewCopy = [](std::string_view in) {
		while (!in.empty() && std::isspace(static_cast<unsigned char>(in.front()))) {
			in.remove_prefix(1);
		}
		while (!in.empty() && std::isspace(static_cast<unsigned char>(in.back()))) {
			in.remove_suffix(1);
		}
		return in;
	};

	line = trimViewCopy(line);
	while (!line.empty() && line.front() == '#') {
		line.remove_prefix(1);
		line = trimViewCopy(line);
	}
	line = trimViewCopy(line);
	for (const auto & label : kRoleLabels) {
		if (line.size() == label.len &&
			line.compare(0, label.len, label.text, label.len) == 0) {
			return true;
		}
	}
	return false;
}

std::string stripLeadingRoleHeaderLines(const std::string & text) {
	size_t pos = 0;
	while (pos <= text.size()) {
		const size_t end = text.find('\n', pos);
		const std::string_view line = (end == std::string::npos)
			? std::string_view(text).substr(pos)
			: std::string_view(text).substr(pos, end - pos);
		const std::string trimmed = trimCopy(std::string(line));
		if (trimmed.empty()) {
			if (end == std::string::npos) {
				return {};
			}
			pos = end + 1;
			continue;
		}
		if (!isStandaloneRoleLabel(trimmed)) {
			break;
		}
		if (end == std::string::npos) {
			return {};
		}
		pos = end + 1;
	}
	return trimCopy(text.substr(pos));
}

std::string stripTrailingArtifacts(const std::string & text) {
	std::string out = trimCopy(text);
	bool stripped = true;
	while (stripped) {
		stripped = false;
		for (const auto & artifact : kTrailingArtifacts) {
			if (out.size() >= artifact.len &&
				out.compare(out.size() - artifact.len, artifact.len, artifact.text, artifact.len) == 0) {
				out = trimCopy(out.substr(0, out.size() - artifact.len));
				stripped = true;
				break;
			}
		}
	}
	return out;
}

std::string stripInlineArtifacts(const std::string & text) {
	std::string out = text;
	for (const auto & artifact : kTrailingArtifacts) {
		size_t pos = 0;
		while ((pos = out.find(artifact.text, pos)) != std::string::npos) {
			out.erase(pos, artifact.len);
		}
	}
	return out;
}

std::string cleanCompletionOutput(const std::string & raw, const std::string & prompt) {
	std::string cleaned = stripLlamaWarnings(raw);
	cleaned = stripLeadingRuntimeNoise(cleaned);
	cleaned = stripInteractivePreamble(cleaned);
	cleaned = stripLiteralAnsiMarkers(cleaned);
	cleaned = stripChatTemplateMarkers(cleaned);
	cleaned = stripLeadingRoleHeaderLines(cleaned);
	cleaned = stripPromptEcho(cleaned, prompt);
	cleaned = stripLeadingRoleLabels(cleaned);
	cleaned = stripPromptEcho(cleaned, prompt);
	cleaned = stripTrailingArtifacts(cleaned);
	cleaned = stripLeadingRoleHeaderLines(cleaned);
	cleaned = stripLeadingRoleLabels(cleaned);
	return trimCopy(cleaned);
}

std::string cleanCompletionOutputConservative(const std::string & raw) {
	std::string cleaned = stripLlamaWarnings(raw);
	cleaned = stripLeadingRuntimeNoise(cleaned);
	cleaned = stripInteractivePreamble(cleaned);
	cleaned = stripLiteralAnsiMarkers(cleaned);
	cleaned = stripChatTemplateMarkers(cleaned);
	cleaned = stripLeadingRoleHeaderLines(cleaned);
	cleaned = stripLeadingRoleLabels(cleaned);
	cleaned = stripTrailingArtifacts(cleaned);
	cleaned = stripInlineArtifacts(cleaned);
	cleaned = stripLeadingRoleHeaderLines(cleaned);
	cleaned = stripLeadingRoleLabels(cleaned);
	return trimCopy(cleaned);
}

} // namespace

namespace ofxGgmlInferenceTextCleanup {

std::string sanitizeGeneratedText(
	const std::string & raw,
	const std::string & prompt) {
	return cleanCompletionOutput(raw, prompt);
}

std::string sanitizeStructuredText(const std::string & raw) {
	return trimCopy(stripLeadingRuntimeNoise(stripLlamaWarnings(raw)));
}

} // namespace ofxGgmlInferenceTextCleanup
