#pragma once

#include "ofMain.h"
#include "ofxGgml.h"
#include "core/ofxGgmlHelpers.h"

#include <random>
#include <string>
#include <vector>

class ofApp : public ofBaseApp {
public:
	void setup();
	void draw();
	void keyPressed(int key);

private:
	struct BenchmarkSample {
		int size = 0;
		double avgMs = 0.0;
		double minMs = 0.0;
		double maxMs = 0.0;
		double gflops = 0.0;
	};

	void appendDeviceSummary();
	void runMatrixDemo();
	void runBenchmark();

	ofxGgml ggml;
	std::vector<std::string> infoLines;
	std::vector<std::string> matrixLines;
	std::vector<BenchmarkSample> benchmarkSamples;
	std::mt19937 rng { 1337 };
};
