#include "VisionHelpers.h"
#include "ImGuiHelpers.h"
#include "ofxGgmlVisionInference.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace {
std::string lowerAscii(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

std::string stripKnownVisionQuantSuffix(std::string stem) {
	const std::vector<std::string> suffixes = {
		"-q8_0",
		"-q4_k_m",
		"-q4_k_s",
		"-q5_k_m",
		"-q5_k_s",
		"-f16",
		"-bf16"
	};
	for (const auto & suffix : suffixes) {
		if (stem.size() >= suffix.size() &&
			stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0) {
			stem.resize(stem.size() - suffix.size());
			break;
		}
	}
	return stem;
}

int scoreMmprojCandidate(
	const std::filesystem::path & candidate,
	const std::string & modelStem,
	const std::string & modelStemNoQuant) {
	const std::string candidateStem =
		lowerAscii(candidate.stem().string());
	int score = 0;
	if (!modelStem.empty() && candidateStem.find(modelStem) != std::string::npos) {
		score += 100;
	}
	if (!modelStemNoQuant.empty() &&
		candidateStem.find(modelStemNoQuant) != std::string::npos) {
		score += 75;
	}
	if (!modelStemNoQuant.empty() &&
		candidateStem.find("mmproj-" + modelStemNoQuant) != std::string::npos) {
		score += 25;
	}
	if (candidateStem.find("q8_0") != std::string::npos) {
		score += 5;
	}
	return score;
}
} // namespace

std::string findMatchingMmprojPath(const std::string & modelPath) {
	if (trim(modelPath).empty()) {
		return {};
	}
	std::error_code ec;
	const std::filesystem::path modelFile(modelPath);
	const std::filesystem::path modelDir = modelFile.parent_path();
	const std::string modelStem = lowerAscii(modelFile.stem().string());
	const std::string modelStemNoQuant = stripKnownVisionQuantSuffix(modelStem);
	if (modelDir.empty() || !std::filesystem::exists(modelDir, ec) || ec) {
		return {};
	}

	std::vector<std::filesystem::path> mmprojCandidates;
	for (const auto & entry : std::filesystem::directory_iterator(modelDir, ec)) {
		if (ec || !entry.is_regular_file()) {
			continue;
		}
		const std::string lowerName = lowerAscii(entry.path().filename().string());
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
	std::sort(
		mmprojCandidates.begin(),
		mmprojCandidates.end(),
		[&](const auto & lhs, const auto & rhs) {
			const int lhsScore = scoreMmprojCandidate(lhs, modelStem, modelStemNoQuant);
			const int rhsScore = scoreMmprojCandidate(rhs, modelStem, modelStemNoQuant);
			if (lhsScore != rhsScore) {
				return lhsScore > rhsScore;
			}
			return lhs.filename().string() < rhs.filename().string();
		});
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
