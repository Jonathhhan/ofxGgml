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
	chmod(exe.c_str(), 0755);
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
	request.buildErrors = "helper.cpp(2): error C2065: missing_symbol";
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
	REQUIRE(prepared.prompt.find("Likely edit target snippets:") != std::string::npos);
	REQUIRE(prepared.prompt.find("helper.cpp") != std::string::npos);
	REQUIRE(prepared.prompt.find("Build or test failure details:") != std::string::npos);
	REQUIRE(prepared.prompt.find("missing_symbol") != std::string::npos);
	REQUIRE(prepared.prompt.find("return 42") != std::string::npos);
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

	const auto codeMap = assistant.buildCodeMap(context);
	REQUIRE(codeMap.totalFiles >= 4);
	REQUIRE(codeMap.totalSymbols >= 3);
	REQUIRE_FALSE(codeMap.entries.empty());
	REQUIRE(codeMap.entries.front().role.size() > 0);
}

TEST_CASE("Code assistant prompt preparation includes repository instructions and specialized action guidance", "[code_assistant]") {
	const auto sourceDir = makeAssistantTestDir("instructions");
	std::filesystem::create_directories(sourceDir / ".github" / "instructions");
	{
		std::ofstream out(sourceDir / "main.cpp");
		out << "int main() { return 0; }\n";
	}
	{
		std::ofstream out(sourceDir / ".github" / "copilot-instructions.md");
		out << "Use concise, reviewer-ready language.\n";
	}
	{
		std::ofstream out(sourceDir / ".github" / "instructions" / "ui.instructions.md");
		out << "---\n";
		out << "applyTo: main.cpp\n";
		out << "---\n";
		out << "Prefer practical UI wording over generic cleanup advice.\n";
	}
	{
		std::ofstream out(sourceDir / "AGENTS.md");
		out << "Keep generated patches small and safe by default.\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	int focusedFileIndex = -1;
	const auto files = scriptSource.getFiles();
	for (int i = 0; i < static_cast<int>(files.size()); ++i) {
		if (!files[static_cast<size_t>(i)].isDirectory &&
			files[static_cast<size_t>(i)].name == "main.cpp") {
			focusedFileIndex = i;
			break;
		}
	}
	REQUIRE(focusedFileIndex >= 0);

	ofxGgmlCodeAssistant assistant;
	auto languages = ofxGgmlCodeAssistant::defaultLanguagePresets();
	REQUIRE_FALSE(languages.empty());

	ofxGgmlCodeAssistantContext context;
	context.scriptSource = &scriptSource;
	context.focusedFileIndex = focusedFileIndex;

	ofxGgmlCodeAssistantRequest nextEditRequest;
	nextEditRequest.action = ofxGgmlCodeAssistantAction::NextEdit;
	nextEditRequest.language = languages.front();
	nextEditRequest.userInput = "Polish the startup flow.";
	nextEditRequest.requestStructuredResult = true;
	nextEditRequest.requestUnifiedDiff = true;

	const auto nextEditPrepared = assistant.preparePrompt(nextEditRequest, context);
	REQUIRE(nextEditPrepared.prompt.find("Repository instructions:") != std::string::npos);
	REQUIRE(nextEditPrepared.prompt.find("Use concise, reviewer-ready language.") != std::string::npos);
	REQUIRE(nextEditPrepared.prompt.find("Prefer practical UI wording over generic cleanup advice.") != std::string::npos);
	REQUIRE(nextEditPrepared.prompt.find("Keep generated patches small and safe by default.") != std::string::npos);
	REQUIRE(nextEditPrepared.prompt.find("Keep the answer tightly scoped to one likely next edit.") != std::string::npos);

	ofxGgmlCodeAssistantRequest summaryRequest;
	summaryRequest.action = ofxGgmlCodeAssistantAction::SummarizeChanges;
	summaryRequest.language = languages.front();
	summaryRequest.userInput =
		"Diff:\n--- a/src/main.cpp\n+++ b/src/main.cpp\n@@\n-int main() { return 0; }\n+int main() { return 1; }\n";

	const auto summaryPrepared = assistant.preparePrompt(summaryRequest, context);
	REQUIRE(summaryPrepared.body.find("Summarize the provided code changes professionally") != std::string::npos);
	REQUIRE(summaryPrepared.prompt.find("Write for a human reviewer.") != std::string::npos);
	REQUIRE(summaryPrepared.requestLabel.find("Summarize local changes.") != std::string::npos);
}

TEST_CASE("Code assistant parser and run path expose structured task results", "[code_assistant]") {
	SECTION("Structured parser understands files, patches, diffs, findings, and commands") {
		const std::string text =
			"GOAL: Stabilize the helper implementation\n"
			"APPROACH: Replace the old greeting and verify the file\n"
			"STEP: Update the output string\n"
			"ACCEPT: the UI shows the new greeting\n"
			"FILE: src/main.txt | user-facing text | greet\n"
			"PATCH: replace | src/main.txt | update greeting\n"
			"SEARCH: hello\n"
			"REPLACE: ready\n"
			"DIFF: --- a/src/main.txt\\n+++ b/src/main.txt\\n@@ replace @@\\n-hello\\n+ready\n"
			"COMMAND: test | . | verify-tool | --fast\n"
			"EXPECT: output contains ready\n"
			"RETRY: true\n"
			"TEST: greeting regression | tests/test_ui.cpp | covers the new greeting | ui-tests | [ui]\n"
			"REVIEWER: correctness\n"
			"FINDING: 1 | 0.95 | src/main.txt | 7 | Greeting is stale\n"
			"DETAIL: The old text leaks into the UI path.\n"
			"FIX: Replace the greeting before rendering.\n"
			"CATEGORY: regression-risk\n"
			"RISK-SCORE: 0.72\n"
			"RISK-LEVEL: high\n"
			"RISK: callers may rely on the old text\n"
			"QUESTION: should we update docs too?\n";

		const auto structured = ofxGgmlCodeAssistant::parseStructuredResult(text);
		REQUIRE(structured.detectedStructuredOutput);
		REQUIRE(structured.goalSummary == "Stabilize the helper implementation");
		REQUIRE(structured.acceptanceCriteria.size() == 1);
		REQUIRE(structured.filesToTouch.size() == 2);
		REQUIRE(structured.filesToTouch[0].filePath == "src/main.txt");
		REQUIRE(structured.filesToTouch[1].filePath == "tests/test_ui.cpp");
		REQUIRE(structured.patchOperations.size() == 1);
		REQUIRE(structured.patchOperations.front().kind ==
			ofxGgmlCodeAssistantPatchKind::ReplaceTextOp);
		REQUIRE(structured.patchOperations.front().searchText == "hello");
		REQUIRE(structured.unifiedDiff.find("--- a/src/main.txt") != std::string::npos);
		REQUIRE(structured.verificationCommands.size() == 1);
		REQUIRE(structured.verificationCommands.front().retryOnFailure);
		REQUIRE(structured.testSuggestions.size() == 1);
		REQUIRE(structured.testSuggestions.front().commandTag == "[ui]");
		REQUIRE(structured.reviewFindings.size() == 1);
		REQUIRE(structured.reviewFindings.front().priority == 1);
		REQUIRE(structured.reviewFindings.front().confidence > 0.9f);
		REQUIRE(structured.reviewFindings.front().category == "regression-risk");
		REQUIRE(structured.reviewFindings.front().reviewerPersona == "correctness");
		REQUIRE(structured.reviewFindings.front().fixSuggestion.find("Replace") != std::string::npos);
		REQUIRE(structured.reviewerSimulations.size() == 1);
		REQUIRE(structured.riskAssessment.score > 0.7f);
		REQUIRE(structured.riskAssessment.level == "high");
		REQUIRE(structured.risks.size() == 1);
		REQUIRE(structured.questions.size() == 1);
	}

	SECTION("Structured parser backfills touched files from patches, findings, and tests") {
		const std::string text =
			"PATCH: replace | src/engine.cpp | tighten branch\n"
			"SEARCH: oldBranch();\n"
			"REPLACE: newBranch();\n"
			"TEST: engine regression | tests/test_engine.cpp | covers the changed branch\n"
			"REVIEWER: safety\n"
			"FINDING: 1 | 0.85 | include/engine.h | 12 | Header contract changed\n"
			"DETAIL: Callers may need updates.\n";

		const auto structured = ofxGgmlCodeAssistant::parseStructuredResult(text);
		REQUIRE(structured.detectedStructuredOutput);
		REQUIRE(structured.filesToTouch.size() == 3);
		REQUIRE(structured.filesToTouch[0].filePath == "src/engine.cpp");
		REQUIRE(structured.filesToTouch[0].reason.find("tighten branch") != std::string::npos);
		REQUIRE(structured.filesToTouch[1].filePath == "include/engine.h");
		REQUIRE(structured.filesToTouch[2].filePath == "tests/test_engine.cpp");
	}

	SECTION("Structured parser backfills touched files from unified diff only") {
		const std::string text =
			"DIFF: --- a/src/cache.cpp\\n+++ b/src/cache.cpp\\n@@ -1,1 +1,1 @@\\n-old\\n+new\\n"
			"QUESTION: verify downstream callers?\n";

		const auto structured = ofxGgmlCodeAssistant::parseStructuredResult(text);
		REQUIRE(structured.detectedStructuredOutput);
		REQUIRE(structured.filesToTouch.size() == 1);
		REQUIRE(structured.filesToTouch.front().filePath == "src/cache.cpp");
		REQUIRE(structured.filesToTouch.front().reason == "unified diff");
	}

	SECTION("Run returns structured metadata alongside inference output") {
		const auto sourceDir = makeAssistantTestDir("run_with_codemap");
		std::filesystem::create_directories(sourceDir / "src");
		{
			std::ofstream out(sourceDir / "src" / "path_helper.cpp");
			out << "std::string normalizePath() { return \"old\"; }\n";
		}
		ofxGgmlScriptSource scriptSource;
		REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

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
		request.specToCodeMode = true;
		request.includeCodeMap = true;
		request.synthesizeTests = true;
		request.simulateReviewers = true;
		request.acceptanceCriteria = {"normalize separators without changing callers"};
		request.constraints = {"do not add dependencies"};
		request.requestStructuredResult = true;
		request.requestUnifiedDiff = true;

		ofxGgmlCodeAssistantContext context;
		context.scriptSource = &scriptSource;

		const auto result = assistant.run(modelPath, request, context);
		REQUIRE(result.inference.success);
		REQUIRE(result.prepared.requestsStructuredResult);
		REQUIRE(result.structured.detectedStructuredOutput);
		REQUIRE(result.structured.patchOperations.size() == 1);
		REQUIRE(result.structured.patchOperations.front().content.find("normalizePath") != std::string::npos);
		REQUIRE(result.structured.unifiedDiff.find("+++ b/src/path_helper.cpp") != std::string::npos);
		REQUIRE(result.structured.verificationCommands.size() == 1);
		REQUIRE(result.structured.verificationCommands.front().executable == "runner");
		REQUIRE_FALSE(result.structured.testSuggestions.empty());
		REQUIRE_FALSE(result.structured.reviewerSimulations.empty());
		REQUIRE(result.structured.riskAssessment.level.size() > 0);
		REQUIRE(result.prepared.includedCodeMap);
		REQUIRE(result.prepared.prompt.find("Generate high-quality code and short explanation") != std::string::npos);
		REQUIRE(result.prepared.prompt.find("Semantic code map:") != std::string::npos);
		REQUIRE(result.prepared.prompt.find("Acceptance criteria:") != std::string::npos);
	}

	SECTION("Spec-to-code mode prepares implementation, tests, and review metadata") {
		const auto sourceDir = makeAssistantTestDir("spec_to_code");
		{
			std::ofstream out(sourceDir / "src.cpp");
			out << "int answer() { return 41; }\n";
		}
		ofxGgmlScriptSource scriptSource;
		REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

		const std::string modelPath = createAssistantDummyModel();
		const std::string exePath = createAssistantExecutable({
			"GOAL: Add a stable answer helper",
			"APPROACH: Update the implementation and add verification",
			"STEP: Write the helper",
			"ACCEPT: answer() returns 42",
			"FILE: src.cpp | update implementation | answer",
			"PATCH: replace | src.cpp | fix return value",
			"SEARCH: return 41;",
			"REPLACE: return 42;",
			"COMMAND: build | . | cmake | --build | tests/build",
			"EXPECT: build passes"
		});

		ofxGgmlCodeAssistant assistant;
		assistant.setCompletionExecutable(exePath);

		ofxGgmlCodeAssistantSpecToCodeRequest request;
		request.language = ofxGgmlCodeAssistant::defaultLanguagePresets().front();
		request.specification = "Implement a stable answer helper.";
		request.acceptanceCriteria = {"answer() returns 42", "existing call sites still compile"};
		request.constraints = {"keep the public API stable"};
		request.allowedFiles = {"src.cpp"};
		request.preservePublicApi = true;

		ofxGgmlCodeAssistantContext context;
		context.scriptSource = &scriptSource;

		const auto result = assistant.runSpecToCode(modelPath, request, context);
		REQUIRE(result.inference.success);
		REQUIRE(result.prepared.includedCodeMap);
		REQUIRE(result.structured.detectedStructuredOutput);
		REQUIRE(result.structured.acceptanceCriteria.size() == 1);
		REQUIRE_FALSE(result.structured.testSuggestions.empty());
		REQUIRE_FALSE(result.structured.reviewerSimulations.empty());
		REQUIRE(result.structured.riskAssessment.level.size() > 0);
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
			"```cpp",
			"return cachedValue;",
			"```"
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
		REQUIRE(prepared.prompt.find("Do not repeat surrounding code") != std::string::npos);

		const auto result = assistant.runInlineCompletion(modelPath, request);
		REQUIRE(result.inference.success);
		REQUIRE(result.completion == "return cachedValue;");
	}

	SECTION("Risk scoring and reviewer simulation react to broad risky changes") {
		ofxGgmlCodeAssistant assistant;
		ofxGgmlCodeAssistantRequest request;
		request.action = ofxGgmlCodeAssistantAction::Refactor;
		request.updateTests = true;
		request.preservePublicApi = false;
		request.forbidNewDependencies = false;

		ofxGgmlCodeAssistantStructuredResult structured;
		structured.detectedStructuredOutput = true;
		structured.verificationCommands.clear();

		ofxGgmlCodeAssistantPatchOperation patchA;
		patchA.kind = ofxGgmlCodeAssistantPatchKind::ReplaceTextOp;
		patchA.filePath = "src/core/api.h";
		patchA.searchText = "old";
		patchA.replacementText = "new";
		structured.patchOperations.push_back(patchA);

		ofxGgmlCodeAssistantPatchOperation patchB;
		patchB.kind = ofxGgmlCodeAssistantPatchKind::DeleteFileOp;
		patchB.filePath = "src/legacy.cpp";
		structured.patchOperations.push_back(patchB);

		const auto tests = assistant.synthesizeTests(request, structured, {});
		REQUIRE_FALSE(tests.empty());

		const auto reviewerPasses = assistant.simulateReviewerPasses(request, structured, {});
		REQUIRE_FALSE(reviewerPasses.empty());

		const auto risk = assistant.assessRisk(request, structured, {});
		REQUIRE(risk.score > 0.6f);
		REQUIRE((risk.level == "high" || risk.level == "critical"));
		REQUIRE_FALSE(risk.reasons.empty());
	}
}
