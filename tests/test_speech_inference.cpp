#include "catch2.hpp"
#include "../src/ofxGgml.h"

#include <memory>

namespace {

class FakeSpeechBackend final : public ofxGgmlSpeechBackend {
public:
	std::string backendName() const override {
		return "FakeSpeech";
	}

	ofxGgmlSpeechResult transcribe(const ofxGgmlSpeechRequest &) const override {
		ofxGgmlSpeechResult result;
		result.success = true;
		result.backendName = backendName();
		result.text = "transcribed text";
		return result;
	}
};

} // namespace

TEST_CASE("Speech inference uses whisper backend by default", "[speech_inference]") {
	ofxGgmlSpeechInference inference;
	REQUIRE(inference.getBackend() != nullptr);
	REQUIRE(inference.getBackend()->backendName() == "WhisperCLI");
}

TEST_CASE("Speech task labels are stable", "[speech_inference]") {
	REQUIRE(std::string(ofxGgmlSpeechInference::taskLabel(ofxGgmlSpeechTask::Transcribe)) == "Transcribe");
	REQUIRE(std::string(ofxGgmlSpeechInference::taskLabel(ofxGgmlSpeechTask::Translate)) == "Translate");
}

TEST_CASE("Speech inference exposes recommended Whisper profiles", "[speech_inference]") {
	const auto profiles = ofxGgmlSpeechInference::defaultProfiles();
	REQUIRE(profiles.size() >= 4);
	REQUIRE(profiles.front().name == "Whisper Tiny.en");
	REQUIRE(profiles.back().modelFileHint == "ggml-large-v3-turbo.bin");
}

TEST_CASE("Whisper backend builds transcription command arguments", "[speech_inference]") {
	ofxGgmlWhisperCliSpeechBackend backend("whisper-cli");
	ofxGgmlSpeechRequest request;
	request.audioPath = "clip.wav";
	request.modelPath = "models/ggml-base.en.bin";
	request.languageHint = "de";
	request.prompt = "Names and technical terms should stay unchanged.";

	const auto args = backend.buildCommandArguments(request, "tmp/out");
	REQUIRE(args.size() >= 10);
	REQUIRE(args[0] == "whisper-cli");
	REQUIRE(args[1] == "-m");
	REQUIRE(args[2] == "models/ggml-base.en.bin");
	REQUIRE(args[3] == "-f");
	REQUIRE(args[4] == "clip.wav");
	REQUIRE(args[5] == "-otxt");
	REQUIRE(args[6] == "-of");
	REQUIRE(args[7] == "tmp/out");
	REQUIRE(args[8] == "-l");
	REQUIRE(args[9] == "de");
}

TEST_CASE("Whisper backend adds translate flag when requested", "[speech_inference]") {
	ofxGgmlWhisperCliSpeechBackend backend("whisper-cli");
	ofxGgmlSpeechRequest request;
	request.audioPath = "clip.wav";
	request.task = ofxGgmlSpeechTask::Translate;

	const auto args = backend.buildCommandArguments(request, "tmp/out");
	REQUIRE(std::find(args.begin(), args.end(), "--translate") != args.end());
	REQUIRE(backend.expectedTranscriptPath("tmp/out") == "tmp/out.txt");
}

TEST_CASE("Whisper backend requests subtitle artifacts when timestamps are enabled", "[speech_inference]") {
	ofxGgmlWhisperCliSpeechBackend backend("whisper-cli");
	ofxGgmlSpeechRequest request;
	request.audioPath = "clip.wav";
	request.returnTimestamps = true;

	const auto args = backend.buildCommandArguments(request, "tmp/out");
	REQUIRE(std::find(args.begin(), args.end(), "-osrt") != args.end());
	REQUIRE(std::find(args.begin(), args.end(), "-ovtt") != args.end());
	REQUIRE(backend.expectedSrtPath("tmp/out") == "tmp/out.srt");
	REQUIRE(backend.expectedVttPath("tmp/out") == "tmp/out.vtt");
}

TEST_CASE("Whisper backend parses SRT segments into addon speech segments", "[speech_inference]") {
	const std::string srt =
		"1\n"
		"00:00:00,000 --> 00:00:01,250\n"
		"Hello world.\n"
		"\n"
		"2\n"
		"00:00:01,500 --> 00:00:03,000\n"
		"Second line.\n";

	const auto segments = ofxGgmlWhisperCliSpeechBackend::parseSrtSegments(srt);
	REQUIRE(segments.size() == 2);
	REQUIRE(segments[0].startSeconds == Approx(0.0));
	REQUIRE(segments[0].endSeconds == Approx(1.25));
	REQUIRE(segments[0].text == "Hello world.");
	REQUIRE(segments[1].startSeconds == Approx(1.5));
	REQUIRE(segments[1].endSeconds == Approx(3.0));
	REQUIRE(segments[1].text == "Second line.");
}

TEST_CASE("Speech inference allows backend replacement", "[speech_inference]") {
	ofxGgmlSpeechInference inference;
	inference.setBackend(std::make_shared<FakeSpeechBackend>());

	ofxGgmlSpeechRequest request;
	request.audioPath = "ignored.wav";

	const auto result = inference.transcribe(request);
	REQUIRE(result.success);
	REQUIRE(result.backendName == "FakeSpeech");
	REQUIRE(result.text == "transcribed text");
}
