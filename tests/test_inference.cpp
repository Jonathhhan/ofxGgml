#include "catch2.hpp"
#include "../src/ofxGgml.h"
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#ifndef _WIN32
	#include <sys/stat.h>
#endif

// Inference tests are mostly API tests since they depend on external llama.cpp executables
// Tests marked [inference][requires-executable] need llama CLI tools installed

namespace {

std::string trimCopy(const std::string & text) {
	size_t start = 0;
	while (start < text.size() &&
		std::isspace(static_cast<unsigned char>(text[start]))) {
		++start;
	}
	size_t end = text.size();
	while (end > start &&
		std::isspace(static_cast<unsigned char>(text[end - 1]))) {
		--end;
	}
	return text.substr(start, end - start);
}

std::vector<std::string> splitLines(const std::string & text) {
	std::vector<std::string> lines;
	std::istringstream stream(text);
	std::string line;
	while (std::getline(stream, line)) {
		const std::string trimmed = trimCopy(line);
		if (!trimmed.empty()) {
			lines.push_back(trimmed);
		}
	}
	return lines;
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

std::string translateWindowsTestScript(const std::string & body) {
	const std::string trimmedBody = trimCopy(body);
	if (trimmedBody.find("OFXGGML_EXIT_TEST_MODE") != std::string::npos) {
		const bool embeddingMode =
			trimmedBody.find("[0.25, 0.50, 0.75]") != std::string::npos;
		std::ostringstream out;
		out << "@echo off\r\n";
		out << "set \"mode=%OFXGGML_EXIT_TEST_MODE%\"\r\n";
		out << "if /I \"%mode%\"==\"nonempty\" (\r\n";
		out << "  echo " << (embeddingMode ? "[0.25, 0.50, 0.75]" : "generated-output") << "\r\n";
		out << "  exit /b 1\r\n";
		out << ")\r\n";
		if (!embeddingMode) {
			out << "if /I \"%mode%\"==\"sigint-empty\" exit /b 130\r\n";
		}
		out << "exit /b 1\r\n";
		return out.str();
	}

	std::ostringstream out;
	out << "@echo off\r\n";
	for (const auto & line : splitLines(body)) {
		if (line.rfind("exit ", 0) == 0) {
			out << "exit /b " << trimCopy(line.substr(5)) << "\r\n";
			continue;
		}
		if (line.rfind("echo ", 0) == 0) {
			std::string payload = trimCopy(line.substr(5));
			if (payload.size() >= 2 &&
				payload.front() == '"' &&
				payload.back() == '"') {
				payload = payload.substr(1, payload.size() - 2);
			}
			out << "echo " << escapeBatchEcho(payload) << "\r\n";
			continue;
		}
		out << line << "\r\n";
	}
	return out.str();
}

std::filesystem::path makeUniqueTestDir(const std::string & name) {
	const auto base = std::filesystem::temp_directory_path() / "ofxggml_tests";
	std::filesystem::create_directories(base);
	const auto dir = base / (name + "_" + std::to_string(std::rand()));
	std::filesystem::create_directories(dir);
	return dir;
}

std::string createDummyModel() {
	const auto dir = makeUniqueTestDir("model");
	const auto model = dir / "dummy.gguf";
	std::ofstream out(model);
	out << "dummy-model";
	out.close();
	return model.string();
}

std::string createExecutableScript(const std::string & body) {
	const auto dir = makeUniqueTestDir("exec");
#ifdef _WIN32
	const auto exe = dir / "fake_llama.bat";
	std::ofstream out(exe);
	out << translateWindowsTestScript(body);
	out.close();
#else
	const auto exe = dir / "fake_llama.sh";
	std::ofstream out(exe);
	out << "#!/usr/bin/env bash\nset -euo pipefail\n" << body << "\n";
	out.close();
	chmod(exe.c_str(), 0755);
#endif
	return exe.string();
}

std::string createCompletionFlagSensitiveExecutable(const std::string & flag, const std::string & output) {
	const auto dir = makeUniqueTestDir("flag_sensitive_exec");
#ifdef _WIN32
	const auto exe = dir / "fake_llama_flag_sensitive.bat";
	std::ofstream out(exe);
	out << "@echo off\r\n";
	out << "set \"args=%*\"\r\n";
	out << "echo %args% | findstr /C:\"" << flag << "\" >nul\r\n";
	out << "if %errorlevel%==0 exit /b 0\r\n";
	out << "echo " << escapeBatchEcho(output) << "\r\n";
	out << "exit /b 0\r\n";
	out.close();
#else
	const auto exe = dir / "fake_llama_flag_sensitive.sh";
	std::ofstream out(exe);
	out << "#!/usr/bin/env bash\nset -euo pipefail\n";
	out << "case \" $* \" in\n";
	out << "  *\" " << flag << " \"*) exit 0 ;;\n";
	out << "esac\n";
	out << "echo \"" << output << "\"\n";
	out.close();
	chmod(exe.c_str(), 0755);
#endif
	return exe.string();
}

void setEnvVar(const std::string & key, const std::string & value) {
#ifdef _WIN32
	_putenv_s(key.c_str(), value.c_str());
#else
	setenv(key.c_str(), value.c_str(), 1);
#endif
}

void unsetEnvVar(const std::string & key) {
#ifdef _WIN32
	_putenv_s(key.c_str(), "");
#else
	unsetenv(key.c_str());
#endif
}

struct ScopedEnvVar {
	std::string key;
	std::string original;
	bool hadOriginal = false;

	ScopedEnvVar(std::string keyIn, std::string value)
		: key(std::move(keyIn)) {
		const char * existing = std::getenv(key.c_str());
		if (existing) {
			hadOriginal = true;
			original = existing;
		}
		setEnvVar(key, value);
	}

	~ScopedEnvVar() {
		if (hadOriginal) {
			setEnvVar(key, original);
		} else {
			unsetEnvVar(key);
		}
	}
};

} // namespace

TEST_CASE("Inference initialization", "[inference]") {
	ofxGgmlInference inf;

	SECTION("Default state") {
		// Should not crash on construction
		REQUIRE(true);
	}

	SECTION("Get executables returns paths") {
		std::string completion = inf.getCompletionExecutable();
		std::string embedding = inf.getEmbeddingExecutable();
		// May be empty if not found, just check API works
		REQUIRE(completion.size() >= 0);
		REQUIRE(embedding.size() >= 0);
	}
}

TEST_CASE("Inference executable configuration", "[inference]") {
	ofxGgmlInference inf;

	SECTION("Set custom completion executable") {
		inf.setCompletionExecutable("/custom/path/llama-completion");
		REQUIRE(inf.getCompletionExecutable() == "/custom/path/llama-completion");
	}

	SECTION("Set custom embedding executable") {
		inf.setEmbeddingExecutable("/custom/path/llama-embedding");
		REQUIRE(inf.getEmbeddingExecutable() == "/custom/path/llama-embedding");
	}

	SECTION("Set empty path") {
		inf.setCompletionExecutable("");
		REQUIRE(inf.getCompletionExecutable() == "");
	}
}

TEST_CASE("Executable resolution accepts absolute path and PATH command", "[inference]") {
	const std::string modelPath = createDummyModel();
	const std::string completionScript = createExecutableScript("echo absolute-ok");
	const std::string embeddingScript = createExecutableScript("echo [0.1, 0.2, 0.3]");

	SECTION("absolute path executable works for completion and embedding") {
		ofxGgmlInference inf;
		inf.setCompletionExecutable(completionScript);
		inf.setEmbeddingExecutable(embeddingScript);

		auto gen = inf.generate(modelPath, "hello");
		REQUIRE(gen.success);
		REQUIRE(gen.text.find("absolute-ok") != std::string::npos);

		auto emb = inf.embed(modelPath, "hello");
		REQUIRE(emb.success);
		REQUIRE(emb.embedding.size() == 3);

		auto genEx = inf.generateEx(modelPath, "hello");
		REQUIRE(genEx.isOk());
		REQUIRE(genEx.value().success);
		REQUIRE(genEx.value().text.find("absolute-ok") != std::string::npos);

		auto embEx = inf.embedEx(modelPath, "hello");
		REQUIRE(embEx.isOk());
		REQUIRE(embEx.value().success);
		REQUIRE(embEx.value().embedding.size() == 3);
	}

	SECTION("PATH-resolvable command works") {
		const auto cmdDir = makeUniqueTestDir("pathcmd");
#ifdef _WIN32
		const auto cmdPath = cmdDir / "ofxggml-path-cmd.bat";
		std::ofstream out(cmdPath);
		out << "@echo off\r\necho path-ok\r\n";
		out.close();
#else
		const auto cmdPath = cmdDir / "ofxggml-path-cmd";
		std::ofstream out(cmdPath);
		out << "#!/usr/bin/env bash\nset -euo pipefail\necho path-ok\n";
		out.close();
		chmod(cmdPath.c_str(), 0755);
#endif
		std::string pathValue = cmdDir.string();
		if (const char * existingPath = std::getenv("PATH")) {
#ifdef _WIN32
			pathValue += ";";
#else
			pathValue += ":";
#endif
			pathValue += existingPath;
		}
		ScopedEnvVar scopedPath("PATH", pathValue);

		ofxGgmlInference inf;
		inf.setCompletionExecutable("ofxggml-path-cmd");
		auto gen = inf.generate(modelPath, "hello");
		REQUIRE(gen.success);
		REQUIRE(gen.text.find("path-ok") != std::string::npos);
	}

	SECTION("missing command and non-file path are rejected") {
		ofxGgmlInference inf;
		inf.setCompletionExecutable("ofxggml-command-that-should-not-exist");
		auto missing = inf.generate(modelPath, "hello");
		REQUIRE_FALSE(missing.success);
		REQUIRE(missing.error.find("invalid or inaccessible completion executable") != std::string::npos);
		auto missingEx = inf.generateEx(modelPath, "hello");
		REQUIRE(missingEx.isError());
		REQUIRE(missingEx.error().code == ofxGgmlErrorCode::InferenceExecutableMissing);
		REQUIRE(missingEx.error().message.find("invalid or inaccessible completion executable") != std::string::npos);

		const auto dirPath = makeUniqueTestDir("nonfile");
		inf.setCompletionExecutable(dirPath.string());
		auto nonFile = inf.generate(modelPath, "hello");
		REQUIRE_FALSE(nonFile.success);
		REQUIRE(nonFile.error.find("invalid or inaccessible completion executable") != std::string::npos);
	}
}

TEST_CASE("Inference settings", "[inference]") {
	SECTION("Default settings") {
		ofxGgmlInferenceSettings settings;

		REQUIRE(settings.maxTokens == 256);
		REQUIRE(settings.temperature == 0.7f);
		REQUIRE(settings.topK == 40);
		REQUIRE(settings.minP == 0.05f);
		REQUIRE(settings.mirostat == 0);
		REQUIRE(settings.mirostatTau == 5.0f);
		REQUIRE(settings.mirostatEta == 0.1f);
		REQUIRE(settings.topP == 0.9f);
		REQUIRE(settings.repeatPenalty == 1.1f);
		REQUIRE(settings.contextSize == 2048);
		REQUIRE(settings.batchSize == 512);
		REQUIRE(settings.gpuLayers == 0);
		REQUIRE(settings.threads == 0);
		REQUIRE(settings.seed == -1);
		REQUIRE(settings.simpleIo == true);
		REQUIRE(settings.promptCacheAll == false);
		REQUIRE(settings.autoPromptCache == true);
		REQUIRE(settings.singleTurn == true);
		REQUIRE(settings.autoProbeCliCapabilities == true);
		REQUIRE(settings.trimPromptToContext == false);
		REQUIRE(settings.allowBatchFallback == true);
		REQUIRE(settings.autoContinueCutoff == false);
		REQUIRE(settings.stopAtNaturalBoundary == true);
		REQUIRE(settings.device.empty());
		REQUIRE(settings.promptCachePath.empty());
	}

	SECTION("Custom settings") {
		ofxGgmlInferenceSettings settings;
		settings.maxTokens = 512;
		settings.temperature = 0.5f;
		settings.threads = 4;

		REQUIRE(settings.maxTokens == 512);
		REQUIRE(settings.temperature == 0.5f);
		REQUIRE(settings.threads == 4);
	}
}

TEST_CASE("Embedding settings", "[inference]") {
	SECTION("Default embedding settings") {
		ofxGgmlEmbeddingSettings settings;

		REQUIRE(settings.normalize == true);
		REQUIRE(settings.pooling == "mean");
	}

	SECTION("Custom embedding settings") {
		ofxGgmlEmbeddingSettings settings;
		settings.normalize = false;
		settings.pooling = "cls";

		REQUIRE(settings.normalize == false);
		REQUIRE(settings.pooling == "cls");
	}
}

TEST_CASE("Inference result structure", "[inference]") {
	SECTION("Default result") {
		ofxGgmlInferenceResult result;

		REQUIRE(result.success == false);
		REQUIRE(result.elapsedMs == 0.0f);
		REQUIRE(result.text.empty());
		REQUIRE(result.error.empty());
		REQUIRE(result.sourcesUsed.empty());
	}

	SECTION("Result with data") {
		ofxGgmlInferenceResult result;
		result.success = true;
		result.elapsedMs = 123.45f;
		result.text = "generated text";
		result.sourcesUsed.push_back({"Doc", "https://example.com", "context", true, false});

		REQUIRE(result.success == true);
		REQUIRE(result.elapsedMs == 123.45f);
		REQUIRE(result.text == "generated text");
		REQUIRE(result.sourcesUsed.size() == 1);
	}
}

TEST_CASE("Diffusion bridge request and result structures", "[inference]") {
	SECTION("Task labels cover modern image modes") {
		REQUIRE(std::string(
			ofxGgmlDiffusionInference::taskLabel(
				ofxGgmlImageGenerationTask::TextToImage)) == "Text to Image");
		REQUIRE(std::string(
			ofxGgmlDiffusionInference::taskLabel(
				ofxGgmlImageGenerationTask::InstructImage)) == "Instruct Image");
		REQUIRE(std::string(
			ofxGgmlDiffusionInference::taskLabel(
				ofxGgmlImageGenerationTask::Variation)) == "Variation");
		REQUIRE(std::string(
			ofxGgmlDiffusionInference::taskLabel(
				ofxGgmlImageGenerationTask::Restyle)) == "Restyle");
	}

	SECTION("Selection mode labels are stable") {
		REQUIRE(std::string(
			ofxGgmlDiffusionInference::selectionModeLabel(
				ofxGgmlImageSelectionMode::KeepOrder)) == "Keep Order");
		REQUIRE(std::string(
			ofxGgmlDiffusionInference::selectionModeLabel(
				ofxGgmlImageSelectionMode::Rerank)) == "Rerank");
		REQUIRE(std::string(
			ofxGgmlDiffusionInference::selectionModeLabel(
				ofxGgmlImageSelectionMode::BestOnly)) == "Best Only");
	}

	SECTION("Default request exposes ranking and instruct fields") {
		ofxGgmlImageGenerationRequest request;

		REQUIRE(request.task == ofxGgmlImageGenerationTask::TextToImage);
		REQUIRE(request.selectionMode == ofxGgmlImageSelectionMode::KeepOrder);
		REQUIRE(request.instruction.empty());
		REQUIRE(request.rankingPrompt.empty());
		REQUIRE(request.normalizeClipEmbeddings);
	}

	SECTION("Generated image metadata supports selection and scores") {
		ofxGgmlGeneratedImage image;

		REQUIRE(image.sourceIndex == 0);
		REQUIRE_FALSE(image.selected);
		REQUIRE(image.score == 0.0f);
		REQUIRE(image.scorer.empty());
		REQUIRE(image.scoreSummary.empty());
	}

	SECTION("Default profiles advertise structured image-task support") {
		const auto profiles = ofxGgmlDiffusionInference::defaultProfiles();

		REQUIRE_FALSE(profiles.empty());
		for (const auto & profile : profiles) {
			REQUIRE(profile.supportsInstructImage);
			REQUIRE(profile.supportsVariation);
			REQUIRE(profile.supportsRestyle);
		}
	}
}

TEST_CASE("Source-aware prompt building", "[inference]") {
	SECTION("Web sources are normalized into cleaner prompt context") {
		ofxGgmlPromptSourceSettings sourceSettings;
		sourceSettings.maxSources = 2;
		sourceSettings.maxCharsPerSource = 400;
		sourceSettings.maxTotalChars = 400;

		std::vector<ofxGgmlPromptSource> sources = {{
			"Example article",
			"https://example.com/post",
			"<html><body><h1>Headline</h1><p>Hello &amp; world.</p><script>ignore()</script></body></html>",
			true,
			false
		}};
		std::vector<ofxGgmlPromptSource> usedSources;
		const std::string prompt = ofxGgmlInference::buildPromptWithSources(
			"Summarize the source.",
			sources,
			sourceSettings,
			&usedSources);

		REQUIRE(prompt.find("Summarize the source.") != std::string::npos);
		REQUIRE(prompt.find("[Source 1]") != std::string::npos);
		REQUIRE(prompt.find("Example article") != std::string::npos);
		REQUIRE(prompt.find("Hello & world.") != std::string::npos);
		REQUIRE(prompt.find("ignore()") == std::string::npos);
		REQUIRE(prompt.find("<html") == std::string::npos);
		REQUIRE(usedSources.size() == 1);
		REQUIRE(usedSources[0].content.find("Hello & world.") != std::string::npos);
	}

	SECTION("Source limits clip large context deterministically") {
		ofxGgmlPromptSourceSettings sourceSettings;
		sourceSettings.maxSources = 1;
		sourceSettings.maxCharsPerSource = 32;
		sourceSettings.maxTotalChars = 32;

		std::vector<ofxGgmlPromptSource> sources = {{
			"Large source",
			"https://example.com/large",
			"This is a long body that should be clipped before reaching the model.",
			true,
			false
		}};
		std::vector<ofxGgmlPromptSource> usedSources;
		const std::string prompt = ofxGgmlInference::buildPromptWithSources(
			"Answer carefully.",
			sources,
			sourceSettings,
			&usedSources);

		REQUIRE(prompt.find("...[truncated]") != std::string::npos);
		REQUIRE(usedSources.size() == 1);
		REQUIRE(usedSources[0].wasTruncated);
		REQUIRE(usedSources[0].content.size() <= sourceSettings.maxCharsPerSource);
	}
}

TEST_CASE("Inference helper utilities", "[inference]") {
	SECTION("Prompt clamp keeps head and tail when context is tight") {
		const std::string prompt(5000, 'a');
		bool trimmed = false;
		const std::string clamped = ofxGgmlInference::clampPromptToContext(
			prompt,
			256,
			&trimmed);

		REQUIRE(trimmed);
		REQUIRE(clamped.size() < prompt.size());
		REQUIRE(clamped.find("...[context trimmed to fit window]...") != std::string::npos);
		REQUIRE(clamped.rfind("aaaa") != std::string::npos);
	}

	SECTION("Cutoff detection distinguishes complete from incomplete output") {
		REQUIRE_FALSE(ofxGgmlInference::isLikelyCutoffOutput("Finished sentence."));
		REQUIRE(ofxGgmlInference::isLikelyCutoffOutput(
			"This answer stops mid-thought and keeps going without punctuation or closure because the model hit a limit"));
		REQUIRE_FALSE(ofxGgmlInference::isLikelyCutoffOutput(
			"int main() {\n    return 0;\n}\n",
			true));
		REQUIRE(ofxGgmlInference::isLikelyCutoffOutput(
			"void renderFrame() {\n    if (ready) {\n        processFrame(buffer, width, height, stride);\n        updateState(cachedGraph, currentPrompt, pendingTokens",
			true));
	}

	SECTION("Continuation request carries the tail forward explicitly") {
		const std::string request = ofxGgmlInference::buildCutoffContinuationRequest(
			"partial tail");
		REQUIRE(request.find("Continue exactly from where the previous answer stopped.") != std::string::npos);
		REQUIRE(request.find("partial tail") != std::string::npos);
	}

	SECTION("Prompt echo stripping does not eat greeting responses") {
		const std::string cleaned = ofxGgmlInference::sanitizeGeneratedText(
			"hello! How can I assist you today? [end of text]",
			"hello");
		REQUIRE(cleaned == "hello! How can I assist you today?");
	}

	SECTION("Realtime prompt helper is a no-op when disabled") {
		ofxGgmlRealtimeInfoSettings realtimeSettings;
		realtimeSettings.enabled = false;
		realtimeSettings.allowPromptUrlFetch = false;

		std::vector<ofxGgmlPromptSource> usedSources;
		const std::string prompt = "Answer the user.";
		const std::string grounded = ofxGgmlInference::buildPromptWithRealtimeInfo(
			prompt,
			"hello",
			realtimeSettings,
			&usedSources);

		REQUIRE(grounded == prompt);
		REQUIRE(usedSources.empty());
	}
}

TEST_CASE("Embedding result structure", "[inference]") {
	SECTION("Default embedding result") {
		ofxGgmlEmbeddingResult result;

		REQUIRE(result.success == false);
		REQUIRE(result.embedding.empty());
		REQUIRE(result.error.empty());
	}

	SECTION("Result with embedding") {
		ofxGgmlEmbeddingResult result;
		result.success = true;
		result.embedding = {0.1f, 0.2f, 0.3f, 0.4f};

		REQUIRE(result.success == true);
		REQUIRE(result.embedding.size() == 4);
		REQUIRE(result.embedding[0] == 0.1f);
	}
}

TEST_CASE("Inference generation - without executable", "[inference]") {
	ofxGgmlInference inf;

	SECTION("Generate fails gracefully without model") {
		auto result = inf.generate("nonexistent_model.gguf", "test prompt");
		REQUIRE_FALSE(result.success);
		REQUIRE_FALSE(result.error.empty());
	}

	SECTION("Custom settings are accepted") {
		ofxGgmlInferenceSettings settings;
		settings.maxTokens = 10;

		auto result = inf.generate("nonexistent_model.gguf", "test", settings);
		REQUIRE_FALSE(result.success);
	}
}

TEST_CASE("Inference streaming callback receives deltas rather than cumulative text", "[inference]") {
	const std::string modelPath = createDummyModel();
	const std::string completionScript = createExecutableScript(
		"echo Hel\n"
		"echo lo");

	ofxGgmlInference inf;
	inf.setCompletionExecutable(completionScript);

	std::vector<std::string> chunks;
	const auto result = inf.generate(
		modelPath,
		"hello",
		{},
		[&chunks](const std::string & chunk) {
			chunks.push_back(chunk);
			return true;
		});

	REQUIRE(result.success);
	REQUIRE(chunks.size() == 2);
	REQUIRE(trimCopy(chunks[0]) == "Hel");
	REQUIRE(trimCopy(chunks[1]) == "lo");
	const auto resultLines = splitLines(result.text);
	REQUIRE(resultLines.size() == 2);
	REQUIRE(resultLines[0] == "Hel");
	REQUIRE(resultLines[1] == "lo");
}

TEST_CASE("Inference server backend bypasses local model validation", "[inference]") {
	ofxGgmlInference inf;
	ofxGgmlInferenceSettings settings;
	settings.useServerBackend = true;
	settings.serverUrl = "http://127.0.0.1:8080";

	auto result = inf.generate("", "test prompt", settings);

#ifdef OFXGGML_HEADLESS_STUBS
	REQUIRE_FALSE(result.success);
	REQUIRE(result.error.find("server-backed inference requires openFrameworks HTTP runtime") != std::string::npos);
#else
	REQUIRE_FALSE(result.success);
	REQUIRE(result.error.find("server-backed inference") != std::string::npos);
#endif
}

TEST_CASE("Embedding server backend bypasses local model validation", "[inference]") {
	ofxGgmlInference inf;
	ofxGgmlEmbeddingSettings settings;
	settings.useServerBackend = true;
	settings.serverUrl = "http://127.0.0.1:8080";

	auto result = inf.embed("", "test prompt", settings);

#ifdef OFXGGML_HEADLESS_STUBS
	REQUIRE_FALSE(result.success);
	REQUIRE(result.error.find("server-backed embeddings require openFrameworks HTTP runtime") != std::string::npos);
#else
	REQUIRE_FALSE(result.success);
	REQUIRE(result.error.find("server-backed embedding") != std::string::npos);
#endif
}

TEST_CASE("Embedding server backend can fall back to local executable", "[inference]") {
#ifdef OFXGGML_HEADLESS_STUBS
	SUCCEED("Server embedding fallback requires HTTP-enabled runtime.");
#else
	const std::string modelPath = createDummyModel();
	const std::string embeddingScript = createExecutableScript("echo [0.1, 0.2, 0.3]");
	ofxGgmlInference inf;
	inf.setEmbeddingExecutable(embeddingScript);

	ofxGgmlEmbeddingSettings settings;
	settings.useServerBackend = true;
	settings.serverUrl = "http://127.0.0.1:1";
	settings.allowLocalFallback = true;

	const auto result = inf.embed(modelPath, "hello", settings);
	REQUIRE(result.success);
	REQUIRE(result.embedding.size() == 3);
#endif
}

TEST_CASE("Inference nonzero exit handling is consistent", "[inference]") {
	const std::string modelPath = createDummyModel();
	const std::string completionScript = createExecutableScript(R"(
case "${OFXGGML_EXIT_TEST_MODE:-}" in
  nonempty) echo "generated-output"; exit 1 ;;
  sigint-empty) exit 130 ;;
  *) exit 1 ;;
esac
)");
	const std::string embeddingScript = createExecutableScript(R"(
case "${OFXGGML_EXIT_TEST_MODE:-}" in
  nonempty) echo "[0.25, 0.50, 0.75]"; exit 1 ;;
  *) exit 1 ;;
esac
)");

	ofxGgmlInference inf;
	inf.setCompletionExecutable(completionScript);
	inf.setEmbeddingExecutable(embeddingScript);

	SECTION("completion: nonzero with non-empty output still fails") {
		ScopedEnvVar mode("OFXGGML_EXIT_TEST_MODE", "nonempty");
		auto result = inf.generate(modelPath, "hello");
		REQUIRE_FALSE(result.success);
		REQUIRE(result.error.find("generated-output") != std::string::npos);
	}

	SECTION("completion: nonzero with empty output fails") {
		ScopedEnvVar mode("OFXGGML_EXIT_TEST_MODE", "empty");
		auto result = inf.generate(modelPath, "hello");
		REQUIRE_FALSE(result.success);
		REQUIRE(result.error.find("exit code 1") != std::string::npos);
	}

	SECTION("completion: SIGINT (130) with empty output still fails when no text is recoverable") {
		ScopedEnvVar mode("OFXGGML_EXIT_TEST_MODE", "sigint-empty");
		auto result = inf.generate(modelPath, "hello");
		REQUIRE_FALSE(result.success);
		REQUIRE(result.error.find("empty output") != std::string::npos);
	}

	SECTION("embedding: nonzero with non-empty output still fails") {
		ScopedEnvVar mode("OFXGGML_EXIT_TEST_MODE", "nonempty");
		auto result = inf.embed(modelPath, "hello");
		REQUIRE_FALSE(result.success);
		REQUIRE(result.error.find("[0.25, 0.50, 0.75]") != std::string::npos);
	}

	SECTION("embedding: nonzero with empty output fails") {
		ScopedEnvVar mode("OFXGGML_EXIT_TEST_MODE", "empty");
		auto result = inf.embed(modelPath, "hello");
		REQUIRE_FALSE(result.success);
		REQUIRE(result.error.find("exit code 1") != std::string::npos);
	}
}

TEST_CASE("Inference retries empty successful completions with safer flags", "[inference]") {
	const std::string modelPath = createDummyModel();

	SECTION("simple-io empty success retries without simple-io") {
		ofxGgmlInference inf;
		inf.setCompletionExecutable(createCompletionFlagSensitiveExecutable(
			"--simple-io",
			"recovered-output"));

		ofxGgmlInferenceSettings settings;
		settings.autoPromptCache = false;

		auto result = inf.generate(modelPath, "hello", settings);
		REQUIRE(result.success);
		REQUIRE(result.text.find("recovered-output") != std::string::npos);
	}

	SECTION("prompt-cache empty success retries without prompt cache") {
		ofxGgmlInference inf;
		inf.setCompletionExecutable(createCompletionFlagSensitiveExecutable(
			"--prompt-cache",
			"cacheless-output"));

		ofxGgmlInferenceSettings settings;
		settings.autoPromptCache = true;
		settings.promptCachePath.clear();
		settings.simpleIo = false;

		auto result = inf.generate(modelPath, "hello", settings);
		REQUIRE(result.success);
		REQUIRE(result.text.find("cacheless-output") != std::string::npos);
	}
}

TEST_CASE("Inference output cleaning removes runtime noise", "[inference]") {
	const std::string modelPath = createDummyModel();
	const std::string completionScript = createExecutableScript(R"(
echo "ofxGgml [INFO] startup"
echo "warning: no usable GPU found"
echo "Device 0: Fake GPU"
echo "clean-payload"
exit 0
)");

	ofxGgmlInference inf;
	inf.setCompletionExecutable(completionScript);
	auto result = inf.generate(modelPath, "hello");
	REQUIRE(result.success);
	REQUIRE(result.text.find("clean-payload") != std::string::npos);
	REQUIRE(result.text.find("ofxGgml [INFO]") == std::string::npos);
	REQUIRE(result.text.find("warning: no usable GPU found") == std::string::npos);
	REQUIRE(result.text.find("Device 0:") == std::string::npos);
}

TEST_CASE("Source-aware generation returns source metadata", "[inference]") {
	const std::string modelPath = createDummyModel();
	const std::string completionScript = createExecutableScript("echo sourced-answer");

	ofxGgmlInference inf;
	inf.setCompletionExecutable(completionScript);

	ofxGgmlPromptSourceSettings sourceSettings;
	sourceSettings.maxSources = 2;
	std::vector<ofxGgmlPromptSource> sources = {{
		"Source doc",
		"https://example.com/source",
		"Important supporting fact.",
		true,
		false
	}};

	auto result = inf.generateWithSources(
		modelPath,
		"Use the source.",
		sources,
		{},
		sourceSettings);

	REQUIRE(result.success);
	REQUIRE(result.text.find("sourced-answer") != std::string::npos);
	REQUIRE(result.sourcesUsed.size() == 1);
	REQUIRE(result.sourcesUsed[0].label == "Source doc");
	REQUIRE(result.sourcesUsed[0].uri == "https://example.com/source");
}

TEST_CASE("ScriptSource documents can be attached to generation", "[inference]") {
	const auto sourceDir = makeUniqueTestDir("script_source");
	{
		std::ofstream cpp(sourceDir / "context.cpp");
		cpp << "int add(int a, int b) { return a + b; }\n";
	}
	{
		std::ofstream py(sourceDir / "helper.py");
		py << "def greet(name):\n    return f'hello {name}'\n";
	}

	ofxGgmlScriptSource scriptSource;
	REQUIRE(scriptSource.setLocalFolder(sourceDir.string()));

	ofxGgmlPromptSourceSettings sourceSettings;
	sourceSettings.maxSources = 1;
	sourceSettings.maxCharsPerSource = 128;
	const auto collected = ofxGgmlInference::collectScriptSourceDocuments(
		scriptSource,
		sourceSettings);
	REQUIRE(collected.size() == 1);
	REQUIRE_FALSE(collected[0].content.empty());

	const std::string modelPath = createDummyModel();
	const std::string completionScript = createExecutableScript("echo script-source-answer");
	ofxGgmlInference inf;
	inf.setCompletionExecutable(completionScript);

	auto result = inf.generateWithScriptSource(
		modelPath,
		"Review the loaded source.",
		scriptSource,
		{},
		sourceSettings);

	REQUIRE(result.success);
	REQUIRE(result.text.find("script-source-answer") != std::string::npos);
	REQUIRE(result.sourcesUsed.size() == 1);
	REQUIRE_FALSE(result.sourcesUsed[0].label.empty());
	REQUIRE_FALSE(result.sourcesUsed[0].content.empty());
}

TEST_CASE("Inference embedding - without executable", "[inference]") {
	ofxGgmlInference inf;

	SECTION("Embed fails gracefully without model") {
		auto result = inf.embed("nonexistent_model.gguf", "test text");
		REQUIRE_FALSE(result.success);
		REQUIRE_FALSE(result.error.empty());
	}

	SECTION("Custom settings are accepted") {
		ofxGgmlEmbeddingSettings settings;
		settings.normalize = false;

		auto result = inf.embed("nonexistent_model.gguf", "test", settings);
		REQUIRE_FALSE(result.success);
	}
}

TEST_CASE("Token counting handles missing artifacts", "[inference]") {
	ofxGgmlInference inf;
	int tokens = inf.countPromptTokens("nonexistent_model.gguf", "hello world");
	REQUIRE(tokens < 0);
}

TEST_CASE("Tokenization utilities", "[inference]") {
	SECTION("Tokenize simple text") {
		auto tokens = ofxGgmlInference::tokenize("Hello world");
		// Implementation may vary, just check it doesn't crash
		REQUIRE(tokens.size() >= 0);
	}

	SECTION("Tokenize empty string") {
		auto tokens = ofxGgmlInference::tokenize("");
		REQUIRE(tokens.size() == 0);
	}

	SECTION("Detokenize tokens") {
		std::vector<std::string> tokens = {"Hello", " ", "world"};
		std::string text = ofxGgmlInference::detokenize(tokens);
		// Should concatenate
		REQUIRE_FALSE(text.empty());
	}

	SECTION("Detokenize empty vector") {
		std::string text = ofxGgmlInference::detokenize({});
		REQUIRE(text.empty());
	}
}

TEST_CASE("Sampling utilities", "[inference]") {
	SECTION("Sample from logits") {
		std::vector<float> logits = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
		int idx = ofxGgmlInference::sampleFromLogits(logits, 1.0f, 1.0f, 42);

		// Should return valid index
		REQUIRE(idx >= 0);
		REQUIRE(idx < static_cast<int>(logits.size()));
	}

	SECTION("Sample with temperature") {
		std::vector<float> logits = {1.0f, 2.0f, 3.0f};
		int idx = ofxGgmlInference::sampleFromLogits(logits, 0.5f, 1.0f, 123);

		REQUIRE(idx >= 0);
		REQUIRE(idx < 3);
	}

	SECTION("Sample with top-p") {
		std::vector<float> logits = {1.0f, 2.0f, 3.0f, 4.0f};
		int idx = ofxGgmlInference::sampleFromLogits(logits, 1.0f, 0.9f, 456);

		REQUIRE(idx >= 0);
		REQUIRE(idx < 4);
	}

	SECTION("Sample from single logit") {
		std::vector<float> logits = {5.0f};
		int idx = ofxGgmlInference::sampleFromLogits(logits);

		REQUIRE(idx == 0);
	}

	SECTION("Sample from empty logits") {
		std::vector<float> logits = {};
		int idx = ofxGgmlInference::sampleFromLogits(logits);

		// Should handle gracefully (return -1 or 0)
		REQUIRE(idx >= -1);
	}
}

TEST_CASE("Embedding index", "[inference]") {
	ofxGgmlEmbeddingIndex index;

	SECTION("Initially empty") {
		auto results = index.search({1.0f, 0.0f, 0.0f}, 3);
		REQUIRE(results.empty());
	}

	SECTION("Add embeddings") {
		index.add("id1", "text1", {1.0f, 0.0f, 0.0f});
		index.add("id2", "text2", {0.0f, 1.0f, 0.0f});
		index.add("id3", "text3", {0.0f, 0.0f, 1.0f});

		auto results = index.search({1.0f, 0.0f, 0.0f}, 3);
		REQUIRE(results.size() <= 3);
		REQUIRE_FALSE(results.empty());

		// First result should be exact match (id1)
		REQUIRE(results[0].id == "id1");
		REQUIRE(results[0].text == "text1");
		REQUIRE(results[0].score > 0.99f);
	}

	SECTION("Search with limit") {
		for (int i = 0; i < 10; i++) {
			std::vector<float> emb(3, 0.0f);
			emb[i % 3] = 1.0f;
			index.add("id" + std::to_string(i), "text" + std::to_string(i), emb);
		}

		auto results = index.search({1.0f, 0.0f, 0.0f}, 3);
		REQUIRE(results.size() == 3);

		// Results should be sorted by score (highest first)
		if (results.size() >= 2) {
			REQUIRE(results[0].score >= results[1].score);
		}
	}

	SECTION("Clear index") {
		index.add("id1", "text1", {1.0f, 0.0f, 0.0f});
		index.clear();

		auto results = index.search({1.0f, 0.0f, 0.0f}, 3);
		REQUIRE(results.empty());
	}
}

TEST_CASE("Cosine similarity", "[inference]") {
	SECTION("Identical vectors") {
		std::vector<float> a = {1.0f, 2.0f, 3.0f};
		std::vector<float> b = {1.0f, 2.0f, 3.0f};

		float sim = ofxGgmlEmbeddingIndex::cosineSimilarity(a, b);
		REQUIRE(sim > 0.99f);
		REQUIRE(sim <= 1.0f);
	}

	SECTION("Orthogonal vectors") {
		std::vector<float> a = {1.0f, 0.0f, 0.0f};
		std::vector<float> b = {0.0f, 1.0f, 0.0f};

		float sim = ofxGgmlEmbeddingIndex::cosineSimilarity(a, b);
		REQUIRE(sim < 0.01f);
		REQUIRE(sim > -0.01f);
	}

	SECTION("Opposite vectors") {
		std::vector<float> a = {1.0f, 0.0f, 0.0f};
		std::vector<float> b = {-1.0f, 0.0f, 0.0f};

		float sim = ofxGgmlEmbeddingIndex::cosineSimilarity(a, b);
		REQUIRE(sim < -0.99f);
		REQUIRE(sim >= -1.0f);
	}

	SECTION("Empty vectors") {
		std::vector<float> a = {};
		std::vector<float> b = {};

		float sim = ofxGgmlEmbeddingIndex::cosineSimilarity(a, b);
		// Should return 0 or handle gracefully
		REQUIRE(sim == sim); // Not NaN
	}

	SECTION("Mismatched dimensions") {
		std::vector<float> a = {1.0f, 2.0f};
		std::vector<float> b = {1.0f, 2.0f, 3.0f};

		// Should handle gracefully (use minimum dimension)
		float sim = ofxGgmlEmbeddingIndex::cosineSimilarity(a, b);
		REQUIRE(sim == sim); // Not NaN
	}
}

TEST_CASE("Similarity hit structure", "[inference]") {
	SECTION("Default similarity hit") {
		ofxGgmlSimilarityHit hit;

		REQUIRE(hit.id.empty());
		REQUIRE(hit.text.empty());
		REQUIRE(hit.score == 0.0f);
		REQUIRE(hit.index == 0);
	}

	SECTION("Similarity hit with data") {
		ofxGgmlSimilarityHit hit;
		hit.id = "test_id";
		hit.text = "test text";
		hit.score = 0.95f;
		hit.index = 5;

		REQUIRE(hit.id == "test_id");
		REQUIRE(hit.text == "test text");
		REQUIRE(hit.score == 0.95f);
		REQUIRE(hit.index == 5);
	}
}

// Tests with actual executable (require llama CLI tools)
TEST_CASE("Inference with executable", "[inference][requires-executable][!mayfail]") {
	ofxGgmlInference inf;

	// These tests are marked [!mayfail] because they may not work in CI
	// Skip if executables not found
	if (inf.getCompletionExecutable().empty()) {
		WARN("Skipping - llama-completion executable not found");
		return;
	}

	// Skip if no test model
	std::string testModel = "test_data/tiny_model.gguf";
	if (!std::ifstream(testModel).good()) {
		WARN("Skipping - test model not found");
		return;
	}

	SECTION("Generate with minimal settings") {
		ofxGgmlInferenceSettings settings;
		settings.maxTokens = 10;

		auto result = inf.generate(testModel, "Hello", settings);

		// May still fail for various reasons (model incompatible, etc.)
		if (result.success) {
			REQUIRE_FALSE(result.text.empty());
			REQUIRE(result.elapsedMs > 0.0f);
		}
	}
}

// Batch inference tests
TEST_CASE("Batch inference structures", "[inference][batch]") {
	SECTION("BatchRequest construction") {
		ofxGgmlBatchRequest req;
		REQUIRE(req.id.empty());
		REQUIRE(req.prompt.empty());

		ofxGgmlInferenceSettings settings;
		settings.maxTokens = 100;
		ofxGgmlBatchRequest req2("test_id", "test prompt", settings);
		REQUIRE(req2.id == "test_id");
		REQUIRE(req2.prompt == "test prompt");
		REQUIRE(req2.settings.maxTokens == 100);
	}

	SECTION("BatchItemResult structure") {
		ofxGgmlBatchItemResult item;
		REQUIRE(item.id.empty());
		REQUIRE_FALSE(item.result.success);
		REQUIRE(item.batchIndex == 0);
	}

	SECTION("BatchResult structure") {
		ofxGgmlBatchResult batchResult;
		REQUIRE_FALSE(batchResult.success);
		REQUIRE(batchResult.totalElapsedMs == 0.0f);
		REQUIRE(batchResult.results.empty());
		REQUIRE(batchResult.error.empty());
		REQUIRE(batchResult.processedCount == 0);
		REQUIRE(batchResult.failedCount == 0);
	}

	SECTION("BatchSettings structure") {
		ofxGgmlBatchSettings settings;
		REQUIRE(settings.allowParallelProcessing == true);
		REQUIRE(settings.stopOnFirstError == false);
		REQUIRE(settings.maxConcurrentRequests == 4);
		REQUIRE(settings.preferServerBatch == true);
		REQUIRE(settings.fallbackToSequential == true);
	}
}

TEST_CASE("Batch inference API", "[inference][batch]") {
	ofxGgmlInference inf;

	SECTION("Empty batch returns success") {
		std::vector<ofxGgmlBatchRequest> requests;
		auto result = inf.generateBatch("model.gguf", requests);
		REQUIRE(result.success);
		REQUIRE(result.results.empty());
		REQUIRE(result.processedCount == 0);
		REQUIRE(result.failedCount == 0);
	}

	SECTION("Simple batch with multiple prompts") {
		std::vector<std::string> prompts = {"Hello", "World", "Test"};
		ofxGgmlInferenceSettings settings;
		settings.maxTokens = 10;

		auto result = inf.generateBatchSimple("model.gguf", prompts, settings);
		// Will fail without executable, but should have proper structure
		REQUIRE(result.results.size() == 3);
		REQUIRE(result.processedCount + result.failedCount == 3);
	}

	SECTION("Batch requests have proper IDs") {
		std::vector<ofxGgmlBatchRequest> requests;
		requests.emplace_back("req1", "prompt1");
		requests.emplace_back("req2", "prompt2");
		requests.emplace_back("req3", "prompt3");

		auto result = inf.generateBatch("model.gguf", requests);
		REQUIRE(result.results.size() == 3);

		// Check IDs are preserved
		bool foundReq1 = false, foundReq2 = false, foundReq3 = false;
		for (const auto& item : result.results) {
			if (item.id == "req1") foundReq1 = true;
			if (item.id == "req2") foundReq2 = true;
			if (item.id == "req3") foundReq3 = true;
		}
		REQUIRE(foundReq1);
		REQUIRE(foundReq2);
		REQUIRE(foundReq3);
	}

	SECTION("Batch settings control behavior") {
		std::vector<std::string> prompts = {"Test1", "Test2"};
		ofxGgmlBatchSettings batchSettings;
		batchSettings.stopOnFirstError = true;
		batchSettings.maxConcurrentRequests = 1;

		auto result = inf.generateBatchSimple("model.gguf", prompts, {}, batchSettings);
		// Structure should be valid even if execution fails
		REQUIRE(result.totalElapsedMs >= 0.0f);
	}
}

TEST_CASE("Batch embedding API", "[inference][batch]") {
	ofxGgmlInference inf;

	SECTION("Empty batch returns empty results") {
		std::vector<std::string> texts;
		auto results = inf.embedBatch("model.gguf", texts);
		REQUIRE(results.empty());
	}

	SECTION("Multiple texts create multiple results") {
		std::vector<std::string> texts = {"text1", "text2", "text3"};
		auto results = inf.embedBatch("model.gguf", texts);
		REQUIRE(results.size() == 3);
		// Results may fail without executable, but structure is correct
	}
}

TEST_CASE("Batch metrics integration", "[inference][batch][metrics]") {
	auto& metrics = ofxGgmlMetrics::getInstance();
	metrics.reset();

	ofxGgmlInference inf;

	SECTION("Batch metrics are recorded") {
		std::vector<std::string> prompts = {"Test1", "Test2", "Test3"};
		inf.generateBatchSimple("test_model", prompts);

		auto stats = metrics.getBatchStats("test_model");
		REQUIRE(stats.totalBatches == 1);
		REQUIRE(stats.totalRequests == 3);
		REQUIRE(stats.activeBatches == 0);
	}

	SECTION("Multiple batches accumulate metrics") {
		std::vector<std::string> batch1 = {"A", "B"};
		std::vector<std::string> batch2 = {"C", "D", "E"};

		inf.generateBatchSimple("test_model", batch1);
		inf.generateBatchSimple("test_model", batch2);

		auto stats = metrics.getBatchStats("test_model");
		REQUIRE(stats.totalBatches == 2);
		REQUIRE(stats.totalRequests == 5);
	}

	SECTION("Batch stats track timing") {
		std::vector<std::string> prompts = {"Test"};
		auto result = inf.generateBatchSimple("test_model", prompts);

		auto stats = metrics.getBatchStats("test_model");
		REQUIRE(stats.totalBatchTimeMs >= 0.0);
		if (stats.totalBatches > 0) {
			REQUIRE(stats.minBatchTimeMs >= 0.0);
			REQUIRE(stats.maxBatchTimeMs >= stats.minBatchTimeMs);
		}
	}
}
