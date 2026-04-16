#include "catch2.hpp"
#include "../src/ofxGgml.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifndef _WIN32
	#include <sys/stat.h>
#endif

namespace {

std::filesystem::path makeUniqueCodeReviewDir(const std::string & name) {
	const auto base = std::filesystem::temp_directory_path() / "ofxggml_code_review_tests";
	std::filesystem::create_directories(base);
	const auto dir = base / (name + "_" + std::to_string(std::rand()));
	std::filesystem::create_directories(dir);
	return dir;
}

std::string createCodeReviewDummyModel() {
	const auto dir = makeUniqueCodeReviewDir("model");
	const auto model = dir / "dummy.gguf";
	std::ofstream out(model);
	out << "dummy-model";
	return model.string();
}

std::string createCodeReviewExecutable(const std::string & body) {
	const auto dir = makeUniqueCodeReviewDir("exec");
#ifdef _WIN32
	const auto exe = dir / "fake_llama.bat";
	std::ofstream out(exe);
	out << "@echo off\r\n" << body << "\r\n";
	out.close();
#else
	const auto exe = dir / "fake_llama.sh";
	std::ofstream out(exe);
	out << "#!/usr/bin/env bash\nset -euo pipefail\n" << body << "\n";
	out.close();
	::chmod(exe.c_str(), 0755);
#endif
	return exe.string();
}

} // namespace

TEST_CASE("Code review pipeline handles local script sources", "[code_review]") {
	const auto sourceDir = makeUniqueCodeReviewDir("source");
	{
		std::ofstream file(sourceDir / "main.cpp");
		file << "#include \"util.h\"\n";
		file << "int main() { return add(1, 2); }\n";
	}
	{
		std::ofstream file(sourceDir / "util.h");
		file << "inline int add(int a, int b) { return a + b; }\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlCodeReview review;
	review.setCompletionExecutable(createCodeReviewExecutable("echo review-output"));
	review.setEmbeddingExecutable(createCodeReviewExecutable("echo [0.1, 0.2, 0.3]"));

	ofxGgmlCodeReviewSettings settings;
	settings.maxTokens = 128;
	settings.contextSize = 1024;
	settings.maxEmbedParallelTasks = 1;
	settings.maxSummaryParallelTasks = 1;

	const auto result = review.reviewScriptSource(
		createCodeReviewDummyModel(),
		scriptSource,
		"Look for integration bugs and missing tests.",
		settings);

	REQUIRE(result.success);
	REQUIRE(result.files.size() == 2);
	REQUIRE_FALSE(result.selectedFileIndices.empty());
	REQUIRE(result.combinedReport.find("Hierarchical code review") != std::string::npos);
	REQUIRE_FALSE(result.architectureReview.empty());
	REQUIRE_FALSE(result.integrationReview.empty());
}

TEST_CASE("Code review reports blank aggregate passes explicitly", "[code_review]") {
	const auto sourceDir = makeUniqueCodeReviewDir("blank_sections");
	{
		std::ofstream file(sourceDir / "main.cpp");
		file << "int main() { return 0; }\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlCodeReview review;
#ifdef _WIN32
	review.setCompletionExecutable(createCodeReviewExecutable("echo."));
#else
	review.setCompletionExecutable(createCodeReviewExecutable("printf '\\n'"));
#endif
	review.setEmbeddingExecutable(createCodeReviewExecutable("echo [0.1, 0.2, 0.3]"));

	ofxGgmlCodeReviewSettings settings;
	settings.maxTokens = 128;
	settings.contextSize = 1024;
	settings.maxEmbedParallelTasks = 1;
	settings.maxSummaryParallelTasks = 1;

	const auto result = review.reviewScriptSource(
		createCodeReviewDummyModel(),
		scriptSource,
		"Review architecture output formatting.",
		settings);

	REQUIRE(result.success);
	const bool firstPassReported =
		result.firstPassSummary.find("[warning] First-pass review for main.cpp returned no findings.") != std::string::npos ||
		result.firstPassSummary.find("[error] llama completion returned empty output") != std::string::npos;
	const bool architectureReported =
		result.architectureReview.find("[warning] Architecture review returned no findings.") != std::string::npos ||
		result.architectureReview.find("[error] llama completion returned empty output") != std::string::npos;
	const bool integrationReported =
		result.integrationReview.find("[warning] Integration review returned no findings.") != std::string::npos ||
		result.integrationReview.find("[error] llama completion returned empty output") != std::string::npos;
	const bool combinedReported =
		result.combinedReport.find("Second pass - architecture issues:\n[warning]") != std::string::npos ||
		result.combinedReport.find("Second pass - architecture issues:\n[error]") != std::string::npos;
	REQUIRE(firstPassReported);
	REQUIRE(architectureReported);
	REQUIRE(integrationReported);
	REQUIRE(combinedReported);
}

TEST_CASE("Code review can recover with a fallback generator", "[code_review]") {
	const auto sourceDir = makeUniqueCodeReviewDir("fallback_sections");
	{
		std::ofstream file(sourceDir / "main.cpp");
		file << "int main() { return 0; }\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlCodeReview review;
#ifdef _WIN32
	review.setCompletionExecutable(createCodeReviewExecutable("echo."));
#else
	review.setCompletionExecutable(createCodeReviewExecutable("printf '\\n'"));
#endif
	review.setEmbeddingExecutable(createCodeReviewExecutable("echo [0.1, 0.2, 0.3]"));
	review.setGenerationFallback([](
		const std::string &,
		const std::string & prompt,
		const ofxGgmlInferenceSettings &) {
		ofxGgmlInferenceResult result;
		result.success = true;
		if (prompt.find("Architecture review") != std::string::npos ||
			prompt.find("Architectural review") != std::string::npos) {
			result.text = "- Architecture fallback";
		} else if (prompt.find("Cross-file integration review") != std::string::npos ||
			prompt.find("Third pass: Cross-file dependency and integration analysis.") != std::string::npos) {
			result.text = "- Integration fallback";
		} else {
			result.text =
				"Summary: main entry point\n"
				"Findings:\n"
				"- Missing validation. Evidence: `int main() { return 0; }`\n"
				"Tests:\n"
				"- Add a smoke test for `main()` startup.\n";
		}
		return result;
	});

	ofxGgmlCodeReviewSettings settings;
	settings.maxTokens = 128;
	settings.contextSize = 1024;
	settings.maxEmbedParallelTasks = 1;
	settings.maxSummaryParallelTasks = 1;

	const auto result = review.reviewScriptSource(
		createCodeReviewDummyModel(),
		scriptSource,
		"Recover empty review output.",
		settings);

	REQUIRE(result.success);
	REQUIRE(result.firstPassSummary.find("Missing validation") != std::string::npos);
	REQUIRE(result.architectureReview.find("fallback") != std::string::npos);
	REQUIRE(result.architectureReview.find("empty output") == std::string::npos);
	REQUIRE(result.integrationReview.find("Integration fallback") != std::string::npos);
}

TEST_CASE("Code review collapses low-signal filler into no-findings text", "[code_review]") {
	const auto sourceDir = makeUniqueCodeReviewDir("low_signal_sections");
	{
		std::ofstream file(sourceDir / "main.cpp");
		file << "int main() { return 0; }\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlCodeReview review;
	review.setCompletionExecutable(createCodeReviewExecutable(
		"echo Summary: This file is a simple OpenFrameworks entry point.\r\n"
		"echo Findings: - The main function sets up a window. - There are no specific tests provided. - The file has no fan-in or fan-out metrics.\r\n"
		"echo Tests: None specific to this file."));
	review.setEmbeddingExecutable(createCodeReviewExecutable("echo [0.1, 0.2, 0.3]"));

	ofxGgmlCodeReviewSettings settings;
	settings.maxTokens = 128;
	settings.contextSize = 1024;
	settings.maxEmbedParallelTasks = 1;
	settings.maxSummaryParallelTasks = 1;

	const auto result = review.reviewScriptSource(
		createCodeReviewDummyModel(),
		scriptSource,
		"Look for real bugs only.",
		settings);

	REQUIRE(result.success);
	REQUIRE(result.firstPassSummary.find("No material findings in this file.") != std::string::npos);
	REQUIRE(result.architectureReview == "No material architecture findings.");
	REQUIRE(result.integrationReview == "No material integration findings.");
}

TEST_CASE("Code review drops unsupported findings without file evidence", "[code_review]") {
	const auto sourceDir = makeUniqueCodeReviewDir("unsupported_findings");
	{
		std::ofstream file(sourceDir / "main.cpp");
		file << "int main() { return 0; }\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlCodeReview review;
	review.setCompletionExecutable(createCodeReviewExecutable(
		"echo Summary: This file starts the app.\r\n"
		"echo Findings:\r\n"
		"echo - std::array is used without proper initialization.\r\n"
		"echo - backendTypeName is called without error handling.\r\n"
		"echo Tests:\r\n"
		"echo - Add tests for the unsupported findings."));
	review.setEmbeddingExecutable(createCodeReviewExecutable("echo [0.1, 0.2, 0.3]"));

	ofxGgmlCodeReviewSettings settings;
	settings.maxTokens = 128;
	settings.contextSize = 1024;
	settings.maxEmbedParallelTasks = 1;
	settings.maxSummaryParallelTasks = 1;

	const auto result = review.reviewScriptSource(
		createCodeReviewDummyModel(),
		scriptSource,
		"Only keep evidence-backed findings.",
		settings);

	REQUIRE(result.success);
	REQUIRE(result.firstPassSummary.find("No material findings in this file.") != std::string::npos);
	REQUIRE(result.firstPassSummary.find("backendTypeName") == std::string::npos);
	REQUIRE(result.firstPassSummary.find("std::array") == std::string::npos);
}

TEST_CASE("Code review ignores fenced or filename-only summaries", "[code_review]") {
	const auto sourceDir = makeUniqueCodeReviewDir("trivial_summary");
	{
		std::ofstream file(sourceDir / "main.cpp");
		file << "int main() { return 0; }\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlCodeReview review;
	review.setCompletionExecutable(createCodeReviewExecutable(
		"echo Summary: ```cpp\r\n"
		"echo Findings: No material findings in this file.\r\n"
		"echo Tests: None beyond current coverage."));
	review.setEmbeddingExecutable(createCodeReviewExecutable("echo [0.1, 0.2, 0.3]"));

	ofxGgmlCodeReviewSettings settings;
	settings.maxTokens = 128;
	settings.contextSize = 1024;
	settings.maxEmbedParallelTasks = 1;
	settings.maxSummaryParallelTasks = 1;

	const auto result = review.reviewScriptSource(
		createCodeReviewDummyModel(),
		scriptSource,
		"Reject broken summaries.",
		settings);

	REQUIRE(result.success);
	REQUIRE(result.firstPassSummary.find("Application entry point that boots the OpenFrameworks app.") != std::string::npos);
	REQUIRE(result.firstPassSummary.find("```cpp") == std::string::npos);
}

TEST_CASE("Code review replaces filename-only summaries with file-type descriptions", "[code_review]") {
	const auto sourceDir = makeUniqueCodeReviewDir("filename_only_summary");
	{
		std::ofstream file(sourceDir / "icon.rc");
		file << "#define MAIN_ICON 101\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlCodeReview review;
	review.setCompletionExecutable(createCodeReviewExecutable(
		"echo Summary: icon.rc\r\n"
		"echo Findings: No material findings in this file.\r\n"
		"echo Tests: None beyond current coverage."));
	review.setEmbeddingExecutable(createCodeReviewExecutable("echo [0.1, 0.2, 0.3]"));

	ofxGgmlCodeReviewSettings settings;
	settings.maxTokens = 128;
	settings.contextSize = 1024;
	settings.maxEmbedParallelTasks = 1;
	settings.maxSummaryParallelTasks = 1;

	const auto result = review.reviewScriptSource(
		createCodeReviewDummyModel(),
		scriptSource,
		"Replace trivial filename summaries.",
		settings);

	REQUIRE(result.success);
	REQUIRE(result.firstPassSummary.find("Windows resource script that selects the application icon") != std::string::npos);
	REQUIRE(result.firstPassSummary.find("Summary: icon.rc") == std::string::npos);
}

TEST_CASE("Code review replaces path-like filename summaries with file-type descriptions", "[code_review]") {
	const auto sourceDir = makeUniqueCodeReviewDir("path_summary");
	{
		std::ofstream file(sourceDir / "main.cpp");
		file << "int main() { return 0; }\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlCodeReview review;
	review.setCompletionExecutable(createCodeReviewExecutable(
		"echo Summary: src\\main.cpp\r\n"
		"echo Findings: No material findings in this file.\r\n"
		"echo Tests: None beyond current coverage."));
	review.setEmbeddingExecutable(createCodeReviewExecutable("echo [0.1, 0.2, 0.3]"));

	ofxGgmlCodeReviewSettings settings;
	settings.maxTokens = 128;
	settings.contextSize = 1024;
	settings.maxEmbedParallelTasks = 1;
	settings.maxSummaryParallelTasks = 1;

	const auto result = review.reviewScriptSource(
		createCodeReviewDummyModel(),
		scriptSource,
		"Replace path-like filename summaries.",
		settings);

	REQUIRE(result.success);
	REQUIRE(result.firstPassSummary.find("Application entry point that boots the OpenFrameworks app.") != std::string::npos);
	REQUIRE(result.firstPassSummary.find("Summary: src\\main.cpp") == std::string::npos);
}

TEST_CASE("Code review replaces generic project-file summaries with file-type descriptions", "[code_review]") {
	const auto sourceDir = makeUniqueCodeReviewDir("generic_project_summary");
	{
		std::ofstream file(sourceDir / "ofxGgmlGuiExample.vcxproj");
		file << "<Project DefaultTargets=\"Build\"></Project>\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlCodeReview review;
	review.setCompletionExecutable(createCodeReviewExecutable(
		"echo Summary: Project file included in the hierarchical review.\r\n"
		"echo Findings: No material findings in this file.\r\n"
		"echo Tests: None beyond current coverage."));
	review.setEmbeddingExecutable(createCodeReviewExecutable("echo [0.1, 0.2, 0.3]"));

	ofxGgmlCodeReviewSettings settings;
	settings.maxTokens = 128;
	settings.contextSize = 1024;
	settings.maxEmbedParallelTasks = 1;
	settings.maxSummaryParallelTasks = 1;

	const auto result = review.reviewScriptSource(
		createCodeReviewDummyModel(),
		scriptSource,
		"Replace generic project-file summaries.",
		settings);

	REQUIRE(result.success);
	REQUIRE(result.firstPassSummary.find("Visual Studio project definition for the example application and its build settings.") != std::string::npos);
	REQUIRE(result.firstPassSummary.find("Summary: Project file included in the hierarchical review.") == std::string::npos);
}

TEST_CASE("Aggregate no-findings sections drop trailing recommendations", "[code_review]") {
	const auto sourceDir = makeUniqueCodeReviewDir("aggregate_cleanup");
	{
		std::ofstream file(sourceDir / "main.cpp");
		file << "int main() { return 0; }\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlCodeReview review;
#ifdef _WIN32
	review.setCompletionExecutable(createCodeReviewExecutable("echo."));
#else
	review.setCompletionExecutable(createCodeReviewExecutable("printf '\\n'"));
#endif
	review.setEmbeddingExecutable(createCodeReviewExecutable("echo [0.1, 0.2, 0.3]"));
	review.setGenerationFallback([](
		const std::string &,
		const std::string & prompt,
		const ofxGgmlInferenceSettings &) {
		ofxGgmlInferenceResult result;
		result.success = true;
		if (prompt.find("Architecture review") != std::string::npos ||
			prompt.find("Architectural review") != std::string::npos) {
			result.text =
				"No material architecture findings.\n"
				"Recommendations:\n"
				"- Add more tests anyway.";
		} else if (prompt.find("Cross-file integration review") != std::string::npos ||
			prompt.find("Third pass: Cross-file dependency and integration analysis.") != std::string::npos) {
			result.text =
				"No material integration findings.\n"
				"Recommendations:\n"
				"- Review dependencies just in case.";
		} else {
			result.text =
				"Summary: main entry point\n"
				"Findings:\n"
				"- Example supported finding. Evidence: `int main() { return 0; }`\n"
				"Tests:\n"
				"- Add a smoke test for `main()` startup.\n";
		}
		return result;
	});

	ofxGgmlCodeReviewSettings settings;
	settings.maxTokens = 128;
	settings.contextSize = 1024;
	settings.maxEmbedParallelTasks = 1;
	settings.maxSummaryParallelTasks = 1;

	const auto result = review.reviewScriptSource(
		createCodeReviewDummyModel(),
		scriptSource,
		"Trim aggregate no-findings noise.",
		settings);

	REQUIRE(result.success);
	REQUIRE(result.architectureReview == "No material architecture findings.");
	REQUIRE(result.integrationReview == "No material integration findings.");
}

TEST_CASE("Code review skips aggregate passes when first pass has no material findings", "[code_review]") {
	const auto sourceDir = makeUniqueCodeReviewDir("skip_aggregate");
	{
		std::ofstream file(sourceDir / "main.cpp");
		file << "int main() { return 0; }\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlCodeReview review;
#ifdef _WIN32
	review.setCompletionExecutable(createCodeReviewExecutable("echo."));
#else
	review.setCompletionExecutable(createCodeReviewExecutable("printf '\\n'"));
#endif
	review.setEmbeddingExecutable(createCodeReviewExecutable("echo [0.1, 0.2, 0.3]"));

	int firstPassFallbacks = 0;
	int aggregateFallbacks = 0;
	review.setGenerationFallback([&](
		const std::string &,
		const std::string & prompt,
		const ofxGgmlInferenceSettings &) {
		ofxGgmlInferenceResult result;
		result.success = true;
		if (prompt.find("Architecture review") != std::string::npos ||
			prompt.find("Architectural review") != std::string::npos ||
			prompt.find("Cross-file integration review") != std::string::npos ||
			prompt.find("Third pass: Cross-file dependency and integration analysis.") != std::string::npos) {
			++aggregateFallbacks;
			result.text = "This should never be used.";
		} else {
			++firstPassFallbacks;
			result.text =
				"Summary: simple entry point\n"
				"Findings: No material findings in this file.\n"
				"Tests: None beyond current coverage.\n";
		}
		return result;
	});

	ofxGgmlCodeReviewSettings settings;
	settings.maxTokens = 768;
	settings.contextSize = 4096;
	settings.maxEmbedParallelTasks = 1;
	settings.maxSummaryParallelTasks = 1;

	const auto result = review.reviewScriptSource(
		createCodeReviewDummyModel(),
		scriptSource,
		"Skip aggregate passes when nothing is found.",
		settings);

	REQUIRE(result.success);
	REQUIRE(firstPassFallbacks == 1);
	REQUIRE(aggregateFallbacks == 0);
	REQUIRE(result.architectureReview == "No material architecture findings.");
	REQUIRE(result.integrationReview == "No material integration findings.");
}

TEST_CASE("Code review uses reduced token budgets for review passes", "[code_review]") {
	const auto sourceDir = makeUniqueCodeReviewDir("token_budgets");
	{
		std::ofstream file(sourceDir / "main.cpp");
		file << "int main() { return 0; }\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlCodeReview review;
#ifdef _WIN32
	review.setCompletionExecutable(createCodeReviewExecutable("echo."));
#else
	review.setCompletionExecutable(createCodeReviewExecutable("printf '\\n'"));
#endif
	review.setEmbeddingExecutable(createCodeReviewExecutable("echo [0.1, 0.2, 0.3]"));

	int firstPassTokens = 0;
	int architectureTokens = 0;
	int integrationTokens = 0;
	review.setGenerationFallback([&](
		const std::string &,
		const std::string & prompt,
		const ofxGgmlInferenceSettings & inferenceSettings) {
		ofxGgmlInferenceResult result;
		result.success = true;
		if (prompt.find("Architecture review") != std::string::npos ||
			prompt.find("Architectural review") != std::string::npos) {
			architectureTokens = inferenceSettings.maxTokens;
			result.text = "- Architecture fallback";
		} else if (prompt.find("Cross-file integration review") != std::string::npos ||
			prompt.find("Third pass: Cross-file dependency and integration analysis.") != std::string::npos) {
			integrationTokens = inferenceSettings.maxTokens;
			result.text = "- Integration fallback";
		} else {
			firstPassTokens = inferenceSettings.maxTokens;
			result.text =
				"Summary: main entry point\n"
				"Findings:\n"
				"- Missing validation. Evidence: `int main() { return 0; }`\n"
				"Tests:\n"
				"- Add a smoke test for `main()` startup.\n";
		}
		return result;
	});

	ofxGgmlCodeReviewSettings settings;
	settings.maxTokens = 2048;
	settings.contextSize = 8192;
	settings.maxEmbedParallelTasks = 1;
	settings.maxSummaryParallelTasks = 1;

	const auto result = review.reviewScriptSource(
		createCodeReviewDummyModel(),
		scriptSource,
		"Use smaller token budgets.",
		settings);

	REQUIRE(result.success);
	REQUIRE(firstPassTokens >= 160);
	REQUIRE(firstPassTokens <= 512);
	REQUIRE(architectureTokens >= 128);
	REQUIRE(architectureTokens <= 384);
	REQUIRE(integrationTokens >= 128);
	REQUIRE(integrationTokens <= 384);
}

TEST_CASE("Code review forwards server-backed generation settings", "[code_review]") {
	const auto sourceDir = makeUniqueCodeReviewDir("server_review");
	{
		std::ofstream file(sourceDir / "main.cpp");
		file << "int main() { return 0; }\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlCodeReview review;
#ifdef _WIN32
	review.setCompletionExecutable(createCodeReviewExecutable("echo."));
#else
	review.setCompletionExecutable(createCodeReviewExecutable("printf '\\n'"));
#endif
	review.setEmbeddingExecutable(createCodeReviewExecutable("echo [0.1, 0.2, 0.3]"));

	int serverBackedCalls = 0;
	review.setGenerationFallback([&](
		const std::string & modelPath,
		const std::string & prompt,
		const ofxGgmlInferenceSettings & inferenceSettings) {
		ofxGgmlInferenceResult result;
		result.success = true;
		REQUIRE(modelPath.empty());
		REQUIRE(inferenceSettings.useServerBackend);
		REQUIRE(inferenceSettings.serverUrl == "http://127.0.0.1:8080");
		REQUIRE(inferenceSettings.serverModel == "qwen-review");
		++serverBackedCalls;
		if (prompt.find("Architecture review") != std::string::npos ||
			prompt.find("Architectural review") != std::string::npos) {
			result.text = "No material architecture findings.";
		} else if (prompt.find("Cross-file integration review") != std::string::npos ||
			prompt.find("Third pass: Cross-file dependency and integration analysis.") != std::string::npos) {
			result.text = "No material integration findings.";
		} else {
			result.text =
				"Summary: main entry point\n"
				"Findings: No material findings in this file.\n"
				"Tests: None beyond current coverage.\n";
		}
		return result;
	});

	ofxGgmlCodeReviewSettings settings;
	settings.maxTokens = 256;
	settings.contextSize = 2048;
	settings.maxEmbedParallelTasks = 1;
	settings.maxSummaryParallelTasks = 1;
	settings.useServerBackend = true;
	settings.serverUrl = "http://127.0.0.1:8080";
	settings.serverModel = "qwen-review";

	const auto result = review.reviewScriptSource(
		"",
		scriptSource,
		"Use the persistent server path for review.",
		settings);

	REQUIRE(result.success);
	REQUIRE(serverBackedCalls >= 1);
	REQUIRE(result.combinedReport.find("Generation backend: llama-server (persistent)") != std::string::npos);
	REQUIRE(result.combinedReport.find("Server URL: http://127.0.0.1:8080") != std::string::npos);
	REQUIRE(result.combinedReport.find("Server model: qwen-review") != std::string::npos);
}

TEST_CASE("Code review rejects code-fragment summaries and weak anchored findings", "[code_review]") {
	const auto sourceDir = makeUniqueCodeReviewDir("weak_signal_cleanup");
	{
		std::ofstream file(sourceDir / "main.cpp");
		file << "int main() {\n";
		file << "    ofGLFWWindowSettings settings;\n";
		file << "    settings.title = \"ofxGgml - AI Studio\";\n";
		file << "    settings.setGLVersion(3, 3);\n";
		file << "}\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlCodeReview review;
	review.setCompletionExecutable(createCodeReviewExecutable(
		"echo Summary: char * value = nullptr;\r\n"
		"echo Findings:\r\n"
		"echo - `settings.title = \"ofxGgml - AI Studio\";` could be a security risk if user-supplied data is used for phishing.\r\n"
		"echo - `settings.setGLVersion(3, 3);` might be too low depending on the requirements and hardware.\r\n"
		"echo Tests:\r\n"
		"echo - Add tests for the supported findings above."));
	review.setEmbeddingExecutable(createCodeReviewExecutable("echo [0.1, 0.2, 0.3]"));

	ofxGgmlCodeReviewSettings settings;
	settings.maxTokens = 128;
	settings.contextSize = 1024;
	settings.maxEmbedParallelTasks = 1;
	settings.maxSummaryParallelTasks = 1;

	const auto result = review.reviewScriptSource(
		createCodeReviewDummyModel(),
		scriptSource,
		"Only keep strong findings.",
		settings);

	REQUIRE(result.success);
	REQUIRE(result.firstPassSummary.find("Application entry point that boots the OpenFrameworks app.") != std::string::npos);
	REQUIRE(result.firstPassSummary.find("char * value = nullptr;") == std::string::npos);
	REQUIRE(result.firstPassSummary.find("security risk") == std::string::npos);
	REQUIRE(result.firstPassSummary.find("might be too low") == std::string::npos);
	REQUIRE(result.integrationReview == "No material integration findings.");
}

TEST_CASE("Code review replaces snippet-like summaries with professional fallbacks", "[code_review]") {
	const auto sourceDir = makeUniqueCodeReviewDir("snippet_summary_cleanup");
	{
		std::ofstream appFile(sourceDir / "ofApp.cpp");
		appFile << "switch (themeIndex) {\n";
		appFile << "case 0:\n";
		appFile << "    break;\n";
		appFile << "}\n";
	}
	{
		std::ofstream mainFile(sourceDir / "main.cpp");
		mainFile << "int main() { return 0; }\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlCodeReview review;
	review.setCompletionExecutable(createCodeReviewExecutable(
		"echo Summary: ofRunApp(window,\r\n"
		"echo Findings: No material findings in this file.\r\n"
		"echo Tests: None beyond current coverage."));
	review.setEmbeddingExecutable(createCodeReviewExecutable("echo [0.1, 0.2, 0.3]"));

	ofxGgmlCodeReviewSettings settings;
	settings.maxTokens = 128;
	settings.contextSize = 1024;
	settings.maxEmbedParallelTasks = 1;
	settings.maxSummaryParallelTasks = 1;

	const auto result = review.reviewScriptSource(
		createCodeReviewDummyModel(),
		scriptSource,
		"Use professional summaries.",
		settings);

	REQUIRE(result.success);
	REQUIRE(result.firstPassSummary.find("Summary: ofRunApp(window,") == std::string::npos);
	REQUIRE(result.firstPassSummary.find("Application entry point that boots the OpenFrameworks app.") != std::string::npos);
	REQUIRE(result.firstPassSummary.find("Main application implementation containing UI, state management, and runtime behavior.") != std::string::npos);
}

TEST_CASE("Code review drops generic tiny-file findings and boilerplate tests", "[code_review]") {
	const auto sourceDir = makeUniqueCodeReviewDir("tiny_file_cleanup");
	{
		std::ofstream file(sourceDir / "icon.rc");
		file << "#define MAIN_ICON 101\n";
		file << "#if defined(_DEBUG)\n";
		file << "MAIN_ICON ICON \"icon_debug.ico\"\n";
		file << "#else\n";
		file << "MAIN_ICON ICON \"icon.ico\"\n";
		file << "#endif\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlCodeReview review;
	review.setCompletionExecutable(createCodeReviewExecutable(
		"echo Summary: Windows resource script that selects the application icon for debug and release builds.\r\n"
		"echo Findings:\r\n"
		"echo - The conditional compilation based on `_DEBUG` is a common practice and seems appropriate for distinguishing debug and release builds.\r\n"
		"echo Tests:\r\n"
		"echo - No file-specific tests are necessary as the functionality of this file is minimal.\r\n"));
	review.setEmbeddingExecutable(createCodeReviewExecutable("echo [0.1, 0.2, 0.3]"));

	ofxGgmlCodeReviewSettings settings;
	settings.maxTokens = 128;
	settings.contextSize = 1024;
	settings.maxEmbedParallelTasks = 1;
	settings.maxSummaryParallelTasks = 1;

	const auto result = review.reviewScriptSource(
		createCodeReviewDummyModel(),
		scriptSource,
		"Keep tiny-file reviews evidence-based and professional.",
		settings);

	REQUIRE(result.success);
	REQUIRE(result.firstPassSummary.find("No material findings in this file.") != std::string::npos);
	REQUIRE(result.firstPassSummary.find("common practice") == std::string::npos);
	REQUIRE(result.firstPassSummary.find("No file-specific tests are necessary") == std::string::npos);
}

TEST_CASE("Code review falls back to lexical ranking when embeddings are unavailable", "[code_review]") {
	const auto sourceDir = makeUniqueCodeReviewDir("lexical_ranking");
	{
		std::ofstream file(sourceDir / "main.cpp");
		file << "int main() { return 0; }\n";
	}
	{
		std::ofstream file(sourceDir / "icon.rc");
		file << "#define MAIN_ICON 101\n";
		file << "MAIN_ICON ICON \"icon.ico\"\n";
	}
	{
		std::ofstream file(sourceDir / "notes.txt");
		file << "misc scratch file\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlCodeReview review;
	review.setCompletionExecutable(createCodeReviewExecutable(
		"echo Summary: Project file included in the hierarchical review.\r\n"
		"echo Findings: No material findings in this file.\r\n"
		"echo Tests: None beyond current coverage."));
#ifdef _WIN32
	review.setEmbeddingExecutable(createCodeReviewExecutable("echo."));
#else
	review.setEmbeddingExecutable(createCodeReviewExecutable("printf '\\n'"));
#endif

	ofxGgmlCodeReviewSettings settings;
	settings.maxTokens = 128;
	settings.contextSize = 256;
	settings.maxEmbedParallelTasks = 1;
	settings.maxSummaryParallelTasks = 1;

	const auto result = review.reviewScriptSource(
		createCodeReviewDummyModel(),
		scriptSource,
		"Review icon resource handling and build resource wiring.",
		settings);

	REQUIRE(result.success);
	REQUIRE_FALSE(result.selectedFileIndices.empty());
	const auto & firstSelected = result.files[result.selectedFileIndices.front()];
	REQUIRE(firstSelected.name == "icon.rc");
	REQUIRE(firstSelected.similarityScore > 0.0f);
}
