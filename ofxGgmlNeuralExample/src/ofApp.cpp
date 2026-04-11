#include "ofApp.h"
#include "support/ofxGgmlHelpers.h"

#include <cmath>
#include <random>
#include <sstream>

// Small feedforward network:  input(4) → hidden(3, ReLU) → output(2, softmax)

static constexpr int kInputDim  = 4;
static constexpr int kHiddenDim = 3;
static constexpr int kOutputDim = 2;

static std::vector<float> s_w1;   // kInputDim  x kHiddenDim (col-major)
static std::vector<float> s_b1;   // kHiddenDim
static std::vector<float> s_w2;   // kHiddenDim x kOutputDim (col-major)
static std::vector<float> s_b2;   // kOutputDim
static std::vector<float> s_input; // kInputDim

static void randomizeWeights() {
	std::mt19937 rng(42);
	std::normal_distribution<float> dist(0.0f, 0.5f);

	s_w1.resize(kInputDim * kHiddenDim);
	s_b1.resize(kHiddenDim, 0.0f);
	s_w2.resize(kHiddenDim * kOutputDim);
	s_b2.resize(kOutputDim, 0.0f);
	s_input.resize(kInputDim);

	for (auto & v : s_w1) v = dist(rng);
	for (auto & v : s_b1) v = dist(rng);
	for (auto & v : s_w2) v = dist(rng);
	for (auto & v : s_b2) v = dist(rng);
	for (auto & v : s_input) v = dist(rng);
}

void ofApp::setup() {
	ofSetWindowTitle("ofxGgml — Neural Network");
	ofBackground(30);

	ofxGgmlSettings settings;
	settings.threads = 4;
	if (!ggml.setup(settings)) {
		logLines.push_back("ERROR: failed to initialize ggml");
		return;
	}
	logLines.push_back("Backend: " + ggml.getBackendName());

	randomizeWeights();
	runInference();
}

void ofApp::runInference() {
	// Clear previous output.
	outputValues.clear();
	if (logLines.size() > 2) logLines.resize(2);

	// Build the graph.
	ofxGgmlGraph graph;

	auto input = graph.newTensor2d(ofxGgmlType::F32, kInputDim, 1);
	auto w1    = graph.newTensor2d(ofxGgmlType::F32, kInputDim, kHiddenDim);
	auto b1    = graph.newTensor1d(ofxGgmlType::F32, kHiddenDim);
	auto w2    = graph.newTensor2d(ofxGgmlType::F32, kHiddenDim, kOutputDim);
	auto b2    = graph.newTensor1d(ofxGgmlType::F32, kOutputDim);

	input.setName("input");
	w1.setName("w1");
	b1.setName("b1");
	w2.setName("w2");
	b2.setName("b2");

	graph.setInput(input);
	graph.setInput(w1);
	graph.setInput(b1);
	graph.setInput(w2);
	graph.setInput(b2);

	// Hidden layer: relu(w1 * input + b1)
	auto h = graph.matMul(w1, input);          // [kHiddenDim x 1]
	auto hb = graph.add(h, b1);
	auto hidden = graph.relu(hb);

	// Output layer: softmax(w2 * hidden + b2)
	auto o  = graph.matMul(w2, hidden);        // [kOutputDim x 1]
	auto ob = graph.add(o, b2);
	auto output = graph.softmax(ob);

	output.setName("output");
	graph.setOutput(output);
	graph.build(output);

	// Set tensor data.
	ggml.setTensorData(input, s_input.data(),  s_input.size()  * sizeof(float));
	ggml.setTensorData(w1,    s_w1.data(),     s_w1.size()     * sizeof(float));
	ggml.setTensorData(b1,    s_b1.data(),     s_b1.size()     * sizeof(float));
	ggml.setTensorData(w2,    s_w2.data(),     s_w2.size()     * sizeof(float));
	ggml.setTensorData(b2,    s_b2.data(),     s_b2.size()     * sizeof(float));

	auto r = ggml.compute(graph);

	if (r.success) {
		logLines.push_back("Inference OK (" + ofToString(r.elapsedMs, 2) + " ms)");

		outputValues.resize(kOutputDim);
		ggml.getTensorData(output, outputValues.data(), outputValues.size() * sizeof(float));

		std::ostringstream oss;
		oss << "Input : [";
		for (int i = 0; i < kInputDim; i++) {
			if (i) oss << ", ";
			oss << ofToString(s_input[static_cast<size_t>(i)], 3);
		}
		oss << "]";
		logLines.push_back(oss.str());

		std::ostringstream oss2;
		oss2 << "Output: [";
		for (int i = 0; i < kOutputDim; i++) {
			if (i) oss2 << ", ";
			oss2 << ofToString(outputValues[static_cast<size_t>(i)], 4);
		}
		oss2 << "]";
		logLines.push_back(oss2.str());
	} else {
		logLines.push_back("Inference FAILED: " + r.error);
	}

	logLines.push_back("");
	logLines.push_back("Press SPACE to re-randomize weights and re-run.");
}

void ofApp::draw() {
	// Text log.
	ofSetColor(220);
	float y = 30;
	for (auto & line : logLines) {
		ofDrawBitmapString(line, 20, y);
		y += 16;
	}

	// Visualize output as horizontal bars.
	if (!outputValues.empty()) {
		const float barX = 20;
		const float barY = y + 20;
		const float barMaxW = 400;
		const float barH = 30;
		const float gap = 10;

		for (size_t i = 0; i < outputValues.size(); i++) {
			float w = outputValues[i] * barMaxW;
			float yy = barY + static_cast<float>(i) * (barH + gap);

			ofSetColor(80, 180, 240);
			ofDrawRectangle(barX, yy, w, barH);

			ofSetColor(220);
			ofDrawBitmapString("class " + ofToString(i) + ": " +
				ofToString(outputValues[i], 4), barX + w + 10, yy + 20);
		}
	}
}

void ofApp::keyPressed(int key) {
	if (key == ' ') {
		randomizeWeights();
		runInference();
	}
}
