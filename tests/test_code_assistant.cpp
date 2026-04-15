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
		out << "int runInference();\n";
		out << "class App {\npublic:\n    int run() const { return runInference(); }\n};\n";
	}
	{
		std::ofstream out(sourceDir / "helper.h");
		out << "#pragma once\nint runInference();\n";
	}
	{
		std::ofstream out(sourceDir / "helper.cpp");
		out << "#include \"helper.h\"\nint runInference() { return 42; }\n";
	}
	{
		std::ofstream out(sourceDir / "caller.cpp");
		out << "#include \"helper.h\"\nint useIt() { return runInference(); }\n";
	}
	{
		std::ofstream out(sourceDir / "compile_commands.json");
		out << "[\n";
		out << "  {\"directory\": " << std::quoted(sourceDir.string())
			<< ", \"file\": \"main.cpp\", \"command\": \"cl /c main.cpp\"},\n";
		out << "  {\"directory\": " << std::quoted(sourceDir.string())
			<< ", \"file\": \"helper.cpp\", \"command\": \"cl /c helper.cpp\"},\n";
		out << "  {\"directory\": " << std::quoted(sourceDir.string())
			<< ", \"file\": \"caller.cpp\", \"command\": \"cl /c caller.cpp\"}\n";
		out << "]\n";
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
	request.userInput = "Refactor runInference and App without changing the public API.";
	request.requestStructuredResult = true;
	request.requestUnifiedDiff = true;
	request.allowedFiles = {"main.cpp", "helper.cpp", "helper.h"};
	request.preservePublicApi = true;
	request.updateTests = true;
	request.forbidNewDependencies = true;
	request.symbolQuery.query = "runInference callers";
	request.symbolQuery.targetSymbols = {"runInference"};
	request.symbolQuery.includeCallers = true;

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
	REQUIRE(prepared.requestedUnifiedDiff);
	REQUIRE_FALSE(prepared.retrievedSymbols.empty());
	REQUIRE(prepared.retrievedSymbolContext.includesCallers);
	REQUIRE_FALSE(prepared.retrievedSymbolContext.relatedReferences.empty());
	REQUIRE(prepared.prompt.find("Project memory from previous coding requests:") != std::string::npos);
	REQUIRE(prepared.prompt.find("Available files in this folder:") != std::string::npos);
	REQUIRE(prepared.prompt.find("Focused file:") != std::string::npos);
	REQUIRE(prepared.prompt.find("Relevant symbols for this request:") != std::string::npos);
	REQUIRE(prepared.prompt.find("Allowed files for modifications:") != std::string::npos);
	REQUIRE(prepared.prompt.find("Preserve the existing public API surface.") != std::string::npos);
	REQUIRE(prepared.prompt.find("Prefer including a DIFF: entry") != std::string::npos);
	REQUIRE(prepared.prompt.find("Return a structured plan using one item per line") != std::string::npos);

	const auto semanticIndex = assistant.buildSemanticIndex(context);
	REQUIRE(semanticIndex.symbols.size() >= 3);
	REQUIRE(semanticIndex.hasCompilationDatabase);
	REQUIRE(semanticIndex.backendName.find("compilation_database") != std::string::npos);
	REQUIRE_FALSE(semanticIndex.callers.empty());
	const auto qualifiedSymbol = std::find_if(
		semanticIndex.symbols.begin(),
		semanticIndex.symbols.end(),
		[](const ofxGgmlCodeAssistantSymbol & symbol) {
			return symbol.qualifiedName.find("runInference") != std::string::npos &&
				symbol.isDefinition;
		});
	REQUIRE(qualifiedSymbol != semanticIndex.symbols.end());
	REQUIRE(qualifiedSymbol->semanticBackend.find("compilation_database") != std::string::npos);
	const auto preciseCaller = std::find_if(
		qualifiedSymbol->references.begin(),
		qualifiedSymbol->references.end(),
		[](const ofxGgmlCodeAssistantSymbolReference & reference) {
			return reference.kind == "caller" &&
				!reference.callerSymbol.empty() &&
				!reference.targetSymbol.empty();
		});
	REQUIRE(preciseCaller != qualifiedSymbol->references.end());
}

TEST_CASE("Code assistant parser and run path expose structured task results", "[code_assistant]") {
	SECTION("Structured parser understands files, patches, diffs, findings, and commands") {
		const std::string text =
			"GOAL: Stabilize the helper implementation\n"
			"APPROACH: Replace the old greeting and verify the file\n"
			"STEP: Update the output string\n"
			"FILE: src/main.txt | user-facing text | greet\n"
			"PATCH: replace | src/main.txt | update greeting\n"
			"SEARCH: hello\n"
			"REPLACE: ready\n"
			"DIFF: --- a/src/main.txt\\n+++ b/src/main.txt\\n@@ replace @@\\n-hello\\n+ready\n"
			"COMMAND: test | . | verify-tool | --fast\n"
			"EXPECT: output contains ready\n"
			"RETRY: true\n"
			"FINDING: 1 | 0.95 | src/main.txt | 7 | Greeting is stale\n"
			"DETAIL: The old text leaks into the UI path.\n"
			"FIX: Replace the greeting before rendering.\n"
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
		REQUIRE(structured.unifiedDiff.find("--- a/src/main.txt") != std::string::npos);
		REQUIRE(structured.verificationCommands.size() == 1);
		REQUIRE(structured.verificationCommands.front().retryOnFailure);
		REQUIRE(structured.reviewFindings.size() == 1);
		REQUIRE(structured.reviewFindings.front().priority == 1);
		REQUIRE(structured.reviewFindings.front().confidence > 0.9f);
		REQUIRE(structured.reviewFindings.front().fixSuggestion.find("Replace") != std::string::npos);
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
			"DIFF: --- /dev/null\\n+++ b/src/path_helper.cpp\\n@@ -0,0 +1,3 @@\\n+std::string normalizePath() {\\n+    return \\\"ok\\\";\\n+}",
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
		request.requestUnifiedDiff = true;

		const auto result = assistant.run(modelPath, request);
		REQUIRE(result.inference.success);
		REQUIRE(result.prepared.requestsStructuredResult);
		REQUIRE(result.structured.detectedStructuredOutput);
		REQUIRE(result.structured.patchOperations.size() == 1);
		REQUIRE(result.structured.patchOperations.front().content.find("normalizePath") != std::string::npos);
		REQUIRE(result.structured.unifiedDiff.find("+++ b/src/path_helper.cpp") != std::string::npos);
		REQUIRE(result.structured.verificationCommands.size() == 1);
		REQUIRE(result.structured.verificationCommands.front().executable == "runner");
		REQUIRE(result.prepared.prompt.find("Generate high-quality code and short explanation") != std::string::npos);
	}

	SECTION("Specialized modes inject edit, fix-build, and grounded-doc constraints") {
		ofxGgmlCodeAssistant assistant;
		const auto language = ofxGgmlCodeAssistant::defaultLanguagePresets().front();

		ofxGgmlCodeAssistantRequest editRequest;
		editRequest.action = ofxGgmlCodeAssistantAction::Edit;
		editRequest.language = language;
		editRequest.userInput = "Rename helper usage.";
		editRequest.allowedFiles = {"src/a.cpp", "src/b.cpp", "src/c.cpp"};
		const auto editPrepared = assistant.preparePrompt(editRequest, {});
		REQUIRE(editPrepared.prompt.find("Touch only the allowed files.") != std::string::npos);

		ofxGgmlCodeAssistantRequest fixBuildRequest;
		fixBuildRequest.action = ofxGgmlCodeAssistantAction::FixBuild;
		fixBuildRequest.language = language;
		fixBuildRequest.userInput = "Repair the broken Release build.";
		fixBuildRequest.buildErrors = "main.cpp(42): error C2065: unknown_symbol";
		const auto fixPrepared = assistant.preparePrompt(fixBuildRequest, {});
		REQUIRE(fixPrepared.prompt.find("Build or test failure details:") != std::string::npos);
		REQUIRE(fixPrepared.prompt.find("unknown_symbol") != std::string::npos);

		ofxGgmlCodeAssistantRequest docsRequest;
		docsRequest.action = ofxGgmlCodeAssistantAction::GroundedDocs;
		docsRequest.language = language;
		docsRequest.userInput = "Explain the Vulkan compiler flags.";
		docsRequest.webUrls = {"https://example.com/vulkan-doc"};
		const auto docsPrepared = assistant.preparePrompt(docsRequest, {});
		REQUIRE(docsPrepared.prompt.find("Grounded web/doc sources requested:") != std::string::npos);
		REQUIRE(docsPrepared.prompt.find("https://example.com/vulkan-doc") != std::string::npos);
	}

	SECTION("Build errors are parsed into structured entries") {
		const std::string errors =
			"src/main.cpp(42,9): error C2065: unknown_symbol: undeclared identifier\n"
			"src/helper.cpp:10:3: error: use of undeclared identifier 'x'\n";
		const auto parsed = ofxGgmlCodeAssistant::parseBuildErrors(errors);
		REQUIRE(parsed.size() == 2);
		REQUIRE(parsed.front().filePath.find("main.cpp") != std::string::npos);
		REQUIRE(parsed.front().line == 42);
		REQUIRE(parsed.front().code == "C2065");
		REQUIRE(parsed.back().line == 10);
	}

	SECTION("Inline completion uses cursor-aware prompt building") {
		const std::string modelPath = createAssistantDummyModel();
		const std::string exePath = createAssistantExecutable({
			"return cachedValue;"
		});

		ofxGgmlCodeAssistant assistant;
		assistant.setCompletionExecutable(exePath);

		ofxGgmlCodeAssistantInlineCompletionRequest request;
		request.language = ofxGgmlCodeAssistant::defaultLanguagePresets().front();
		request.filePath = "src/cache.cpp";
		request.prefix = "int load() {\n    ";
		request.suffix = "\n}";
		request.instruction = "Complete the return statement.";
		request.singleLine = true;

		const auto prepared = assistant.prepareInlineCompletion(request);
		REQUIRE(prepared.prompt.find("<PRE>") != std::string::npos);
		REQUIRE(prepared.prompt.find("<SUF>") != std::string::npos);
		REQUIRE(prepared.prompt.find("Complete the return statement.") != std::string::npos);

		const auto result = assistant.runInlineCompletion(modelPath, request);
		REQUIRE(result.inference.success);
		REQUIRE(result.completion.find("cachedValue") != std::string::npos);
	}
}
