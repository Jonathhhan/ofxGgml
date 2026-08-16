#pragma once

#include "ofMain.h"
#include "ofxGgml.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class ofApp : public ofBaseApp {
public:
	ofApp();
	~ofApp() override;

	void setup() override;
	void update() override;
	void draw() override;
	void keyPressed(int key) override;
	void exit() override;

private:
	void inspectServer();
	void sendMessage();
	void finishWorker();

	ofxGgml::Server server;
	ofxGgml::ChatSession chat;
	ofxGgml::DocumentIndex documents;
	ofxGgml::ToolRegistry tools;
	ofxGgml::ToolLoop toolLoop;
	std::string input;
	std::string output;
	std::string status;
	std::thread worker;
	std::mutex resultMutex;
	std::string pendingOutput;
	std::string pendingStatus;
	std::atomic<bool> busy{ false };
	std::atomic<bool> finished{ false };
};
