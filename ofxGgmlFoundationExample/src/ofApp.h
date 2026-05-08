#pragma once

#include "ofMain.h"
#include "ofxGgmlInference.h"

#include <cstdint>
#include <string>
#include <vector>

class ofApp : public ofBaseApp {
public:
	void setup();
	void update();
	void draw();
	void keyPressed(int key);

private:
	void submitPrompt(const std::string & prompt);

	ofxGgmlSession ai;
	ofxGgmlConversation conversation;
	std::string prompt = "Describe local-first AI in one sentence.";
	std::string reply;
	std::vector<std::string> logLines;
	uint64_t activeTaskId = 0;
};
