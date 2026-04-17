#include "ofApp.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <random>
#include <sstream>

void ofApp::setup() {
	ofSetWindowTitle("ofxGgml - Basic Example");
	ofBackground(18, 22, 28);
	ofSetFrameRate(60);

	ofxGgmlSettings settings;
	settings.threads = 4;
	auto result = ggml.setup(settings);
	if (!result.isOk()) {
		infoLines.push_back("ERROR: failed to initialize ggml - " + result.error().message);
		return;
	}

	appendDeviceSummary();
	runMatrixDemo();
	runBenchmark();
}

void ofApp::appendDeviceSummary() {
	infoLines.clear();
	infoLines.push_back("Backend: " + ggml.getBackendName());
	infoLines.push_back("Controls: SPACE reruns matrix demo, B reruns benchmark.");

	const auto devices = ggml.listDevices();
	if (devices.empty()) {
		infoLines.push_back("Devices: no backend devices reported by ggml.");
		return;
	}

	for (const auto & device : devices) {
		std::string line = "Device: " + device.name + " (" + device.description + ")";
		if (device.memoryTotal > 0) {
			line += "  " + ofxGgmlHelpers::formatBytes(device.memoryTotal);
		}
		infoLines.push_back(line);
	}
}

void ofApp::runMatrixDemo() {
	matrixLines.clear();

	const int rowsA = 4;
	const int colsA = 2;
	const int rowsB = 3;
	const int colsB = 2;

	std::uniform_real_distribution<float> dist(-2.5f, 2.5f);
	std::vector<float> matA(static_cast<size_t>(rowsA * colsA));
	std::vector<float> matB(static_cast<size_t>(rowsB * colsB));
	for (float & value : matA) {
		value = dist(rng);
	}
	for (float & value : matB) {
		value = dist(rng);
	}

	ofxGgmlGraph graph;
	auto a = graph.newTensor2d(ofxGgmlType::F32, colsA, rowsA);
	auto b = graph.newTensor2d(ofxGgmlType::F32, colsB, rowsB);
	a.setName("A");
	b.setName("B");
	graph.setInput(a);
	graph.setInput(b);

	auto resultTensor = graph.matMul(a, b);
	resultTensor.setName("result");
	graph.setOutput(resultTensor);
	graph.build(resultTensor);

	if (!ggml.allocGraph(graph).isOk()) {
		matrixLines.push_back("Matrix demo failed: graph allocation failed.");
		return;
	}

	ggml.setTensorData(a, matA.data(), matA.size() * sizeof(float));
	ggml.setTensorData(b, matB.data(), matB.size() * sizeof(float));

	const auto result = ggml.computeGraph(graph);
	if (!result.success) {
		matrixLines.push_back("Matrix demo failed: " + result.error);
		return;
	}

	std::vector<float> output(static_cast<size_t>(resultTensor.getNumElements()));
	ggml.getTensorData(resultTensor, output.data(), output.size() * sizeof(float));

	const int outCols = static_cast<int>(resultTensor.getDimSize(0));
	const int outRows = static_cast<int>(resultTensor.getDimSize(1));
	matrixLines.push_back("Matrix demo latency: " + ofxGgmlHelpers::formatDurationMs(result.elapsedMs));
	matrixLines.push_back("Result shape: " + ofToString(outRows) + " x " + ofToString(outCols));

	for (int row = 0; row < outRows; ++row) {
		std::ostringstream line;
		line << "[";
		for (int col = 0; col < outCols; ++col) {
			if (col > 0) {
				line << ", ";
			}
			line << ofToString(output[static_cast<size_t>(row * outCols + col)], 2);
		}
		line << "]";
		matrixLines.push_back(line.str());
	}
}

void ofApp::runBenchmark() {
	benchmarkSamples.clear();

	for (int size : {64, 128, 256}) {
		ofxGgmlGraph graph;
		auto a = graph.newTensor2d(ofxGgmlType::F32, size, size);
		auto b = graph.newTensor2d(ofxGgmlType::F32, size, size);
		graph.setInput(a);
		graph.setInput(b);
		auto c = graph.matMul(a, b);
		graph.setOutput(c);
		graph.build(c);

		if (!ggml.allocGraph(graph).isOk()) {
			continue;
		}

		std::vector<float> data(static_cast<size_t>(size * size), 1.0f);
		ggml.setTensorData(a, data.data(), data.size() * sizeof(float));
		ggml.setTensorData(b, data.data(), data.size() * sizeof(float));

		ggml.computeGraph(graph);

		double totalMs = 0.0;
		double minMs = std::numeric_limits<double>::max();
		double maxMs = 0.0;
		const int iterations = 8;

		for (int i = 0; i < iterations; ++i) {
			const auto result = ggml.computeGraph(graph);
			if (!result.success) {
				minMs = 0.0;
				maxMs = 0.0;
				totalMs = 0.0;
				break;
			}

			const double measuredMs = result.elapsedMs;
			totalMs += measuredMs;
			minMs = std::min(minMs, measuredMs);
			maxMs = std::max(maxMs, measuredMs);
		}

		if (totalMs <= 0.0) {
			continue;
		}

		const double avgMs = totalMs / static_cast<double>(iterations);
		const double flopsPerSecond =
			(2.0 * static_cast<double>(size) * static_cast<double>(size) * static_cast<double>(size)) /
			(avgMs / 1000.0);

		benchmarkSamples.push_back({ size, avgMs, minMs, maxMs, flopsPerSecond / 1e9 });
	}
}

void ofApp::draw() {
	ofBackgroundGradient(ofColor(16, 20, 26), ofColor(8, 10, 14), OF_GRADIENT_CIRCULAR);

	ofSetColor(245);
	ofDrawBitmapStringHighlight("ofxGgml Basic Example", 24, 28);

	float y = 60.0f;
	for (const auto & line : infoLines) {
		ofSetColor(220);
		ofDrawBitmapString(line, 24, y);
		y += 18.0f;
	}

	ofSetColor(32, 40, 52, 220);
	ofDrawRectangle(20, 150, 420, 180);
	ofDrawRectangle(460, 150, 460, 220);

	ofSetColor(240);
	ofDrawBitmapStringHighlight("Latest Matrix Result", 28, 172);
	y = 198.0f;
	for (const auto & line : matrixLines) {
		ofSetColor(225);
		ofDrawBitmapString(line, 28, y);
		y += 18.0f;
	}

	ofSetColor(240);
	ofDrawBitmapStringHighlight("MatMul Benchmark", 468, 172);
	ofSetColor(205);
	ofDrawBitmapString("size      avg          min          max          throughput", 468, 198);

	y = 224.0f;
	for (const auto & sample : benchmarkSamples) {
		std::ostringstream line;
		line << sample.size << "x" << sample.size
			 << "   "
			 << ofxGgmlHelpers::formatDurationMs(sample.avgMs)
			 << "   "
			 << ofxGgmlHelpers::formatDurationMs(sample.minMs)
			 << "   "
			 << ofxGgmlHelpers::formatDurationMs(sample.maxMs)
			 << "   "
			 << ofToString(sample.gflops, 2) << " GFLOP/s";
		ofDrawBitmapString(line.str(), 468, y);
		y += 20.0f;
	}

	ofSetColor(170);
	ofDrawBitmapString(
		"The benchmark reuses an allocated graph so the numbers reflect steady-state compute rather than setup cost.",
		24, 364);
}

void ofApp::keyPressed(int key) {
	if (key == ' ') {
		runMatrixDemo();
	} else if (key == 'b' || key == 'B') {
		runBenchmark();
	}
}
