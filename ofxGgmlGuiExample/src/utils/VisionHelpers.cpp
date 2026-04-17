#include "VisionHelpers.h"
#include "ImGuiHelpers.h"

#include <algorithm>
#include <filesystem>
#include <vector>

std::string findMatchingMmprojPath(const std::string & modelPath) {
	if (trim(modelPath).empty()) {
		return {};
	}
	std::error_code ec;
	const std::filesystem::path modelFile(modelPath);
	const std::filesystem::path modelDir = modelFile.parent_path();
	if (modelDir.empty() || !std::filesystem::exists(modelDir, ec) || ec) {
		return {};
	}

	std::vector<std::filesystem::path> mmprojCandidates;
	for (const auto & entry : std::filesystem::directory_iterator(modelDir, ec)) {
		if (ec || !entry.is_regular_file()) {
			continue;
		}
		std::string name = entry.path().filename().string();
		std::string lowerName = name;
		std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (lowerName.find("mmproj") == std::string::npos &&
			lowerName.find("projector") == std::string::npos) {
			continue;
		}
		if (entry.path().extension() != ".gguf") {
			continue;
		}
		mmprojCandidates.push_back(entry.path());
	}

	if (mmprojCandidates.empty()) {
		return {};
	}
	std::sort(mmprojCandidates.begin(), mmprojCandidates.end());
	return mmprojCandidates.front().string();
}

std::string visionCapabilityFailureDetail(
	const std::string & configuredUrl,
	const std::string & modelPath) {
	const ofxGgmlServerProbeResult probe = ofxGgmlInference::probeServer(configuredUrl, true);
	if (!probe.reachable) {
		std::string detail = "Vision server is not reachable.";
		if (!probe.error.empty()) {
			detail += " " + probe.error;
		}
		return detail;
	}
	if (probe.visionCapable) {
		return {};
	}

	std::string detail =
		"Server is reachable but does not report multimodal capability.";
	const std::string mmprojPath = findMatchingMmprojPath(modelPath);
	if (mmprojPath.empty()) {
		detail += " This model likely needs a matching mmproj .gguf file next to the model.";
	} else {
		detail += " Restart the local server with mmproj support: " +
			ofFilePath::getFileName(mmprojPath) + ".";
	}
	if (!probe.activeModel.empty()) {
		detail += " Active model: " + probe.activeModel + ".";
	}
	return detail;
}

std::string prepareVisionImageForUpload(
	const std::string & imagePath,
	std::string * note) {
	constexpr int kMaxVisionUploadDimension = 1024;
	const std::string trimmedPath = trim(imagePath);
	if (trimmedPath.empty()) {
		return trimmedPath;
	}

	std::string lowerExt = std::filesystem::path(trimmedPath).extension().string();
	std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	ofImage image;
	if (!image.load(trimmedPath)) {
		return trimmedPath;
	}

	const int srcWidth = image.getWidth();
	const int srcHeight = image.getHeight();
	if (srcWidth <= 0 || srcHeight <= 0) {
		return trimmedPath;
	}

	const bool shouldNormalize =
		lowerExt == ".jpg" || lowerExt == ".jpeg" ||
		srcWidth > kMaxVisionUploadDimension || srcHeight > kMaxVisionUploadDimension;
	if (!shouldNormalize) {
		return trimmedPath;
	}

	float scale = 1.0f;
	const int maxDim = std::max(srcWidth, srcHeight);
	if (maxDim > kMaxVisionUploadDimension) {
		scale = static_cast<float>(kMaxVisionUploadDimension) / static_cast<float>(maxDim);
	}

	const int dstWidth = std::max(1, static_cast<int>(std::round(srcWidth * scale)));
	const int dstHeight = std::max(1, static_cast<int>(std::round(srcHeight * scale)));
	if (dstWidth != srcWidth || dstHeight != srcHeight) {
		image.resize(dstWidth, dstHeight);
	}

	const std::string cacheDir = ofToDataPath("cache/vision_uploads", true);
	std::error_code ec;
	std::filesystem::create_directories(cacheDir, ec);
	const std::string cacheKey =
		trimmedPath + "|" +
		std::to_string(static_cast<long long>(std::filesystem::file_size(trimmedPath, ec))) + "|" +
		std::to_string(dstWidth) + "x" + std::to_string(dstHeight);
	const std::string outputName =
		ofFilePath::getBaseName(trimmedPath) + "_" +
		std::to_string(std::hash<std::string>{}(cacheKey)) + ".png";
	const std::string outputPath = ofFilePath::join(cacheDir, outputName);
	std::error_code outEc;
	if (!std::filesystem::exists(std::filesystem::path(outputPath), outEc) || outEc) {
		ofSaveImage(image.getPixels(), std::filesystem::path(outputPath), OF_IMAGE_QUALITY_BEST);
	}

	if (note) {
		*note =
			"Normalized local image for Vision upload: " +
			ofFilePath::getFileName(trimmedPath) + " -> " +
			ofFilePath::getFileName(outputPath);
	}
	return outputPath;
}

bool isEuRestrictedVisionProfile(const ofxGgmlVisionModelProfile & profile) {
	const std::string repoHint = trim(profile.modelRepoHint);
	return repoHint.find("meta-llama/Llama-3.2-11B-Vision") != std::string::npos;
}
