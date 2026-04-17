#include "catch2.hpp"
#include "../src/inference/ofxGgmlLiveSpeechTranscriber.h"

TEST_CASE("Live Speech Transcriber initialization", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechTranscriber transcriber;

	SECTION("Default construction") {
		REQUIRE_NOTHROW(ofxGgmlLiveSpeechTranscriber());
	}

	SECTION("Not capturing by default") {
		REQUIRE_FALSE(transcriber.isCapturing());
	}

	SECTION("Not busy by default") {
		REQUIRE_FALSE(transcriber.isBusy());
	}
}

TEST_CASE("Live Speech Transcriber configuration", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechTranscriber transcriber;

	SECTION("Set and get settings") {
		ofxGgmlLiveSpeechSettings settings;
		settings.sampleRate = 22050;
		settings.intervalSeconds = 2.0f;
		settings.windowSeconds = 10.0f;
		settings.languageHint = "en";

		transcriber.setSettings(settings);

		auto retrieved = transcriber.getSettings();
		REQUIRE(retrieved.sampleRate == 22050);
		REQUIRE(retrieved.intervalSeconds == 2.0f);
		REQUIRE(retrieved.windowSeconds == 10.0f);
		REQUIRE(retrieved.languageHint == "en");
	}

	SECTION("Default settings") {
		auto settings = transcriber.getSettings();
		REQUIRE(settings.sampleRate == 16000);
		REQUIRE_FALSE(settings.enabled);
	}
}

TEST_CASE("Live Speech Transcriber capture control", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechTranscriber transcriber;

	SECTION("Begin capture") {
		REQUIRE_NOTHROW(transcriber.beginCapture());
		REQUIRE(transcriber.isCapturing());
	}

	SECTION("Stop capture") {
		transcriber.beginCapture();
		REQUIRE(transcriber.isCapturing());

		transcriber.stopCapture();
		REQUIRE_FALSE(transcriber.isCapturing());
	}

	SECTION("Stop when not capturing") {
		REQUIRE_FALSE(transcriber.isCapturing());
		REQUIRE_NOTHROW(transcriber.stopCapture());
		REQUIRE_FALSE(transcriber.isCapturing());
	}

	SECTION("Multiple begin calls") {
		transcriber.beginCapture();
		bool wasCapturing = transcriber.isCapturing();
		transcriber.beginCapture();
		REQUIRE(transcriber.isCapturing() == wasCapturing);
		transcriber.stopCapture();
	}
}

TEST_CASE("Live Speech Transcriber audio submission", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechTranscriber transcriber;

	SECTION("Append mono samples vector") {
		std::vector<float> audioData(1600, 0.0f);
		REQUIRE_NOTHROW(transcriber.appendMonoSamples(audioData));
	}

	SECTION("Append mono samples pointer") {
		std::vector<float> audioData(1600, 0.0f);
		REQUIRE_NOTHROW(transcriber.appendMonoSamples(audioData.data(), audioData.size()));
	}

	SECTION("Append empty audio") {
		REQUIRE_NOTHROW(transcriber.appendMonoSamples(nullptr, 0));
	}

	SECTION("Append large audio chunk") {
		std::vector<float> audioData(160000, 0.0f);
		REQUIRE_NOTHROW(transcriber.appendMonoSamples(audioData));
	}
}

TEST_CASE("Live Speech Transcriber snapshot", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechTranscriber transcriber;

	SECTION("Get snapshot when not started") {
		auto snapshot = transcriber.getSnapshot();
		REQUIRE_FALSE(snapshot.capturing);
		REQUIRE_FALSE(snapshot.busy);
		REQUIRE(snapshot.bufferedSeconds >= 0.0);
	}

	SECTION("Snapshot reflects capturing state") {
		transcriber.beginCapture();
		auto snapshot = transcriber.getSnapshot();
		REQUIRE(snapshot.capturing);
		transcriber.stopCapture();
	}
}

TEST_CASE("Live Speech Transcriber reset", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechTranscriber transcriber;

	SECTION("Reset clears state") {
		transcriber.beginCapture();
		transcriber.reset();
		REQUIRE_FALSE(transcriber.isCapturing());
	}

	SECTION("Reset when not active") {
		REQUIRE_NOTHROW(transcriber.reset());
		REQUIRE_FALSE(transcriber.isCapturing());
	}

	SECTION("Clear transcript") {
		REQUIRE_NOTHROW(transcriber.clearTranscript());
	}
}

TEST_CASE("Live Speech Transcriber update", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechTranscriber transcriber;

	SECTION("Update can be called safely") {
		REQUIRE_NOTHROW(transcriber.update());
	}

	SECTION("Update while capturing") {
		transcriber.beginCapture();
		REQUIRE_NOTHROW(transcriber.update());
		transcriber.stopCapture();
	}
}

TEST_CASE("Live Speech Transcriber log callback", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechTranscriber transcriber;

	SECTION("Set log callback") {
		std::vector<std::string> logMessages;
		transcriber.setLogCallback([&logMessages](ofLogLevel level, const std::string & msg) {
			logMessages.push_back(msg);
		});

		// Should not crash
		REQUIRE(logMessages.size() >= 0);
	}
}

TEST_CASE("Live Speech Transcriber settings structure", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechSettings settings;

	SECTION("Default values") {
		REQUIRE(settings.sampleRate == 16000);
		REQUIRE(settings.intervalSeconds == 1.25f);
		REQUIRE(settings.windowSeconds == 8.0f);
		REQUIRE(settings.overlapSeconds == 0.75f);
		REQUIRE_FALSE(settings.enabled);
		REQUIRE_FALSE(settings.returnTimestamps);
	}

	SECTION("Task can be set") {
		settings.task = ofxGgmlSpeechTask::Translate;
		REQUIRE(settings.task == ofxGgmlSpeechTask::Translate);
	}

	SECTION("Paths can be set") {
		settings.executable = "/path/to/whisper";
		settings.modelPath = "/path/to/model.bin";
		settings.serverUrl = "http://localhost:8080";

		REQUIRE(settings.executable == "/path/to/whisper");
		REQUIRE(settings.modelPath == "/path/to/model.bin");
		REQUIRE(settings.serverUrl == "http://localhost:8080");
	}
}

TEST_CASE("Live Speech Transcriber snapshot structure", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechSnapshot snapshot;

	SECTION("Default values") {
		REQUIRE_FALSE(snapshot.enabled);
		REQUIRE_FALSE(snapshot.capturing);
		REQUIRE_FALSE(snapshot.busy);
		REQUIRE(snapshot.bufferedSeconds == 0.0);
		REQUIRE(snapshot.transcript.empty());
		REQUIRE(snapshot.status.empty());
		REQUIRE(snapshot.detectedLanguage.empty());
	}

	SECTION("Values can be set") {
		snapshot.enabled = true;
		snapshot.capturing = true;
		snapshot.busy = true;
		snapshot.bufferedSeconds = 5.5;
		snapshot.transcript = "Test transcript";
		snapshot.status = "Processing";
		snapshot.detectedLanguage = "en";

		REQUIRE(snapshot.enabled);
		REQUIRE(snapshot.capturing);
		REQUIRE(snapshot.busy);
		REQUIRE(snapshot.bufferedSeconds == 5.5);
		REQUIRE(snapshot.transcript == "Test transcript");
		REQUIRE(snapshot.status == "Processing");
		REQUIRE(snapshot.detectedLanguage == "en");
	}
}
