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
	REQUIRE(result.architectureReview.find("review-output") != std::string::npos);
	REQUIRE(result.integrationReview.find("review-output") != std::string::npos);
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
		"Recover empty review output.",
		settings);

	REQUIRE(result.success);
	REQUIRE(result.firstPassSummary.find("Example supported finding") != std::string::npos);
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
	REQUIRE(result.firstPassSummary.find("Summary: main.cpp") != std::string::npos);
	REQUIRE(result.firstPassSummary.find("```cpp") == std::string::npos);
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
