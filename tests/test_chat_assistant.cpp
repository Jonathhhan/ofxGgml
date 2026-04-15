#include "catch2.hpp"
#include "../src/ofxGgml.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path makeChatAssistantTestDir(const std::string & name) {
	const auto base = std::filesystem::temp_directory_path() / "ofxggml_chat_assistant_tests";
	std::filesystem::create_directories(base);
	const auto dir = base / (name + "_" + std::to_string(std::rand()));
	std::filesystem::create_directories(dir);
	return dir;
}

std::string createChatAssistantDummyModel() {
	const auto dir = makeChatAssistantTestDir("model");
	const auto model = dir / "dummy.gguf";
	std::ofstream out(model);
	out << "dummy-model";
	return model.string();
}

std::string createChatAssistantExecutable(const std::string & outputLine) {
	const auto dir = makeChatAssistantTestDir("exec");
#ifdef _WIN32
	const auto exe = dir / "fake_chat_assistant.bat";
	std::ofstream out(exe);
	out << "@echo off\r\necho " << outputLine << "\r\n";
#else
	const auto exe = dir / "fake_chat_assistant.sh";
	std::ofstream out(exe);
	out << "#!/usr/bin/env bash\nset -euo pipefail\necho " << outputLine << "\n";
	::chmod(exe.c_str(), 0755);
#endif
	return exe.string();
}

} // namespace

TEST_CASE("Chat assistant prepares conversation prompts", "[chat_assistant]") {
	ofxGgmlChatAssistant assistant;

	SECTION("response language is injected when selected") {
		ofxGgmlChatAssistantRequest request;
		request.userText = "Explain ggml graphs.";
		request.systemPrompt = "You are a concise technical assistant.";
		request.responseLanguage = "German";

		const auto prepared = assistant.preparePrompt(request);
		REQUIRE(prepared.prompt.find("System:\nYou are a concise technical assistant.") != std::string::npos);
		REQUIRE(prepared.prompt.find("Respond in German") != std::string::npos);
		REQUIRE(prepared.prompt.find("User:\nExplain ggml graphs.") != std::string::npos);
		REQUIRE(prepared.label == "Explain ggml graphs.");
	}

	SECTION("default response languages include Auto") {
		const auto languages = ofxGgmlChatAssistant::defaultResponseLanguages();
		REQUIRE(languages.size() >= 5);
		REQUIRE(languages.front().name == "Auto");
	}
}

TEST_CASE("Chat assistant run returns prepared prompt and output", "[chat_assistant]") {
	const std::string modelPath = createChatAssistantDummyModel();
	const std::string exePath = createChatAssistantExecutable("chat-assistant-ok");

	ofxGgmlChatAssistant assistant;
	assistant.setCompletionExecutable(exePath);

	ofxGgmlChatAssistantRequest request;
	request.userText = "Summarize the current backend status.";
	request.responseLanguage = "English";

	const auto result = assistant.run(modelPath, request);
	REQUIRE(result.inference.success);
	REQUIRE(result.inference.text.find("chat-assistant-ok") != std::string::npos);
	REQUIRE(result.prepared.prompt.find("Respond in English") != std::string::npos);
	REQUIRE(result.prepared.prompt.find("Assistant:") != std::string::npos);
}
