#include "catch2.hpp"
#include "../src/ofxGgml.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path makeVisionTestDir(const std::string & name) {
	const auto base = std::filesystem::temp_directory_path() / "ofxggml_vision_tests";
	std::filesystem::create_directories(base);
	const auto dir = base / (name + "_" + std::to_string(std::rand()));
	std::filesystem::create_directories(dir);
	return dir;
}

std::string createFakeImageFile(const std::string & extension, const std::string & bytes) {
	const auto dir = makeVisionTestDir("image");
	const auto file = dir / ("sample" + extension);
	std::ofstream out(file, std::ios::binary);
	out << bytes;
	return file.string();
}

} // namespace

TEST_CASE("Vision inference provides default multimodal profiles", "[vision_inference]") {
	const auto profiles = ofxGgmlVisionInference::defaultProfiles();
	REQUIRE(profiles.size() >= 3);
	REQUIRE_FALSE(profiles[0].name.empty());
	REQUIRE_FALSE(profiles[0].serverUrl.empty());
	REQUIRE_FALSE(profiles[0].modelFileHint.empty());
}

TEST_CASE("Vision inference builds task-aware prompts", "[vision_inference]") {
	ofxGgmlVisionRequest request;
	request.task = ofxGgmlVisionTask::Ocr;
	request.responseLanguage = "English";

	const auto prepared = ofxGgmlVisionInference::preparePrompt(request);
	REQUIRE(prepared.systemPrompt.find("OCR assistant") != std::string::npos);
	REQUIRE(prepared.userPrompt.find("Extract all readable text") != std::string::npos);
	REQUIRE(prepared.userPrompt.find("Respond in English.") != std::string::npos);
}

TEST_CASE("Vision inference detects common image mime types", "[vision_inference]") {
	REQUIRE(ofxGgmlVisionInference::detectMimeType("photo.png") == "image/png");
	REQUIRE(ofxGgmlVisionInference::detectMimeType("photo.jpg") == "image/jpeg");
	REQUIRE(ofxGgmlVisionInference::detectMimeType("photo.webp") == "image/webp");
}

TEST_CASE("Vision inference encodes image files and builds server payloads", "[vision_inference]") {
	const std::string imagePath = createFakeImageFile(".png", "fake-image-bytes");

	ofxGgmlVisionModelProfile profile;
	profile.name = "Local vision server";
	profile.serverUrl = "http://127.0.0.1:8080";
	profile.modelRepoHint = "unsloth/Qwen3.5-4B-GGUF";

	ofxGgmlVisionRequest request;
	request.task = ofxGgmlVisionTask::Ocr;
	request.prompt = "What is visible in this screenshot?";
	request.images.push_back({imagePath, "Screenshot", ""});

	const std::string encoded = ofxGgmlVisionInference::encodeImageFileBase64(imagePath);
	REQUIRE_FALSE(encoded.empty());

	const std::string payload = ofxGgmlVisionInference::buildChatCompletionsJson(profile, request);
	REQUIRE(payload.find("\"type\":\"image_url\"") != std::string::npos);
	REQUIRE(payload.find("data:image/png;base64,") != std::string::npos);
	REQUIRE(payload.find("What is visible in this screenshot?") != std::string::npos);
	REQUIRE(payload.find("unsloth/Qwen3.5-4B-GGUF") != std::string::npos);
	REQUIRE(payload.find("Image 1: Screenshot") != std::string::npos);
	REQUIRE(payload.find("\"detail\":\"high\"") != std::string::npos);
}

TEST_CASE("Vision inference normalizes llama-server URLs", "[vision_inference]") {
	REQUIRE(
		ofxGgmlVisionInference::normalizeServerUrl("http://127.0.0.1:8080") ==
		"http://127.0.0.1:8080/v1/chat/completions");
	REQUIRE(
		ofxGgmlVisionInference::normalizeServerUrl("http://127.0.0.1:8080/v1") ==
		"http://127.0.0.1:8080/v1/chat/completions");
	REQUIRE(
		ofxGgmlVisionInference::normalizeServerUrl("http://127.0.0.1:8080/v1/chat/completions") ==
		"http://127.0.0.1:8080/v1/chat/completions");
}
