#include "ofApp.h"

void ofApp::setup() {
	ofSetWindowTitle("ofxGgml Foundation Example");
	ofBackground(18);
	ofSetFrameRate(60);

	ofxGgmlRuntimeSettings settings;
	settings.threads = 4;
	settings.modelDirectory = "models";

	ofxGgmlSessionSettings sessionSettings;
	sessionSettings.runtime = settings;

	ofxGgmlModelSpec model;
	model.id = "echo";
	model.name = "Deterministic Echo";
	model.role = ofxGgmlModelRole::Text;
	model.description = "A local test backend used while the clean inference API is being built.";
	sessionSettings.models.add(model);
	sessionSettings.defaultTextModelId = "echo";

	if (!ai.setup(sessionSettings)) {
		logLines.push_back("Failed to setup ofxGgml foundation.");
		return;
	}

	conversation.setSystemPrompt("Keep replies short.");

	logLines.push_back("Backend: " + ai.app().runtime().getBackendName());
	logLines.push_back("Models: " + ofToString(ai.models().size()));
	logLines.push_back("Press SPACE to generate. Press 1/2/3 for sample prompts.");
	submitPrompt(prompt);
}

void ofApp::update() {
	ai.update();
	while (auto completed = ai.popCompleted()) {
		if (completed->id != activeTaskId) {
			continue;
		}
		reply = completed->response.ok ? completed->response.text : completed->response.error;
		conversation.addAssistantMessage(reply);
		activeTaskId = 0;
	}
}

void ofApp::draw() {
	ofBackground(20, 22, 26);
	ofSetColor(245);
	ofDrawBitmapStringHighlight("ofxGgml Foundation Example", 24, 32);

	ofSetColor(220);
	float y = 72;
	for (const auto & line : logLines) {
		ofDrawBitmapString(line, 24, y);
		y += 20;
	}

	y += 20;
	ofSetColor(180);
	ofDrawBitmapString("Prompt:", 24, y);
	ofSetColor(245);
	ofDrawBitmapString(prompt, 24, y + 24);

	y += 76;
	ofSetColor(180);
	ofDrawBitmapString("Response:", 24, y);
	ofSetColor(245);
	ofDrawBitmapString(activeTaskId == 0 ? reply : "Working...", 24, y + 24);
}

void ofApp::keyPressed(int key) {
	if (key == ' ') {
		submitPrompt(prompt);
	} else if (key == '1') {
		submitPrompt("Describe local-first AI in one sentence.");
	} else if (key == '2') {
		submitPrompt("Name one good openFrameworks addon design rule.");
	} else if (key == '3') {
		submitPrompt("Keep the first API small and friendly.");
	}
}

void ofApp::submitPrompt(const std::string & nextPrompt) {
	prompt = nextPrompt;
	conversation.addUserMessage(prompt);

	ofxGgmlTextRequest request;
	request.messages = conversation.messages();
	activeTaskId = ai.submit(request);
	reply.clear();
}
