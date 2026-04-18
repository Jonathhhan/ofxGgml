#pragma once

#include "ofMain.h"
#include "ofxGgmlInference.h"

#include <string>

struct ofxGgmlVisionModelProfile;

// Find matching mmproj file in same directory as model
std::string findMatchingMmprojPath(const std::string & modelPath);

// Check vision server capability and provide detailed failure explanation
std::string visionCapabilityFailureDetail(
	const std::string & configuredUrl,
	const std::string & modelPath);

// Prepare vision image for upload (resize/convert if needed)
std::string prepareVisionImageForUpload(
	const std::string & imagePath,
	std::string * note = nullptr);

// Check if vision profile is EU-restricted
bool isEuRestrictedVisionProfile(const ofxGgmlVisionModelProfile & profile);
