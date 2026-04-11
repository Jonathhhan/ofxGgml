#include "ofMain.h"
#include "ofApp.h"

int main() {
	ofGLFWWindowSettings settings;
	settings.setGLVersion(3, 3);
	settings.setSize(1280, 800);
	settings.title = "ofxGgml AI Studio";

	auto window = ofCreateWindow(settings);
	ofRunApp(window, std::make_shared<ofApp>());
	ofRunMainLoop();
}
