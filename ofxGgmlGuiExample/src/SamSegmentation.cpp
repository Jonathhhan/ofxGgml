#include "ofApp.h"

#include "ImHelpers.h"
#include "utils/ImGuiHelpers.h"
#include "utils/PathHelpers.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>
#include <thread>

namespace {
constexpr float kSamWaitingDotsAnimationSpeed = 3.0f;
const char * const kSamWaitingLabels[] = {
	"segmenting",
	"segmenting.",
	"segmenting..",
	"segmenting..."
};

std::vector<unsigned char> makeRgbBytes(const ofPixels & pixels) {
	std::vector<unsigned char> rgb;
	const int width = pixels.getWidth();
	const int height = pixels.getHeight();
	if (width <= 0 || height <= 0) {
		return rgb;
	}
	rgb.reserve(static_cast<size_t>(width) * static_cast<size_t>(height) * 3u);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const ofColor color = pixels.getColor(x, y);
			rgb.push_back(color.r);
			rgb.push_back(color.g);
			rgb.push_back(color.b);
		}
	}
	return rgb;
}
} // namespace

bool ofApp::ensureSamBackendConfigured(
	const std::string & modelPath,
	int threads) {
	if (modelPath.empty()) {
		return false;
	}
#if OFXGGML_HAS_SAMCPP
	if (configuredSamBackendModelPath == modelPath &&
		configuredSamBackendThreads == threads &&
		samInference.getBackend() != nullptr) {
		return true;
	}

	ofxGgmlSamCppAdapters::RuntimeOptions options;
	options.threads = threads;
	ofxGgmlSamCppAdapters::attachBackend(samInference, modelPath, options, "sam.cpp");
	configuredSamBackendModelPath = modelPath;
	configuredSamBackendThreads = threads;
	return true;
#else
	(void)threads;
	samInference.setBackend(ofxGgmlSamCppAdapters::createBackend(modelPath));
	configuredSamBackendModelPath = modelPath;
	configuredSamBackendThreads = threads;
	return false;
#endif
}

void ofApp::drawSamPanel() {
	drawPanelHeader("SAM", "optional Segment Anything masks via sam.cpp");
	const float compactModeFieldWidth =
		std::min(360.0f, ImGui::GetContentRegionAvail().x);

	ImGui::TextWrapped(
		"This panel sends a point prompt and local image pixels through the "
		"ofxGgml segmentation bridge. Add sam.cpp headers/libs to the generated "
		"project to enable native Segment Anything inference.");
	ImGui::TextDisabled(
		"Mirrors ggml/examples/sam: ViT-B ggml model, point prompts, multi-mask "
		"output, and CPU thread control.");
	drawHelpMarker(
		"The upstream ggml SAM example also exposes box prompts and mask/iou/"
		"stability thresholds. The current ofxGgml GUI lane keeps to the "
		"portable point-prompt bridge until those controls are available in the "
		"attached sam.cpp adapter.");

#if OFXGGML_HAS_SAMCPP
	const bool samCppAvailable = true;
#else
	const bool samCppAvailable = false;
#endif

	const std::string backendLabel =
		samInference.getBackend()
			? samInference.getBackend()->backendName()
			: std::string("(none)");
	ImGui::TextDisabled("Backend: %s", backendLabel.c_str());
	ImGui::TextDisabled(
		"sam.cpp integration: %s",
		samCppAvailable ? "available in this build" : "not compiled in");
	if (!samCppAvailable) {
		ImGui::TextColored(
			ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
			"Add sam.cpp to the generated project include/lib paths to enable local segmentation.");
	}

	if (ImGui::Button("Use ggml example defaults##SAM", ImVec2(210, 0))) {
		copyStringToBuffer(
			samModelPath,
			sizeof(samModelPath),
			"models/sam-vit-b/ggml-model-f16.bin");
		copyStringToBuffer(
			samImagePath,
			sizeof(samImagePath),
			"examples/sam/example.jpg");
		samPointNormalized = false;
		samPointX = 414.375f;
		samPointY = 162.796875f;
		samThreads = 8;
		samReturnMultipleMasks = true;
		autoSaveSession();
	}
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip(
			"Matches ggml/examples/sam README defaults: converted ViT-B model, "
			"example.jpg, point prompt (414.375, 162.796875), and multi-mask output.");
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Open ggml SAM docs##SAM")) {
		ofLaunchBrowser("https://github.com/ggml-org/ggml/tree/master/examples/sam");
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("SAM model path", samModelPath, sizeof(samModelPath));
	ImGui::SameLine();
	if (ImGui::Button("Browse model...##SAM", ImVec2(120, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select sam.cpp ggml model", false);
		if (result.bSuccess) {
			copyStringToBuffer(samModelPath, sizeof(samModelPath), result.getPath());
			autoSaveSession();
		}
	}

	ImGui::SetNextItemWidth(compactModeFieldWidth);
	ImGui::InputText("Image path", samImagePath, sizeof(samImagePath));
	ImGui::SameLine();
	if (ImGui::Button("Browse image...##SAM", ImVec2(120, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select image to segment", false);
		if (result.bSuccess) {
			copyStringToBuffer(samImagePath, sizeof(samImagePath), result.getPath());
			autoSaveSession();
		}
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(std::strlen(visionImagePath) == 0);
	if (ImGui::SmallButton("Use Vision image##SAM")) {
		copyStringToBuffer(samImagePath, sizeof(samImagePath), visionImagePath);
		autoSaveSession();
	}
	ImGui::EndDisabled();

	ImGui::Checkbox("Point is normalized 0..1", &samPointNormalized);
	if (samPointNormalized) {
		ImGui::SetNextItemWidth(220);
		ImGui::SliderFloat("Point X", &samPointX, 0.0f, 1.0f, "%.3f");
		ImGui::SetNextItemWidth(220);
		ImGui::SliderFloat("Point Y", &samPointY, 0.0f, 1.0f, "%.3f");
	} else {
		ImGui::SetNextItemWidth(220);
		ImGui::DragFloat("Point X", &samPointX, 1.0f, 0.0f, 32768.0f, "%.1f");
		ImGui::SetNextItemWidth(220);
		ImGui::DragFloat("Point Y", &samPointY, 1.0f, 0.0f, 32768.0f, "%.1f");
	}
	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Threads (-1 auto)", &samThreads, -1, 64);
	ImGui::Checkbox("Return multiple masks", &samReturnMultipleMasks);

	ensureLocalImagePreview(trim(samImagePath), samPreviewImage, samPreviewLoadedPath, samPreviewError);
	drawLocalImagePreview(
		"Input preview",
		trim(samImagePath),
		samPreviewImage,
		samPreviewError,
		"##SamInputPreview");

	std::string disabledReason;
	const bool hasModel = std::strlen(samModelPath) > 0;
	const bool hasImage = std::strlen(samImagePath) > 0;
	const bool canRunSam =
		!generating.load() &&
		samCppAvailable &&
		hasModel &&
		hasImage;
	if (!hasModel) {
		disabledReason = "Select a sam.cpp model first.";
	} else if (!hasImage) {
		disabledReason = "Select an image first.";
	} else if (!samCppAvailable) {
		disabledReason = "sam.cpp is not compiled into this build.";
	}
	ImGui::BeginDisabled(!canRunSam);
	if (ImGui::Button("Run SAM Segmentation", ImVec2(180, 0))) {
		runSamSegmentation();
	}
	ImGui::EndDisabled();
	if (!disabledReason.empty()) {
		ImGui::SameLine();
		ImGui::TextDisabled("(%s)", disabledReason.c_str());
	}

	ImGui::Separator();
	ImGui::Text("Output:");
	if (!samOutput.empty()) {
		ImGui::SameLine();
		if (ImGui::SmallButton("Copy##SamCopy")) copyToClipboard(samOutput);
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear##SamClear")) {
			samOutput.clear();
			samBackendName.clear();
			samElapsedMs = 0.0f;
			samMasks.clear();
			samMaskPreviewImage.clear();
			samMaskPreviewError.clear();
		}
	}
	if (!samBackendName.empty()) {
		ImGui::TextDisabled(
			"Last backend: %s%s",
			samBackendName.c_str(),
			samElapsedMs > 0.0f
				? (" in " + ofxGgmlHelpers::formatDurationMs(samElapsedMs)).c_str()
				: "");
	}
	if (!samMasks.empty()) {
		ImGui::TextDisabled("Masks: %d", static_cast<int>(samMasks.size()));
		for (const auto & mask : samMasks) {
			ImGui::BulletText(
				"%s  %dx%d  %zu bytes",
				mask.maskId.c_str(),
				mask.width,
				mask.height,
				mask.pixels.size());
		}
		if (samMaskPreviewImage.isAllocated() &&
			samMaskPreviewImage.getTexture().isAllocated()) {
			ImGui::TextDisabled("First mask preview:");
			drawMediaTexturePreview(samMaskPreviewImage, "##SamMaskPreview");
		} else if (!samMaskPreviewError.empty()) {
			ImGui::TextDisabled("%s", samMaskPreviewError.c_str());
		}
	}

	if (generating.load() && activeGenerationMode == AiMode::Sam) {
		ImGui::BeginChild("##SamOut", ImVec2(0, 0), true);
		std::string partial;
		{
			std::lock_guard<std::mutex> lock(streamMutex);
			partial = streamingOutput;
		}
		if (partial.empty()) {
			const int dots =
				static_cast<int>(ImGui::GetTime() * kSamWaitingDotsAnimationSpeed) % 4;
			ImGui::TextColored(
				ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
				"%s",
				kSamWaitingLabels[dots]);
		} else {
			ImGui::TextWrapped("%s", partial.c_str());
		}
		ImGui::EndChild();
	} else {
		ImGui::BeginChild("##SamOut", ImVec2(0, 0), true);
		if (samOutput.empty()) {
			ImGui::TextDisabled("SAM segmentation results appear here.");
		} else {
			ImGui::TextWrapped("%s", samOutput.c_str());
		}
		ImGui::EndChild();
	}
}

void ofApp::runSamSegmentation() {
	if (generating.load()) return;

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::Sam;
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = "Preparing SAM segmentation request...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	const std::string modelPath = trim(samModelPath);
	const std::string imagePath = trim(samImagePath);
	const float pointX = samPointX;
	const float pointY = samPointY;
	const int threads = samThreads;
	const bool pointNormalized = samPointNormalized;
	const bool returnMultipleMasks = samReturnMultipleMasks;

#if OFXGGML_HAS_SAMCPP
	if (!modelPath.empty()) {
		ensureSamBackendConfigured(modelPath, threads);
	}
#endif
	autoSaveSession();

	workerThread = std::thread([this, modelPath, imagePath, pointX, pointY, threads, pointNormalized, returnMultipleMasks]() {
		auto setPending = [this](const std::string & text) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingOutput = text;
			pendingRole = "assistant";
			pendingMode = AiMode::Sam;
		};

		auto clearPendingSamArtifacts = [this]() {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingSamBackendName.clear();
			pendingSamElapsedMs = 0.0f;
			pendingSamMasks.clear();
		};

		try {
			if (modelPath.empty()) {
				clearPendingSamArtifacts();
				setPending("[Error] Select a sam.cpp model first.");
				generating.store(false);
				return;
			}
			if (imagePath.empty()) {
				clearPendingSamArtifacts();
				setPending("[Error] Select an image first.");
				generating.store(false);
				return;
			}

#if OFXGGML_HAS_SAMCPP
			ofPixels pixels;
			if (!ofLoadImage(pixels, imagePath)) {
				clearPendingSamArtifacts();
				setPending("[Error] Failed to load image: " + imagePath);
				generating.store(false);
				return;
			}
			const int width = pixels.getWidth();
			const int height = pixels.getHeight();
			if (width <= 0 || height <= 0) {
				clearPendingSamArtifacts();
				setPending("[Error] Loaded image has invalid dimensions.");
				generating.store(false);
				return;
			}

			ofxGgmlSegmentationRequest request;
			request.imagePath = imagePath;
			request.imageWidth = width;
			request.imageHeight = height;
			request.imageRgb = makeRgbBytes(pixels);
			request.modelPath = modelPath;
			request.threads = threads;
			request.returnMultipleMasks = returnMultipleMasks;
			const float absoluteX = pointNormalized
				? std::clamp(pointX, 0.0f, 1.0f) * static_cast<float>(std::max(0, width - 1))
				: pointX;
			const float absoluteY = pointNormalized
				? std::clamp(pointY, 0.0f, 1.0f) * static_cast<float>(std::max(0, height - 1))
				: pointY;
			request.points.push_back({absoluteX, absoluteY, true});

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput = "Running sam.cpp on " + ofToString(width) + "x" + ofToString(height) + " image...";
			}

			const ofxGgmlSegmentationResult result = samInference.segment(request);
			if (cancelRequested.load()) {
				clearPendingSamArtifacts();
				setPending("[Cancelled] SAM segmentation cancelled.");
			} else if (result.success) {
				std::ostringstream summary;
				summary << "Generated " << result.masks.size() << " SAM mask";
				if (result.masks.size() != 1) {
					summary << "s";
				}
				if (!result.backendName.empty()) {
					summary << " via " << result.backendName;
				}
				if (result.elapsedMs > 0.0f) {
					summary << " in " << ofxGgmlHelpers::formatDurationMs(result.elapsedMs);
				}
				summary << ".\nPoint: (" << ofToString(absoluteX, 1)
					<< ", " << ofToString(absoluteY, 1) << ")";
				for (size_t i = 0; i < result.masks.size(); ++i) {
					const auto & mask = result.masks[i];
					summary << "\n- " << (mask.maskId.empty()
						? ("mask-" + ofToString(i))
						: mask.maskId)
						<< " " << mask.width << "x" << mask.height
						<< " (" << mask.pixels.size() << " bytes)";
				}
				if (!result.rawOutput.empty()) {
					summary << "\n\nBackend output:\n" << result.rawOutput;
				}
				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingSamBackendName = result.backendName;
					pendingSamElapsedMs = result.elapsedMs;
					pendingSamMasks = result.masks;
				}
				setPending(summary.str());
			} else {
				clearPendingSamArtifacts();
				std::string errorText = result.error.empty()
					? std::string("SAM backend returned no result.")
					: result.error;
				if (!result.rawOutput.empty()) {
					errorText += "\n\nBackend output:\n" + result.rawOutput;
				}
				setPending("[Error] " + errorText);
			}
#else
			clearPendingSamArtifacts();
			setPending("[Error] sam.cpp support is not available in this build yet.");
#endif
		} catch (const std::exception & e) {
			clearPendingSamArtifacts();
			setPending(std::string("[Error] SAM segmentation failed: ") + e.what());
		} catch (...) {
			clearPendingSamArtifacts();
			setPending("[Error] Unknown failure during SAM segmentation.");
		}

		{
			std::lock_guard<std::mutex> lock(streamMutex);
			streamingOutput.clear();
		}
		generating.store(false);
	});
}
