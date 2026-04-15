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

std::filesystem::path makeWorkspaceTestDir(const std::string & name) {
	const auto base =
		std::filesystem::temp_directory_path() / "ofxggml_workspace_tests";
	std::filesystem::create_directories(base);
	const auto dir = base / (name + "_" + std::to_string(std::rand()));
	std::filesystem::create_directories(dir);
	return dir;
}

std::string createWorkspaceDummyModel() {
	const auto dir = makeWorkspaceTestDir("model");
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

std::string createWorkspaceExecutable(const std::vector<std::string> & lines) {
	const auto dir = makeWorkspaceTestDir("exec");
#ifdef _WIN32
	const auto exe = dir / "fake_workspace_assistant.bat";
	std::ofstream out(exe);
	out << "@echo off\r\n";
	for (const auto & line : lines) {
		out << "echo " << escapeBatchEcho(line) << "\r\n";
	}
#else
	const auto exe = dir / "fake_workspace_assistant.sh";
	std::ofstream out(exe);
	out << "#!/usr/bin/env bash\nset -euo pipefail\n";
	for (const auto & line : lines) {
		out << "printf '%s\\n' " << std::quoted(line) << "\n";
	}
	::chmod(exe.c_str(), 0755);
#endif
	return exe.string();
}

std::string readFile(const std::filesystem::path & path) {
	std::ifstream in(path, std::ios::binary);
	return std::string(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("Workspace assistant applies structured patch operations", "[workspace_assistant]") {
	const auto root = makeWorkspaceTestDir("apply");
	std::filesystem::create_directories(root / "src");
	{
		std::ofstream out(root / "src" / "main.txt");
		out << "hello";
	}

	ofxGgmlWorkspaceAssistant assistant;
	std::vector<ofxGgmlCodeAssistantPatchOperation> operations;

	ofxGgmlCodeAssistantPatchOperation replaceOp;
	replaceOp.kind = ofxGgmlCodeAssistantPatchKind::ReplaceTextOp;
	replaceOp.filePath = "src/main.txt";
	replaceOp.summary = "Update greeting";
	replaceOp.searchText = "hello";
	replaceOp.replacementText = "ready";
	operations.push_back(replaceOp);

	ofxGgmlCodeAssistantPatchOperation writeOp;
	writeOp.kind = ofxGgmlCodeAssistantPatchKind::WriteFile;
	writeOp.filePath = "src/new.txt";
	writeOp.summary = "Create helper";
	writeOp.content = "helper";
	operations.push_back(writeOp);

	const auto applyResult = assistant.applyPatchOperations(
		operations,
		root.string(),
		{},
		false);
	REQUIRE(applyResult.success);
	REQUIRE(readFile(root / "src" / "main.txt") == "ready");
	REQUIRE(readFile(root / "src" / "new.txt") == "helper");
	REQUIRE(applyResult.touchedFiles.size() == 2);
	REQUIRE(applyResult.unifiedDiffPreview.find("+++ b/src/new.txt") != std::string::npos);
}

TEST_CASE("Workspace assistant enforces allowed file edits", "[workspace_assistant]") {
	const auto root = makeWorkspaceTestDir("allowlist");
	std::filesystem::create_directories(root / "src");

	ofxGgmlWorkspaceAssistant assistant;
	std::vector<ofxGgmlCodeAssistantPatchOperation> operations;

	ofxGgmlCodeAssistantPatchOperation writeOp;
	writeOp.kind = ofxGgmlCodeAssistantPatchKind::WriteFile;
	writeOp.filePath = "src/blocked.txt";
	writeOp.summary = "Create blocked file";
	writeOp.content = "blocked";
	operations.push_back(writeOp);

	const auto applyResult = assistant.applyPatchOperations(
		operations,
		root.string(),
		{"src/allowed.txt"},
		false);
	REQUIRE_FALSE(applyResult.success);
	REQUIRE(applyResult.messages.front().find("allowed file list") != std::string::npos);
}

TEST_CASE("Workspace assistant verification loop can retry with a new structured plan", "[workspace_assistant]") {
	const auto root = makeWorkspaceTestDir("retry");
	std::filesystem::create_directories(root / "src");
	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(root.string()));

	const std::string modelPath = createWorkspaceDummyModel();
	const std::string exePath = createWorkspaceExecutable({
		"GOAL: Create the app file",
		"APPROACH: Start with a placeholder and verify it",
		"PATCH: write | src/app.txt | create file",
		"CONTENT: hello",
		"COMMAND: verify | . | verify-tool | --quick",
		"EXPECT: file contains ready",
		"RETRY: true"
	});

	ofxGgmlWorkspaceAssistant assistant;
	assistant.setCompletionExecutable(exePath);

	ofxGgmlCodeAssistantRequest request;
	request.action = ofxGgmlCodeAssistantAction::Generate;
	request.language = ofxGgmlCodeAssistant::defaultLanguagePresets().front();
	request.userInput = "Create src/app.txt with the final ready state.";

	ofxGgmlCodeAssistantContext context;
	context.scriptSource = &scriptSource;

	ofxGgmlWorkspaceSettings settings;
	settings.workspaceRoot = root.string();
	settings.maxVerificationAttempts = 2;

	auto runner = [root](const ofxGgmlCodeAssistantCommandSuggestion & command) {
		ofxGgmlWorkspaceCommandResult result;
		result.command = command;
		const std::string content = readFile(root / "src" / "app.txt");
		result.output = content;
		result.exitCode = (content.find("ready") != std::string::npos) ? 0 : 1;
		result.success = (result.exitCode == 0);
		return result;
	};

	auto retryProvider = [](const ofxGgmlWorkspaceVerificationResult & verification,
		int attempt) {
		REQUIRE(attempt == 1);
		REQUIRE_FALSE(verification.success);
		ofxGgmlCodeAssistantStructuredResult structured;
		structured.detectedStructuredOutput = true;
		structured.goalSummary = "Fix verification failure";

		ofxGgmlCodeAssistantPatchOperation patch;
		patch.kind = ofxGgmlCodeAssistantPatchKind::ReplaceTextOp;
		patch.filePath = "src/app.txt";
		patch.summary = "Replace placeholder";
		patch.searchText = "hello";
		patch.replacementText = "ready";
		structured.patchOperations.push_back(patch);

		ofxGgmlCodeAssistantCommandSuggestion command;
		command.label = "verify";
		command.workingDirectory = ".";
		command.executable = "verify-tool";
		command.arguments = {"--quick"};
		command.retryOnFailure = false;
		structured.verificationCommands.push_back(command);
		return structured;
	};

	const auto result = assistant.runTask(
		modelPath,
		request,
		context,
		settings,
		{},
		{},
		runner,
		retryProvider);

	REQUIRE(result.success);
	REQUIRE(result.assistantAttempts.size() == 2);
	REQUIRE(result.verificationResult.success);
	REQUIRE(result.verificationResult.attempts == 2);
	REQUIRE(readFile(root / "src" / "app.txt") == "ready");
}
