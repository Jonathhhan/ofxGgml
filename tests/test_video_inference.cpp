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
	REQUIRE(prompt.find("Sample count: 2 frame(s).") != std::string::npos);
	REQUIRE(prompt.find("Frame 1 at 0:00") != std::string::npos);
	REQUIRE(prompt.find("Frame 2 at 0:04") != std::string::npos);
	REQUIRE(prompt.find("Opening frame") != std::string::npos);
	REQUIRE(prompt.find("Closing frame") != std::string::npos);
}

TEST_CASE("Video inference builds action and emotion prompts", "[video_inference]") {
	ofxGgmlVideoRequest actionRequest;
	actionRequest.task = ofxGgmlVideoTask::Action;
	std::vector<ofxGgmlSampledVideoFrame> frames = {
		{"frame0.png", "Opening frame", 0.0},
		{"frame1.png", "Middle frame", 2.0}
	};
	const std::string actionPrompt = ofxGgmlVideoInference::buildFrameAwarePrompt(actionRequest, frames);
	REQUIRE(actionPrompt.find("primary action") != std::string::npos);

	ofxGgmlVideoRequest emotionRequest;
	emotionRequest.task = ofxGgmlVideoTask::Emotion;
	const std::string emotionPrompt = ofxGgmlVideoInference::buildFrameAwarePrompt(emotionRequest, frames);
	REQUIRE(emotionPrompt.find("dominant emotion") != std::string::npos);
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

TEST_CASE("Video inference normalizes temporal sidecar URLs", "[video_inference]") {
	REQUIRE(
		ofxGgmlVideoInference::normalizeSidecarUrl("http://127.0.0.1:8090") ==
		"http://127.0.0.1:8090/analyze");
	REQUIRE(
		ofxGgmlVideoInference::normalizeSidecarUrl("http://127.0.0.1:8090/analyze") ==
		"http://127.0.0.1:8090/analyze");
}

TEST_CASE("Video inference builds temporal sidecar payloads", "[video_inference]") {
	ofxGgmlVideoRequest request;
	request.task = ofxGgmlVideoTask::Action;
	request.videoPath = "clip.mp4";
	request.prompt = "Classify the action.";
	request.sidecarModel = "temporal-action-v1";
	std::vector<ofxGgmlSampledVideoFrame> frames = {
		{"frame0.png", "Opening frame", 0.0},
		{"frame1.png", "Closing frame", 4.0}
	};

	const std::string payload = ofxGgmlVideoInference::buildTemporalSidecarJson(request, frames);
	REQUIRE(payload.find("\"task\":\"Action\"") != std::string::npos);
	REQUIRE(payload.find("\"model\":\"temporal-action-v1\"") != std::string::npos);
	REQUIRE(payload.find("\"video_path\":\"clip.mp4\"") != std::string::npos);
	REQUIRE(payload.find("\"sampled_frames\"") != std::string::npos);
	REQUIRE(payload.find("\"primary_label\":\"string\"") != std::string::npos);
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
