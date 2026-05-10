#include "test_harness.h"
#include "../src/ofxGgmlText.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

bool containsPair(
	const std::vector<std::string> & args,
	const std::string & option,
	const std::string & value) {
	for (std::size_t i = 0; i + 1 < args.size(); ++i) {
		if (args[i] == option && args[i + 1] == value) {
			return true;
		}
	}
	return false;
}

} // namespace

OFXGGML_TEST(llama_cli_backend_validates_required_fields) {
	ofxGgmlLlamaCliTextBackend backend(
		[](const ofxGgmlTextCommand &, const ofxGgmlTextChunkCallback &) {
			ofxGgmlTextCommandResult result;
			result.started = true;
			result.exitCode = 0;
			result.output = "unused";
			return result;
		});

	ofxGgmlTextRequest request;
	request.modelPath = "model.gguf";
	request.prompt = "hello";

	OFXGGML_REQUIRE(!backend.generate(request).success);
	request.settings.executablePath = "llama-cli";
	request.modelPath.clear();
	OFXGGML_REQUIRE(!backend.generate(request).success);
	request.modelPath = "model.gguf";
	request.prompt.clear();
	OFXGGML_REQUIRE(!backend.generate(request).success);
}

OFXGGML_TEST(llama_cli_backend_has_default_runner) {
	ofxGgmlLlamaCliTextBackend backend;

	OFXGGML_REQUIRE(backend.hasCommandRunner());
	backend.setCommandRunner({});
	OFXGGML_REQUIRE(backend.hasCommandRunner());
}

OFXGGML_TEST(llama_cli_default_runner_reports_missing_executable) {
	ofxGgmlLlamaCliTextBackend backend;
	ofxGgmlTextRequest request;
	request.settings.executablePath = "__ofxggml_missing_llama_cli_executable__";
	request.modelPath = "model.gguf";
	request.prompt = "hello";

	const auto result = backend.generate(request);

	OFXGGML_REQUIRE(!result.success);
	OFXGGML_REQUIRE(!result.error.empty());
}

OFXGGML_TEST(llama_cli_builds_expected_command) {
	ofxGgmlTextRequest request;
	request.settings.executablePath = "llama-cli";
	request.modelPath = "model.gguf";
	request.prompt = "hello";
	request.settings.maxTokens = 32;
	request.settings.temperature = 0.25f;
	request.settings.topP = 0.9f;
	request.settings.topK = 20;
	request.settings.repeatPenalty = 1.1f;
	request.settings.contextSize = 1024;
	request.settings.batchSize = 128;
	request.settings.gpuLayers = 35;
	request.settings.threads = 8;
	request.settings.seed = 123;
	request.settings.stopSequences = { "</s>" };

	const auto command = ofxGgmlLlamaCliTextBackend::buildCommand(
		request,
		request.prompt);

	OFXGGML_REQUIRE(command.executablePath == "llama-cli");
	OFXGGML_REQUIRE(command.inputText == "hello");
	OFXGGML_REQUIRE(containsPair(command.arguments, "-m", "model.gguf"));
	OFXGGML_REQUIRE(containsPair(command.arguments, "-p", "hello"));
	OFXGGML_REQUIRE(containsPair(command.arguments, "-n", "32"));
	OFXGGML_REQUIRE(containsPair(command.arguments, "--temp", "0.25"));
	OFXGGML_REQUIRE(containsPair(command.arguments, "--top-p", "0.9"));
	OFXGGML_REQUIRE(containsPair(command.arguments, "--top-k", "20"));
	OFXGGML_REQUIRE(containsPair(command.arguments, "--repeat-penalty", "1.1"));
	OFXGGML_REQUIRE(containsPair(command.arguments, "-c", "1024"));
	OFXGGML_REQUIRE(containsPair(command.arguments, "-b", "128"));
	OFXGGML_REQUIRE(containsPair(command.arguments, "-ngl", "35"));
	OFXGGML_REQUIRE(containsPair(command.arguments, "-t", "8"));
	OFXGGML_REQUIRE(containsPair(command.arguments, "--seed", "123"));
	OFXGGML_REQUIRE(containsPair(command.arguments, "--reverse-prompt", "</s>"));
	OFXGGML_REQUIRE(std::find(
		command.arguments.begin(),
		command.arguments.end(),
		"--no-display-prompt") != command.arguments.end());
	OFXGGML_REQUIRE(std::find(
		command.arguments.begin(),
		command.arguments.end(),
		"--log-disable") != command.arguments.end());
	OFXGGML_REQUIRE(std::find(
		command.arguments.begin(),
		command.arguments.end(),
		"--simple-io") != command.arguments.end());
	OFXGGML_REQUIRE(std::find(
		command.arguments.begin(),
		command.arguments.end(),
		"--no-conversation") != command.arguments.end());
	OFXGGML_REQUIRE(containsPair(command.arguments, "--color", "off"));
}

OFXGGML_TEST(llama_cli_backend_runs_injected_runner) {
	ofxGgmlTextCommand capturedCommand;
	ofxGgmlLlamaCliTextBackend backend(
		[&](const ofxGgmlTextCommand & command,
			const ofxGgmlTextChunkCallback & onChunk) {
			capturedCommand = command;
			if (onChunk) {
				OFXGGML_REQUIRE(onChunk("hello"));
			}
			ofxGgmlTextCommandResult result;
			result.started = true;
			result.exitCode = 0;
			result.output = "hello from llama";
			return result;
		});

	ofxGgmlTextRequest request;
	request.settings.executablePath = "llama-cli";
	request.modelPath = "model.gguf";
	request.prompt = "hello";

	std::string streamed;
	const auto result = backend.generate(
		request,
		[&](const std::string & chunk) {
			streamed += chunk;
			return true;
		});

	OFXGGML_REQUIRE(result.success);
	OFXGGML_REQUIRE(result.backendName == "llama.cpp CLI");
	OFXGGML_REQUIRE(result.text == "hello from llama");
	OFXGGML_REQUIRE(result.rawOutput == "hello from llama");
	OFXGGML_REQUIRE(streamed == "hello");
	OFXGGML_REQUIRE(capturedCommand.executablePath == "llama-cli");
	OFXGGML_REQUIRE(containsPair(capturedCommand.arguments, "-m", "model.gguf"));
}

OFXGGML_TEST(llama_cli_backend_strips_startup_noise_from_final_text) {
	ofxGgmlLlamaCliTextBackend backend(
		[](const ofxGgmlTextCommand &,
			const ofxGgmlTextChunkCallback &) {
			ofxGgmlTextCommandResult result;
			result.started = true;
			result.exitCode = 0;
			result.output =
				"ggml_cuda_init: found 1 CUDA devices (Total VRAM: 24575 MiB):\n"
				"  Device 0: NVIDIA GeForce RTX 3090, compute capability 8.6, VRAM: 24575 MiB\n"
				"Loading model...\n"
				"?? ?? ????? ?????\n"
				"Local inference keeps the model and data on your machine.\n"
				"llama_perf_context_print:        load time = 1.00 ms\n";
			return result;
		});

	ofxGgmlTextRequest request;
	request.settings.executablePath = "llama-cli";
	request.modelPath = "model.gguf";
	request.prompt = "hello";

	const auto result = backend.generate(request);

	OFXGGML_REQUIRE(result.success);
	OFXGGML_REQUIRE(result.text == "Local inference keeps the model and data on your machine.");
	OFXGGML_REQUIRE(result.rawOutput.find("ggml_cuda_init") != std::string::npos);
}

OFXGGML_TEST(llama_cli_composes_messages_when_prompt_empty) {
	ofxGgmlTextRequest request;
	request.systemPrompt = "be brief";
	request.messages.push_back({ ofxGgmlTextRole::User, "hello" });
	request.messages.push_back({ ofxGgmlTextRole::Assistant, "hi" });

	const auto prompt = ofxGgmlLlamaCliTextBackend::composePrompt(request);

	OFXGGML_REQUIRE(prompt.find("System: be brief") != std::string::npos);
	OFXGGML_REQUIRE(prompt.find("User: hello") != std::string::npos);
	OFXGGML_REQUIRE(prompt.find("Assistant: hi") != std::string::npos);
}
