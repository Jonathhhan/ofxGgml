#pragma once

#include <string>

namespace ofxGgmlInferenceTextCleanup {

std::string sanitizeGeneratedText(
	const std::string & raw,
	const std::string & prompt = {});

std::string sanitizeStructuredText(const std::string & raw);

} // namespace ofxGgmlInferenceTextCleanup
