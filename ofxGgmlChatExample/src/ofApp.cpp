#include "ofApp.h"

#include <cstdlib>
#include <fstream>
#include <utility>

namespace {

std::string configuredServerUrl() {
	const char * value = std::getenv("OFXGGML_SERVER_URL");
	return value && *value ? value : "http://127.0.0.1:8080";
}

std::string environmentValue(const char * name) {
	const char * value = std::getenv(name);
	return value && *value ? value : "";
}

void writeAutomationResult(const std::string & status, const std::string & output) {
	const std::string path = environmentValue("OFXGGML_GUI_RESULT_PATH");
	if (path.empty()) return;
	std::ofstream result(path, std::ios::binary | std::ios::trunc);
	if (!result) {
		ofLogError("ofxGgml") << "Could not write GUI result to " << path;
		return;
	}
	result << status << "\n" << output << "\n";
}

} // namespace

ofApp::ofApp()
	: server(configuredServerUrl())
	, chat(server)
	, toolLoop(chat, tools) {
	server.setBearerToken(environmentValue("OFXGGML_API_KEY"));
}

ofApp::~ofApp() {
	finishWorker();
}

void ofApp::setup() {
	ofSetWindowTitle("ofxGgml V2 Document Tool");
	ofSetBackgroundColor(20);
	chat.setSystemPrompt(
		"Use search_documents for questions about the addon. "
		"Ground answers only in returned text and include its citation values.");
	ofxGgml::ChatOptions options;
	options.model = environmentValue("OFXGGML_MODEL");
	chat.setOptions(options);
	documents.addText(
		"v2-architecture.md",
		"ofxGgml V2 keeps llama-server, ggml, CUDA, and model runtimes outside "
		"the addon behind an HTTP process boundary. The addon provides server "
		"access, chat history, explicit document search, and allowlisted tools.");
	tools.addDocumentSearch(documents);
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
	writeAutomationResult(status, output);
}

void ofApp::draw() {
	ofSetColor(240);
	ofDrawBitmapString("ofxGgml V2: document search tool", 30, 40);
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
		} else if (!chat.getOptions().model.empty()) {
			pendingStatus = "Server ready; configured model: " + chat.getOptions().model;
		} else if (inspection.models.empty()) {
			pendingStatus = "Server reachable; set OFXGGML_MODEL";
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
		const auto result = toolLoop.run(message);
		std::lock_guard<std::mutex> lock(resultMutex);
		pendingOutput = result.text;
		pendingStatus = result
			? "Completed with " + ofToString(result.modelRequests) + " model request(s)"
			: "Request failed: " + result.error;
		finished = true;
	});
}

void ofApp::finishWorker() {
	if (worker.joinable()) {
		worker.join();
	}
}
