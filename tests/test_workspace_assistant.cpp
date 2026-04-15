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

TEST_CASE("Workspace assistant validates, applies, and rolls back transactions", "[workspace_assistant]") {
	const auto root = makeWorkspaceTestDir("transaction");
	std::filesystem::create_directories(root / "src");
	{
		std::ofstream out(root / "src" / "main.txt");
		out << "before";
	}

	ofxGgmlWorkspaceAssistant assistant;
	std::vector<ofxGgmlCodeAssistantPatchOperation> operations;
	ofxGgmlCodeAssistantPatchOperation replaceOp;
	replaceOp.kind = ofxGgmlCodeAssistantPatchKind::ReplaceTextOp;
	replaceOp.filePath = "src/main.txt";
	replaceOp.summary = "Swap content";
	replaceOp.searchText = "before";
	replaceOp.replacementText = "after";
	operations.push_back(replaceOp);

	const auto validation = assistant.validatePatchOperations(
		operations,
		root.string(),
		{"src/main.txt"});
	REQUIRE(validation.success);
	REQUIRE(validation.validatedFiles.size() == 1);

	auto transaction = assistant.beginTransaction(
		operations,
		root.string(),
		{"src/main.txt"});
	REQUIRE(transaction.validationResult.success);
	REQUIRE(transaction.backups.size() == 1);

	const auto applyResult = assistant.applyTransaction(transaction, false);
	REQUIRE(applyResult.success);
	REQUIRE(readFile(root / "src" / "main.txt") == "after");

	std::vector<std::string> rollbackMessages;
	REQUIRE(assistant.rollbackTransaction(transaction, &rollbackMessages));
	REQUIRE(readFile(root / "src" / "main.txt") == "before");
	REQUIRE_FALSE(rollbackMessages.empty());
}

TEST_CASE("Workspace assistant validates and applies unified diffs with drift-aware matching", "[workspace_assistant]") {
	const auto root = makeWorkspaceTestDir("unified_diff");
	std::filesystem::create_directories(root / "src");
	{
		std::ofstream out(root / "src" / "main.txt");
		out << "banner\nheader\nbefore\nfooter\n";
	}

	const std::string diff =
		"--- a/src/main.txt\n"
		"+++ b/src/main.txt\n"
		"@@ -1,2 +1,2 @@\n"
		" header\n"
		"-before\n"
		"+after\n";

	ofxGgmlWorkspaceAssistant assistant;
	const auto validation = assistant.validateUnifiedDiff(
		diff,
		root.string(),
		{"src/main.txt"});
	REQUIRE(validation.success);
	REQUIRE(validation.validatedFiles.size() == 1);

	auto transaction = assistant.beginUnifiedDiffTransaction(
		diff,
		root.string(),
		{"src/main.txt"});
	REQUIRE(transaction.validationResult.success);
	REQUIRE(transaction.usesUnifiedDiff);
	REQUIRE(transaction.parsedDiffFiles.size() == 1);

	const auto applyResult = assistant.applyTransaction(transaction, false);
	REQUIRE(applyResult.success);
	REQUIRE(readFile(root / "src" / "main.txt").find("after") != std::string::npos);

	std::vector<std::string> rollbackMessages;
	REQUIRE(assistant.rollbackTransaction(transaction, &rollbackMessages));
	REQUIRE(readFile(root / "src" / "main.txt").find("before") != std::string::npos);
}

TEST_CASE("Workspace assistant suggests verification commands from changed files", "[workspace_assistant]") {
	const auto root = makeWorkspaceTestDir("suggest");
	std::filesystem::create_directories(root / "tests" / "build" / "Release");
	{
		std::ofstream out(root / "tests" / "build" / "Release" / "ofxGgml-tests.exe");
		out << "stub";
	}

	ofxGgmlWorkspaceAssistant assistant;
	const auto commands = assistant.suggestVerificationCommands(
		{"src/assistants/ofxGgmlCodeAssistant.cpp"},
		root.string());
	REQUIRE(commands.size() >= 1);
	REQUIRE(commands.front().label == "build-tests");
	const bool hasExpectedAssistantOutcome =
		commands.back().expectedOutcome.find("assistant tests") != std::string::npos ||
		commands.back().expectedOutcome.find("full addon test suite") != std::string::npos;
	REQUIRE(hasExpectedAssistantOutcome);
}

TEST_CASE("Script source parses GitHub URLs and detects Visual Studio workspaces", "[workspace_assistant]") {
	ofxGgmlScriptSource githubSource;
	REQUIRE(githubSource.setGitHubRepoFromInput(
		"https://github.com/Jonathhhan/ofxGgml/tree/main",
		""));
	REQUIRE(githubSource.getGitHubOwnerRepo() == "Jonathhhan/ofxGgml");
	REQUIRE(githubSource.getGitHubBranch() == "main");

	const auto root = makeWorkspaceTestDir("vs_workspace");
	std::filesystem::create_directories(root / "example");
	{
		std::ofstream out(root / "ofxGgml.sln");
		out << "Microsoft Visual Studio Solution File";
	}
	{
		std::ofstream out(root / "example" / "example.vcxproj");
		out << "<Project DefaultTargets=\"Build\"></Project>";
	}
	{
		std::ofstream out(root / "compile_commands.json");
		out << "[]";
	}
	{
		std::ofstream out(root / "addons.make");
		out << "ofxGgml\n";
	}

	ofxGgmlScriptSource localSource;
	REQUIRE(localSource.setVisualStudioWorkspace((root / "ofxGgml.sln").string()));
	const auto info = localSource.getWorkspaceInfo();
	REQUIRE(info.hasVisualStudioSolution);
	REQUIRE(info.visualStudioSolutionPath == "ofxGgml.sln");
	REQUIRE(info.visualStudioProjectPaths.size() == 1);
	REQUIRE(info.visualStudioProjectPaths.front() == "example/example.vcxproj");
	REQUIRE(info.hasCompilationDatabase);
	REQUIRE(info.hasOpenFrameworksProject);
}

TEST_CASE("Workspace assistant suggests Visual Studio verification commands", "[workspace_assistant]") {
	const auto root = makeWorkspaceTestDir("vs_suggest");
	{
		std::ofstream out(root / "workspace.sln");
		out << "Microsoft Visual Studio Solution File";
	}

	ofxGgmlWorkspaceAssistant assistant;
	const auto commands = assistant.suggestVerificationCommands({}, root.string());
#ifdef _WIN32
	REQUIRE_FALSE(commands.empty());
	REQUIRE(commands.front().label == "build-visual-studio-solution");
	REQUIRE(commands.front().executable == "MSBuild.exe");
	REQUIRE(commands.front().arguments.front() == "workspace.sln");
#else
	REQUIRE(commands.empty());
#endif
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

TEST_CASE("Workspace assistant auto-selects verification commands and can roll back on failure", "[workspace_assistant]") {
	const auto root = makeWorkspaceTestDir("autorollback");
	std::filesystem::create_directories(root / "src");
	std::filesystem::create_directories(root / "tests" / "build" / "Release");
	{
		std::ofstream out(root / "tests" / "build" / "Release" / "ofxGgml-tests.exe");
		out << "stub";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(root.string()));

	const std::string modelPath = createWorkspaceDummyModel();
	const std::string exePath = createWorkspaceExecutable({
		"GOAL: Update source",
		"PATCH: write | src/app.txt | create file",
		"CONTENT: broken"
	});

	ofxGgmlWorkspaceAssistant assistant;
	assistant.setCompletionExecutable(exePath);

	ofxGgmlCodeAssistantRequest request;
	request.action = ofxGgmlCodeAssistantAction::FixBuild;
	request.language = ofxGgmlCodeAssistant::defaultLanguagePresets().front();
	request.userInput = "Fix the failing code path.";
	request.buildErrors = "src/app.txt(1): error C2001: broken";
	request.allowedFiles = {"src/app.txt"};

	ofxGgmlCodeAssistantContext context;
	context.scriptSource = &scriptSource;

	ofxGgmlWorkspaceSettings settings;
	settings.workspaceRoot = root.string();
	settings.maxVerificationAttempts = 1;
	settings.rollbackOnVerificationFailure = true;

	auto runner = [](const ofxGgmlCodeAssistantCommandSuggestion & command) {
		ofxGgmlWorkspaceCommandResult result;
		result.command = command;
		result.success = false;
		result.exitCode = 1;
		result.output = "forced failure";
		return result;
	};

	const auto result = assistant.runTask(
		modelPath,
		request,
		context,
		settings,
		{},
		{},
		runner,
		nullptr);

	REQUIRE_FALSE(result.success);
	REQUIRE(result.applyResult.unifiedDiffPreview.find("+++ b/src/app.txt") != std::string::npos);
	REQUIRE_FALSE(std::filesystem::exists(root / "src" / "app.txt"));
}
