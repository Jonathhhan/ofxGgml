#pragma once

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct ofxGgmlSimpleSrtCue {
	int startMs = 0;
	int endMs = 0;
	std::string text;
};

class ofxGgmlSimpleSrtSubtitleParser {
public:
	static bool parseText(
		const std::string & rawText,
		std::vector<ofxGgmlSimpleSrtCue> & cuesOut,
		std::string & errorOut) {
		cuesOut.clear();
		errorOut.clear();

		std::string content;
		if (!decodeTextContent(rawText, content, errorOut)) {
			return false;
		}

		normalizeLineEndings(content);

		size_t cursor = 0;
		while (cursor < content.size()) {
			const size_t blockEnd = content.find("\n\n", cursor);
			std::string block = content.substr(
				cursor,
				blockEnd == std::string::npos
					? std::string::npos
					: blockEnd - cursor);
			cursor = (blockEnd == std::string::npos)
				? content.size()
				: blockEnd + 2;

			trim(block);
			if (block.empty()) {
				continue;
			}

			std::istringstream blockStream(block);
			std::string line;
			std::vector<std::string> lines;
			while (std::getline(blockStream, line)) {
				trimRight(line);
				lines.push_back(line);
			}

			if (lines.empty()) {
				continue;
			}

			size_t timingIndex = 0;
			if (lines.size() > 1 && lines[0].find("-->") == std::string::npos) {
				timingIndex = 1;
			}
			if (timingIndex >= lines.size()) {
				continue;
			}

			const std::string & timingLine = lines[timingIndex];
			const size_t arrowPos = timingLine.find("-->");
			if (arrowPos == std::string::npos) {
				continue;
			}

			const std::string startText = trimCopy(
				timingLine.substr(0, arrowPos));
			const std::string endText = trimCopy(
				timingLine.substr(arrowPos + 3));

			ofxGgmlSimpleSrtCue cue;
			if (!parseTimecode(startText, cue.startMs) ||
				!parseTimecode(endText, cue.endMs) ||
				cue.endMs < cue.startMs) {
				continue;
			}

			std::ostringstream textStream;
			for (size_t i = timingIndex + 1; i < lines.size(); ++i) {
				if (lines[i].empty()) {
					continue;
				}
				if (textStream.tellp() > 0) {
					textStream << '\n';
				}
				textStream << lines[i];
			}

			cue.text = trimCopy(textStream.str());
			if (!cue.text.empty()) {
				cuesOut.push_back(std::move(cue));
			}
		}

		if (cuesOut.empty()) {
			errorOut = "No SRT subtitle cues were parsed.";
			return false;
		}

		return true;
	}

	static bool parseFile(
		const std::string & path,
		std::vector<ofxGgmlSimpleSrtCue> & cuesOut,
		std::string & errorOut) {
		cuesOut.clear();
		errorOut.clear();

		std::ifstream input(path, std::ios::binary);
		if (!input.is_open()) {
			errorOut = "Failed to open subtitle file.";
			return false;
		}

		std::ostringstream stream;
		stream << input.rdbuf();
		if (!input.good() && !input.eof()) {
			errorOut = "Failed to read subtitle file.";
			return false;
		}

		return parseText(stream.str(), cuesOut, errorOut);
	}

private:
	static bool decodeTextContent(
		const std::string & rawContent,
		std::string & decodedOut,
		std::string & errorOut) {
		decodedOut.clear();

		if (rawContent.size() >= 2) {
			const unsigned char byte0 = static_cast<unsigned char>(rawContent[0]);
			const unsigned char byte1 = static_cast<unsigned char>(rawContent[1]);
			const bool isUtf16Le = byte0 == 0xFF && byte1 == 0xFE;
			const bool isUtf16Be = byte0 == 0xFE && byte1 == 0xFF;
			if (isUtf16Le || isUtf16Be) {
				const size_t payloadOffset = 2;
				const size_t payloadSize = rawContent.size() - payloadOffset;
				if ((payloadSize % 2) != 0) {
					errorOut = "Subtitle file has invalid UTF-16 byte length.";
					return false;
				}

				std::u16string utf16Content;
				utf16Content.reserve(payloadSize / 2);
				for (size_t i = payloadOffset; i < rawContent.size(); i += 2) {
					const unsigned char lowByte = static_cast<unsigned char>(rawContent[i]);
					const unsigned char highByte = static_cast<unsigned char>(rawContent[i + 1]);
					const char16_t codeUnit = isUtf16Le
						? static_cast<char16_t>(lowByte | (highByte << 8))
						: static_cast<char16_t>((lowByte << 8) | highByte);
					utf16Content.push_back(codeUnit);
				}

				if (!appendUtf16AsUtf8(utf16Content, decodedOut)) {
					errorOut = "Subtitle file uses an invalid UTF-16 encoding.";
					return false;
				}

				return true;
			}
		}

		decodedOut = rawContent;
		if (decodedOut.size() >= 3 &&
			static_cast<unsigned char>(decodedOut[0]) == 0xEF &&
			static_cast<unsigned char>(decodedOut[1]) == 0xBB &&
			static_cast<unsigned char>(decodedOut[2]) == 0xBF) {
			decodedOut.erase(0, 3);
		}

		return true;
	}

	static void normalizeLineEndings(std::string & value) {
		value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
	}

	static void appendUtf8Codepoint(uint32_t codepoint, std::string & output) {
		if (codepoint <= 0x7F) {
			output.push_back(static_cast<char>(codepoint));
			return;
		}
		if (codepoint <= 0x7FF) {
			output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
			output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
			return;
		}
		if (codepoint <= 0xFFFF) {
			output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
			output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
			output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
			return;
		}

		output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
		output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
		output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
		output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
	}

	static bool appendUtf16AsUtf8(
		const std::u16string & utf16Content,
		std::string & output) {
		output.clear();
		output.reserve(utf16Content.size());

		for (size_t i = 0; i < utf16Content.size(); ++i) {
			uint32_t codepoint = utf16Content[i];
			if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
				if (i + 1 >= utf16Content.size()) {
					return false;
				}

				const uint32_t lowSurrogate = utf16Content[i + 1];
				if (lowSurrogate < 0xDC00 || lowSurrogate > 0xDFFF) {
					return false;
				}

				codepoint = 0x10000 +
					(((codepoint - 0xD800) << 10) | (lowSurrogate - 0xDC00));
				++i;
			} else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
				return false;
			}

			appendUtf8Codepoint(codepoint, output);
		}

		return true;
	}

	static void trimLeft(std::string & value) {
		value.erase(
			value.begin(),
			std::find_if(
				value.begin(),
				value.end(),
				[](unsigned char c) { return !std::isspace(c); }));
	}

	static void trimRight(std::string & value) {
		value.erase(
			std::find_if(
				value.rbegin(),
				value.rend(),
				[](unsigned char c) { return !std::isspace(c); }).base(),
			value.end());
	}

	static void trim(std::string & value) {
		trimLeft(value);
		trimRight(value);
	}

	static std::string trimCopy(std::string value) {
		trim(value);
		return value;
	}

	static bool parseTimecode(const std::string & rawText, int & timeMsOut) {
		std::string text = trimCopy(rawText);
		const size_t spacePos = text.find(' ');
		if (spacePos != std::string::npos) {
			text = text.substr(0, spacePos);
		}

		int hours = 0;
		int minutes = 0;
		int seconds = 0;
		int millis = 0;
		char colon1 = 0;
		char colon2 = 0;
		char comma = 0;
		std::istringstream parser(text);
		parser >> hours >> colon1 >> minutes >> colon2 >> seconds >> comma >> millis;
		if (!parser || colon1 != ':' || colon2 != ':' ||
			(comma != ',' && comma != '.')) {
			return false;
		}

		timeMsOut = (((hours * 60) + minutes) * 60 + seconds) * 1000 + millis;
		return true;
	}
};
