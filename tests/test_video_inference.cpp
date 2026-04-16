#include "catch2.hpp"
#include "../src/ofxGgml.h"

#include <memory>

namespace {

class FakeVideoBackend final : public ofxGgmlVideoBackend {
public:
	std::string backendName() const override {
		return "FakeBackend";
	}

	ofxGgmlVideoBackendSampleResult sampleFrames(
		const ofxGgmlVideoRequest &) const override {
		ofxGgmlVideoBackendSampleResult result;
		result.success = true;
		result.backendName = backendName();
		result.sampledFrames.push_back({"frame0.png", "Fake frame", 1.0});
		return result;
	}
};

} // namespace

TEST_CASE("Video inference builds stable sample timelines", "[video_inference]") {
	const auto timeline = ofxGgmlVideoInference::buildSampleTimeline(12.0, 4, 0.0, 12.0, 2.0);
	REQUIRE(timeline.size() == 4);
	REQUIRE(timeline.front() == Approx(0.0));
	REQUIRE(timeline.back() == Approx(12.0));
}

TEST_CASE("Video inference can limit sample count by spacing", "[video_inference]") {
	const auto timeline = ofxGgmlVideoInference::buildSampleTimeline(5.0, 8, 0.0, 5.0, 2.0);
	REQUIRE(timeline.size() == 3);
	REQUIRE(timeline[1] == Approx(2.5));
}

TEST_CASE("Video inference builds frame-aware prompts", "[video_inference]") {
	ofxGgmlVideoRequest request;
	request.task = ofxGgmlVideoTask::Ask;
	request.prompt = "What happens in the clip?";

	std::vector<ofxGgmlSampledVideoFrame> frames = {
		{"frame0.png", "Opening frame", 0.0},
		{"frame1.png", "Closing frame", 4.0}
	};

	const std::string prompt = ofxGgmlVideoInference::buildFrameAwarePrompt(request, frames);
	REQUIRE(prompt.find("What happens in the clip?") != std::string::npos);
	REQUIRE(prompt.find("Frame 1 at 0:00") != std::string::npos);
	REQUIRE(prompt.find("Frame 2 at 0:04") != std::string::npos);
}

TEST_CASE("Video inference formats timestamps", "[video_inference]") {
	REQUIRE(ofxGgmlVideoInference::formatTimestamp(4.2) == "0:04");
	REQUIRE(ofxGgmlVideoInference::formatTimestamp(125.0) == "2:05");
	REQUIRE(ofxGgmlVideoInference::formatTimestamp(3661.0) == "1:01:01");
}

TEST_CASE("Video inference uses sampled-frames backend by default", "[video_inference]") {
	ofxGgmlVideoInference inference;
	REQUIRE(inference.getBackend() != nullptr);
	REQUIRE(inference.getBackend()->backendName() == "SampledFrames");
}

TEST_CASE("Video inference allows backend replacement", "[video_inference]") {
	ofxGgmlVideoInference inference;
	inference.setBackend(std::make_shared<FakeVideoBackend>());

	std::string error;
	ofxGgmlVideoRequest request;
	request.videoPath = "ignored.mp4";

	const auto frames = inference.sampleFrames(request, error);
	REQUIRE(error.empty());
	REQUIRE(frames.size() == 1);
	REQUIRE(frames[0].label == "Fake frame");
	REQUIRE(inference.getBackend()->backendName() == "FakeBackend");
}
