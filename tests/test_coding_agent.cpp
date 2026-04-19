#include "catch2.hpp"
#include "../src/ofxGgml.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

std::filesystem::path makeCodingAgentTestDir(const std::string & name) {
	const auto base =
		std::filesystem::temp_directory_path() / "ofxggml_coding_agent_tests";
	std::filesystem::create_directories(base);
	const auto dir = base / (name + "_" + std::to_string(std::rand()));
	std::filesystem::create_directories(dir);
	return dir;
}

std::string createCodingAgentDummyModel() {
	const auto dir = makeCodingAgentTestDir("model");
	const auto model = dir / "dummy.gguf";
	std::ofstream out(model);
	out << "dummy-model";
	return model.string();
}

std::string escapeBatchEcho(const std::string & line) {
	std::string escaped;
	for (char c : line) {
		switch (c) {
		case '^':
		case '&':
		case '|':
		case '<':
		case '>':
			escaped.push_back('^');
			escaped.push_back(c);
			break;
		case '%':
			escaped += "%%";
			break;
		default:
			escaped.push_back(c);
			break;
		}
	}
	return escaped;
}

#ifndef _WIN32
std::string shellQuote(const std::string & line) {
	std::string quoted;
	quoted.push_back('\'');
	for (char c : line) {
		if (c == '\'') {
			quoted += "'\"'\"'";
		} else {
			quoted.push_back(c);
		}
	}
	quoted.push_back('\'');
	return quoted;
}
#endif

std::string createCodingAgentExecutable(const std::vector<std::string> & lines) {
	const auto dir = makeCodingAgentTestDir("exec");
#ifdef _WIN32
	const auto exe = dir / "fake_coding_agent.bat";
	std::ofstream out(exe);
	out << "@echo off\r\n";
	for (const auto & line : lines) {
		out << "echo " << escapeBatchEcho(line) << "\r\n";
	}
#else
	const auto exe = dir / "fake_coding_agent.sh";
	std::ofstream out(exe);
	out << "#!/usr/bin/env bash\nset -euo pipefail\n";
	for (const auto & line : lines) {
		out << "printf '%s\\n' " << shellQuote(line) << "\n";
	}
	chmod(exe.c_str(), 0755);
#endif
	return exe.string();
}

std::string readFileText(const std::filesystem::path & path) {
	std::ifstream in(path, std::ios::binary);
	return std::string(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("Coding agent applies structured edits and verification", "[coding_agent]") {
	const auto root = makeCodingAgentTestDir("workspace");
	const auto sourcePath = root / "src.cpp";
	{
		std::ofstream out(sourcePath);
		out << "int answer() { return 41; }\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(root.string()));

	ofxGgmlCodingAgent agent;
	agent.setCompletionExecutable(createCodingAgentExecutable({
		"GOAL: Make answer return 42",
		"APPROACH: Update the helper and run one quick verification command",
		"STEP: Replace the old return value",
		"FILE: src.cpp | update implementation | answer",
		"PATCH: replace | src.cpp | return the correct value",
		"SEARCH: return 41;",
		"REPLACE: return 42;",
		"DIFF: --- a/src.cpp\\n+++ b/src.cpp\\n@@ -1,1 +1,1 @@\\n-int answer() { return 41; }\\n+int answer() { return 42; }",
		"COMMAND: verify | . | fake-runner | --check",
		"EXPECT: helper verification passes"
	}));

	ofxGgmlCodingAgentRequest request;
	request.taskLabel = "Fix helper";
	request.assistantRequest.action = ofxGgmlCodeAssistantAction::Edit;
	request.assistantRequest.language =
		ofxGgmlCodeAssistant::defaultLanguagePresets().front();
	request.assistantRequest.userInput =
		"Update answer() so it returns 42 and keep the file otherwise stable.";

	ofxGgmlCodeAssistantContext context;
	context.scriptSource = &scriptSource;

	ofxGgmlCodingAgentSettings settings;
	settings.workspaceSettings.stopOnFirstFailedCommand = true;
	settings.commandRunner =
		[](const ofxGgmlCodeAssistantCommandSuggestion & command) {
			ofxGgmlWorkspaceCommandResult result;
			result.command = command;
			result.success = true;
			result.exitCode = 0;
			result.output = "verification ok";
			return result;
		};

	std::vector<ofxGgmlCodeAssistantEventKind> eventKinds;
	const auto result = agent.run(
		createCodingAgentDummyModel(),
		request,
		context,
		settings,
		nullptr,
		[&eventKinds](const ofxGgmlCodeAssistantEvent & event) {
			eventKinds.push_back(event.kind);
			return true;
		});

	REQUIRE(result.success);
	REQUIRE_FALSE(result.readOnly);
	REQUIRE(result.appliedChanges);
	REQUIRE(result.verificationAttempted);
	REQUIRE(result.verificationResult.success);
	REQUIRE(result.applyResult.touchedFiles.size() == 1);
	REQUIRE_FALSE(result.applyResult.messages.empty());
	REQUIRE(std::find_if(
		result.applyResult.messages.begin(),
		result.applyResult.messages.end(),
		[](const std::string & message) {
			return message.find("Updated file") != std::string::npos ||
				message.find("Updated file via diff") != std::string::npos;
		}) != result.applyResult.messages.end());
	REQUIRE(result.changedFiles.size() == 1);
	REQUIRE(result.changedFiles.front() == "src.cpp");
	REQUIRE(result.effectiveVerificationCommands.size() == 1);
	REQUIRE(result.sessionRevision > 0);
	REQUIRE_FALSE(agent.getSession().recentPrompts.empty());
	REQUIRE(std::find(
		eventKinds.begin(),
		eventKinds.end(),
		ofxGgmlCodeAssistantEventKind::Completed) != eventKinds.end());
}

TEST_CASE("Coding agent plan mode stays read-only", "[coding_agent]") {
	const auto root = makeCodingAgentTestDir("plan_workspace");
	const auto sourcePath = root / "src.cpp";
	{
		std::ofstream out(sourcePath);
		out << "int answer() { return 10; }\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(root.string()));

	ofxGgmlCodingAgent agent;
	agent.setCompletionExecutable(createCodingAgentExecutable({
		"GOAL: Plan a safer answer implementation",
		"APPROACH: Replace the return value after review",
		"FILE: src.cpp | planned edit | answer",
		"PATCH: replace | src.cpp | update return value",
		"SEARCH: return 10;",
		"REPLACE: return 11;",
		"DIFF: --- a/src.cpp\\n+++ b/src.cpp\\n@@ -1,1 +1,1 @@\\n-int answer() { return 10; }\\n+int answer() { return 11; }"
	}));

	ofxGgmlCodingAgentRequest request;
	request.taskLabel = "Plan helper change";
	request.assistantRequest.action = ofxGgmlCodeAssistantAction::Edit;
	request.assistantRequest.language =
		ofxGgmlCodeAssistant::defaultLanguagePresets().front();
	request.assistantRequest.userInput =
		"Plan how to change answer() to return 11 without editing files yet.";

	ofxGgmlCodeAssistantContext context;
	context.scriptSource = &scriptSource;

	ofxGgmlCodingAgentSettings settings;
	settings.mode = ofxGgmlCodingAgentMode::Plan;

	const auto result = agent.run(
		createCodingAgentDummyModel(),
		request,
		context,
		settings);

	REQUIRE(result.success);
	REQUIRE(result.readOnly);
	REQUIRE_FALSE(result.appliedChanges);
	REQUIRE_FALSE(result.verificationAttempted);
	REQUIRE(readFileText(sourcePath).find("return 10;") != std::string::npos);
	REQUIRE(result.changedFiles.size() == 1);
	REQUIRE(result.changedFiles.front() == "src.cpp");
}
