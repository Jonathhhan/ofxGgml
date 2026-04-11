#pragma once

#include "ofMain.h"
#include "ofxGgml.h"

#include <string>
#include <vector>

/// Demonstrates a single hidden-layer neural network (4 → 3 → 2) evaluated
/// with ofxGgml.  The weights are random, so the output is not meaningful —
/// the example shows how to wire up a feedforward pass.
class ofApp : public ofBaseApp {
public:
	void setup();
	void draw();
	void keyPressed(int key);

	ofxGgml ggml;
	std::vector<std::string> logLines;
	std::vector<float> outputValues;

	void runInference();
};
