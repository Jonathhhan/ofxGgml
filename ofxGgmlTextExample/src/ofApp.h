#pragma once

#include "ofMain.h"
#include "ofxGgmlText.h"
#include "ofxImGui.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class ofApp : public ofBaseApp {
public:
	void setup() override;
	void draw() override;
	void keyPressed(int key) override;
	void exit() override;

private:
	void startPrompt();
	void requestCancel();
	void runPromptWorker();
	void rebuildLinesLocked();
	void configureGenerator();
	void refreshModelChoices();
	static std::string envValue(const char * name);
	static bool isTruthyEnvValue(const char * name, bool defaultValue = false);
	static void autoConfigureTextBackend(ofxGgmlTextGenerationSettings & settings, std::string & modelPath);
	static std::string normalizeEnvPath(const std::string & path);
	static bool fileExists(const std::string & path);

	ofxGgmlTextGenerator generator;
	ofxGgmlTextGenerationSettings settings;
	ofxImGui::Gui gui;
	std::string modelPath;
	std::string prompt;
	std::string promptEdit;
	std::string output;
	std::string status;
	std::vector<std::string> lines;
	std::vector<std::string> modelChoices;
	std::thread worker;
	std::mutex stateMutex;
	std::atomic_bool cancelRequested { false };
	int selectedModelIndex = -1;
	bool running = false;
	bool strictTextModel = false;
	bool textModelWarningLogged = false;
};
