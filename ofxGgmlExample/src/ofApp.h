#pragma once

#include "ofMain.h"
#include "ofxGgml.h"

#include <string>
#include <vector>

class ofApp : public ofBaseApp {
public:
	void setup();
	void draw();

	ofxGgml ggml;
	std::vector<std::string> logLines;
};
