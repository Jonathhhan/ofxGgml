#include "ofApp.h"

#include "utils/ImGuiHelpers.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace {

std::string sanitizeFilenameStem(const std::string & text) {
	std::string sanitized;
	sanitized.reserve(text.size());
	for (unsigned char c : text) {
		if (std::isalnum(c)) {
			sanitized.push_back(static_cast<char>(c));
		} else if (c == '-' || c == '_') {
			sanitized.push_back(static_cast<char>(c));
		} else if (std::isspace(c)) {
			sanitized.push_back('_');
		}
	}
	return sanitized.empty() ? "image_search" : sanitized;
}

std::string extensionFromUrl(const std::string & url) {
	const size_t queryPos = url.find_first_of("?#");
	const std::string pathOnly =
		queryPos == std::string::npos ? url : url.substr(0, queryPos);
	std::string ext = std::filesystem::path(pathOnly).extension().string();
	std::transform(
		ext.begin(),
		ext.end(),
		ext.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" ||
		ext == ".webp" || ext == ".gif" || ext == ".bmp") {
		return ext;
	}
	return ".jpg";
}

} // namespace

bool ofApp::cacheImageSearchResultAsset(
	const ofxGgmlImageSearchItem & item,
	bool preferThumbnail,
	std::string & localPath,
	std::string & errorMessage) const {
	localPath.clear();
	errorMessage.clear();

	const std::string remoteUrl =
		preferThumbnail && !trim(item.thumbnailUrl).empty()
			? trim(item.thumbnailUrl)
			: trim(item.imageUrl);
	if (remoteUrl.empty()) {
		errorMessage = "Selected search result does not include a downloadable image URL.";
		return false;
	}

	const std::string cacheDir = ofToDataPath("cache/image_search", true);
	std::error_code ec;
	std::filesystem::create_directories(cacheDir, ec);
	const std::string fileName =
		sanitizeFilenameStem(item.title) + "_" +
		std::to_string(std::hash<std::string>{}(remoteUrl + (preferThumbnail ? "|thumb" : "|full"))) +
		extensionFromUrl(remoteUrl);
	localPath = ofFilePath::join(cacheDir, fileName);
	if (std::filesystem::exists(localPath, ec) && !ec) {
		return true;
	}

	const ofHttpResponse response = ofLoadURL(remoteUrl);
	if (response.status != 200) {
		errorMessage =
			"Image download failed with HTTP " + ofToString(response.status) + ".";
		localPath.clear();
		return false;
	}
	if (!ofBufferToFile(localPath, response.data, true)) {
		errorMessage = "Failed to save downloaded image into cache.";
		localPath.clear();
		return false;
	}
	return true;
}

void ofApp::ensureImageSearchPreviewResources() {
	std::string previewPath;
	std::string previewError;
	const bool validSelection =
		selectedImageSearchResultIndex >= 0 &&
		selectedImageSearchResultIndex < static_cast<int>(imageSearchResults.size());
	if (validSelection) {
		std::string cachedPath;
		if (cacheImageSearchResultAsset(
				imageSearchResults[static_cast<size_t>(selectedImageSearchResultIndex)],
				true,
				cachedPath,
				previewError)) {
			previewPath = cachedPath;
			imageSearchPreviewSourceUrl =
				imageSearchResults[static_cast<size_t>(selectedImageSearchResultIndex)].pageUrl;
		}
	}
	if (!previewError.empty()) {
		imageSearchPreviewImage.clear();
		imageSearchPreviewLoadedPath.clear();
		imageSearchPreviewError = previewError;
		return;
	}
	imageSearchPreviewError.clear();
	ensureLocalImagePreview(
		previewPath,
		imageSearchPreviewImage,
		imageSearchPreviewLoadedPath,
		imageSearchPreviewError);
}

void ofApp::useImageSearchResultForVision(size_t index) {
	if (index >= imageSearchResults.size()) {
		return;
	}
	std::string cachedPath;
	std::string errorMessage;
	if (!cacheImageSearchResultAsset(
			imageSearchResults[index],
			false,
			cachedPath,
			errorMessage)) {
		imageSearchOutput = "[Error] " + errorMessage;
		return;
	}
	copyStringToBuffer(visionImagePath, sizeof(visionImagePath), cachedPath);
	imageSearchOutput =
		"Downloaded search result for Vision: " +
		ofFilePath::getFileName(cachedPath);
	autoSaveSession();
}

void ofApp::useImageSearchResultForDiffusion(size_t index) {
	if (index >= imageSearchResults.size()) {
		return;
	}
	std::string cachedPath;
	std::string errorMessage;
	if (!cacheImageSearchResultAsset(
			imageSearchResults[index],
			false,
			cachedPath,
			errorMessage)) {
		imageSearchOutput = "[Error] " + errorMessage;
		return;
	}
	setDiffusionInitImagePath(cachedPath, true);
	imageSearchOutput =
		"Downloaded search result for Diffusion: " +
		ofFilePath::getFileName(cachedPath);
	autoSaveSession();
}

void ofApp::drawImageSearchPanel(
	const char * copyPromptButtonLabel,
	const std::string & suggestedPrompt) {
	ImGui::Separator();
	ImGui::Text("Internet Image Search");
	ImGui::TextWrapped(
		"Searches Wikimedia Commons from a text prompt, then lets you pull a result straight into Vision or Diffusion.");

	if (hasDeferredImageSearchPrompt) {
		copyStringToBuffer(
			imageSearchPrompt,
			sizeof(imageSearchPrompt),
			deferredImageSearchPrompt);
		hasDeferredImageSearchPrompt = false;
		deferredImageSearchPrompt.clear();
	}

	ImGui::InputText(
		"Search prompt",
		imageSearchPrompt,
		sizeof(imageSearchPrompt));
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}
	ImGui::SameLine();
	if (ImGui::Button(copyPromptButtonLabel, ImVec2(140, 0))) {
		deferredImageSearchPrompt = suggestedPrompt;
		hasDeferredImageSearchPrompt = true;
		autoSaveSession();
	}

	ImGui::SetNextItemWidth(180);
	ImGui::SliderInt("Max results", &imageSearchMaxResults, 1, 16);
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		autoSaveSession();
	}

	const bool searchPromptReady = !trim(imageSearchPrompt).empty();
	ImGui::BeginDisabled(generating.load() || !searchPromptReady);
	if (ImGui::Button("Search Images", ImVec2(160, 0))) {
		runImageSearch();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled("Provider: Wikimedia Commons");

	if (!imageSearchOutput.empty()) {
		ImGui::TextWrapped("%s", imageSearchOutput.c_str());
	}
	if (!imageSearchBackendName.empty()) {
		ImGui::TextDisabled(
			"Backend: %s  %.1f ms  Results: %d",
			imageSearchBackendName.c_str(),
			imageSearchElapsedMs,
			static_cast<int>(imageSearchResults.size()));
	}

	if (imageSearchResults.empty()) {
		return;
	}

	selectedImageSearchResultIndex = std::clamp(
		selectedImageSearchResultIndex,
		0,
		std::max(0, static_cast<int>(imageSearchResults.size()) - 1));
	ensureImageSearchPreviewResources();

	if (ImGui::BeginCombo(
			"Results",
			imageSearchResults[static_cast<size_t>(selectedImageSearchResultIndex)].title.c_str())) {
		for (int i = 0; i < static_cast<int>(imageSearchResults.size()); ++i) {
			const bool selected = (selectedImageSearchResultIndex == i);
			if (ImGui::Selectable(imageSearchResults[static_cast<size_t>(i)].title.c_str(), selected)) {
				selectedImageSearchResultIndex = i;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	const auto & selectedItem =
		imageSearchResults[static_cast<size_t>(selectedImageSearchResultIndex)];
	if (!selectedItem.description.empty()) {
		ImGui::TextWrapped("%s", selectedItem.description.c_str());
	}
	std::string sourceLine = selectedItem.sourceLabel;
	if (selectedItem.width > 0 && selectedItem.height > 0) {
		sourceLine +=
			"  " + ofToString(selectedItem.width) + " x " + ofToString(selectedItem.height);
	}
	ImGui::TextDisabled("Source: %s", sourceLine.c_str());

	drawLocalImagePreview(
		"Preview",
		imageSearchPreviewLoadedPath,
		imageSearchPreviewImage,
		imageSearchPreviewError,
		"##ImageSearchPreview");

	if (!trim(selectedItem.pageUrl).empty()) {
		if (ImGui::SmallButton("Open Source Page")) {
			ofLaunchBrowser(selectedItem.pageUrl);
		}
		ImGui::SameLine();
	}
	if (ImGui::SmallButton("Use for Vision")) {
		useImageSearchResultForVision(static_cast<size_t>(selectedImageSearchResultIndex));
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Use as Init Image")) {
		useImageSearchResultForDiffusion(static_cast<size_t>(selectedImageSearchResultIndex));
	}
}

void ofApp::runImageSearch() {
	if (generating.load()) return;

	const std::string prompt = trim(imageSearchPrompt);
	if (prompt.empty()) {
		return;
	}

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = activeMode;
	generatingStatus = "Searching internet images...";
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = "Searching Wikimedia Commons images...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	const size_t maxResults =
		static_cast<size_t>(std::clamp(imageSearchMaxResults, 1, 16));
	workerThread = std::thread([this, prompt, maxResults]() {
		try {
			ofxGgmlImageSearchRequest request;
			request.prompt = prompt;
			request.maxResults = maxResults;
			request.thumbnailWidth = 320;
			request.provider = ofxGgmlImageSearchProvider::WikimediaCommons;

			const ofxGgmlImageSearchResult result = imageSearch.search(request);
			{
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingImageSearchDirty = true;
				pendingImageSearchBackendName = result.providerName;
				pendingImageSearchElapsedMs = result.elapsedMs;
				pendingImageSearchResults = result.items;
				pendingImageSearchOutput =
					result.success
						? ("Found " + ofToString(static_cast<int>(result.items.size())) +
							" image result(s) for \"" + result.normalizedQuery + "\".")
						: ("[Error] " + result.error);
			}
		} catch (const std::exception & e) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingImageSearchDirty = true;
			pendingImageSearchBackendName.clear();
			pendingImageSearchElapsedMs = 0.0f;
			pendingImageSearchResults.clear();
			pendingImageSearchOutput =
				std::string("[Error] Image search failed: ") + e.what();
		} catch (...) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingImageSearchDirty = true;
			pendingImageSearchBackendName.clear();
			pendingImageSearchElapsedMs = 0.0f;
			pendingImageSearchResults.clear();
			pendingImageSearchOutput = "[Error] Image search failed.";
		}

		{
			std::lock_guard<std::mutex> lock(streamMutex);
			streamingOutput.clear();
		}
		generating.store(false);
	});
}
