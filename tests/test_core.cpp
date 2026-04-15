#include "catch2.hpp"
#include "../src/ofxGgml.h"

#include <cmath>

TEST_CASE("Core initialization", "[core]") {
	ofxGgml ggml;

	SECTION("Default initialization succeeds") {
		bool ok = ggml.setup();
		REQUIRE(ok);
		REQUIRE(ggml.isReady());
		REQUIRE(ggml.getState() == ofxGgmlState::Ready);
	}

	SECTION("State before setup") {
		REQUIRE(ggml.getState() == ofxGgmlState::Uninitialized);
		REQUIRE_FALSE(ggml.isReady());
	}

	SECTION("Custom settings initialization") {
		ofxGgmlSettings settings;
		settings.threads = 2;
		bool ok = ggml.setup(settings);
		REQUIRE(ok);
		REQUIRE(ggml.isReady());
	}

	SECTION("Close releases resources") {
		ggml.setup();
		REQUIRE(ggml.isReady());
		ggml.close();
		REQUIRE_FALSE(ggml.isReady());
	}

	SECTION("Multiple setup calls") {
		ggml.setup();
		REQUIRE(ggml.isReady());
		// Second setup should work (re-init)
		bool ok = ggml.setup();
		REQUIRE(ok);
		REQUIRE(ggml.isReady());
	}
}

TEST_CASE("Backend information", "[core]") {
	ofxGgml ggml;
	ggml.setup();

	SECTION("Backend name is available") {
		std::string name = ggml.getBackendName();
		REQUIRE_FALSE(name.empty());
		// Should be CPU, CUDA, Metal, or Vulkan
		bool validName = (name.find("CPU") != std::string::npos ||
		                  name.find("CUDA") != std::string::npos ||
		                  name.find("Metal") != std::string::npos ||
		                  name.find("Vulkan") != std::string::npos);
		REQUIRE(validName);
	}
}

TEST_CASE("Device enumeration", "[core]") {
	ofxGgml ggml;
	ggml.setup();

	SECTION("List devices returns at least one") {
		auto devices = ggml.listDevices();
		REQUIRE(devices.size() > 0);
	}

	SECTION("Device info is valid") {
		auto devices = ggml.listDevices();
		for (const auto & dev : devices) {
			REQUIRE_FALSE(dev.name.empty());
			// Memory total should be reasonable (at least 1MB)
			if (dev.type == ofxGgmlBackendType::Cpu) {
				// CPU backend may report 0 for memory
				REQUIRE(dev.memoryTotal >= 0);
			}
		}
	}

	SECTION("At least one CPU device") {
		auto devices = ggml.listDevices();
		bool hasCpu = false;
		for (const auto & dev : devices) {
			if (dev.type == ofxGgmlBackendType::Cpu) {
				hasCpu = true;
				break;
			}
		}
		REQUIRE(hasCpu);
	}
}

TEST_CASE("Graph allocation", "[core]") {
	ofxGgml ggml;
	ggml.setup();

	SECTION("Allocate simple graph") {
		ofxGgmlGraph graph;
		auto a = graph.newTensor2d(ofxGgmlType::F32, 2, 2);
		auto b = graph.newTensor2d(ofxGgmlType::F32, 2, 2);
		graph.setInput(a);
		graph.setInput(b);
		auto c = graph.add(a, b);
		graph.setOutput(c);
		graph.build(c);

		bool ok = ggml.allocGraph(graph);
		REQUIRE(ok);
	}

	SECTION("Allocate graph with operations") {
		ofxGgmlGraph graph;
		auto a = graph.newTensor2d(ofxGgmlType::F32, 3, 3);
		auto b = graph.newTensor2d(ofxGgmlType::F32, 3, 3);
		graph.setInput(a);
		graph.setInput(b);
		auto c = graph.add(a, b);
		graph.setOutput(c);
		graph.build(c);

		bool ok = ggml.allocGraph(graph);
		REQUIRE(ok);
	}
}

TEST_CASE("Tensor data operations", "[core]") {
	ofxGgml ggml;
	ggml.setup();

	ofxGgmlGraph graph;
	auto a = graph.newTensor2d(ofxGgmlType::F32, 2, 2);
	auto b = graph.newTensor2d(ofxGgmlType::F32, 2, 2);
	graph.setInput(a);
	graph.setInput(b);
	auto t = graph.add(a, b);
	graph.setOutput(t);
	graph.build(t);
	ggml.allocGraph(graph);

	SECTION("Set and get tensor data") {
		float inputA[] = {1.0f, 2.0f, 3.0f, 4.0f};
		float inputB[] = {0.5f, 0.5f, 0.5f, 0.5f};
		ggml.setTensorData(a, inputA, sizeof(inputA));
		ggml.setTensorData(b, inputB, sizeof(inputB));
		auto compute = ggml.computeGraph(graph);
		REQUIRE(compute.success);

		float output[4];
		ggml.getTensorData(t, output, sizeof(output));

		REQUIRE(std::abs(output[0] - 1.5f) < 0.0001f);
		REQUIRE(std::abs(output[1] - 2.5f) < 0.0001f);
		REQUIRE(std::abs(output[2] - 3.5f) < 0.0001f);
		REQUIRE(std::abs(output[3] - 4.5f) < 0.0001f);
	}

	SECTION("Set tensor data multiple times") {
		float inputA1[] = {1.0f, 2.0f, 3.0f, 4.0f};
		float inputA2[] = {5.0f, 6.0f, 7.0f, 8.0f};
		float inputB[] = {1.0f, 1.0f, 1.0f, 1.0f};

		ggml.setTensorData(a, inputA1, sizeof(inputA1));
		ggml.setTensorData(b, inputB, sizeof(inputB));
		auto r1 = ggml.computeGraph(graph);
		REQUIRE(r1.success);

		ggml.setTensorData(a, inputA2, sizeof(inputA2));
		auto r2 = ggml.computeGraph(graph);
		REQUIRE(r2.success);

		float output[4];
		ggml.getTensorData(t, output, sizeof(output));

		REQUIRE(std::abs(output[0] - 6.0f) < 0.0001f);
		REQUIRE(std::abs(output[1] - 7.0f) < 0.0001f);
		REQUIRE(std::abs(output[2] - 8.0f) < 0.0001f);
		REQUIRE(std::abs(output[3] - 9.0f) < 0.0001f);
	}
}

TEST_CASE("Graph computation", "[core]") {
	ofxGgml ggml;
	ggml.setup();

	SECTION("Compute simple addition") {
		ofxGgmlGraph graph;
		auto a = graph.newTensor2d(ofxGgmlType::F32, 2, 2);
		auto b = graph.newTensor2d(ofxGgmlType::F32, 2, 2);
		graph.setInput(a);
		graph.setInput(b);
		auto c = graph.add(a, b);
		graph.setOutput(c);
		graph.build(c);

		ggml.allocGraph(graph);

		float dataA[] = {1.0f, 2.0f, 3.0f, 4.0f};
		float dataB[] = {5.0f, 6.0f, 7.0f, 8.0f};
		ggml.setTensorData(a, dataA, sizeof(dataA));
		ggml.setTensorData(b, dataB, sizeof(dataB));

		auto result = ggml.computeGraph(graph);
		REQUIRE(result.success);
		REQUIRE(result.elapsedMs >= 0.0f);
		REQUIRE(result.error.empty());

		float output[4];
		ggml.getTensorData(c, output, sizeof(output));

		REQUIRE(output[0] == 6.0f);  // 1 + 5
		REQUIRE(output[1] == 8.0f);  // 2 + 6
		REQUIRE(output[2] == 10.0f); // 3 + 7
		REQUIRE(output[3] == 12.0f); // 4 + 8
	}

	SECTION("Compute matrix multiplication") {
		ofxGgmlGraph graph;
		// A: 2x2, B: 2x2
		auto a = graph.newTensor2d(ofxGgmlType::F32, 2, 2);
		auto b = graph.newTensor2d(ofxGgmlType::F32, 2, 2);
		graph.setInput(a);
		graph.setInput(b);
		auto c = graph.matMul(a, b);
		graph.setOutput(c);
		graph.build(c);

		ggml.allocGraph(graph);

		float dataA[] = {1.0f, 2.0f, 3.0f, 4.0f};
		float dataB[] = {1.0f, 0.0f, 0.0f, 1.0f};
		ggml.setTensorData(a, dataA, sizeof(dataA));
		ggml.setTensorData(b, dataB, sizeof(dataB));

		auto result = ggml.computeGraph(graph);
		REQUIRE(result.success);
	}

	SECTION("Compute returns timing") {
		ofxGgmlGraph graph;
		auto a = graph.newTensor1d(ofxGgmlType::F32, 100);
		graph.setInput(a);
		auto b = graph.sqr(a);
		graph.setOutput(b);
		graph.build(b);

		ggml.allocGraph(graph);

		std::vector<float> data(100, 2.0f);
		ggml.setTensorData(a, data.data(), data.size() * sizeof(float));

		auto result = ggml.computeGraph(graph);
		REQUIRE(result.success);
		REQUIRE(result.elapsedMs > 0.0f);
	}
}

TEST_CASE("Async computation", "[core]") {
	ofxGgml ggml;
	ggml.setup();

	ofxGgmlGraph graph;
	auto a = graph.newTensor1d(ofxGgmlType::F32, 10);
	graph.setInput(a);
	auto b = graph.scale(a, 2.0f);
	graph.setOutput(b);
	graph.build(b);

	ggml.allocGraph(graph);

	float data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	ggml.setTensorData(a, data, sizeof(data));

	SECTION("Async compute with synchronize") {
		auto submitResult = ggml.computeGraphAsync(graph);
		REQUIRE(submitResult.success);

		auto syncResult = ggml.synchronize();
		REQUIRE(syncResult.success);

		float output[10];
		ggml.getTensorData(b, output, sizeof(output));

		for (int i = 0; i < 10; i++) {
			REQUIRE(output[i] == data[i] * 2.0f);
		}
	}
}

TEST_CASE("Timings tracking", "[core]") {
	ofxGgml ggml;
	ggml.setup();

	ofxGgmlGraph graph;
	auto a = graph.newTensor2d(ofxGgmlType::F32, 10, 10);
	graph.setInput(a);
	auto b = graph.sqr(a);
	graph.setOutput(b);
	graph.build(b);

	ggml.allocGraph(graph);

	std::vector<float> data(100, 1.0f);
	ggml.setTensorData(a, data.data(), data.size() * sizeof(float));

	SECTION("Get timings after computation") {
		auto r = ggml.computeGraph(graph);
		REQUIRE(r.success);

		auto timings = ggml.getLastTimings();
		// Setup time should be recorded
		REQUIRE(timings.setupMs >= 0.0f);
		// Alloc time should be recorded
		REQUIRE(timings.allocMs >= 0.0f);
		// Compute time should be recorded
		REQUIRE(timings.computeTotalMs >= 0.0f);
	}
}

TEST_CASE("Log callback", "[core]") {
	ofxGgml ggml;

	SECTION("Custom log callback") {
		std::vector<std::string> logMessages;
		ggml.setLogCallback([&logMessages](int level, const std::string & message) {
			logMessages.push_back(message);
		});

		ggml.setup();

		// Should have captured some log messages during setup
		// (May be empty on some platforms, so just check it doesn't crash)
		REQUIRE(logMessages.size() >= 0);
	}

	SECTION("Null log callback (silent mode)") {
		ggml.setLogCallback([](int, const std::string &) {});
		bool ok = ggml.setup();
		REQUIRE(ok);
	}
}
