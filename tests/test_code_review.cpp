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
	REQUIRE(result.firstPassSummary.find("[warning] First-pass review for main.cpp returned no findings.") != std::string::npos);
	REQUIRE(result.architectureReview.find("[warning] Architecture review returned no findings.") != std::string::npos);
	REQUIRE(result.integrationReview.find("[warning] Integration review returned no findings.") != std::string::npos);
	REQUIRE(result.combinedReport.find("Second pass - architecture issues:\n[warning]") != std::string::npos);
}
