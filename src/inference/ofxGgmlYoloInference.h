#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct ofxGgmlYoloModelProfile {
	std::string backendId;
	std::string name;
	std::string architecture;
	std::string modelRepoHint;
	std::string modelFileHint;
	std::string executableHint;
};

struct ofxGgmlYoloRequest {
	std::string imagePath;
	std::string modelPath;
	std::string outputPath;
	std::string device;
	int threads = -1;
	float threshold = 0.5f;
};

struct ofxGgmlYoloDetection {
	std::string label;
	float confidence = 0.0f;
	size_t index = 0;
	std::vector<std::pair<std::string, std::string>> metadata;
};

struct ofxGgmlYoloResult {
	bool success = false;
	float elapsedMs = 0.0f;
	std::string error;
	std::string backendName;
	std::string rawOutput;
	std::string imagePath;
	std::string outputPath;
	std::vector<ofxGgmlYoloDetection> detections;
	std::vector<std::pair<std::string, std::string>> metadata;
};

class ofxGgmlYoloBackend {
public:
	virtual ~ofxGgmlYoloBackend() = default;
	virtual std::string backendName() const = 0;
	virtual ofxGgmlYoloResult detect(
		const ofxGgmlYoloRequest & request) const = 0;
};

class ofxGgmlYoloBridgeBackend : public ofxGgmlYoloBackend {
public:
	using DetectFunction = std::function<ofxGgmlYoloResult(
		const ofxGgmlYoloRequest &)>;

	explicit ofxGgmlYoloBridgeBackend(
		DetectFunction detectFunction = {},
		std::string displayName = "YoloBridge");

	void setDetectFunction(DetectFunction detectFunction);
	bool isConfigured() const;

	std::string backendName() const override;
	ofxGgmlYoloResult detect(
		const ofxGgmlYoloRequest & request) const override;

private:
	DetectFunction m_detectFunction;
	std::string m_displayName;
};

class ofxGgmlYoloCliBackend : public ofxGgmlYoloBackend {
public:
	explicit ofxGgmlYoloCliBackend(
		std::string executable = "yolov3-tiny");

	void setExecutable(const std::string & executable);
	const std::string & getExecutable() const;

	std::string backendName() const override;
	std::vector<std::string> buildCommandArguments(
		const ofxGgmlYoloRequest & request) const;
	ofxGgmlYoloResult detect(
		const ofxGgmlYoloRequest & request) const override;

	static std::vector<ofxGgmlYoloDetection> parseDetections(
		const std::string & output);
	static std::string parseSavedOutputPath(
		const std::string & output);

private:
	std::string m_executable;
};

class ofxGgmlYoloInference {
public:
	ofxGgmlYoloInference();

	static std::vector<ofxGgmlYoloModelProfile> defaultProfiles();
	static std::shared_ptr<ofxGgmlYoloBackend>
		createYoloBridgeBackend(
			ofxGgmlYoloBridgeBackend::DetectFunction detectFunction = {},
			const std::string & displayName = "YoloBridge");
	static std::shared_ptr<ofxGgmlYoloBackend>
		createGgmlYoloCliBackend(
			const std::string & executable = "yolov3-tiny");

	void setBackend(std::shared_ptr<ofxGgmlYoloBackend> backend);
	std::shared_ptr<ofxGgmlYoloBackend> getBackend() const;

	ofxGgmlYoloResult detect(
		const ofxGgmlYoloRequest & request) const;
	ofxGgmlYoloResult detectImage(
		const std::string & imagePath,
		const std::string & modelPath = {},
		float threshold = 0.5f,
		const std::string & outputPath = {}) const;

private:
	std::shared_ptr<ofxGgmlYoloBackend> m_backend;
};

