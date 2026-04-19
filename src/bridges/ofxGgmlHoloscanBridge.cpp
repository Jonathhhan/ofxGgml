#include "bridges/ofxGgmlHoloscanBridge.h"

#include "inference/ofxGgmlVisionInference.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <future>
#include <mutex>
#include <thread>

#if !defined(_WIN32) && defined(__has_include)
#  if __has_include(<holoscan/holoscan.hpp>)
#    include <holoscan/holoscan.hpp>
#    define OFXGGML_HAS_HOLOSCAN 1
#  else
#    define OFXGGML_HAS_HOLOSCAN 0
#  endif
#else
#  define OFXGGML_HAS_HOLOSCAN 0
#endif

namespace {

using namespace std::chrono_literals;

std::string sanitizeTempLabel(const std::string& label) {
	if (label.empty()) {
		return "frame";
	}
	std::string sanitized = label;
	for (char& c : sanitized) {
		const bool allowed =
			(c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '_' || c == '-';
		if (!allowed) {
			c = '_';
		}
	}
	return sanitized;
}

std::filesystem::path defaultTempDir() {
	return std::filesystem::temp_directory_path() / "ofxGgmlHoloscan";
}

bool persistPixelsToTempImage(
	const ofPixels& pixels,
	uint64_t frameIndex,
	const std::string& sourceLabel,
	std::string* outPath,
	std::string* outError) {
	if (!pixels.isAllocated()) {
		if (outError) {
			*outError = "Holoscan bridge received an empty frame.";
		}
		return false;
	}

	std::error_code ec;
	const auto tempDir = defaultTempDir();
	std::filesystem::create_directories(tempDir, ec);
	if (ec) {
		if (outError) {
			*outError = "Failed to create Holoscan temp directory: " + ec.message();
		}
		return false;
	}

	const auto filePath =
		tempDir /
		(ofToString(frameIndex) + "_" + sanitizeTempLabel(sourceLabel) + ".png");
	if (!ofSaveImage(pixels, filePath)) {
		if (outError) {
			*outError = "Failed to write temporary frame image for Holoscan bridge.";
		}
		return false;
	}

	if (outPath) {
		*outPath = filePath.string();
	}
	return true;
}

void removeTempImage(const std::string& path) {
	if (path.empty()) {
		return;
	}
	std::error_code ec;
	std::filesystem::remove(path, ec);
}

ofxGgmlVisionRequest makeVisionRequest(
	const ofxGgmlHoloscanVisionRequestTemplate& requestTemplate,
	const std::string& imagePath,
	const std::string& sourceLabel) {
	ofxGgmlVisionRequest request;
	request.task = requestTemplate.task;
	request.prompt = requestTemplate.prompt.empty()
		? ofxGgmlVisionInference::defaultPromptForTask(requestTemplate.task)
		: requestTemplate.prompt;
	request.systemPrompt = requestTemplate.systemPrompt.empty()
		? ofxGgmlVisionInference::defaultSystemPromptForTask(requestTemplate.task)
		: requestTemplate.systemPrompt;
	request.responseLanguage = requestTemplate.responseLanguage;
	request.maxTokens = requestTemplate.maxTokens;
	request.temperature = requestTemplate.temperature;
	request.images.push_back({
		imagePath,
		sourceLabel,
		ofxGgmlVisionInference::detectMimeType(imagePath)
	});
	return request;
}

void clearPreview(ofTexture* texture, ofxGgmlHoloscanPreviewFrame* preview) {
	if (texture) {
		texture->clear();
	}
	if (preview) {
		*preview = {};
	}
}

#if OFXGGML_HAS_HOLOSCAN

struct HoloscanPreparedVisionPacket {
	uint64_t frameIndex = 0;
	double timestampSeconds = 0.0;
	std::string sourceLabel;
	ofPixels previewPixels;
	std::string tempImagePath;
	ofxGgmlVisionModelProfile profile;
	ofxGgmlVisionRequest request;
};

struct HoloscanFinishedVisionPacket {
	ofxGgmlHoloscanVisionResultPacket resultPacket;
	ofxGgmlHoloscanPreviewFrame previewFrame;
};

struct HoloscanSharedRuntimeState {
	mutable std::mutex mutex;
	std::deque<ofxGgmlHoloscanFramePacket> pendingFrames;
	std::vector<HoloscanFinishedVisionPacket> finishedPackets;
	ofxGgmlHoloscanPreviewFrame previewFrame;
	bool previewDirty = false;
	ofxGgmlVisionModelProfile profile;
	ofxGgmlHoloscanVisionRequestTemplate requestTemplate;
	std::string lastError;
	bool stopRequested = false;
};

void setHoloscanRuntimeError(
	const std::shared_ptr<HoloscanSharedRuntimeState>& state,
	const std::string& error) {
	if (!state) {
		return;
	}
	std::lock_guard<std::mutex> lock(state->mutex);
	state->lastError = error;
}

namespace holoscanops {

class FrameSourceOp : public holoscan::Operator {
public:
	HOLOSCAN_OPERATOR_FORWARD_ARGS(FrameSourceOp)

	std::shared_ptr<HoloscanSharedRuntimeState> sharedState;

	void setup(holoscan::OperatorSpec& spec) override {
		spec.output<std::shared_ptr<ofxGgmlHoloscanFramePacket>>("out");
		spec.param(allocator_,
			"allocator",
			"Allocator",
			"Allocator resource used by the Holoscan frame source.",
			{});
	}

	void compute(
		holoscan::InputContext&,
		holoscan::OutputContext& op_output,
		holoscan::ExecutionContext&) override {
		if (!sharedState) {
			return;
		}

		ofxGgmlHoloscanFramePacket packet;
		{
			std::lock_guard<std::mutex> lock(sharedState->mutex);
			if (sharedState->stopRequested || sharedState->pendingFrames.empty()) {
				return;
			}
			packet = sharedState->pendingFrames.front();
			sharedState->pendingFrames.pop_front();
		}

		if (!packet.isValid()) {
			return;
		}

		op_output.emit(
			std::make_shared<ofxGgmlHoloscanFramePacket>(std::move(packet)),
			"out");
	}

private:
	holoscan::Parameter<std::shared_ptr<holoscan::Allocator>> allocator_;
};

class RequestPrepOp : public holoscan::Operator {
public:
	HOLOSCAN_OPERATOR_FORWARD_ARGS(RequestPrepOp)

	std::shared_ptr<HoloscanSharedRuntimeState> sharedState;

	void setup(holoscan::OperatorSpec& spec) override {
		spec.input<std::shared_ptr<ofxGgmlHoloscanFramePacket>>("in");
		spec.output<std::shared_ptr<HoloscanPreparedVisionPacket>>("out");
		spec.param(allocator_,
			"allocator",
			"Allocator",
			"Allocator resource used by the Holoscan request preparation stage.",
			{});
	}

	void compute(
		holoscan::InputContext& op_input,
		holoscan::OutputContext& op_output,
		holoscan::ExecutionContext&) override {
		auto packet = op_input.receive<std::shared_ptr<ofxGgmlHoloscanFramePacket>>("in");
		if (!packet) {
			return;
		}

		auto framePacket = packet.value();
		if (!framePacket || !framePacket->isValid() || !sharedState) {
			return;
		}

		ofxGgmlVisionModelProfile profile;
		ofxGgmlHoloscanVisionRequestTemplate requestTemplate;
		{
			std::lock_guard<std::mutex> lock(sharedState->mutex);
			profile = sharedState->profile;
			requestTemplate = sharedState->requestTemplate;
		}

		if (profile.serverUrl.empty()) {
			setHoloscanRuntimeError(
				sharedState,
				"Holoscan vision bridge needs a valid vision profile before running.");
			return;
		}

		auto prepared = std::make_shared<HoloscanPreparedVisionPacket>();
		prepared->frameIndex = framePacket->frameIndex;
		prepared->timestampSeconds = framePacket->timestampSeconds;
		prepared->sourceLabel = framePacket->sourceLabel;
		prepared->previewPixels = *framePacket->pixels;
		prepared->profile = profile;

		std::string tempImagePath;
		std::string error;
		if (!persistPixelsToTempImage(
				*framePacket->pixels,
				framePacket->frameIndex,
				framePacket->sourceLabel,
				&tempImagePath,
				&error)) {
			setHoloscanRuntimeError(sharedState, error);
			return;
		}

		prepared->tempImagePath = tempImagePath;
		prepared->request = makeVisionRequest(
			requestTemplate,
			tempImagePath,
			framePacket->sourceLabel);

		op_output.emit(prepared, "out");
	}

private:
	holoscan::Parameter<std::shared_ptr<holoscan::Allocator>> allocator_;
};

class VisionInferOp : public holoscan::Operator {
public:
	HOLOSCAN_OPERATOR_FORWARD_ARGS(VisionInferOp)

	ofxGgmlVisionInference* visionInference = nullptr;
	std::shared_ptr<HoloscanSharedRuntimeState> sharedState;

	void setup(holoscan::OperatorSpec& spec) override {
		spec.input<std::shared_ptr<HoloscanPreparedVisionPacket>>("in");
		spec.output<std::shared_ptr<HoloscanFinishedVisionPacket>>("out");
		spec.param(allocator_,
			"allocator",
			"Allocator",
			"Allocator resource used by the Holoscan vision inference stage.",
			{});
	}

	void compute(
		holoscan::InputContext& op_input,
		holoscan::OutputContext& op_output,
		holoscan::ExecutionContext&) override {
		auto packet = op_input.receive<std::shared_ptr<HoloscanPreparedVisionPacket>>("in");
		if (!packet) {
			return;
		}

		auto prepared = packet.value();
		if (!prepared || !visionInference) {
			return;
		}

		std::lock_guard<std::mutex> lock(visionMutex_);
		try {
			auto finished = std::make_shared<HoloscanFinishedVisionPacket>();
			finished->resultPacket.frameIndex = prepared->frameIndex;
			finished->resultPacket.timestampSeconds = prepared->timestampSeconds;
			finished->resultPacket.sourceLabel = prepared->sourceLabel;
			finished->resultPacket.result =
				visionInference->runServerRequest(prepared->profile, prepared->request);
			finished->previewFrame.valid = true;
			finished->previewFrame.frameIndex = prepared->frameIndex;
			finished->previewFrame.timestampSeconds = prepared->timestampSeconds;
			finished->previewFrame.sourceLabel = prepared->sourceLabel;
			finished->previewFrame.text = finished->resultPacket.result.success
				? finished->resultPacket.result.text
				: finished->resultPacket.result.error;
			finished->previewFrame.pixels = prepared->previewPixels;
			removeTempImage(prepared->tempImagePath);
			op_output.emit(finished, "out");
		} catch (const std::exception& e) {
			removeTempImage(prepared->tempImagePath);
			setHoloscanRuntimeError(sharedState, e.what());
		}
	}

private:
	holoscan::Parameter<std::shared_ptr<holoscan::Allocator>> allocator_;
	std::mutex visionMutex_;
};

class PreviewSinkOp : public holoscan::Operator {
public:
	HOLOSCAN_OPERATOR_FORWARD_ARGS(PreviewSinkOp)

	std::shared_ptr<HoloscanSharedRuntimeState> sharedState;

	void setup(holoscan::OperatorSpec& spec) override {
		spec.input<std::shared_ptr<HoloscanFinishedVisionPacket>>("in");
		spec.param(allocator_,
			"allocator",
			"Allocator",
			"Allocator resource used by the Holoscan preview sink.",
			{});
	}

	void compute(
		holoscan::InputContext& op_input,
		holoscan::OutputContext&,
		holoscan::ExecutionContext&) override {
		auto packet =
			op_input.receive<std::shared_ptr<HoloscanFinishedVisionPacket>>("in");
		if (!packet) {
			return;
		}

		auto finished = packet.value();
		if (!finished || !sharedState) {
			return;
		}

		std::lock_guard<std::mutex> lock(sharedState->mutex);
		sharedState->previewFrame = finished->previewFrame;
		sharedState->previewDirty = true;
		sharedState->finishedPackets.push_back(*finished);
	}

private:
	holoscan::Parameter<std::shared_ptr<holoscan::Allocator>> allocator_;
};

} // namespace holoscanops

class HoloscanVisionPipelineApp : public holoscan::Application {
public:
	HoloscanVisionPipelineApp(
		ofxGgmlVisionInference* visionInference,
		std::shared_ptr<HoloscanSharedRuntimeState> sharedState,
		ofxGgmlHoloscanSettings settings)
		: visionInference_(visionInference)
		, sharedState_(std::move(sharedState))
		, settings_(settings) {
	}

	void compose() override {
		using holoscan::Arg;

		auto allocator = make_resource<holoscan::UnboundedAllocator>("bridge_allocator");

		std::shared_ptr<holoscan::Scheduler> schedulerResource;
		if (settings_.useEventScheduler) {
			schedulerResource = make_scheduler<holoscan::EventBasedScheduler>(
				"event_scheduler",
				Arg("worker_thread_number",
					static_cast<int64_t>(std::max(1, settings_.workerThreads))),
				Arg("stop_on_deadlock", true));
		} else {
			schedulerResource = make_scheduler<holoscan::GreedyScheduler>(
				"greedy_scheduler",
				Arg("stop_on_deadlock", true));
		}
		scheduler(schedulerResource);

		auto frameSource = make_operator<holoscanops::FrameSourceOp>(
			"frame_source",
			Arg("allocator", allocator));
		auto requestPrep = make_operator<holoscanops::RequestPrepOp>(
			"request_prep",
			Arg("allocator", allocator));
		auto visionInfer = make_operator<holoscanops::VisionInferOp>(
			"vision_infer",
			Arg("allocator", allocator));
		auto previewSink = make_operator<holoscanops::PreviewSinkOp>(
			"preview_sink",
			Arg("allocator", allocator));

		frameSource->sharedState = sharedState_;
		requestPrep->sharedState = sharedState_;
		visionInfer->visionInference = visionInference_;
		visionInfer->sharedState = sharedState_;
		previewSink->sharedState = sharedState_;

		add_flow(frameSource, requestPrep, {{"out", "in"}});
		add_flow(requestPrep, visionInfer, {{"out", "in"}});
		add_flow(visionInfer, previewSink, {{"out", "in"}});
	}

private:
	ofxGgmlVisionInference* visionInference_ = nullptr;
	std::shared_ptr<HoloscanSharedRuntimeState> sharedState_;
	ofxGgmlHoloscanSettings settings_;
};

#endif // OFXGGML_HAS_HOLOSCAN

} // namespace

struct ofxGgmlHoloscanBridge::Impl {
	ofxGgmlVisionInference* visionInference = nullptr;
	ofxGgmlHoloscanSettings settings;
	ofxGgmlVisionModelProfile profile;
	ofxGgmlHoloscanVisionRequestTemplate requestTemplate;
	bool configured = false;
	bool running = false;
	bool holoscanAvailable = false;
	bool usingHoloscanRuntime = false;
	std::string lastError;
	uint64_t nextFrameIndex = 1;
	ofTexture previewTexture;
	ofxGgmlHoloscanPreviewFrame previewFrame;
	std::deque<ofxGgmlHoloscanFramePacket> pendingFrames;
	std::vector<ofxGgmlHoloscanVisionResultPacket> finishedResults;
	std::mutex mutex;

#if OFXGGML_HAS_HOLOSCAN
	std::shared_ptr<HoloscanSharedRuntimeState> holoscanState;
	std::shared_ptr<HoloscanVisionPipelineApp> holoscanApp;
	std::future<void> holoscanFuture;
#endif

	void clearFallbackState() {
		std::lock_guard<std::mutex> lock(mutex);
		pendingFrames.clear();
		finishedResults.clear();
	}
};

ofxGgmlHoloscanBridge::ofxGgmlHoloscanBridge()
	: impl_(std::make_unique<Impl>()) {
#if OFXGGML_HAS_HOLOSCAN
	impl_->holoscanAvailable = true;
	impl_->holoscanState = std::make_shared<HoloscanSharedRuntimeState>();
#endif
}

ofxGgmlHoloscanBridge::~ofxGgmlHoloscanBridge() = default;

bool ofxGgmlHoloscanBridge::setup(
	ofxGgmlVisionInference* visionInference,
	const ofxGgmlHoloscanSettings& settings) {
	impl_->visionInference = visionInference;
	impl_->settings = settings;
	impl_->configured = (visionInference != nullptr);
	impl_->lastError.clear();
	if (!impl_->configured) {
		impl_->lastError = "Holoscan bridge setup requires a valid ofxGgmlVisionInference instance.";
	}
#if OFXGGML_HAS_HOLOSCAN
	if (impl_->holoscanState) {
		std::lock_guard<std::mutex> lock(impl_->holoscanState->mutex);
		impl_->holoscanState->profile = impl_->profile;
		impl_->holoscanState->requestTemplate = impl_->requestTemplate;
		impl_->holoscanState->lastError.clear();
		impl_->holoscanState->stopRequested = false;
	}
#endif
	return impl_->configured;
}

void ofxGgmlHoloscanBridge::shutdown() {
	stop();
	impl_->configured = false;
	impl_->visionInference = nullptr;
	impl_->lastError.clear();
}

bool ofxGgmlHoloscanBridge::startVisionPipeline() {
	if (!impl_->configured || impl_->visionInference == nullptr) {
		impl_->lastError = "Holoscan bridge is not configured.";
		return false;
	}

	impl_->lastError.clear();
	impl_->clearFallbackState();
	clearPreview(&impl_->previewTexture, &impl_->previewFrame);

#if OFXGGML_HAS_HOLOSCAN
	if (impl_->settings.enabled && impl_->holoscanAvailable && impl_->holoscanState) {
		{
			std::lock_guard<std::mutex> lock(impl_->holoscanState->mutex);
			impl_->holoscanState->pendingFrames.clear();
			impl_->holoscanState->finishedPackets.clear();
			impl_->holoscanState->previewFrame = {};
			impl_->holoscanState->previewDirty = false;
			impl_->holoscanState->lastError.clear();
			impl_->holoscanState->stopRequested = false;
			impl_->holoscanState->profile = impl_->profile;
			impl_->holoscanState->requestTemplate = impl_->requestTemplate;
		}

		impl_->holoscanApp = std::make_shared<HoloscanVisionPipelineApp>(
			impl_->visionInference,
			impl_->holoscanState,
			impl_->settings);

		try {
			impl_->holoscanFuture = impl_->holoscanApp->run_async();
			impl_->usingHoloscanRuntime = true;
			impl_->running = true;
			return true;
		} catch (const std::exception& e) {
			impl_->lastError = e.what();
			impl_->holoscanApp.reset();
			impl_->usingHoloscanRuntime = false;
		}
	}
#endif

	impl_->usingHoloscanRuntime = false;
	impl_->running = true;
	return true;
}

void ofxGgmlHoloscanBridge::stop() {
#if OFXGGML_HAS_HOLOSCAN
	if (impl_->usingHoloscanRuntime) {
		if (impl_->holoscanState) {
			std::lock_guard<std::mutex> lock(impl_->holoscanState->mutex);
			impl_->holoscanState->stopRequested = true;
			impl_->holoscanState->pendingFrames.clear();
		}
		if (impl_->holoscanApp) {
			try {
				impl_->holoscanApp->stop_execution();
			} catch (...) {
			}
		}
		if (impl_->holoscanFuture.valid()) {
			try {
				impl_->holoscanFuture.get();
			} catch (const std::exception& e) {
				impl_->lastError = e.what();
			}
		}
		impl_->holoscanApp.reset();
		impl_->usingHoloscanRuntime = false;
		if (impl_->holoscanState) {
			std::lock_guard<std::mutex> lock(impl_->holoscanState->mutex);
			impl_->holoscanState->finishedPackets.clear();
			impl_->holoscanState->previewFrame = {};
			impl_->holoscanState->previewDirty = false;
		}
	}
#endif

	impl_->running = false;
	impl_->clearFallbackState();
	clearPreview(&impl_->previewTexture, &impl_->previewFrame);
}

void ofxGgmlHoloscanBridge::update() {
	if (!impl_->running || !impl_->visionInference) {
		return;
	}

#if OFXGGML_HAS_HOLOSCAN
	if (impl_->usingHoloscanRuntime && impl_->holoscanState) {
		std::lock_guard<std::mutex> lock(impl_->holoscanState->mutex);
		impl_->lastError = impl_->holoscanState->lastError;
		if (impl_->holoscanState->previewDirty && impl_->holoscanState->previewFrame.valid) {
			impl_->previewFrame = impl_->holoscanState->previewFrame;
			impl_->previewTexture.loadData(impl_->previewFrame.pixels);
			impl_->holoscanState->previewDirty = false;
		}
		return;
	}
#endif

	ofxGgmlHoloscanFramePacket frame;
	{
		std::lock_guard<std::mutex> lock(impl_->mutex);
		if (impl_->pendingFrames.empty()) {
			return;
		}
		frame = impl_->pendingFrames.front();
		impl_->pendingFrames.pop_front();
	}

	if (!frame.isValid()) {
		return;
	}

	if (impl_->profile.serverUrl.empty()) {
		impl_->lastError =
			"Holoscan vision bridge needs a valid vision profile before running.";
		return;
	}

	std::string tempImagePath;
	if (!persistPixelsToTempImage(
			*frame.pixels,
			frame.frameIndex,
			frame.sourceLabel,
			&tempImagePath,
			&impl_->lastError)) {
		return;
	}

	const auto request = makeVisionRequest(
		impl_->requestTemplate,
		tempImagePath,
		frame.sourceLabel);
	const auto result = impl_->visionInference->runServerRequest(impl_->profile, request);
	removeTempImage(tempImagePath);

	impl_->previewFrame.valid = true;
	impl_->previewFrame.frameIndex = frame.frameIndex;
	impl_->previewFrame.timestampSeconds = frame.timestampSeconds;
	impl_->previewFrame.sourceLabel = frame.sourceLabel;
	impl_->previewFrame.text = result.success ? result.text : result.error;
	impl_->previewFrame.pixels = *frame.pixels;
	impl_->previewTexture.loadData(impl_->previewFrame.pixels);

	std::lock_guard<std::mutex> lock(impl_->mutex);
	impl_->finishedResults.push_back({
		frame.frameIndex,
		frame.timestampSeconds,
		frame.sourceLabel,
		result
	});
}

void ofxGgmlHoloscanBridge::submitFrame(
	const ofPixels& pixels,
	double timestampSeconds,
	const std::string& sourceLabel) {
	if (!pixels.isAllocated()) {
		impl_->lastError = "Holoscan bridge received an empty frame.";
		return;
	}

	auto ownedPixels = std::make_shared<ofPixels>(pixels);
	ofxGgmlHoloscanFramePacket packet;
	packet.frameIndex = impl_->nextFrameIndex++;
	packet.timestampSeconds = timestampSeconds;
	packet.pixels = ownedPixels;
	packet.sourceLabel = sourceLabel;

#if OFXGGML_HAS_HOLOSCAN
	if (impl_->usingHoloscanRuntime && impl_->holoscanState) {
		std::lock_guard<std::mutex> lock(impl_->holoscanState->mutex);
		impl_->holoscanState->pendingFrames.push_back(std::move(packet));
		return;
	}
#endif

	std::lock_guard<std::mutex> lock(impl_->mutex);
	impl_->pendingFrames.push_back(std::move(packet));
}

void ofxGgmlHoloscanBridge::submitProfile(
	const ofxGgmlVisionModelProfile& profile) {
	impl_->profile = profile;
#if OFXGGML_HAS_HOLOSCAN
	if (impl_->holoscanState) {
		std::lock_guard<std::mutex> lock(impl_->holoscanState->mutex);
		impl_->holoscanState->profile = profile;
	}
#endif
}

void ofxGgmlHoloscanBridge::submitRequestTemplate(
	const ofxGgmlHoloscanVisionRequestTemplate& requestTemplate) {
	impl_->requestTemplate = requestTemplate;
#if OFXGGML_HAS_HOLOSCAN
	if (impl_->holoscanState) {
		std::lock_guard<std::mutex> lock(impl_->holoscanState->mutex);
		impl_->holoscanState->requestTemplate = requestTemplate;
	}
#endif
}

bool ofxGgmlHoloscanBridge::hasPreviewFrame() const {
	return impl_->previewFrame.valid && impl_->previewTexture.isAllocated();
}

const ofTexture& ofxGgmlHoloscanBridge::getPreviewTexture() const {
	return impl_->previewTexture;
}

ofxGgmlHoloscanPreviewFrame ofxGgmlHoloscanBridge::getPreviewFrameCopy() const {
	return impl_->previewFrame;
}

std::vector<ofxGgmlHoloscanVisionResultPacket>
ofxGgmlHoloscanBridge::consumeFinishedResults() {
#if OFXGGML_HAS_HOLOSCAN
	if (impl_->usingHoloscanRuntime && impl_->holoscanState) {
		std::lock_guard<std::mutex> lock(impl_->holoscanState->mutex);
		std::vector<ofxGgmlHoloscanVisionResultPacket> results;
		results.reserve(impl_->holoscanState->finishedPackets.size());
		for (const auto& packet : impl_->holoscanState->finishedPackets) {
			results.push_back(packet.resultPacket);
		}
		impl_->holoscanState->finishedPackets.clear();
		return results;
	}
#endif

	std::lock_guard<std::mutex> lock(impl_->mutex);
	auto results = std::move(impl_->finishedResults);
	impl_->finishedResults.clear();
	return results;
}

bool ofxGgmlHoloscanBridge::isConfigured() const {
	return impl_->configured;
}

bool ofxGgmlHoloscanBridge::isRunning() const {
	return impl_->running;
}

bool ofxGgmlHoloscanBridge::isHoloscanAvailable() const {
	return impl_->holoscanAvailable;
}

std::string ofxGgmlHoloscanBridge::getLastError() const {
#if OFXGGML_HAS_HOLOSCAN
	if (impl_->usingHoloscanRuntime && impl_->holoscanState) {
		std::lock_guard<std::mutex> lock(impl_->holoscanState->mutex);
		if (!impl_->holoscanState->lastError.empty()) {
			return impl_->holoscanState->lastError;
		}
	}
#endif
	return impl_->lastError;
}

const ofxGgmlHoloscanSettings& ofxGgmlHoloscanBridge::getSettings() const {
	return impl_->settings;
}
