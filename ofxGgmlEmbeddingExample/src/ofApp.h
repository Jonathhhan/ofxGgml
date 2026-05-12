#pragma once

#include "ofMain.h"
#include "ofxGgmlEmbedding.h"
#include "ofxImGui.h"

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
	void startEmbedding();
	void runEmbeddingWorker();
	void configureGenerator();

	static std::string envValue(const char * name);
	static std::string normalizeEnvText(const std::string & text);
	static std::string embeddingPreview(const std::vector<float> & values);
	static bool isTruthyEnvValue(const char * name, bool defaultValue = false);

	ofxGgmlEmbeddingGenerator generator;
	ofxGgmlEmbeddingSettings settings;
	ofxImGui::Gui gui;

	std::string inputA;
	std::string inputB;
	std::string inputAEdit;
	std::string inputBEdit;
	std::string status;
	std::string error;
	std::vector<std::vector<float>> embeddings;
	float similarity = 0.0f;
	bool hasSimilarity = false;

	std::thread worker;
	std::mutex stateMutex;
	bool running = false;
	bool strictEmbeddingModel = false;
	bool embeddingModelWarningLogged = false;
};
