#include "catch2.hpp"
#include "../src/ofxGgml.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>

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
			auto result = ggml.allocGraph(graph);
	REQUIRE(result.isOk());

			std::vector<float> data(size, 1.0f);
			ggml.setTensorData(a, data.data(), data.size() * sizeof(float));
			ggml.setTensorData(b, data.data(), data.size() * sizeof(float));

			std::string name = "Add " + std::to_string(size) + " elements";
			auto result = benchmark::measure(name, [&]() {
				ggml.computeGraph(graph);
			}, 20, size);

			results.push_back(result);
			INFO(result.toString());
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
			auto result = ggml.allocGraph(graph);
	REQUIRE(result.isOk());

			std::vector<float> data(size, 1.0f);
			ggml.setTensorData(a, data.data(), data.size() * sizeof(float));

			std::string name = "Scale " + std::to_string(size) + " elements";
			auto result = benchmark::measure(name, [&]() {
				ggml.computeGraph(graph);
			}, 20, size);

			results.push_back(result);
			INFO(result.toString());
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
		auto result = ggml.allocGraph(graph);
	REQUIRE(result.isOk());

		std::vector<float> data(size * size, 1.0f);
		ggml.setTensorData(a, data.data(), data.size() * sizeof(float));
		ggml.setTensorData(b, data.data(), data.size() * sizeof(float));

		std::string name = "MatMul " + std::to_string(size) + "x" + std::to_string(size);
		size_t ops = size * size * size * 2; // rough FLOP count

		auto result = benchmark::measure(name, [&]() {
			ggml.computeGraph(graph);
		}, 10, ops);

		results.push_back(result);
		INFO(result.toString());

		// Calculate GFLOPS
		double gflops = (ops / 1e9) / (result.avgMs / 1000.0);
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
		auto result = ggml.allocGraph(graph);
	REQUIRE(result.isOk());

		std::vector<float> data(size, 0.5f);
		ggml.setTensorData(input, data.data(), data.size() * sizeof(float));

		auto result = benchmark::measure(name, [&]() {
			ggml.computeGraph(graph);
		}, 20, size);

		results.push_back(result);
		INFO(result.toString());
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
		auto result = ggml.allocGraph(graph);
	REQUIRE(result.isOk());

		std::vector<float> data(size, 1.0f);
		ggml.setTensorData(input, data.data(), data.size() * sizeof(float));

		std::string name = "Sum " + std::to_string(size) + " elements";
		auto result = benchmark::measure(name, [&]() {
			ggml.computeGraph(graph);
		}, 20, size);

		results.push_back(result);
		INFO(result.toString());
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

		auto result = benchmark::measure(name, [&]() {
			// Close and reopen to force reallocation
			ggml.close();
			ofxGgmlSettings settings;
			settings.threads = 4;
			auto result = ggml.setup(settings);
		if (!result.isOk()) {
				throw std::runtime_error("benchmark setup failed");
			}
			auto result = ggml.allocGraph(graph);
			if (!result.isOk()) {
				throw std::runtime_error("benchmark graph allocation failed");
			}
		}, 5);

		results.push_back(result);
		INFO(result.toString());
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
		auto result = ggml.allocGraph(graph);
	REQUIRE(result.isOk());

		std::vector<float> data(numFloats, 1.0f);

		std::string name = "Transfer " + std::to_string(sizeMB) + " MB to device";
		auto result = benchmark::measure(name, [&]() {
			ggml.setTensorData(tensor, data.data(), data.size() * sizeof(float));
		}, 10);

		results.push_back(result);
		INFO(result.toString());

		// Calculate bandwidth
		double bandwidthGBps = (sizeMB / 1024.0) / (result.avgMs / 1000.0);
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
	auto result = ggml.allocGraph(graph);
	REQUIRE(result.isOk());

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
	auto result = ggml.setup();
	REQUIRE(result.isOk());

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
	auto result = ggml.allocGraph(graph);
	REQUIRE(result.isOk());

	std::vector<float> data(10000, 1.0f);
	ggml.setTensorData(a, data.data(), data.size() * sizeof(float));
	ggml.setTensorData(b, data.data(), data.size() * sizeof(float));

	auto result = benchmark::measure("100x100 MatMul on " + ggml.getBackendName(), [&]() {
		ggml.computeGraph(graph);
	}, 10, 100 * 100 * 100 * 2);

	INFO(result.toString());

	REQUIRE(result.avgMs > 0);
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
		auto result = ggml.allocGraph(graph);
	REQUIRE(result.isOk());

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
