#include "ofApp.h"

#include <algorithm>
#include <limits>
#include <random>
#include <sstream>

namespace {
static constexpr int kInputDim = 4;
static constexpr int kHiddenDim = 3;
static constexpr int kOutputDim = 2;
} // namespace

void ofApp::randomizeNetwork() {
	std::normal_distribution<float> dist(0.0f, 0.5f);

	weights1.resize(static_cast<size_t>(kInputDim * kHiddenDim));
	bias1.resize(static_cast<size_t>(kHiddenDim));
	weights2.resize(static_cast<size_t>(kHiddenDim * kOutputDim));
	bias2.resize(static_cast<size_t>(kOutputDim));

	for (float & value : weights1) value = dist(rng);
	for (float & value : bias1) value = dist(rng);
	for (float & value : weights2) value = dist(rng);
	for (float & value : bias2) value = dist(rng);
}

void ofApp::randomizeInput() {
	std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

	inputValues.resize(static_cast<size_t>(kInputDim));
	for (float & value : inputValues) {
		value = dist(rng);
	}
}

void ofApp::setup() {
	ofSetWindowTitle("ofxGgml - Neural Example");
	ofBackground(18, 22, 28);
	ofSetFrameRate(60);

	ofxGgmlSettings settings;
	settings.threads = 4;
	if (!ggml.setup(settings)) {
		logLines.push_back("ERROR: failed to initialize ggml");
		return;
	}

	buildGraph();
	randomizeNetwork();
	randomizeInput();
	runInference();
	runBenchmark();
}

void ofApp::buildGraph() {
	graph.reset();

	inputTensor = graph.newTensor2d(ofxGgmlType::F32, kInputDim, 1);
	w1Tensor = graph.newTensor2d(ofxGgmlType::F32, kInputDim, kHiddenDim);
	b1Tensor = graph.newTensor1d(ofxGgmlType::F32, kHiddenDim);
	w2Tensor = graph.newTensor2d(ofxGgmlType::F32, kHiddenDim, kOutputDim);
	b2Tensor = graph.newTensor1d(ofxGgmlType::F32, kOutputDim);

	inputTensor.setName("input");
	w1Tensor.setName("w1");
	b1Tensor.setName("b1");
	w2Tensor.setName("w2");
	b2Tensor.setName("b2");

	graph.setInput(inputTensor);
	graph.setInput(w1Tensor);
	graph.setInput(b1Tensor);
	graph.setInput(w2Tensor);
	graph.setInput(b2Tensor);

	auto hiddenLinear = graph.matMul(w1Tensor, inputTensor);
	auto hiddenBiased = graph.add(hiddenLinear, b1Tensor);
	auto hidden = graph.relu(hiddenBiased);

	auto outputLinear = graph.matMul(w2Tensor, hidden);
	auto outputBiased = graph.add(outputLinear, b2Tensor);
	outputTensor = graph.softmax(outputBiased);

	outputTensor.setName("output");
	graph.setOutput(outputTensor);
	graph.build(outputTensor);
	graphReady = ggml.allocGraph(graph);
}

void ofApp::runInference() {
	outputValues.clear();

	if (!graphReady) {
		rebuildLogLines(0.0f, "Inference failed: graph allocation failed.");
		return;
	}

	ggml.setTensorData(inputTensor, inputValues.data(), inputValues.size() * sizeof(float));
	ggml.setTensorData(w1Tensor, weights1.data(), weights1.size() * sizeof(float));
	ggml.setTensorData(b1Tensor, bias1.data(), bias1.size() * sizeof(float));
	ggml.setTensorData(w2Tensor, weights2.data(), weights2.size() * sizeof(float));
	ggml.setTensorData(b2Tensor, bias2.data(), bias2.size() * sizeof(float));

	const auto result = ggml.computeGraph(graph);
	if (!result.success) {
		rebuildLogLines(result.elapsedMs, "Inference failed: " + result.error);
		return;
	}

	outputValues.resize(static_cast<size_t>(kOutputDim));
	ggml.getTensorData(outputTensor, outputValues.data(), outputValues.size() * sizeof(float));
	rebuildLogLines(result.elapsedMs, "Inference ready.");
}

void ofApp::runBenchmark() {
	benchmarkLines.clear();
	if (!graphReady) {
		benchmarkLines.push_back("Benchmark unavailable: graph allocation failed.");
		return;
	}

	ggml.setTensorData(inputTensor, inputValues.data(), inputValues.size() * sizeof(float));
	ggml.setTensorData(w1Tensor, weights1.data(), weights1.size() * sizeof(float));
	ggml.setTensorData(b1Tensor, bias1.data(), bias1.size() * sizeof(float));
	ggml.setTensorData(w2Tensor, weights2.data(), weights2.size() * sizeof(float));
	ggml.setTensorData(b2Tensor, bias2.data(), bias2.size() * sizeof(float));

	ggml.computeGraph(graph);

	double totalMs = 0.0;
	double minMs = std::numeric_limits<double>::max();
	double maxMs = 0.0;
	const int iterations = 40;

	for (int i = 0; i < iterations; ++i) {
		const auto result = ggml.computeGraph(graph);
		if (!result.success) {
			benchmarkLines.push_back("Benchmark failed: " + result.error);
			return;
		}

		const double measuredMs = result.elapsedMs;
		totalMs += measuredMs;
		minMs = std::min(minMs, measuredMs);
		maxMs = std::max(maxMs, measuredMs);
	}

	const double avgMs = totalMs / static_cast<double>(iterations);
	const double inferencesPerSecond = 1000.0 / avgMs;
	benchmarkLines.push_back("Benchmark: " + ofToString(iterations) + " repeated inferences");
	benchmarkLines.push_back("Average: " + ofxGgmlHelpers::formatDurationMs(avgMs));
	benchmarkLines.push_back("Min / Max: " + ofxGgmlHelpers::formatDurationMs(minMs) +
		" / " + ofxGgmlHelpers::formatDurationMs(maxMs));
	benchmarkLines.push_back("Throughput: " + ofToString(inferencesPerSecond, 2) + " inf/s");
}

int ofApp::findPredictedClass() const {
	if (outputValues.empty()) {
		return -1;
	}
	return static_cast<int>(std::distance(
		outputValues.begin(),
		std::max_element(outputValues.begin(), outputValues.end())));
}

void ofApp::rebuildLogLines(float elapsedMs, const std::string & status) {
	logLines.clear();
	logLines.push_back("Backend: " + ggml.getBackendName());
	logLines.push_back("Controls: SPACE reruns, I randomizes input, R randomizes network, B reruns benchmark.");
	logLines.push_back(status);

	if (elapsedMs > 0.0f) {
		logLines.push_back("Latency: " + ofxGgmlHelpers::formatDurationMs(elapsedMs));
	}

	std::ostringstream inputLine;
	inputLine << "Input: [";
	for (int i = 0; i < kInputDim; ++i) {
		if (i > 0) {
			inputLine << ", ";
		}
		inputLine << ofToString(inputValues[static_cast<size_t>(i)], 3);
	}
	inputLine << "]";
	logLines.push_back(inputLine.str());

	if (!outputValues.empty()) {
		std::ostringstream outputLine;
		outputLine << "Output: [";
		for (int i = 0; i < kOutputDim; ++i) {
			if (i > 0) {
				outputLine << ", ";
			}
			outputLine << ofToString(outputValues[static_cast<size_t>(i)], 4);
		}
		outputLine << "]";
		logLines.push_back(outputLine.str());

		const int predictedClass = findPredictedClass();
		if (predictedClass >= 0) {
			logLines.push_back(
				"Predicted class: " + ofToString(predictedClass) + " (" +
				ofToString(outputValues[static_cast<size_t>(predictedClass)] * 100.0f, 1) + "%)");
		}
	}
}

void ofApp::draw() {
	ofBackgroundGradient(ofColor(16, 20, 26), ofColor(7, 10, 16), OF_GRADIENT_CIRCULAR);

	ofSetColor(245);
	ofDrawBitmapStringHighlight("ofxGgml Neural Example", 24, 28);

	float y = 60.0f;
	for (const auto & line : logLines) {
		ofSetColor(220);
		ofDrawBitmapString(line, 24, y);
		y += 18.0f;
	}

	ofSetColor(32, 40, 52, 220);
	ofDrawRectangle(20, 180, 430, 160);
	ofDrawRectangle(470, 180, 430, 220);

	ofSetColor(240);
	ofDrawBitmapStringHighlight("Class Scores", 28, 202);
	if (!outputValues.empty()) {
		const float barX = 28.0f;
		const float barY = 226.0f;
		const float barMaxW = 320.0f;
		const float barH = 32.0f;
		const float gap = 14.0f;

		for (size_t i = 0; i < outputValues.size(); ++i) {
			const float width = std::max(0.0f, outputValues[i]) * barMaxW;
			const float yy = barY + static_cast<float>(i) * (barH + gap);
			ofSetColor(70, 150, 240);
			ofDrawRectangle(barX, yy, width, barH);
			ofSetColor(225);
			ofDrawBitmapString(
				"class " + ofToString(i) + ": " + ofToString(outputValues[i], 4),
				barX + barMaxW + 12.0f, yy + 20.0f);
		}
	}

	ofSetColor(240);
	ofDrawBitmapStringHighlight("Steady-State Benchmark", 478, 202);
	y = 232.0f;
	for (const auto & line : benchmarkLines) {
		ofSetColor(220);
		ofDrawBitmapString(line, 478, y);
		y += 20.0f;
	}

	ofSetColor(170);
	ofDrawBitmapString(
		"This example keeps the graph allocated and only refreshes tensor data between runs.",
		24, 372);
}

void ofApp::keyPressed(int key) {
	if (key == ' ') {
		runInference();
	} else if (key == 'i' || key == 'I') {
		randomizeInput();
		runInference();
	} else if (key == 'r' || key == 'R') {
		randomizeNetwork();
		randomizeInput();
		runInference();
		runBenchmark();
	} else if (key == 'b' || key == 'B') {
		runBenchmark();
	}
}
