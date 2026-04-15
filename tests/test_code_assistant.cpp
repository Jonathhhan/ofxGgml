#include "catch2.hpp"
#include "../src/ofxGgml.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

std::filesystem::path makeAssistantTestDir(const std::string & name) {
	const auto base =
		std::filesystem::temp_directory_path() / "ofxggml_code_assistant_tests";
	std::filesystem::create_directories(base);
	const auto dir = base / (name + "_" + std::to_string(std::rand()));
	std::filesystem::create_directories(dir);
	return dir;
}

std::string createAssistantDummyModel() {
	const auto dir = makeAssistantTestDir("model");
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

std::string createAssistantExecutable(const std::vector<std::string> & lines) {
	const auto dir = makeAssistantTestDir("exec");
#ifdef _WIN32
	const auto exe = dir / "fake_assistant.bat";
	std::ofstream out(exe);
	out << "@echo off\r\n";
	for (const auto & line : lines) {
		out << "echo " << escapeBatchEcho(line) << "\r\n";
	}
#else
	const auto exe = dir / "fake_assistant.sh";
	std::ofstream out(exe);
	out << "#!/usr/bin/env bash\nset -euo pipefail\n";
	for (const auto & line : lines) {
		out << "printf '%s\\n' " << std::quoted(line) << "\n";
	}
	::chmod(exe.c_str(), 0755);
#endif
	return exe.string();
}

} // namespace

TEST_CASE("Code assistant prompt preparation includes coding and symbol context", "[code_assistant]") {
	const auto sourceDir = makeAssistantTestDir("source");
	{
		std::ofstream out(sourceDir / "main.cpp");
		out << "#include \"helper.h\"\n";
		out << "class App {\npublic:\n    int run() const { return add(1, 2); }\n};\n";
	}
	{
		std::ofstream out(sourceDir / "helper.h");
		out << "#pragma once\nint add(int a, int b);\n";
	}
	{
		std::ofstream out(sourceDir / "helper.cpp");
		out << "#include \"helper.h\"\nint add(int a, int b) { return a + b; }\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlProjectMemory memory;
	REQUIRE(memory.addInteraction(
		"Add logging",
		"Use structured logging instead of printf."));

	ofxGgmlCodeAssistant assistant;
	auto languages = ofxGgmlCodeAssistant::defaultLanguagePresets();
	REQUIRE_FALSE(languages.empty());

	ofxGgmlCodeAssistantRequest request;
	request.action = ofxGgmlCodeAssistantAction::Refactor;
	request.language = languages.front();
	request.userInput = "Refactor the add function and App class.";
	request.requestStructuredResult = true;

	ofxGgmlCodeAssistantContext context;
	context.scriptSource = &scriptSource;
	context.projectMemory = &memory;
	context.focusedFileIndex = 0;
	context.maxSymbols = 4;

	const auto prepared = assistant.preparePrompt(request, context);
	REQUIRE(prepared.includedRepoContext);
	REQUIRE(prepared.includedFocusedFile);
	REQUIRE(prepared.includedSymbolContext);
	REQUIRE(prepared.requestsStructuredResult);
	REQUIRE_FALSE(prepared.retrievedSymbols.empty());
	REQUIRE(prepared.prompt.find("Project memory from previous coding requests:") != std::string::npos);
	REQUIRE(prepared.prompt.find("Available files in this folder:") != std::string::npos);
	REQUIRE(prepared.prompt.find("Focused file:") != std::string::npos);
	REQUIRE(prepared.prompt.find("Relevant symbols for this request:") != std::string::npos);
	REQUIRE(prepared.prompt.find("Return a structured plan using one item per line") != std::string::npos);
}

TEST_CASE("Code assistant parser and run path expose structured task results", "[code_assistant]") {
	SECTION("Structured parser understands files, patches, and commands") {
		const std::string text =
			"GOAL: Stabilize the helper implementation\n"
			"APPROACH: Replace the old greeting and verify the file\n"
			"STEP: Update the output string\n"
			"FILE: src/main.txt | user-facing text | greet\n"
			"PATCH: replace | src/main.txt | update greeting\n"
			"SEARCH: hello\n"
			"REPLACE: ready\n"
			"COMMAND: test | . | verify-tool | --fast\n"
			"EXPECT: output contains ready\n"
			"RETRY: true\n"
			"RISK: callers may rely on the old text\n"
			"QUESTION: should we update docs too?\n";

		const auto structured = ofxGgmlCodeAssistant::parseStructuredResult(text);
		REQUIRE(structured.detectedStructuredOutput);
		REQUIRE(structured.goalSummary == "Stabilize the helper implementation");
		REQUIRE(structured.filesToTouch.size() == 1);
		REQUIRE(structured.patchOperations.size() == 1);
		REQUIRE(structured.patchOperations.front().kind ==
			ofxGgmlCodeAssistantPatchKind::ReplaceTextOp);
		REQUIRE(structured.patchOperations.front().searchText == "hello");
		REQUIRE(structured.verificationCommands.size() == 1);
		REQUIRE(structured.verificationCommands.front().retryOnFailure);
		REQUIRE(structured.risks.size() == 1);
		REQUIRE(structured.questions.size() == 1);
	}

	SECTION("Run returns structured metadata alongside inference output") {
		const std::string modelPath = createAssistantDummyModel();
		const std::string exePath = createAssistantExecutable({
			"GOAL: Normalize paths in the workspace",
			"APPROACH: Write a helper and suggest a build command",
			"STEP: Create the helper file",
			"FILE: src/path_helper.cpp | new utility implementation | normalizePath",
			"PATCH: write | src/path_helper.cpp | add helper file",
			"CONTENT: std::string normalizePath() {\\n    return \\\"ok\\\";\\n}",
			"COMMAND: build | . | runner | --check",
			"EXPECT: build succeeds",
			"RETRY: false"
		});

		ofxGgmlCodeAssistant assistant;
		assistant.setCompletionExecutable(exePath);

		ofxGgmlCodeAssistantRequest request;
		request.action = ofxGgmlCodeAssistantAction::Generate;
		request.userInput = "Write a helper to normalize paths.";
		request.language = ofxGgmlCodeAssistant::defaultLanguagePresets().front();
		request.requestStructuredResult = true;

		const auto result = assistant.run(modelPath, request);
		REQUIRE(result.inference.success);
		REQUIRE(result.prepared.requestsStructuredResult);
		REQUIRE(result.structured.detectedStructuredOutput);
		REQUIRE(result.structured.patchOperations.size() == 1);
		REQUIRE(result.structured.patchOperations.front().content.find("normalizePath") != std::string::npos);
		REQUIRE(result.structured.verificationCommands.size() == 1);
		REQUIRE(result.structured.verificationCommands.front().executable == "runner");
		REQUIRE(result.prepared.prompt.find("Generate high-quality code and short explanation") != std::string::npos);
	}
}
