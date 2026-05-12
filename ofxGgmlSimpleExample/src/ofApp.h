#pragma once

#include "ofMain.h"
#include "ofxGgml.h"
#include "ofxImGui.h"

#include <vector>

class ofApp : public ofBaseApp {
public:
	void setup() override;
	void draw() override;

private:
	void configureRuntime(ofxGgmlBackend backend);
	void runComputation();

	ofxGgml runtime;
	ofxGgmlGraph graph;
	ofxImGui::Gui gui;
	std::vector<std::string> lines;
	int selectedBackendIndex = 0;
	std::string lastComputeTime;
};
