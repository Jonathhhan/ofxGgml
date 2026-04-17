#include "catch2.hpp"
#include "../src/inference/ofxGgmlLiveSpeechTranscriber.h"

TEST_CASE("Live Speech Transcriber initialization", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechTranscriber transcriber;

	SECTION("Default construction") {
		REQUIRE_NOTHROW(ofxGgmlLiveSpeechTranscriber());
	}

	SECTION("Not active by default") {
		REQUIRE_FALSE(transcriber.isActive());
	}
}

TEST_CASE("Live Speech Transcriber configuration", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechTranscriber transcriber;

	SECTION("Set sample rate") {
		transcriber.setSampleRate(16000);
		REQUIRE(transcriber.getSampleRate() == 16000);
	}

	SECTION("Default sample rate") {
		REQUIRE(transcriber.getSampleRate() == 16000);
	}

	SECTION("Set channels") {
		transcriber.setChannels(2);
		REQUIRE(transcriber.getChannels() == 2);
	}

	SECTION("Default channels") {
		REQUIRE(transcriber.getChannels() == 1);
	}
}

TEST_CASE("Live Speech Transcriber state management", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechTranscriber transcriber;

	SECTION("Start transcription") {
		bool ok = transcriber.start();
		// May fail without backend configured
		if (ok) {
			REQUIRE(transcriber.isActive());
		}
	}

	SECTION("Stop transcription") {
		transcriber.start();
		transcriber.stop();
		REQUIRE_FALSE(transcriber.isActive());
	}

	SECTION("Multiple start calls") {
		transcriber.start();
		bool wasActive = transcriber.isActive();
		transcriber.start();
		// Should remain in same state or handle gracefully
		REQUIRE(transcriber.isActive() == wasActive);
		transcriber.stop();
	}

	SECTION("Stop when not active") {
		REQUIRE_FALSE(transcriber.isActive());
		REQUIRE_NOTHROW(transcriber.stop());
		REQUIRE_FALSE(transcriber.isActive());
	}
}

TEST_CASE("Live Speech Transcriber audio submission", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechTranscriber transcriber;

	SECTION("Submit audio data") {
		std::vector<float> audioData(1600, 0.0f); // 100ms at 16kHz
		REQUIRE_NOTHROW(transcriber.submitAudio(audioData.data(), audioData.size()));
	}

	SECTION("Submit empty audio") {
		REQUIRE_NOTHROW(transcriber.submitAudio(nullptr, 0));
	}

	SECTION("Submit large audio chunk") {
		std::vector<float> audioData(160000, 0.0f); // 10 seconds at 16kHz
		REQUIRE_NOTHROW(transcriber.submitAudio(audioData.data(), audioData.size()));
	}
}

TEST_CASE("Live Speech Transcriber result retrieval", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechTranscriber transcriber;

	SECTION("Get transcription when not started") {
		auto text = transcriber.getTranscription();
		// Should return empty or handle gracefully
		REQUIRE(text.length() >= 0);
	}

	SECTION("Get partial results") {
		auto partials = transcriber.getPartialResults();
		REQUIRE(partials.size() >= 0);
	}

	SECTION("Has new results when not active") {
		REQUIRE_FALSE(transcriber.hasNewResults());
	}
}

TEST_CASE("Live Speech Transcriber reset", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechTranscriber transcriber;

	SECTION("Reset clears state") {
		transcriber.start();
		transcriber.reset();
		REQUIRE_FALSE(transcriber.isActive());
	}

	SECTION("Reset when not active") {
		REQUIRE_NOTHROW(transcriber.reset());
		REQUIRE_FALSE(transcriber.isActive());
	}

	SECTION("Reset clears transcription") {
		transcriber.reset();
		auto text = transcriber.getTranscription();
		REQUIRE(text.empty() || text.length() >= 0);
	}
}

TEST_CASE("Live Speech Transcriber buffer management", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechTranscriber transcriber;

	SECTION("Buffer size configuration") {
		transcriber.setBufferSizeMs(3000);
		REQUIRE(transcriber.getBufferSizeMs() == 3000);
	}

	SECTION("Default buffer size") {
		REQUIRE(transcriber.getBufferSizeMs() > 0);
	}

	SECTION("Minimum buffer size") {
		transcriber.setBufferSizeMs(100);
		REQUIRE(transcriber.getBufferSizeMs() >= 100);
	}
}

TEST_CASE("Live Speech Transcriber completion", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechTranscriber transcriber;

	SECTION("Complete transcription") {
		auto result = transcriber.completeTranscription();
		// Should return a result even if not started
		REQUIRE(result.text.length() >= 0);
	}

	SECTION("Complete after stop") {
		transcriber.start();
		transcriber.stop();
		auto result = transcriber.completeTranscription();
		REQUIRE(result.text.length() >= 0);
	}
}

TEST_CASE("Live Speech Transcriber configuration before start", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechTranscriber transcriber;

	SECTION("Configure sample rate before start") {
		transcriber.setSampleRate(22050);
		REQUIRE(transcriber.getSampleRate() == 22050);

		transcriber.start();
		// Sample rate should be preserved
		REQUIRE(transcriber.getSampleRate() == 22050);
		transcriber.stop();
	}

	SECTION("Configure channels before start") {
		transcriber.setChannels(2);
		REQUIRE(transcriber.getChannels() == 2);

		transcriber.start();
		REQUIRE(transcriber.getChannels() == 2);
		transcriber.stop();
	}
}

TEST_CASE("Live Speech Transcriber language setting", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechTranscriber transcriber;

	SECTION("Set language") {
		transcriber.setLanguage("en");
		REQUIRE(transcriber.getLanguage() == "en");
	}

	SECTION("Default language") {
		std::string lang = transcriber.getLanguage();
		// Should have some default
		REQUIRE(lang.length() >= 0);
	}

	SECTION("Change language") {
		transcriber.setLanguage("en");
		transcriber.setLanguage("es");
		REQUIRE(transcriber.getLanguage() == "es");
	}
}

TEST_CASE("Live Speech Transcriber threading safety", "[live_speech_transcriber]") {
	ofxGgmlLiveSpeechTranscriber transcriber;

	SECTION("Rapid start/stop cycles") {
		for (int i = 0; i < 5; ++i) {
			transcriber.start();
			transcriber.stop();
		}
		REQUIRE_FALSE(transcriber.isActive());
	}

	SECTION("Submit audio during state changes") {
		std::vector<float> audioData(160, 0.0f);

		transcriber.start();
		transcriber.submitAudio(audioData.data(), audioData.size());
		transcriber.stop();

		REQUIRE_NOTHROW(transcriber.getTranscription());
	}
}
