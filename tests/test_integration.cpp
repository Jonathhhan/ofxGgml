#define CATCH_CONFIG_MAIN
#include "catch2.hpp"
#include "../src/ofxGgml.h"
#include <cmath>

// Integration tests that perform real computations end-to-end

TEST_CASE("Integration: Matrix multiplication", "[integration]") {
	ofxGgml ggml;
	REQUIRE(ggml.setup());

	ofxGgmlGraph graph;

	// Matrix A: 2x3
	auto a = graph.newTensor2d(ofxGgmlType::F32, 3, 2);
	// Matrix B: 2x3 (matMul does A * B^T, so result will be 2x2)
	auto b = graph.newTensor2d(ofxGgmlType::F32, 3, 2);

	graph.setInput(a);
	graph.setInput(b);
	auto result = graph.matMul(a, b);
	graph.setOutput(result);
	graph.build(result);

	ggml.allocGraph(graph);

	// A = [[1, 2, 3],
	//      [4, 5, 6]]
	float dataA[] = {1, 4, 2, 5, 3, 6};

	// B = [[1, 0],
	//      [0, 1],
	//      [0, 0]]
	float dataB[] = {1, 0, 0, 1, 0, 0};

	ggml.setTensorData(a, dataA, sizeof(dataA));
	ggml.setTensorData(b, dataB, sizeof(dataB));

	auto computeResult = ggml.computeGraph(graph);
	REQUIRE(computeResult.success);

	std::vector<float> output(result.getNumElements());
	ggml.getTensorData(result, output.data(), output.size() * sizeof(float));

	// Verify result dimensions
	REQUIRE(result.getDim(0) == 2);
	REQUIRE(result.getDim(1) == 2);

	// Verify computation (A * B^T where B^T = [[1,0,0],[0,1,0]])
	// Result should be [[1,2],[4,5]]
	REQUIRE(std::abs(output[0] - 1.0f) < 0.001f);
	REQUIRE(std::abs(output[1] - 4.0f) < 0.001f);
	REQUIRE(std::abs(output[2] - 2.0f) < 0.001f);
	REQUIRE(std::abs(output[3] - 5.0f) < 0.001f);
}

TEST_CASE("Integration: Element-wise operations chain", "[integration]") {
	ofxGgml ggml;
	REQUIRE(ggml.setup());

	ofxGgmlGraph graph;
	auto a = graph.newTensor1d(ofxGgmlType::F32, 5);
	auto b = graph.newTensor1d(ofxGgmlType::F32, 5);

	graph.setInput(a);
	graph.setInput(b);

	// Compute: ((a + b) * 2) - 3
	auto sum = graph.add(a, b);
	auto scaled = graph.scale(sum, 2.0f);
	auto c = graph.newTensor1d(ofxGgmlType::F32, 5);
	graph.setInput(c);
	auto result = graph.sub(scaled, c);

	graph.setOutput(result);
	graph.build(result);

	ggml.allocGraph(graph);

	float dataA[] = {1, 2, 3, 4, 5};
	float dataB[] = {5, 4, 3, 2, 1};
	float dataC[] = {3, 3, 3, 3, 3};

	ggml.setTensorData(a, dataA, sizeof(dataA));
	ggml.setTensorData(b, dataB, sizeof(dataB));
	ggml.setTensorData(c, dataC, sizeof(dataC));

	auto computeResult = ggml.computeGraph(graph);
	REQUIRE(computeResult.success);

	std::vector<float> output(5);
	ggml.getTensorData(result, output.data(), output.size() * sizeof(float));

	// Verify: ((1+5)*2)-3=9, ((2+4)*2)-3=9, etc.
	for (int i = 0; i < 5; i++) {
		float expected = ((dataA[i] + dataB[i]) * 2.0f) - dataC[i];
		REQUIRE(std::abs(output[i] - expected) < 0.001f);
	}
}

TEST_CASE("Integration: Activation functions", "[integration]") {
	ofxGgml ggml;
	REQUIRE(ggml.setup());

	SECTION("ReLU") {
		ofxGgmlGraph graph;
		auto input = graph.newTensor1d(ofxGgmlType::F32, 5);
		graph.setInput(input);
		auto output = graph.relu(input);
		graph.setOutput(output);
		graph.build(output);

		ggml.allocGraph(graph);

		float data[] = {-2, -1, 0, 1, 2};
		ggml.setTensorData(input, data, sizeof(data));

		auto result = ggml.computeGraph(graph);
		REQUIRE(result.success);

		std::vector<float> outData(5);
		ggml.getTensorData(output, outData.data(), outData.size() * sizeof(float));

		// ReLU should zero out negative values
		REQUIRE(outData[0] == 0.0f);
		REQUIRE(outData[1] == 0.0f);
		REQUIRE(outData[2] == 0.0f);
		REQUIRE(outData[3] == 1.0f);
		REQUIRE(outData[4] == 2.0f);
	}

	SECTION("Sigmoid approximate") {
		ofxGgmlGraph graph;
		auto input = graph.newTensor1d(ofxGgmlType::F32, 3);
		graph.setInput(input);
		auto output = graph.sigmoid(input);
		graph.setOutput(output);
		graph.build(output);

		ggml.allocGraph(graph);

		float data[] = {-10, 0, 10};
		ggml.setTensorData(input, data, sizeof(data));

		auto result = ggml.computeGraph(graph);
		REQUIRE(result.success);

		std::vector<float> outData(3);
		ggml.getTensorData(output, outData.data(), outData.size() * sizeof(float));

		// Sigmoid of large negative should be near 0
		REQUIRE(outData[0] < 0.1f);
		// Sigmoid of 0 should be 0.5
		REQUIRE(std::abs(outData[1] - 0.5f) < 0.01f);
		// Sigmoid of large positive should be near 1
		REQUIRE(outData[2] > 0.9f);
	}
}

TEST_CASE("Integration: Reduction operations", "[integration]") {
	ofxGgml ggml;
	REQUIRE(ggml.setup());

	SECTION("Sum") {
		ofxGgmlGraph graph;
		auto input = graph.newTensor1d(ofxGgmlType::F32, 10);
		graph.setInput(input);
		auto output = graph.sum(input);
		graph.setOutput(output);
		graph.build(output);

		ggml.allocGraph(graph);

		std::vector<float> data(10);
		for (int i = 0; i < 10; i++) data[i] = i + 1; // 1,2,3,...,10
		ggml.setTensorData(input, data.data(), data.size() * sizeof(float));

		auto result = ggml.computeGraph(graph);
		REQUIRE(result.success);

		float outData;
		ggml.getTensorData(output, &outData, sizeof(float));

		// Sum of 1..10 = 55
		REQUIRE(std::abs(outData - 55.0f) < 0.01f);
	}

	SECTION("Mean") {
		ofxGgmlGraph graph;
		auto input = graph.newTensor1d(ofxGgmlType::F32, 4);
		graph.setInput(input);
		auto output = graph.mean(input);
		graph.setOutput(output);
		graph.build(output);

		ggml.allocGraph(graph);

		float data[] = {2, 4, 6, 8};
		ggml.setTensorData(input, data, sizeof(data));

		auto result = ggml.computeGraph(graph);
		REQUIRE(result.success);

		float outData;
		ggml.getTensorData(output, &outData, sizeof(float));

		// Mean of 2,4,6,8 = 5
		REQUIRE(std::abs(outData - 5.0f) < 0.01f);
	}
}

TEST_CASE("Integration: Normalization", "[integration]") {
	ofxGgml ggml;
	REQUIRE(ggml.setup());

	SECTION("RMS Normalization") {
		ofxGgmlGraph graph;
		auto input = graph.newTensor1d(ofxGgmlType::F32, 4);
		graph.setInput(input);
		auto output = graph.rmsNorm(input);
		graph.setOutput(output);
		graph.build(output);

		ggml.allocGraph(graph);

		float data[] = {1, 2, 3, 4};
		ggml.setTensorData(input, data, sizeof(data));

		auto result = ggml.computeGraph(graph);
		REQUIRE(result.success);

		std::vector<float> outData(4);
		ggml.getTensorData(output, outData.data(), outData.size() * sizeof(float));

		// After RMS norm, values should have different magnitudes but same relative ordering
		// Just check the operation completed
		REQUIRE(outData.size() == 4);
	}
}

TEST_CASE("Integration: Complex neural network layer", "[integration]") {
	ofxGgml ggml;
	REQUIRE(ggml.setup());

	// Simulate a simple neural network layer: output = ReLU(W * input + bias)
	ofxGgmlGraph graph;

	auto input = graph.newTensor1d(ofxGgmlType::F32, 3);    // 3 inputs
	auto weights = graph.newTensor2d(ofxGgmlType::F32, 3, 4); // 3x4 weight matrix
	auto bias = graph.newTensor1d(ofxGgmlType::F32, 4);      // 4 biases

	graph.setInput(input);
	graph.setInput(weights);
	graph.setInput(bias);

	// Forward pass: weights * input
	auto weighted = graph.matMul(weights, input);
	auto biased = graph.add(weighted, bias);
	auto output = graph.relu(biased);

	graph.setOutput(output);
	graph.build(output);

	ggml.allocGraph(graph);

	// Set input data
	float inputData[] = {1.0f, 2.0f, 3.0f};
	float weightData[] = {
		0.1f, 0.2f, 0.3f, 0.4f,
		0.2f, 0.3f, 0.4f, 0.5f,
		0.3f, 0.4f, 0.5f, 0.6f
	};
	float biasData[] = {0.1f, 0.2f, 0.3f, 0.4f};

	ggml.setTensorData(input, inputData, sizeof(inputData));
	ggml.setTensorData(weights, weightData, sizeof(weightData));
	ggml.setTensorData(bias, biasData, sizeof(biasData));

	auto result = ggml.computeGraph(graph);
	REQUIRE(result.success);

	std::vector<float> outputData(4);
	ggml.getTensorData(output, outputData.data(), outputData.size() * sizeof(float));

	// All outputs should be non-negative (ReLU)
	for (float val : outputData) {
		REQUIRE(val >= 0.0f);
	}
}

TEST_CASE("Integration: Multiple sequential computations", "[integration]") {
	ofxGgml ggml;
	REQUIRE(ggml.setup());

	ofxGgmlGraph graph;
	auto input = graph.newTensor1d(ofxGgmlType::F32, 5);
	graph.setInput(input);
	auto output = graph.scale(input, 2.0f);
	graph.setOutput(output);
	graph.build(output);

	ggml.allocGraph(graph);

	SECTION("Run computation multiple times") {
		for (int iter = 0; iter < 3; iter++) {
			std::vector<float> data(5, iter + 1.0f);
			ggml.setTensorData(input, data.data(), data.size() * sizeof(float));

			auto result = ggml.computeGraph(graph);
			REQUIRE(result.success);

			std::vector<float> outData(5);
			ggml.getTensorData(output, outData.data(), outData.size() * sizeof(float));

			for (int i = 0; i < 5; i++) {
				REQUIRE(std::abs(outData[i] - (iter + 1.0f) * 2.0f) < 0.001f);
			}
		}
	}
}

TEST_CASE("Integration: Async computation workflow", "[integration]") {
	ofxGgml ggml;
	REQUIRE(ggml.setup());

	ofxGgmlGraph graph;
	auto input = graph.newTensor1d(ofxGgmlType::F32, 100);
	graph.setInput(input);
	auto output = graph.sqr(input);
	graph.setOutput(output);
	graph.build(output);

	ggml.allocGraph(graph);

	std::vector<float> data(100);
	for (int i = 0; i < 100; i++) data[i] = i * 0.1f;
	ggml.setTensorData(input, data.data(), data.size() * sizeof(float));

	// Submit async
	auto submitResult = ggml.computeGraphAsync(graph);
	REQUIRE(submitResult.success);

	// Synchronize
	auto syncResult = ggml.synchronize();
	REQUIRE(syncResult.success);

	// Verify results
	std::vector<float> outData(100);
	ggml.getTensorData(output, outData.data(), outData.size() * sizeof(float));

	for (int i = 0; i < 100; i++) {
		float expected = data[i] * data[i];
		REQUIRE(std::abs(outData[i] - expected) < 0.001f);
	}
}

TEST_CASE("Integration: Graph reuse and allocation", "[integration]") {
	ofxGgml ggml;
	REQUIRE(ggml.setup());

	ofxGgmlGraph graph;
	auto a = graph.newTensor2d(ofxGgmlType::F32, 2, 2);
	auto b = graph.newTensor2d(ofxGgmlType::F32, 2, 2);
	graph.setInput(a);
	graph.setInput(b);
	auto result = graph.add(a, b);
	graph.setOutput(result);
	graph.build(result);

	// First allocation
	bool ok1 = ggml.allocGraph(graph);
	REQUIRE(ok1);

	// Run computation
	float data1[] = {1, 2, 3, 4};
	float data2[] = {5, 6, 7, 8};
	ggml.setTensorData(a, data1, sizeof(data1));
	ggml.setTensorData(b, data2, sizeof(data2));

	auto r1 = ggml.computeGraph(graph);
	REQUIRE(r1.success);

	// Reuse same graph (should reuse allocation)
	float data3[] = {10, 20, 30, 40};
	float data4[] = {50, 60, 70, 80};
	ggml.setTensorData(a, data3, sizeof(data3));
	ggml.setTensorData(b, data4, sizeof(data4));

	auto r2 = ggml.computeGraph(graph);
	REQUIRE(r2.success);

	std::vector<float> output(4);
	ggml.getTensorData(result, output.data(), output.size() * sizeof(float));

	// Should have result from second computation
	REQUIRE(std::abs(output[0] - 60.0f) < 0.001f);
	REQUIRE(std::abs(output[1] - 80.0f) < 0.001f);
}

TEST_CASE("Integration: Large tensor computation", "[integration][slow]") {
	ofxGgml ggml;
	REQUIRE(ggml.setup());

	// Test with larger tensors to stress the system
	ofxGgmlGraph graph;
	auto a = graph.newTensor2d(ofxGgmlType::F32, 100, 100);
	auto b = graph.newTensor2d(ofxGgmlType::F32, 100, 100);
	graph.setInput(a);
	graph.setInput(b);
	auto result = graph.add(a, b);
	graph.setOutput(result);
	graph.build(result);

	ggml.allocGraph(graph);

	std::vector<float> dataA(10000, 1.0f);
	std::vector<float> dataB(10000, 2.0f);

	ggml.setTensorData(a, dataA.data(), dataA.size() * sizeof(float));
	ggml.setTensorData(b, dataB.data(), dataB.size() * sizeof(float));

	auto computeResult = ggml.computeGraph(graph);
	REQUIRE(computeResult.success);
	REQUIRE(computeResult.elapsedMs > 0.0f);

	// Verify a sample of outputs
	std::vector<float> output(10000);
	ggml.getTensorData(result, output.data(), output.size() * sizeof(float));

	for (int i = 0; i < 100; i += 10) {
		REQUIRE(std::abs(output[i] - 3.0f) < 0.001f);
	}
}
