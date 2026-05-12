#pragma once

#include "ofMain.h"
#include "ofxGgmlText.h"
#include "ofxImGui.h"

#include <array>
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
	struct ChatEntry {
		ofxGgmlTextRole role = ofxGgmlTextRole::User;
		std::string content;
	};

	void sendPrompt();
	void requestCancel();
	void clearChat();
	void runChatWorker();
	void configureGenerator();
	void appendAssistantText(const std::string & text);
	void refreshModelChoices();
	static std::string envValue(const char * name);
	static bool isTruthyEnvValue(const char * name, bool defaultValue = false);
	static void autoConfigureTextBackend(ofxGgmlTextGenerationSettings & settings, std::string & modelPath);
	static std::string normalizeEnvPath(const std::string & path);
	static bool fileExists(const std::string & path);
	static std::string trimCopy(const std::string & value);
	static const char * roleName(ofxGgmlTextRole role);

	ofxGgmlTextGenerator generator;
	ofxGgmlTextGenerationSettings settings;
	ofxImGui::Gui gui;
	std::string modelPath;
	std::string status;
	std::vector<ChatEntry> chat;
	std::vector<std::string> modelChoices;
	std::thread worker;
	std::mutex stateMutex;
	std::atomic_bool cancelRequested { false };
	std::array<char, 4096> promptBuffer {};
	std::array<char, 1024> systemBuffer {};
	std::size_t pendingAssistantIndex = 0;
	int selectedModelIndex = -1;
	bool running = false;
	bool scrollToBottom = true;
	bool strictTextModel = false;
	bool textModelWarningLogged = false;
};
