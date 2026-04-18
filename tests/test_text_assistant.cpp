#include "catch2.hpp"
#include "../src/ofxGgml.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

std::filesystem::path makeTextAssistantTestDir(const std::string & name) {
	const auto base = std::filesystem::temp_directory_path() / "ofxggml_text_assistant_tests";
	std::filesystem::create_directories(base);
	const auto dir = base / (name + "_" + std::to_string(std::rand()));
	std::filesystem::create_directories(dir);
	return dir;
}

std::string createTextAssistantDummyModel() {
	const auto dir = makeTextAssistantTestDir("model");
	const auto model = dir / "dummy.gguf";
	std::ofstream out(model);
	out << "dummy-model";
	return model.string();
}

std::string createTextAssistantExecutable(const std::string & outputLine) {
	const auto dir = makeTextAssistantTestDir("exec");
#ifdef _WIN32
	const auto exe = dir / "fake_text_assistant.bat";
	std::ofstream out(exe);
	out << "@echo off\r\necho " << outputLine << "\r\n";
#else
	const auto exe = dir / "fake_text_assistant.sh";
	std::ofstream out(exe);
	out << "#!/usr/bin/env bash\nset -euo pipefail\necho " << outputLine << "\n";
	out.close();
	chmod(exe.c_str(), 0755);
#endif
	return exe.string();
}

} // namespace

TEST_CASE("Text assistant prepares task-specific prompts", "[text_assistant]") {
	ofxGgmlTextAssistant assistant;

	SECTION("translation prompt includes source and target languages") {
		ofxGgmlTextAssistantRequest request;
		request.task = ofxGgmlTextTask::Translate;
		request.inputText = "Guten Morgen";
		request.sourceLanguage = "German";
		request.targetLanguage = "English";

		const auto prepared = assistant.preparePrompt(request);
		REQUIRE(prepared.prompt.find("Translate the following text from German to English.") != std::string::npos);
		REQUIRE(prepared.prompt.find("Return only the translated text with no explanations, notes, or labels.") != std::string::npos);
		REQUIRE(prepared.prompt.find("Translation:") != std::string::npos);
	}

	SECTION("custom prompt preserves explicit system prompt") {
		ofxGgmlTextAssistantRequest request;
		request.task = ofxGgmlTextTask::Custom;
		request.inputText = "Write a short release note.";
		request.systemPrompt = "You are a concise release manager.";

		const auto prepared = assistant.preparePrompt(request);
		REQUIRE(prepared.prompt.find("System:\nYou are a concise release manager.") != std::string::npos);
		REQUIRE(prepared.prompt.find("User:\nWrite a short release note.") != std::string::npos);
	}

	SECTION("default language list is available") {
		const auto languages = ofxGgmlTextAssistant::defaultTranslateLanguages();
		REQUIRE(languages.size() >= 10);
		REQUIRE(languages.front().name == "Auto detect");
		REQUIRE(languages[1].name == "English");
	}
}

TEST_CASE("Text assistant run returns prompt metadata and inference output", "[text_assistant]") {
	const std::string modelPath = createTextAssistantDummyModel();
	const std::string exePath = createTextAssistantExecutable("text-assistant-ok");

	ofxGgmlTextAssistant assistant;
	assistant.setCompletionExecutable(exePath);

	ofxGgmlTextAssistantRequest request;
	request.task = ofxGgmlTextTask::Summarize;
	request.inputText = "This is a long paragraph that should be summarized.";

	const auto result = assistant.run(modelPath, request);
	REQUIRE(result.inference.success);
	REQUIRE(result.inference.text.find("text-assistant-ok") != std::string::npos);
	REQUIRE(result.prepared.prompt.find("Summarize this text concisely with key points:") != std::string::npos);
	REQUIRE(result.prepared.label == "Summarize text.");
}
