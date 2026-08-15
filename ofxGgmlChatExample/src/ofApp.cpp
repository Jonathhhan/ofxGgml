#include "ofApp.h"

#include <cstdlib>
#include <utility>

namespace {

std::string configuredServerUrl() {
	const char * value = std::getenv("OFXGGML_SERVER_URL");
	return value && *value ? value : "http://127.0.0.1:8080";
}

} // namespace

ofApp::ofApp()
	: server(configuredServerUrl())
	, chat(server) {
}

ofApp::~ofApp() {
	finishWorker();
}

void ofApp::setup() {
	ofSetWindowTitle("ofxGgml V2 Chat");
	ofSetBackgroundColor(20);
	chat.setSystemPrompt("Be concise and explicit when you are uncertain.");
	status = "Press I to inspect " + server.getBaseUrl();
}

void ofApp::update() {
	if (!finished.exchange(false)) {
		return;
	}
	finishWorker();
	busy = false;
	std::lock_guard<std::mutex> lock(resultMutex);
	output = std::move(pendingOutput);
	status = std::move(pendingStatus);
}

void ofApp::draw() {
	ofSetColor(240);
	ofDrawBitmapString("ofxGgml V2", 30, 40);
	ofDrawBitmapString(status, 30, 75);
	ofDrawBitmapString("I: inspect   Enter: send   C: clear", 30, 105);
	ofDrawBitmapString("Message: " + input + (busy ? "  [busy]" : ""), 30, 150);
	ofDrawBitmapString("Response:\n" + output, 30, 205);
}

void ofApp::keyPressed(int key) {
	if (key == 'i' || key == 'I') {
		inspectServer();
		return;
	}
	if (key == 'c' || key == 'C') {
		if (!busy) {
			chat.clear();
			output.clear();
			status = "Conversation cleared";
		}
		return;
	}
	if (key == OF_KEY_BACKSPACE) {
		if (!input.empty() && !busy) {
			input.pop_back();
		}
		return;
	}
	if (key == OF_KEY_RETURN) {
		sendMessage();
		return;
	}
	if (key >= 32 && key <= 126 && !busy) {
		input.push_back(static_cast<char>(key));
	}
}

void ofApp::exit() {
	finishWorker();
}

void ofApp::inspectServer() {
	if (busy.exchange(true)) {
		return;
	}
	status = "Inspecting server...";
	const std::string currentOutput = output;
	worker = std::thread([this, currentOutput]() {
		const auto inspection = server.inspect();
		std::lock_guard<std::mutex> lock(resultMutex);
		if (!inspection) {
			pendingStatus = "Inspection failed: " + inspection.error;
		} else if (inspection.models.empty()) {
			pendingStatus = "Server reachable; no model id advertised";
		} else {
			ofxGgml::ChatOptions options = chat.getOptions();
			options.model = inspection.models.front();
			chat.setOptions(options);
			pendingStatus = "Server ready; model: " + options.model;
		}
		pendingOutput = currentOutput;
		finished = true;
	});
}

void ofApp::sendMessage() {
	if (input.empty() || busy.exchange(true)) {
		return;
	}
	const std::string message = std::move(input);
	input.clear();
	status = "Waiting for model...";
	worker = std::thread([this, message]() {
		const auto result = chat.send(message);
		std::lock_guard<std::mutex> lock(resultMutex);
		pendingOutput = result.text;
		pendingStatus = result
			? "Completed in " + ofToString(result.elapsedMs, 1) + " ms"
			: "Request failed: " + result.error;
		finished = true;
	});
}

void ofApp::finishWorker() {
	if (worker.joinable()) {
		worker.join();
	}
}
