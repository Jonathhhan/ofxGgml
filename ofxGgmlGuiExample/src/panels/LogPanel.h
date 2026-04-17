#pragma once

#include "ofMain.h"
#include "ofxImGui.h"

#include <deque>
#include <mutex>
#include <string>

// ---------------------------------------------------------------------------
// LogPanel — displays engine log messages with auto-scroll
// ---------------------------------------------------------------------------

class LogPanel {
public:
	void draw(
		bool & showWindow,
		std::deque<std::string> & logMessages,
		std::mutex & logMutex);
};
