#include "ofApp.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>

namespace {
constexpr int kRowsA = 4;
constexpr int kColsA = 2;
constexpr int kRowsB = 3;
constexpr int kColsB = 2;

constexpr float kMargin = 24.0f;
constexpr float kTopY = 28.0f;
constexpr float kInfoStartY = 60.0f;
constexpr float kLineHeight = 18.0f;
constexpr float kPanelY = 150.0f;
constexpr float kMatrixPanelWidth = 500.0f;
constexpr float kMatrixPanelHeight = 320.0f;
constexpr float kBenchmarkPanelX = 544.0f;
constexpr float kBenchmarkPanelWidth = 500.0f;
constexpr float kBenchmarkPanelHeight = 220.0f;
constexpr float kPanelTextInset = 8.0f;

void appendMatrixBlock(std::vector<std::string> & lines,
					   const std::string & label,
					   const std::vector<float> & values,
					   int rows,
					   int cols) {
	lines.push_back(label + " (" + ofToString(rows) + " x " + ofToString(cols) + ")");
	for (int row = 0; row < rows; ++row) {
		std::ostringstream line;
		line << "  [";
		for (int col = 0; col < cols; ++col) {
			if (col > 0) {
				line << ", ";
			}
			line << std::fixed << std::setprecision(2)
				 << values[static_cast<size_t>(row * cols + col)];
		}
		line << "]";
		lines.push_back(line.str());
	}
}
} // namespace

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

	std::uniform_real_distribution<float> dist(-2.5f, 2.5f);
	std::vector<float> matA(static_cast<size_t>(kRowsA * kColsA));
	std::vector<float> matB(static_cast<size_t>(kRowsB * kColsB));
	for (float & value : matA) {
		value = dist(rng);
	}
	for (float & value : matB) {
		value = dist(rng);
	}

	ofxGgmlGraph graph;
	auto a = graph.newTensor2d(ofxGgmlType::F32, kColsA, kRowsA);
	auto b = graph.newTensor2d(ofxGgmlType::F32, kColsB, kRowsB);
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
	matrixLines.push_back("Matrix demo");
	matrixLines.push_back("Latency: " + ofxGgmlHelpers::formatDurationMs(result.elapsedMs));
	matrixLines.push_back("");
	appendMatrixBlock(matrixLines, "Input A", matA, kRowsA, kColsA);
	matrixLines.push_back("");
	appendMatrixBlock(matrixLines, "Input B", matB, kRowsB, kColsB);
	matrixLines.push_back("");
	appendMatrixBlock(matrixLines, "A x B result", output, outRows, outCols);
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
	ofDrawBitmapStringHighlight("ofxGgml Basic Example", kMargin, kTopY);

	float y = kInfoStartY;
	for (const auto & line : infoLines) {
		ofSetColor(220);
		ofDrawBitmapString(line, kMargin, y);
		y += kLineHeight;
	}

	ofSetColor(32, 40, 52, 220);
	ofDrawRectangle(20, kPanelY, kMatrixPanelWidth, kMatrixPanelHeight);
	ofDrawRectangle(kBenchmarkPanelX, kPanelY, kBenchmarkPanelWidth, kBenchmarkPanelHeight);

	ofSetColor(240);
	ofDrawBitmapStringHighlight("Matrix Walkthrough", 20.0f + kPanelTextInset, 172);
	y = 198.0f;
	for (const auto & line : matrixLines) {
		ofSetColor(225);
		ofDrawBitmapString(line, 20.0f + kPanelTextInset, y);
		y += line.empty() ? 12.0f : kLineHeight;
	}

	ofSetColor(240);
	ofDrawBitmapStringHighlight("MatMul Benchmark", kBenchmarkPanelX + kPanelTextInset, 172);
	ofSetColor(205);
	std::ostringstream header;
	header << std::left
		   << std::setw(10) << "size"
		   << std::setw(12) << "avg"
		   << std::setw(12) << "min"
		   << std::setw(12) << "max"
		   << "throughput";
	ofDrawBitmapString(header.str(), kBenchmarkPanelX + kPanelTextInset, 198);

	y = 224.0f;
	for (const auto & sample : benchmarkSamples) {
		std::ostringstream line;
		line << std::left
			 << std::setw(10) << (ofToString(sample.size) + "x" + ofToString(sample.size))
			 << std::setw(12) << ofxGgmlHelpers::formatDurationMs(sample.avgMs)
			 << std::setw(12) << ofxGgmlHelpers::formatDurationMs(sample.minMs)
			 << std::setw(12) << ofxGgmlHelpers::formatDurationMs(sample.maxMs)
			 << ofToString(sample.gflops, 2) << " GFLOP/s";
		ofDrawBitmapString(line.str(), kBenchmarkPanelX + kPanelTextInset, y);
		y += 20.0f;
	}

	ofSetColor(170);
	ofDrawBitmapString(
		"SPACE reruns the matrix demo. B reruns the steady-state benchmark on the current backend.",
		kMargin, 494);
	ofDrawBitmapString(
		"The benchmark reuses one allocated graph so the numbers stay focused on compute instead of setup.",
		kMargin, 514);
}

void ofApp::keyPressed(int key) {
	if (key == ' ') {
		runMatrixDemo();
	} else if (key == 'b' || key == 'B') {
		runBenchmark();
	}
}
