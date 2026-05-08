#pragma once

/// Fresh minimal foundation for a smaller, openFrameworks-style ofxGgml API.
///
/// This layer is intentionally small:
/// - setup/close/isReady runtime lifecycle
/// - a swappable text backend interface
/// - a high-level app object that is easy to use from an ofApp
///
/// It does not replace the legacy public headers yet. It gives the rebuild a
/// clean place to grow while existing examples and tests keep working.

#include "foundation/ofxGgmlRuntime.h"
#include "foundation/ofxGgmlTextClient.h"

class ofxGgmlApp {
public:
	bool setup(const ofxGgmlRuntimeSettings & settings = {}) {
		if (!runtime_.setup(settings)) {
			return false;
		}
		return text_.setup();
	}

	void close() {
		text_ = ofxGgmlTextClient();
		runtime_.close();
	}

	bool isReady() const {
		return runtime_.isReady() && text_.isReady();
	}

	ofxGgmlRuntime & runtime() {
		return runtime_;
	}

	const ofxGgmlRuntime & runtime() const {
		return runtime_;
	}

	ofxGgmlTextClient & text() {
		return text_;
	}

	const ofxGgmlTextClient & text() const {
		return text_;
	}

	ofxGgmlTextResponse generate(const std::string & prompt) {
		return text_.generate(prompt);
	}

private:
	ofxGgmlRuntime runtime_;
	ofxGgmlTextClient text_;
};
