#pragma once

#include "ofxGgmlTextGeneration.h"

#include <functional>
#include <string>
#include <vector>

struct ofxGgmlTextCommand {
	std::string executablePath;
	std::vector<std::string> arguments;
	std::string inputText;
};

struct ofxGgmlTextCommandResult {
	bool started = false;
	int exitCode = -1;
	std::string output;
	std::string error;

	explicit operator bool() const {
		return isOk();
	}

	bool isOk() const {
		return started && exitCode == 0;
	}

	bool isError() const {
		return !isOk();
	}
};

using ofxGgmlTextCommandRunner = std::function<ofxGgmlTextCommandResult(
	const ofxGgmlTextCommand &,
	const ofxGgmlTextChunkCallback &)>;

class ofxGgmlLlamaCliTextBackend : public ofxGgmlTextBackend {
public:
	explicit ofxGgmlLlamaCliTextBackend(
		ofxGgmlTextCommandRunner runner = {},
		std::string displayName = "llama.cpp CLI");

	void setCommandRunner(ofxGgmlTextCommandRunner runner);
	bool hasCommandRunner() const;

	std::string backendName() const override;
	ofxGgmlTextResult generate(
		const ofxGgmlTextRequest & request,
		ofxGgmlTextChunkCallback onChunk = nullptr) const override;

	static std::string composePrompt(const ofxGgmlTextRequest & request);
	static ofxGgmlTextCommand buildCommand(
		const ofxGgmlTextRequest & request,
		const std::string & prompt);
	static ofxGgmlTextCommandResult runCommand(
		const ofxGgmlTextCommand & command,
		const ofxGgmlTextChunkCallback & onChunk = nullptr);

private:
	ofxGgmlTextCommandRunner m_runner;
	std::string m_displayName;
};
