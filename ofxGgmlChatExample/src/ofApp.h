#pragma once

#include "ofMain.h"
#include "ofxGgml.h"
#include "ofxImGui.h"

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
	void applyConfiguration();
	void selectEndpointProfile(int profileIndex);
	void inspectServer();
	void sendMessage();
	void finishWorker();
	std::string configuredToken() const;
	std::string configuredTokenSource() const;

	ofxImGui::Gui gui;
	ofxGgml::Server server;
	ofxGgml::ChatSession chat;
	ofxGgml::DocumentIndex documents;
	ofxGgml::ToolRegistry tools;
	ofxGgml::ToolLoop toolLoop;
	std::array<char, 512> endpointUrl{};
	std::array<char, 256> modelId{};
	std::array<char, 2048> input{};
	int selectedProfile = 0;
	bool configurationDirty = false;
	bool focusMessageInput = true;
	std::string lastMessage;
	std::string output;
	std::string status;
	std::vector<std::string> availableModels;
	std::thread worker;
	std::mutex resultMutex;
	std::string pendingOutput;
	std::string pendingStatus;
	std::string pendingModelSelection;
	std::vector<std::string> pendingModels;
	std::atomic<bool> busy{ false };
	std::atomic<bool> finished{ false };
};
