#include "ofMain.h"
#include "ofApp.h"

int main() {
	ofGLFWWindowSettings settings;
	settings.setGLVersion(3, 3);
	settings.setSize(800, 600);
	settings.title = "ofxGgml Example";
	auto window = ofCreateWindow(settings);
	ofRunApp(window, std::make_shared<ofApp>());
	ofRunMainLoop();
}
