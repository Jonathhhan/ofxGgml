#include "ofMain.h"
#include "ofApp.h"
#include "utils/PathHelpers.h"

int main() {
	configureCentralRuntimeSearchPaths();
	ofLogWarning("ofxGgml")
		<< "First run setup required: run the ofxGgml setup script, build bundled ggml, then regenerate this example before building.";

	ofGLFWWindowSettings settings;
	settings.setGLVersion(3, 3);
	settings.setSize(1280, 800);
	settings.title = "ofxGgml GUI Example";

	auto window = ofCreateWindow(settings);
	ofRunApp(window, std::make_shared<ofApp>());
	ofRunMainLoop();
}
