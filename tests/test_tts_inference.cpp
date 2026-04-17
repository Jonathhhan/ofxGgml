#include "catch2.hpp"
#include "../src/inference/ofxGgmlTtsInference.h"

TEST_CASE("TTS Inference initialization", "[tts_inference]") {
	ofxGgmlTtsInference tts;

	SECTION("Default construction") {
		REQUIRE_NOTHROW(ofxGgmlTtsInference());
	}

	SECTION("No backend by default") {
		REQUIRE(tts.getBackend() == nullptr);
	}
}

TEST_CASE("TTS task labels", "[tts_inference]") {
	SECTION("Synthesize task") {
		const char * label = ofxGgmlTtsInference::taskLabel(ofxGgmlTtsTask::Synthesize);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label).length() > 0);
	}

	SECTION("Clone voice task") {
		const char * label = ofxGgmlTtsInference::taskLabel(ofxGgmlTtsTask::CloneVoice);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label).length() > 0);
	}

	SECTION("Continue speech task") {
		const char * label = ofxGgmlTtsInference::taskLabel(ofxGgmlTtsTask::ContinueSpeech);
		REQUIRE(label != nullptr);
		REQUIRE(std::string(label).length() > 0);
	}
}

TEST_CASE("TTS default model profiles", "[tts_inference]") {
	auto profiles = ofxGgmlTtsInference::defaultProfiles();

	SECTION("Returns at least one profile") {
		REQUIRE(profiles.size() > 0);
	}

	SECTION("Profiles have names") {
		for (const auto & profile : profiles) {
			REQUIRE_FALSE(profile.name.empty());
		}
	}

	SECTION("Profiles have architecture") {
		for (const auto & profile : profiles) {
			REQUIRE_FALSE(profile.architecture.empty());
		}
	}
}

TEST_CASE("TTS bridge backend creation", "[tts_inference]") {
	SECTION("Create generic bridge backend") {
		auto backend = ofxGgmlTtsInference::createTtsBridgeBackend();
		REQUIRE(backend != nullptr);
		REQUIRE_FALSE(backend->backendName().empty());
	}

	SECTION("Create with custom name") {
		auto backend = ofxGgmlTtsInference::createTtsBridgeBackend({}, "CustomTTS");
		REQUIRE(backend != nullptr);
		REQUIRE(backend->backendName() == "CustomTTS");
	}

	SECTION("Create ChatLLM TTS backend") {
		auto backend = ofxGgmlTtsInference::createChatLlmTtsBridgeBackend();
		REQUIRE(backend != nullptr);
		REQUIRE_FALSE(backend->backendName().empty());
	}

	SECTION("Create OuteTTS backend") {
		auto backend = ofxGgmlTtsInference::createOuteTtsBridgeBackend();
		REQUIRE(backend != nullptr);
		REQUIRE_FALSE(backend->backendName().empty());
	}
}

TEST_CASE("TTS bridge backend configuration", "[tts_inference]") {
	auto backend = std::dynamic_pointer_cast<ofxGgmlTtsBridgeBackend>(
		ofxGgmlTtsInference::createTtsBridgeBackend());

	SECTION("Unconfigured by default") {
		REQUIRE_FALSE(backend->isConfigured());
	}

	SECTION("Configured after setting function") {
		backend->setSynthesizeFunction([](const ofxGgmlTtsRequest &) {
			ofxGgmlTtsResult result;
			result.success = true;
			return result;
		});
		REQUIRE(backend->isConfigured());
	}
}

TEST_CASE("TTS backend setting and getting", "[tts_inference]") {
	ofxGgmlTtsInference tts;

	SECTION("Set backend") {
		auto backend = ofxGgmlTtsInference::createTtsBridgeBackend();
		tts.setBackend(backend);
		REQUIRE(tts.getBackend() == backend);
	}

	SECTION("Replace backend") {
		auto backend1 = ofxGgmlTtsInference::createTtsBridgeBackend({}, "Backend1");
		auto backend2 = ofxGgmlTtsInference::createTtsBridgeBackend({}, "Backend2");

		tts.setBackend(backend1);
		REQUIRE(tts.getBackend()->backendName() == "Backend1");

		tts.setBackend(backend2);
		REQUIRE(tts.getBackend()->backendName() == "Backend2");
	}

	SECTION("Set null backend") {
		auto backend = ofxGgmlTtsInference::createTtsBridgeBackend();
		tts.setBackend(backend);
		tts.setBackend(nullptr);
		REQUIRE(tts.getBackend() == nullptr);
	}
}

TEST_CASE("TTS request structure", "[tts_inference]") {
	ofxGgmlTtsRequest request;

	SECTION("Default task is Synthesize") {
		REQUIRE(request.task == ofxGgmlTtsTask::Synthesize);
	}

	SECTION("Default parameters") {
		REQUIRE(request.seed == -1);
		REQUIRE(request.maxTokens == 0);
		REQUIRE(request.temperature == 0.4f);
		REQUIRE(request.repetitionPenalty == 1.1f);
		REQUIRE(request.repetitionRange == 64);
		REQUIRE(request.topK == 40);
		REQUIRE(request.topP == 0.9f);
		REQUIRE(request.minP == 0.05f);
		REQUIRE_FALSE(request.streamAudio);
		REQUIRE(request.normalizeText);
	}

	SECTION("Text can be set") {
		request.text = "Hello world";
		REQUIRE(request.text == "Hello world");
	}

	SECTION("Paths can be set") {
		request.modelPath = "/path/to/model";
		request.outputPath = "/path/to/output";
		REQUIRE(request.modelPath == "/path/to/model");
		REQUIRE(request.outputPath == "/path/to/output");
	}
}

TEST_CASE("TTS result structure", "[tts_inference]") {
	ofxGgmlTtsResult result;

	SECTION("Default state is failure") {
		REQUIRE_FALSE(result.success);
		REQUIRE(result.elapsedMs == 0.0f);
		REQUIRE(result.audioFiles.empty());
		REQUIRE(result.metadata.empty());
	}

	SECTION("Success can be set") {
		result.success = true;
		REQUIRE(result.success);
	}

	SECTION("Error message can be set") {
		result.error = "Test error";
		REQUIRE(result.error == "Test error");
	}

	SECTION("Backend name can be set") {
		result.backendName = "TestBackend";
		REQUIRE(result.backendName == "TestBackend");
	}
}

TEST_CASE("TTS audio artifact structure", "[tts_inference]") {
	ofxGgmlTtsAudioArtifact artifact;

	SECTION("Default values") {
		REQUIRE(artifact.path.empty());
		REQUIRE(artifact.sampleRate == 0);
		REQUIRE(artifact.channels == 0);
		REQUIRE(artifact.durationSeconds == 0.0f);
	}

	SECTION("Values can be set") {
		artifact.path = "/tmp/audio.wav";
		artifact.sampleRate = 22050;
		artifact.channels = 1;
		artifact.durationSeconds = 3.5f;

		REQUIRE(artifact.path == "/tmp/audio.wav");
		REQUIRE(artifact.sampleRate == 22050);
		REQUIRE(artifact.channels == 1);
		REQUIRE(artifact.durationSeconds == 3.5f);
	}
}

TEST_CASE("TTS model profile structure", "[tts_inference]") {
	ofxGgmlTtsModelProfile profile;

	SECTION("Default values") {
		REQUIRE(profile.name.empty());
		REQUIRE(profile.supportsVoiceCloning);
		REQUIRE_FALSE(profile.supportsStreaming);
		REQUIRE_FALSE(profile.requiresSpeakerProfile);
	}

	SECTION("Values can be set") {
		profile.name = "TestModel";
		profile.architecture = "Transformer";
		profile.supportsVoiceCloning = false;
		profile.supportsStreaming = true;

		REQUIRE(profile.name == "TestModel");
		REQUIRE(profile.architecture == "Transformer");
		REQUIRE_FALSE(profile.supportsVoiceCloning);
		REQUIRE(profile.supportsStreaming);
	}
}

TEST_CASE("TTS synthesize with mock backend", "[tts_inference]") {
	ofxGgmlTtsInference tts;

	SECTION("Synthesize with no backend returns error") {
		ofxGgmlTtsRequest request;
		request.text = "Hello";
		auto result = tts.synthesize(request);
		REQUIRE_FALSE(result.success);
	}

	SECTION("Synthesize with configured backend") {
		auto backend = std::dynamic_pointer_cast<ofxGgmlTtsBridgeBackend>(
			ofxGgmlTtsInference::createTtsBridgeBackend());

		backend->setSynthesizeFunction([](const ofxGgmlTtsRequest & req) {
			ofxGgmlTtsResult res;
			res.success = true;
			res.backendName = "MockBackend";
			res.elapsedMs = 123.45f;

			ofxGgmlTtsAudioArtifact artifact;
			artifact.path = "/tmp/output.wav";
			artifact.sampleRate = 22050;
			artifact.channels = 1;
			artifact.durationSeconds = 2.5f;
			res.audioFiles.push_back(artifact);

			return res;
		});

		tts.setBackend(backend);

		ofxGgmlTtsRequest request;
		request.text = "Test synthesis";
		auto result = tts.synthesize(request);

		REQUIRE(result.success);
		REQUIRE(result.backendName == "MockBackend");
		REQUIRE(result.elapsedMs == 123.45f);
		REQUIRE(result.audioFiles.size() == 1);
		REQUIRE(result.audioFiles[0].path == "/tmp/output.wav");
		REQUIRE(result.audioFiles[0].sampleRate == 22050);
	}

	SECTION("Synthesize propagates backend errors") {
		auto backend = std::dynamic_pointer_cast<ofxGgmlTtsBridgeBackend>(
			ofxGgmlTtsInference::createTtsBridgeBackend());

		backend->setSynthesizeFunction([](const ofxGgmlTtsRequest &) {
			ofxGgmlTtsResult res;
			res.success = false;
			res.error = "Mock error";
			return res;
		});

		tts.setBackend(backend);

		ofxGgmlTtsRequest request;
		request.text = "Test";
		auto result = tts.synthesize(request);

		REQUIRE_FALSE(result.success);
		REQUIRE(result.error == "Mock error");
	}
}

TEST_CASE("TTS metadata handling", "[tts_inference]") {
	ofxGgmlTtsResult result;

	SECTION("Add metadata") {
		result.metadata.push_back({"key1", "value1"});
		result.metadata.push_back({"key2", "value2"});

		REQUIRE(result.metadata.size() == 2);
		REQUIRE(result.metadata[0].first == "key1");
		REQUIRE(result.metadata[0].second == "value1");
		REQUIRE(result.metadata[1].first == "key2");
		REQUIRE(result.metadata[1].second == "value2");
	}
}
