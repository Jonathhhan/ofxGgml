#include "catch2.hpp"
#include "../src/support/ofxGgmlSimpleSrtSubtitleParser.h"

#include <fstream>
#include <sstream>

TEST_CASE("SRT parser - valid basic subtitle", "[srt_parser]") {
	std::string srt =
		"1\n"
		"00:00:00,000 --> 00:00:02,500\n"
		"First subtitle line\n"
		"\n"
		"2\n"
		"00:00:03,000 --> 00:00:05,000\n"
		"Second subtitle line\n";

	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;

	SECTION("Parse succeeds") {
		bool ok = ofxGgmlSimpleSrtSubtitleParser::parseText(srt, cues, error);
		REQUIRE(ok);
		REQUIRE(error.empty());
		REQUIRE(cues.size() == 2);
	}

	SECTION("First cue timings") {
		ofxGgmlSimpleSrtSubtitleParser::parseText(srt, cues, error);
		REQUIRE(cues[0].startMs == 0);
		REQUIRE(cues[0].endMs == 2500);
		REQUIRE(cues[0].text == "First subtitle line");
	}

	SECTION("Second cue timings") {
		ofxGgmlSimpleSrtSubtitleParser::parseText(srt, cues, error);
		REQUIRE(cues[1].startMs == 3000);
		REQUIRE(cues[1].endMs == 5000);
		REQUIRE(cues[1].text == "Second subtitle line");
	}
}

TEST_CASE("SRT parser - multiline text", "[srt_parser]") {
	std::string srt =
		"1\n"
		"00:00:00,000 --> 00:00:02,000\n"
		"First line\n"
		"Second line\n"
		"Third line\n";

	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;

	bool ok = ofxGgmlSimpleSrtSubtitleParser::parseText(srt, cues, error);
	REQUIRE(ok);
	REQUIRE(cues.size() == 1);
	REQUIRE(cues[0].text == "First line\nSecond line\nThird line");
}

TEST_CASE("SRT parser - timecode with period separator", "[srt_parser]") {
	std::string srt =
		"1\n"
		"00:00:01.500 --> 00:00:03.250\n"
		"Text with period separator\n";

	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;

	bool ok = ofxGgmlSimpleSrtSubtitleParser::parseText(srt, cues, error);
	REQUIRE(ok);
	REQUIRE(cues.size() == 1);
	REQUIRE(cues[0].startMs == 1500);
	REQUIRE(cues[0].endMs == 3250);
}

TEST_CASE("SRT parser - hours and minutes", "[srt_parser]") {
	std::string srt =
		"1\n"
		"01:23:45,678 --> 02:34:56,789\n"
		"Long duration subtitle\n";

	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;

	bool ok = ofxGgmlSimpleSrtSubtitleParser::parseText(srt, cues, error);
	REQUIRE(ok);
	REQUIRE(cues.size() == 1);

	// 1 hour 23 min 45 sec 678 ms = (1*60*60 + 23*60 + 45) * 1000 + 678
	int expected_start = (1 * 3600 + 23 * 60 + 45) * 1000 + 678;
	REQUIRE(cues[0].startMs == expected_start);

	// 2 hour 34 min 56 sec 789 ms
	int expected_end = (2 * 3600 + 34 * 60 + 56) * 1000 + 789;
	REQUIRE(cues[0].endMs == expected_end);
}

TEST_CASE("SRT parser - empty input", "[srt_parser]") {
	std::string srt = "";
	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;

	bool ok = ofxGgmlSimpleSrtSubtitleParser::parseText(srt, cues, error);
	REQUIRE_FALSE(ok);
	REQUIRE_FALSE(error.empty());
	REQUIRE(cues.empty());
}

TEST_CASE("SRT parser - invalid timecode format", "[srt_parser]") {
	std::string srt =
		"1\n"
		"invalid timecode\n"
		"Text\n";

	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;

	bool ok = ofxGgmlSimpleSrtSubtitleParser::parseText(srt, cues, error);
	// Should fail to parse or produce no cues
	REQUIRE_FALSE(ok);
}

TEST_CASE("SRT parser - end time before start time", "[srt_parser]") {
	std::string srt =
		"1\n"
		"00:00:05,000 --> 00:00:02,000\n"
		"Invalid timing\n";

	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;

	// Should skip invalid cues
	ofxGgmlSimpleSrtSubtitleParser::parseText(srt, cues, error);
	REQUIRE(cues.empty());
}

TEST_CASE("SRT parser - whitespace handling", "[srt_parser]") {
	std::string srt =
		"  1  \n"
		"  00:00:00,000  -->  00:00:02,000  \n"
		"  Text with whitespace  \n"
		"\n";

	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;

	bool ok = ofxGgmlSimpleSrtSubtitleParser::parseText(srt, cues, error);
	REQUIRE(ok);
	REQUIRE(cues.size() == 1);
	REQUIRE(cues[0].text == "Text with whitespace");
}

TEST_CASE("SRT parser - multiple blocks", "[srt_parser]") {
	std::string srt =
		"1\n"
		"00:00:00,000 --> 00:00:01,000\n"
		"First\n"
		"\n"
		"2\n"
		"00:00:02,000 --> 00:00:03,000\n"
		"Second\n"
		"\n"
		"3\n"
		"00:00:04,000 --> 00:00:05,000\n"
		"Third\n";

	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;

	bool ok = ofxGgmlSimpleSrtSubtitleParser::parseText(srt, cues, error);
	REQUIRE(ok);
	REQUIRE(cues.size() == 3);
	REQUIRE(cues[0].text == "First");
	REQUIRE(cues[1].text == "Second");
	REQUIRE(cues[2].text == "Third");
}

TEST_CASE("SRT parser - no sequence number", "[srt_parser]") {
	// SRT can optionally omit sequence numbers
	std::string srt =
		"00:00:00,000 --> 00:00:01,000\n"
		"Text without number\n";

	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;

	bool ok = ofxGgmlSimpleSrtSubtitleParser::parseText(srt, cues, error);
	REQUIRE(ok);
	REQUIRE(cues.size() == 1);
	REQUIRE(cues[0].text == "Text without number");
}

TEST_CASE("SRT parser - UTF-8 BOM", "[srt_parser]") {
	// UTF-8 BOM: EF BB BF
	std::string srt = std::string("\xEF\xBB\xBF") +
		"1\n"
		"00:00:00,000 --> 00:00:01,000\n"
		"Text with BOM\n";

	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;

	bool ok = ofxGgmlSimpleSrtSubtitleParser::parseText(srt, cues, error);
	REQUIRE(ok);
	REQUIRE(cues.size() == 1);
	REQUIRE(cues[0].text == "Text with BOM");
}

TEST_CASE("SRT parser - UTF-16 LE encoding", "[srt_parser]") {
	// UTF-16 LE BOM: FF FE
	// Simple ASCII text encoded as UTF-16 LE
	std::string utf16le;
	utf16le += "\xFF\xFE"; // BOM

	// "1\n00:00:00,000 --> 00:00:01,000\nTest\n\n"
	const char * srtData = "1\n00:00:00,000 --> 00:00:01,000\nTest\n\n";
	for (const char * p = srtData; *p; ++p) {
		utf16le += *p;
		utf16le += '\0';
	}

	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;

	bool ok = ofxGgmlSimpleSrtSubtitleParser::parseText(utf16le, cues, error);
	WARN("UTF-16 LE error: " << error);
	REQUIRE(ok);
	REQUIRE(cues.size() == 1);
	REQUIRE(cues[0].text == "Test");
}

TEST_CASE("SRT parser - UTF-16 BE encoding", "[srt_parser]") {
	// UTF-16 BE BOM: FE FF
	std::string utf16be;
	utf16be += "\xFE\xFF"; // BOM

	// "1\n00:00:00,000 --> 00:00:01,000\nTest\n\n"
	const char * srtData = "1\n00:00:00,000 --> 00:00:01,000\nTest\n\n";
	for (const char * p = srtData; *p; ++p) {
		utf16be += '\0';
		utf16be += *p;
	}

	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;

	bool ok = ofxGgmlSimpleSrtSubtitleParser::parseText(utf16be, cues, error);
	if (!ok) {
		INFO("Parse error: " << error);
	}
	REQUIRE(ok);
	REQUIRE(cues.size() == 1);
	REQUIRE(cues[0].text == "Test");
}

TEST_CASE("SRT parser - invalid UTF-16 byte length", "[srt_parser]") {
	// UTF-16 with odd number of bytes (invalid)
	std::string invalid_utf16;
	invalid_utf16 += "\xFF\xFE"; // BOM
	invalid_utf16 += "A\0B"; // Only 3 bytes after BOM (should be even)

	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;

	bool ok = ofxGgmlSimpleSrtSubtitleParser::parseText(invalid_utf16, cues, error);
	REQUIRE_FALSE(ok);
	REQUIRE_FALSE(error.empty());
}

TEST_CASE("SRT parser - empty text blocks ignored", "[srt_parser]") {
	std::string srt =
		"1\n"
		"00:00:00,000 --> 00:00:01,000\n"
		"\n"
		"\n"
		"2\n"
		"00:00:02,000 --> 00:00:03,000\n"
		"Valid text\n";

	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;

	bool ok = ofxGgmlSimpleSrtSubtitleParser::parseText(srt, cues, error);
	REQUIRE(ok);
	// First cue should be skipped because text is empty
	REQUIRE(cues.size() == 1);
	REQUIRE(cues[0].text == "Valid text");
}

TEST_CASE("SRT parser - carriage return handling", "[srt_parser]") {
	std::string srt =
		"1\r\n"
		"00:00:00,000 --> 00:00:01,000\r\n"
		"Windows line endings\r\n";

	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;

	bool ok = ofxGgmlSimpleSrtSubtitleParser::parseText(srt, cues, error);
	REQUIRE(ok);
	REQUIRE(cues.size() == 1);
	REQUIRE(cues[0].text == "Windows line endings");
}
