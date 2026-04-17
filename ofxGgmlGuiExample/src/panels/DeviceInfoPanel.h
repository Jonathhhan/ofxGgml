#pragma once

#include "ofMain.h"
#include "ofxGgml.h"
#include "ofxImGui.h"

// ---------------------------------------------------------------------------
// DeviceInfoPanel — displays ggml backend and device information
// ---------------------------------------------------------------------------

class DeviceInfoPanel {
public:
	void draw(
		bool & showWindow,
		ofxGgml & ggml,
		const std::vector<ofxGgmlDeviceInfo> & devices);
};
