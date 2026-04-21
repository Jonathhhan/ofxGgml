#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

/// Common utilities shared across TTS adapters (ChatLlm, Piper, etc.)
namespace ofxGgmlTtsAdapterCommon {

/// Type alias for metadata key-value pairs used in TTS results.
using MetadataEntries = std::vector<std::pair<std::string, std::string>>;

/// Generate a unique temporary file path with the given extension.
///
/// Uses the system temp directory if available, falling back to current directory.
/// Generates unique filenames using microsecond-precision timestamps.
///
/// @param prefix Filename prefix (e.g., "ofxggml_tts_")
/// @param extension File extension including dot (e.g., ".wav", ".txt")
/// @return Absolute path to a unique temporary file
inline std::string makeTempPath(
	const char * prefix = "ofxggml_tts_",
	const char * extension = ".wav") {
	std::error_code ec;
	std::filesystem::path base = std::filesystem::temp_directory_path(ec);
	if (ec) {
		base = std::filesystem::current_path();
	}
	const auto stamp =
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	return (base / (std::string(prefix) + std::to_string(stamp) + extension)).string();
}

/// Generate a unique temporary output audio file path.
///
/// @param extension File extension including dot (default: ".wav")
/// @return Absolute path to a unique temporary audio file
inline std::string makeTempOutputPath(const char * extension = ".wav") {
	return makeTempPath("ofxggml_tts_", extension);
}

/// Generate a unique temporary input text file path.
///
/// @param extension File extension including dot (default: ".txt")
/// @return Absolute path to a unique temporary text file
inline std::string makeTempInputPath(const char * extension = ".txt") {
	return makeTempPath("ofxggml_tts_input_", extension);
}

/// Find the first existing executable from a list of candidate paths.
///
/// Iterates through the candidates and returns the first path that exists.
/// Uses std::filesystem::exists with error handling.
///
/// @param candidates List of filesystem paths to check
/// @return String path to first existing file, or empty string if none exist
inline std::string findFirstExistingExecutable(
	const std::vector<std::filesystem::path> & candidates) {
	for (const auto & candidate : candidates) {
		std::error_code ec;
		if (std::filesystem::exists(candidate, ec) && !ec) {
			return candidate.string();
		}
	}
	return {};
}

} // namespace ofxGgmlTtsAdapterCommon
