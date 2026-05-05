#include "catch2.hpp"
#include "../src/ofxGgml.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifndef _WIN32
	#include <sys/stat.h>
#endif

// Benchmark utilities
namespace benchmark {
	struct Result {
		std::string name;
		double avgMs;
		double minMs;
		double maxMs;
		int iterations;
		size_t operationsPerIteration;

		std::string toString() const {
			std::ostringstream ss;
			ss << std::fixed << std::setprecision(3);
			ss << name << ": ";
			ss << avgMs << " ms avg (min: " << minMs << ", max: " << maxMs << ")";
			if (operationsPerIteration > 0) {
				double opsPerSec = (operationsPerIteration * iterations) / (avgMs * iterations / 1000.0);
				ss << ", " << std::setprecision(0) << opsPerSec << " ops/sec";
			}
			return ss.str();
		}
	};

	template<typename F>
	Result measure(const std::string& name, F&& func, int iterations = 10, size_t opsPerIter = 0) {
		Result result;
		result.name = name;
		result.iterations = iterations;
		result.operationsPerIteration = opsPerIter;
		result.minMs = 1e9;
		result.maxMs = 0;
		double totalMs = 0;

		// Warmup
		func();

		for (int i = 0; i < iterations; i++) {
			auto start = std::chrono::high_resolution_clock::now();
			func();
			auto end = std::chrono::high_resolution_clock::now();

			double ms = std::chrono::duration<double, std::milli>(end - start).count();
			totalMs += ms;
			result.minMs = std::min(result.minMs, ms);
			result.maxMs = std::max(result.maxMs, ms);
		}

		result.avgMs = totalMs / iterations;
		return result;
	}
}

namespace {

std::filesystem::path makeBenchmarkTempDir(const std::string & name) {
	const auto base = std::filesystem::temp_directory_path() / "ofxggml_benchmark_tests";
	std::filesystem::create_directories(base);
	static std::atomic<uint64_t> counter{0};
	const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	const auto dir = base / (name + "_" + std::to_string(now) + "_" +
		std::to_string(counter.fetch_add(1, std::memory_order_relaxed)));
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
	std::filesystem::create_directories(dir);
	return dir;
}

std::string createBenchmarkDummyModel() {
	const auto dir = makeBenchmarkTempDir("model");
	const auto model = dir / "dummy.gguf";
	std::ofstream out(model, std::ios::binary);
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

std::string createBenchmarkCompletionExecutable(const std::string & outputLine) {
	const auto dir = makeBenchmarkTempDir("completion");
#ifdef _WIN32
	const auto exe = dir / "benchmark_completion.bat";
	std::ofstream out(exe);
	out << "@echo off\r\n";
	out << "echo " << escapeBatchEcho(outputLine) << "\r\n";
#else
	const auto exe = dir / "benchmark_completion.sh";
	std::ofstream out(exe);
	out << "#!/usr/bin/env bash\n";
	out << "printf '%s\\n' " << std::quoted(outputLine) << "\n";
	out.close();
	chmod(exe.c_str(), 0755);
#endif
	return exe.string();
}

std::string makeRepeatedText(const std::string & seed, size_t repetitions) {
	std::ostringstream out;
	for (size_t i = 0; i < repetitions; ++i) {
		out << seed << " #" << i << "\n";
	}
	return out.str();
}

} // namespace

static void setupBenchmarkRuntime(ofxGgml & ggml) {
	ofxGgmlSettings settings;
	settings.threads = 4;
	auto result = ggml.setup(settings);
	REQUIRE(result.isOk());
}

TEST_CASE("Benchmark: Tensor operations", "[benchmark][!hide]") {
	ofxGgml ggml;
	setupBenchmarkRuntime(ggml);

	SECTION("Element-wise addition") {
		std::vector<benchmark::Result> results;

		for (int size : {100, 1000, 10000}) {
			ofxGgmlGraph graph;
			auto a = graph.newTensor1d(ofxGgmlType::F32, size);
			auto b = graph.newTensor1d(ofxGgmlType::F32, size);
			graph.setInput(a);
			graph.setInput(b);
			auto c = graph.add(a, b);
			graph.setOutput(c);
			graph.build(c);
			auto allocResult = ggml.allocGraph(graph);
	REQUIRE(allocResult.isOk());

			std::vector<float> data(size, 1.0f);
			ggml.setTensorData(a, data.data(), data.size() * sizeof(float));
			ggml.setTensorData(b, data.data(), data.size() * sizeof(float));

			std::string name = "Add " + std::to_string(size) + " elements";
			auto benchResult = benchmark::measure(name, [&]() {
				ggml.computeGraph(graph);
			}, 20, size);

			results.push_back(benchResult);
			INFO(benchResult.toString());
		}

		// All results should complete
		REQUIRE(results.size() == 3);
	}

	SECTION("Scalar operations") {
		std::vector<benchmark::Result> results;

		for (int size : {1000, 10000, 100000}) {
			ofxGgmlGraph graph;
			auto a = graph.newTensor1d(ofxGgmlType::F32, size);
			graph.setInput(a);
			auto b = graph.scale(a, 2.5f);
			graph.setOutput(b);
			graph.build(b);
			auto allocResult = ggml.allocGraph(graph);
	REQUIRE(allocResult.isOk());

			std::vector<float> data(size, 1.0f);
			ggml.setTensorData(a, data.data(), data.size() * sizeof(float));

			std::string name = "Scale " + std::to_string(size) + " elements";
			auto benchResult = benchmark::measure(name, [&]() {
				ggml.computeGraph(graph);
			}, 20, size);

			results.push_back(benchResult);
			INFO(benchResult.toString());
		}

		REQUIRE(results.size() == 3);
	}
}

TEST_CASE("Benchmark: Matrix multiplication", "[benchmark][!hide]") {
	ofxGgml ggml;
	setupBenchmarkRuntime(ggml);

	std::vector<benchmark::Result> results;

	for (int size : {10, 50, 100, 200}) {
		ofxGgmlGraph graph;
		auto a = graph.newTensor2d(ofxGgmlType::F32, size, size);
		auto b = graph.newTensor2d(ofxGgmlType::F32, size, size);
		graph.setInput(a);
		graph.setInput(b);
		auto c = graph.matMul(a, b);
		graph.setOutput(c);
		graph.build(c);
		auto allocResult = ggml.allocGraph(graph);
	REQUIRE(allocResult.isOk());

		std::vector<float> data(size * size, 1.0f);
		ggml.setTensorData(a, data.data(), data.size() * sizeof(float));
		ggml.setTensorData(b, data.data(), data.size() * sizeof(float));

		std::string name = "MatMul " + std::to_string(size) + "x" + std::to_string(size);
		size_t ops = size * size * size * 2; // rough FLOP count

		auto benchResult = benchmark::measure(name, [&]() {
			ggml.computeGraph(graph);
		}, 10, ops);

		results.push_back(benchResult);
		INFO(benchResult.toString());

		// Calculate GFLOPS
		double gflops = (ops / 1e9) / (benchResult.avgMs / 1000.0);
		INFO("  GFLOPS: " << std::fixed << std::setprecision(2) << gflops);
	}

	REQUIRE(results.size() == 4);
}

TEST_CASE("Benchmark: Activation functions", "[benchmark][!hide]") {
	ofxGgml ggml;
	setupBenchmarkRuntime(ggml);

	int size = 10000;
	std::vector<benchmark::Result> results;

	auto benchmarkActivation = [&](const std::string& name, auto activationFunc) {
		ofxGgmlGraph graph;
		auto input = graph.newTensor1d(ofxGgmlType::F32, size);
		graph.setInput(input);
		auto output = activationFunc(graph, input);
		graph.setOutput(output);
		graph.build(output);
		auto allocResult = ggml.allocGraph(graph);
	REQUIRE(allocResult.isOk());

		std::vector<float> data(size, 0.5f);
		ggml.setTensorData(input, data.data(), data.size() * sizeof(float));

		auto benchResult = benchmark::measure(name, [&]() {
			ggml.computeGraph(graph);
		}, 20, size);

		results.push_back(benchResult);
		INFO(benchResult.toString());
	};

	benchmarkActivation("ReLU", [](ofxGgmlGraph& g, ofxGgmlTensor t) { return g.relu(t); });
	benchmarkActivation("GELU", [](ofxGgmlGraph& g, ofxGgmlTensor t) { return g.gelu(t); });
	benchmarkActivation("SiLU", [](ofxGgmlGraph& g, ofxGgmlTensor t) { return g.silu(t); });
	benchmarkActivation("Sigmoid", [](ofxGgmlGraph& g, ofxGgmlTensor t) { return g.sigmoid(t); });
	benchmarkActivation("Tanh", [](ofxGgmlGraph& g, ofxGgmlTensor t) { return g.tanh(t); });

	REQUIRE(results.size() == 5);
}

TEST_CASE("Benchmark: Reduction operations", "[benchmark][!hide]") {
	ofxGgml ggml;
	setupBenchmarkRuntime(ggml);

	std::vector<benchmark::Result> results;

	for (int size : {1000, 10000, 100000}) {
		ofxGgmlGraph graph;
		auto input = graph.newTensor1d(ofxGgmlType::F32, size);
		graph.setInput(input);
		auto output = graph.sum(input);
		graph.setOutput(output);
		graph.build(output);
		auto allocResult = ggml.allocGraph(graph);
	REQUIRE(allocResult.isOk());

		std::vector<float> data(size, 1.0f);
		ggml.setTensorData(input, data.data(), data.size() * sizeof(float));

		std::string name = "Sum " + std::to_string(size) + " elements";
		auto benchResult = benchmark::measure(name, [&]() {
			ggml.computeGraph(graph);
		}, 20, size);

		results.push_back(benchResult);
		INFO(benchResult.toString());
	}

	REQUIRE(results.size() == 3);
}

TEST_CASE("Benchmark: Graph allocation", "[benchmark][!hide][manual]") {
	ofxGgml ggml;
	setupBenchmarkRuntime(ggml);

	std::vector<benchmark::Result> results;

	for (int numTensors : {10, 50, 100}) {
		ofxGgmlGraph graph;

		// Create a chain of operations
		auto prev = graph.newTensor1d(ofxGgmlType::F32, 1000);
		graph.setInput(prev);

		for (int i = 0; i < numTensors; i++) {
			prev = graph.scale(prev, 1.0f);
		}

		graph.setOutput(prev);
		graph.build(prev);

		std::string name = "Allocate graph with " + std::to_string(numTensors) + " operations";

		auto benchResult = benchmark::measure(name, [&]() {
			// Close and reopen to force reallocation
			ggml.close();
			ofxGgmlSettings settings;
			settings.threads = 4;
			auto setupResult = ggml.setup(settings);
		if (!setupResult.isOk()) {
				throw std::runtime_error("benchmark setup failed");
			}
			auto allocResult = ggml.allocGraph(graph);
			if (!allocResult.isOk()) {
				throw std::runtime_error("benchmark graph allocation failed");
			}
		}, 5);

		results.push_back(benchResult);
		INFO(benchResult.toString());
	}

	REQUIRE(results.size() == 3);
}

TEST_CASE("Benchmark: Data transfer", "[benchmark][!hide]") {
	ofxGgml ggml;
	setupBenchmarkRuntime(ggml);

	std::vector<benchmark::Result> results;

	for (int sizeMB : {1, 10, 100}) {
		int numFloats = (sizeMB * 1024 * 1024) / sizeof(float);

		ofxGgmlGraph graph;
		auto tensor = graph.newTensor1d(ofxGgmlType::F32, numFloats);
		graph.setInput(tensor);
		auto output = graph.scale(tensor, 1.0f);
		graph.setOutput(output);
		graph.build(output);
		auto allocResult = ggml.allocGraph(graph);
	REQUIRE(allocResult.isOk());

		std::vector<float> data(numFloats, 1.0f);

		std::string name = "Transfer " + std::to_string(sizeMB) + " MB to device";
		auto benchResult = benchmark::measure(name, [&]() {
			ggml.setTensorData(tensor, data.data(), data.size() * sizeof(float));
		}, 10);

		results.push_back(benchResult);
		INFO(benchResult.toString());

		// Calculate bandwidth
		double bandwidthGBps = (sizeMB / 1024.0) / (benchResult.avgMs / 1000.0);
		INFO("  Bandwidth: " << std::fixed << std::setprecision(2) << bandwidthGBps << " GB/s");
	}

	REQUIRE(results.size() == 3);
}

TEST_CASE("Benchmark: Async vs Sync", "[benchmark][!hide]") {
	ofxGgml ggml;
	setupBenchmarkRuntime(ggml);

	ofxGgmlGraph graph;
	auto a = graph.newTensor1d(ofxGgmlType::F32, 10000);
	graph.setInput(a);
	auto b = graph.sqr(a);
	auto c = graph.sqrt(b);
	graph.setOutput(c);
	graph.build(c);
	auto allocResult = ggml.allocGraph(graph);
	REQUIRE(allocResult.isOk());

	std::vector<float> data(10000, 2.0f);
	ggml.setTensorData(a, data.data(), data.size() * sizeof(float));

	SECTION("Synchronous") {
		auto result = benchmark::measure("Sync compute", [&]() {
			ggml.computeGraph(graph);
		}, 20);

		INFO(result.toString());
		REQUIRE(result.avgMs > 0);
	}

	SECTION("Asynchronous") {
		auto result = benchmark::measure("Async compute", [&]() {
			ggml.computeGraphAsync(graph);
			ggml.synchronize();
		}, 20);

		INFO(result.toString());
		REQUIRE(result.avgMs > 0);
	}
}

TEST_CASE("Benchmark: Backend comparison", "[benchmark][!hide][manual]") {
	// This test is marked [manual] because it requires manual interpretation
	// of results depending on available backends

	ofxGgml ggml;
	auto setupResult = ggml.setup();
	REQUIRE(setupResult.isOk());

	auto devices = ggml.listDevices();
	INFO("Available backends:");
	for (const auto& dev : devices) {
		INFO("  " << dev.name << " (" << dev.description << ")");
	}

	// Run a standard benchmark
	ofxGgmlGraph graph;
	auto a = graph.newTensor2d(ofxGgmlType::F32, 100, 100);
	auto b = graph.newTensor2d(ofxGgmlType::F32, 100, 100);
	graph.setInput(a);
	graph.setInput(b);
	auto c = graph.matMul(a, b);
	graph.setOutput(c);
	graph.build(c);
	auto allocResult = ggml.allocGraph(graph);
	REQUIRE(allocResult.isOk());

	std::vector<float> data(10000, 1.0f);
	ggml.setTensorData(a, data.data(), data.size() * sizeof(float));
	ggml.setTensorData(b, data.data(), data.size() * sizeof(float));

	auto benchResult = benchmark::measure("100x100 MatMul on " + ggml.getBackendName(), [&]() {
		ggml.computeGraph(graph);
	}, 10, 100 * 100 * 100 * 2);

	INFO(benchResult.toString());

	REQUIRE(benchResult.avgMs > 0);
}

TEST_CASE("Benchmark: Memory operations", "[benchmark][!hide]") {
	ofxGgml ggml;
	setupBenchmarkRuntime(ggml);

	std::vector<benchmark::Result> results;

	for (int size : {1000, 10000, 100000}) {
		ofxGgmlGraph graph;
		auto tensor = graph.newTensor1d(ofxGgmlType::F32, size);
		graph.setInput(tensor);
		auto output = graph.scale(tensor, 1.0f);
		graph.setOutput(output);
		graph.build(output);
		auto allocResult = ggml.allocGraph(graph);
	REQUIRE(allocResult.isOk());

		std::vector<float> writeData(size, 1.0f);
		std::vector<float> readData(size);

		// Write benchmark
		std::string writeName = "Write " + std::to_string(size) + " floats";
		auto writeResult = benchmark::measure(writeName, [&]() {
			ggml.setTensorData(tensor, writeData.data(), writeData.size() * sizeof(float));
		}, 20, size);

		results.push_back(writeResult);
		INFO(writeResult.toString());

		// Read benchmark
		std::string readName = "Read " + std::to_string(size) + " floats";
		auto readResult = benchmark::measure(readName, [&]() {
			ggml.getTensorData(tensor, readData.data(), readData.size() * sizeof(float));
		}, 20, size);

		results.push_back(readResult);
		INFO(readResult.toString());
	}

	REQUIRE(results.size() == 6);
}

TEST_CASE("Benchmark: Inference command path", "[benchmark][!hide]") {
	const std::string modelPath = createBenchmarkDummyModel();
	const std::string completionExe =
		createBenchmarkCompletionExecutable("benchmark-generated-output");

	ofxGgmlInference inference;
	inference.setCompletionExecutable(completionExe);

	ofxGgmlInferenceSettings settings;
	settings.autoPromptCache = false;
	settings.simpleIo = true;
	settings.maxTokens = 16;

	auto singleResult = benchmark::measure("Fake CLI single generation", [&]() {
		const auto result = inference.generate(
			modelPath,
			"Summarize the benchmark fixture.",
			settings);
		if (!result.success) {
			throw std::runtime_error(result.error);
		}
	}, 10);
	INFO(singleResult.toString());
	REQUIRE(singleResult.avgMs > 0.0);
}

TEST_CASE("Benchmark: Batch inference scheduling", "[benchmark][!hide]") {
	const std::string modelPath = createBenchmarkDummyModel();
	const std::string completionExe =
		createBenchmarkCompletionExecutable("benchmark-batch-output");

	ofxGgmlInference inference;
	inference.setCompletionExecutable(completionExe);

	ofxGgmlInferenceSettings inferenceSettings;
	inferenceSettings.autoPromptCache = false;
	inferenceSettings.simpleIo = true;
	inferenceSettings.maxTokens = 16;

	ofxGgmlBatchSettings batchSettings;
	batchSettings.allowParallelProcessing = false;
	batchSettings.preferServerBatch = false;
	batchSettings.fallbackToSequential = true;

	const std::vector<std::string> prompts = {
		"Batch prompt 1",
		"Batch prompt 2",
		"Batch prompt 3",
		"Batch prompt 4"
	};

	auto batchResult = benchmark::measure("Fake CLI sequential batch generation", [&]() {
		const auto result = inference.generateBatchSimple(
			modelPath,
			prompts,
			inferenceSettings,
			batchSettings);
		if (!result.success) {
			throw std::runtime_error(result.error);
		}
	}, 5, prompts.size());
	INFO(batchResult.toString());
	REQUIRE(batchResult.avgMs > 0.0);
}

TEST_CASE("Benchmark: Support string-heavy paths", "[benchmark][!hide]") {
	SECTION("Prompt template substitution") {
		const std::string templateText =
			"System: {{system}}\n"
			"Task: {{task}}\n"
			"Context:\n{{context}}\n"
			"Constraints:\n{{constraints}}\n"
			"Output: {{format|plain text}}\n";
		const ofxGgmlPromptTemplates::VariableMap variables = {
			{"system", "You are a local benchmark assistant."},
			{"task", "Rewrite and summarize source-heavy prompt context."},
			{"context", makeRepeatedText("Context paragraph with source tokens", 80)},
			{"constraints", makeRepeatedText("Keep citations stable", 20)}
		};
		auto result = benchmark::measure("Prompt template fillText", [&]() {
			const auto filled = ofxGgmlPromptTemplates::fillText(templateText, variables);
			if (filled.empty()) {
				throw std::runtime_error("prompt template output was empty");
			}
		}, 100);
		INFO(result.toString());
		REQUIRE(result.avgMs > 0.0);
	}

	SECTION("Prompt source assembly") {
		std::vector<ofxGgmlPromptSource> sources;
		for (int i = 0; i < 8; ++i) {
			ofxGgmlPromptSource source;
			source.label = "Source " + std::to_string(i + 1);
			source.uri = "file:///source_" + std::to_string(i + 1) + ".cpp";
			source.content = makeRepeatedText("int value = source_index;", 60);
			sources.push_back(std::move(source));
		}
		ofxGgmlPromptSourceSettings sourceSettings;
		sourceSettings.maxSources = sources.size();
		sourceSettings.maxCharsPerSource = 2000;
		sourceSettings.maxTotalChars = 8000;

		auto result = benchmark::measure("Build prompt with sources", [&]() {
			std::vector<ofxGgmlPromptSource> usedSources;
			const auto prompt = ofxGgmlInference::buildPromptWithSources(
				"Review these source files for performance-sensitive paths.",
				sources,
				sourceSettings,
				&usedSources);
			if (prompt.empty() || usedSources.empty()) {
				throw std::runtime_error("source prompt assembly failed");
			}
		}, 100, sources.size());
		INFO(result.toString());
		REQUIRE(result.avgMs > 0.0);
	}

	SECTION("Project memory clamp and prompt context") {
		auto result = benchmark::measure("Project memory add/build context", [&]() {
			ofxGgmlProjectMemory memory;
			memory.setMaxChars(12000);
			for (int i = 0; i < 48; ++i) {
				if (!memory.addInteraction(
						"Request " + std::to_string(i) + "\n" +
							makeRepeatedText("Optimize code path", 4),
						"Response " + std::to_string(i) + "\n" +
							makeRepeatedText("Applied careful low-risk change", 4))) {
					throw std::runtime_error("project memory add failed");
				}
			}
			if (memory.buildPromptContext().empty()) {
				throw std::runtime_error("project memory prompt context was empty");
			}
		}, 50);
		INFO(result.toString());
		REQUIRE(result.avgMs > 0.0);
	}

	SECTION("Script source scan and cached load") {
		const auto root = makeBenchmarkTempDir("script_source");
		std::filesystem::create_directories(root / "src");
		for (int i = 0; i < 32; ++i) {
			std::ofstream out(root / "src" / ("file_" + std::to_string(i) + ".cpp"));
			out << makeRepeatedText("int benchmark_symbol() { return 42; }", 8);
		}

		auto result = benchmark::measure("ScriptSource local scan and cached load", [&]() {
			ofxGgmlScriptSource source;
			if (!source.setLocalFolder(root.string())) {
				throw std::runtime_error("script source scan failed");
			}
			std::string content;
			const auto files = source.getFiles();
			for (int i = 0; i < static_cast<int>(files.size()); ++i) {
				if (!files[static_cast<size_t>(i)].isDirectory &&
					source.loadFileContent(i, content)) {
					break;
				}
			}
			if (content.empty()) {
				throw std::runtime_error("script source load failed");
			}
		}, 25, 32);
		INFO(result.toString());
		REQUIRE(result.avgMs > 0.0);
	}
}
