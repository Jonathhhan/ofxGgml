#pragma once

#include "ofMain.h"
#include "ofxGgml.h"
#include "core/ofxGgmlHelpers.h"

#include <random>
#include <string>
#include <vector>

/// Demonstrates a single hidden-layer neural network (4 -> 3 -> 2) evaluated
/// with ofxGgml. The graph stays allocated between runs so repeated inference
/// reflects the steady-state path instead of setup overhead.
class ofApp : public ofBaseApp {
public:
	void setup();
	void draw();
	void keyPressed(int key);

private:
	void buildGraph();
	void randomizeNetwork();
	void randomizeInput();
	void runInference();
	void runBenchmark();
	void rebuildLogLines(float elapsedMs, const std::string & status);
	int findPredictedClass() const;

	ofxGgml ggml;
	ofxGgmlGraph graph;
	ofxGgmlTensor inputTensor;
	ofxGgmlTensor w1Tensor;
	ofxGgmlTensor b1Tensor;
	ofxGgmlTensor w2Tensor;
	ofxGgmlTensor b2Tensor;
	ofxGgmlTensor outputTensor;

	std::vector<std::string> logLines;
	std::vector<std::string> benchmarkLines;
	std::vector<float> outputValues;
	std::vector<float> weights1;
	std::vector<float> bias1;
	std::vector<float> weights2;
	std::vector<float> bias2;
	std::vector<float> inputValues;
	std::mt19937 rng { 42 };
	bool graphReady = false;
};
