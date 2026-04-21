#include "catch2.hpp"
#include "../src/ofxGgml.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

std::filesystem::path makeConvTestDir(const std::string & name) {
	const auto base =
		std::filesystem::temp_directory_path() / "ofxggml_conversation_tests";
	std::filesystem::create_directories(base);
	const auto dir = base / (name + "_" + std::to_string(std::rand()));
	std::filesystem::create_directories(dir);
	return dir;
}

std::string createConvExecutable(const std::string & outputLine) {
	const auto dir = makeConvTestDir("exec");
#ifdef _WIN32
	const auto exe = dir / "fake_conv.bat";
	const auto outputFile = dir / "fake_conv_output.txt";
	{
		std::ofstream output(outputFile, std::ios::binary);
		output << outputLine;
	}
	std::ofstream out(exe);
	out << "@echo off\r\ntype \"%~dp0fake_conv_output.txt\"\r\n";
#else
	const auto exe = dir / "fake_conv.sh";
	std::ofstream out(exe);
	out << "#!/usr/bin/env bash\nset -euo pipefail\necho " << outputLine << "\n";
	out.close();
	chmod(exe.c_str(), 0755);
#endif
	return exe.string();
}

} // namespace

TEST_CASE("Conversation manager starts empty", "[conversation]") {
	ofxGgmlConversationManager mgr;
	REQUIRE(mgr.isEmpty());
	REQUIRE(mgr.turnCount() == 0);
}

TEST_CASE("Conversation manager adds and retrieves turns", "[conversation]") {
	ofxGgmlConversationManager mgr;
	mgr.addSystemTurn("You are a helpful assistant.");
	mgr.addUserTurn("Hello!");
	mgr.addAssistantTurn("Hi there! How can I help?");

	REQUIRE(mgr.turnCount() == 3);
	REQUIRE_FALSE(mgr.isEmpty());

	const auto & turns = mgr.getTurns();
	REQUIRE(turns[0].role == ofxGgmlConversationRole::System);
	REQUIRE(turns[0].content == "You are a helpful assistant.");
	REQUIRE(turns[1].role == ofxGgmlConversationRole::User);
	REQUIRE(turns[1].content == "Hello!");
	REQUIRE(turns[2].role == ofxGgmlConversationRole::Assistant);
	REQUIRE(turns[2].content == "Hi there! How can I help?");
}

TEST_CASE("Conversation manager clears history", "[conversation]") {
	ofxGgmlConversationManager mgr;
	mgr.addUserTurn("First message.");
	mgr.addAssistantTurn("First reply.");
	REQUIRE(mgr.turnCount() == 2);

	mgr.clear();
	REQUIRE(mgr.isEmpty());
	REQUIRE(mgr.turnCount() == 0);
}

TEST_CASE("Conversation manager builds prompt correctly", "[conversation]") {
	ofxGgmlConversationManager mgr;
	mgr.addSystemTurn("You are a helpful assistant.");
	mgr.addUserTurn("What is 2 + 2?");
	mgr.addAssistantTurn("It is 4.");
	mgr.addUserTurn("Thanks!");

	const auto prompt = mgr.buildPrompt();
	REQUIRE(prompt.find("System: You are a helpful assistant.") != std::string::npos);
	REQUIRE(prompt.find("User: What is 2 + 2?") != std::string::npos);
	REQUIRE(prompt.find("Assistant: It is 4.") != std::string::npos);
	REQUIRE(prompt.find("User: Thanks!") != std::string::npos);
	// Should end with "Assistant: " to prime the model response.
	REQUIRE(prompt.rfind("Assistant: ") != std::string::npos);
}

TEST_CASE("Conversation manager prompt can use custom prefixes", "[conversation]") {
	ofxGgmlConversationManager mgr;
	mgr.addUserTurn("Hello");
	mgr.addAssistantTurn("Hi!");

	ofxGgmlConversationPromptSettings settings;
	settings.userPrefix = "Human: ";
	settings.assistantPrefix = "Bot: ";
	settings.addFinalPromptPrefix = false;

	const auto prompt = mgr.buildPrompt(settings);
	REQUIRE(prompt.find("Human: Hello") != std::string::npos);
	REQUIRE(prompt.find("Bot: Hi!") != std::string::npos);
	// Without addFinalPromptPrefix, the prompt should not end with "Bot: ".
	REQUIRE(prompt.rfind("Bot: ") == prompt.find("Bot: "));
}

TEST_CASE("Conversation manager prunes old turns when limit is exceeded", "[conversation]") {
	ofxGgmlConversationPruneSettings pruneSettings;
	pruneSettings.maxTurns = 5;
	pruneSettings.targetTurns = 3;
	pruneSettings.preserveSystemTurns = true;
	pruneSettings.preserveFirstUserTurn = true;

	ofxGgmlConversationManager mgr(pruneSettings);
	mgr.addSystemTurn("System context.");
	mgr.addUserTurn("User turn 1.");           // first user - preserved
	mgr.addAssistantTurn("Assistant reply 1."); // will be pruned
	mgr.addUserTurn("User turn 2.");            // will be pruned
	mgr.addAssistantTurn("Assistant reply 2."); // will be pruned
	// Adding the 6th turn triggers pruning down to targetTurns = 3.
	mgr.addUserTurn("User turn 3.");

	REQUIRE(mgr.turnCount() <= pruneSettings.targetTurns + 1);

	// System and first user turn should be preserved.
	bool foundSystem = false;
	bool foundFirstUser = false;
	for (const auto & turn : mgr.getTurns()) {
		if (turn.role == ofxGgmlConversationRole::System) foundSystem = true;
		if (turn.content == "User turn 1.") foundFirstUser = true;
	}
	REQUIRE(foundSystem);
	REQUIRE(foundFirstUser);
}

TEST_CASE("Conversation manager serializes to JSON and deserializes back", "[conversation]") {
	ofxGgmlConversationManager mgr;
	mgr.addSystemTurn("You are helpful.");
	mgr.addUserTurn("Tell me about AI.");
	mgr.addAssistantTurn("AI is a broad field.");

	const auto json = mgr.toJson();
	REQUIRE(json.find("\"role\":\"system\"") != std::string::npos);
	REQUIRE(json.find("\"role\":\"user\"") != std::string::npos);
	REQUIRE(json.find("\"role\":\"assistant\"") != std::string::npos);
	REQUIRE(json.find("You are helpful.") != std::string::npos);
	REQUIRE(json.find("Tell me about AI.") != std::string::npos);

	ofxGgmlConversationManager restored;
	const bool ok = ofxGgmlConversationManager::fromJson(json, restored);
	REQUIRE(ok);
	REQUIRE(restored.turnCount() == 3);
	REQUIRE(restored.getTurns()[0].role == ofxGgmlConversationRole::System);
	REQUIRE(restored.getTurns()[0].content == "You are helpful.");
	REQUIRE(restored.getTurns()[1].role == ofxGgmlConversationRole::User);
	REQUIRE(restored.getTurns()[1].content == "Tell me about AI.");
	REQUIRE(restored.getTurns()[2].role == ofxGgmlConversationRole::Assistant);
	REQUIRE(restored.getTurns()[2].content == "AI is a broad field.");
}

TEST_CASE("Conversation manager JSON round-trips content with special characters", "[conversation]") {
	ofxGgmlConversationManager mgr;
	mgr.addUserTurn("He said: \"Hello, world!\" and it's great.\nNew line here.");

	const auto json = mgr.toJson();
	ofxGgmlConversationManager restored;
	ofxGgmlConversationManager::fromJson(json, restored);
	REQUIRE(restored.turnCount() == 1);
	REQUIRE(restored.getTurns()[0].content == "He said: \"Hello, world!\" and it's great.\nNew line here.");
}

TEST_CASE("Conversation manager fromJson returns false for invalid input", "[conversation]") {
	ofxGgmlConversationManager empty;
	REQUIRE_FALSE(ofxGgmlConversationManager::fromJson("", empty));
	REQUIRE(empty.isEmpty());

	ofxGgmlConversationManager invalid;
	REQUIRE_FALSE(ofxGgmlConversationManager::fromJson("not json at all", invalid));
	REQUIRE(invalid.isEmpty());
}

TEST_CASE("Conversation manager roleLabel returns correct strings", "[conversation]") {
	REQUIRE(ofxGgmlConversationManager::roleLabel(ofxGgmlConversationRole::System) == "system");
	REQUIRE(ofxGgmlConversationManager::roleLabel(ofxGgmlConversationRole::User) == "user");
	REQUIRE(ofxGgmlConversationManager::roleLabel(ofxGgmlConversationRole::Assistant) == "assistant");
}

TEST_CASE("Conversation manager summarize returns error with empty history", "[conversation]") {
	ofxGgmlConversationManager mgr;
	const auto result = mgr.summarizeHistory("model.gguf");
	REQUIRE_FALSE(result.success);
	REQUIRE_FALSE(result.error.empty());
}

TEST_CASE("Conversation manager summarize runs inference when configured", "[conversation]") {
	const std::string exePath = createConvExecutable("A brief conversation about greetings.");
	const auto modelDir = makeConvTestDir("model");
	const auto modelPath = modelDir / "dummy.gguf";
	{
		std::ofstream out(modelPath);
		out << "dummy-model";
	}

	ofxGgmlConversationManager mgr;
	mgr.getInference().setCompletionExecutable(exePath);
	mgr.addUserTurn("Hello!");
	mgr.addAssistantTurn("Hi there!");

	const auto result = mgr.summarizeHistory(modelPath.string());
	REQUIRE(result.success);
	REQUIRE(result.summary.find("brief conversation") != std::string::npos);
}

TEST_CASE("Easy API exposes conversation manager", "[easy_api][conversation]") {
	ofxGgmlEasy easy;

	REQUIRE(easy.getConversationManager().isEmpty());

	easy.getConversationManager().addUserTurn("What is openFrameworks?");
	easy.getConversationManager().addAssistantTurn("A creative coding toolkit.");

	REQUIRE(easy.getConversationManager().turnCount() == 2);

	const auto json = easy.getConversationManager().toJson();
	REQUIRE(json.find("openFrameworks") != std::string::npos);

	easy.getConversationManager().clear();
	REQUIRE(easy.getConversationManager().isEmpty());
}
