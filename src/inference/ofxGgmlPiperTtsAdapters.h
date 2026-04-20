#pragma once

#include "ofxGgmlChatLlmTtsAdapters.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace ofxGgmlPiperTtsAdapters {

struct RuntimeOptions {
	std::string executablePath;
};

using MetadataEntries = std::vector<std::pair<std::string, std::string>>;

inline std::string defaultExecutableHint() {
#ifdef _WIN32
	return "piper.bat";
#else
	return "piper";
#endif
}

inline std::vector<std::filesystem::path> executableSearchRoots() {
	std::vector<std::filesystem::path> roots;
	const std::filesystem::path exeDir(ofFilePath::getCurrentExeDir());
	ofxGgmlChatLlmTtsAdapters::appendUniquePath(roots, exeDir);
	ofxGgmlChatLlmTtsAdapters::appendUniquePath(roots, exeDir / ".." / ".." / "libs" / "piper" / "bin");

	std::error_code ec;
	const std::filesystem::path cwd = std::filesystem::current_path(ec);
	if (!ec) {
		ofxGgmlChatLlmTtsAdapters::appendUniquePath(roots, cwd);
		ofxGgmlChatLlmTtsAdapters::appendUniquePath(roots, cwd / "libs" / "piper" / "bin");
	}
	return roots;
}

inline std::string preferredLocalExecutablePath() {
#ifdef _WIN32
	const std::string executableName = "piper.bat";
#else
	const std::string executableName = "piper";
#endif
	const std::filesystem::path exeDir(ofFilePath::getCurrentExeDir());
	return (exeDir / ".." / ".." / "libs" / "piper" / "bin" / executableName)
		.lexically_normal()
		.string();
}

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

inline bool looksLikePythonLauncher(const std::string & executable) {
	const std::string fileName =
		ofxGgmlChatLlmTtsAdapters::lowerCopy(
			std::filesystem::path(
				ofxGgmlChatLlmTtsAdapters::trimCopy(executable))
				.filename()
				.string());
	return fileName == "python" || fileName == "python.exe" ||
		fileName == "python3" || fileName == "python3.exe" ||
		fileName == "py" || fileName == "py.exe";
}

inline bool looksLikeChatLlmExecutable(const std::string & executable) {
	const std::string fileName =
		ofxGgmlChatLlmTtsAdapters::lowerCopy(
			std::filesystem::path(
				ofxGgmlChatLlmTtsAdapters::trimCopy(executable))
				.filename()
				.string());
	return fileName == "chatllm.exe" || fileName == "chatllm" ||
		fileName == "main.exe" || fileName == "main";
}

inline std::string expectedConfigPathForModel(const std::string & modelPath) {
	const std::string trimmedModelPath =
		ofxGgmlChatLlmTtsAdapters::trimCopy(modelPath);
	if (trimmedModelPath.empty()) {
		return {};
	}
	return trimmedModelPath + ".json";
}

inline std::string voiceNameFromModelPath(const std::string & modelPath) {
	const std::filesystem::path path(
		ofxGgmlChatLlmTtsAdapters::trimCopy(modelPath));
	return path.stem().string();
}

inline std::string dataDirFromModelPath(const std::string & modelPath) {
	const std::filesystem::path path(
		ofxGgmlChatLlmTtsAdapters::trimCopy(modelPath));
	return path.parent_path().string();
}

inline std::string makeTempInputPath(const char * extension = ".txt") {
	std::error_code ec;
	std::filesystem::path base = std::filesystem::temp_directory_path(ec);
	if (ec) {
		base = std::filesystem::current_path();
	}
	const auto stamp =
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	return (base / ("ofxggml_tts_input_" + std::to_string(stamp) + extension)).string();
}

inline std::string validateRequestForPiper(
	const ofxGgmlTtsRequest & request,
	MetadataEntries & metadata) {
	metadata.clear();
	ofxGgmlChatLlmTtsAdapters::appendMetadataEntry(
		metadata,
		"backendFamily",
		"piper");
	ofxGgmlChatLlmTtsAdapters::appendMetadataEntry(
		metadata,
		"backendModelFamily",
		"Piper ONNX voice");

	const std::string modelPath =
		ofxGgmlChatLlmTtsAdapters::trimCopy(request.modelPath);
	if (modelPath.empty()) {
		return "Piper model path is empty.";
	}

	const std::string extension =
		ofxGgmlChatLlmTtsAdapters::loweredModelExtension(modelPath);
	if (extension != ".onnx") {
		return
			"Piper expects a voice model file ending in .onnx. "
			"Select the Piper voice model and keep the matching .onnx.json file next to it.";
	}

	if (request.task == ofxGgmlTtsTask::CloneVoice) {
		return
			"Clone Voice is not wired into the Piper adapter yet. "
			"Use a prepared Piper voice instead.";
	}
	if (request.task == ofxGgmlTtsTask::ContinueSpeech) {
		return "Continue Speech is not wired into the Piper adapter yet.";
	}

	const std::string configPath = expectedConfigPathForModel(modelPath);
	std::error_code configEc;
	if (!configPath.empty() &&
		!std::filesystem::exists(std::filesystem::path(configPath), configEc)) {
		return
			"Piper also needs the matching voice config file next to the model: " +
			configPath;
	}
	ofxGgmlChatLlmTtsAdapters::appendMetadataEntry(
		metadata,
		"modelConfigPath",
		configPath);

	ofxGgmlChatLlmTtsAdapters::appendMetadataEntry(
		metadata,
		"task",
		ofxGgmlTtsInference::taskLabel(request.task));
	ofxGgmlChatLlmTtsAdapters::appendMetadataEntry(
		metadata,
		"languageHint",
		ofxGgmlChatLlmTtsAdapters::trimCopy(request.language));

	const std::string speakerHint =
		ofxGgmlChatLlmTtsAdapters::trimCopy(request.speakerPath);
	if (!speakerHint.empty()) {
		ofxGgmlChatLlmTtsAdapters::appendMetadataEntry(
			metadata,
			"speakerHint",
			speakerHint);
	}
	if (!ofxGgmlChatLlmTtsAdapters::trimCopy(request.speakerReferencePath).empty()) {
		ofxGgmlChatLlmTtsAdapters::appendMetadataEntry(
			metadata,
			"speakerReferenceIgnored",
			ofxGgmlChatLlmTtsAdapters::trimCopy(request.speakerReferencePath));
	}
	if (!ofxGgmlChatLlmTtsAdapters::trimCopy(request.promptAudioPath).empty()) {
		ofxGgmlChatLlmTtsAdapters::appendMetadataEntry(
			metadata,
			"promptAudioIgnored",
			ofxGgmlChatLlmTtsAdapters::trimCopy(request.promptAudioPath));
	}
	if (request.streamAudio) {
		ofxGgmlChatLlmTtsAdapters::appendMetadataEntry(
			metadata,
			"streamAudioIgnored",
			"Piper adapter exports a completed audio file");
	}
	if (!request.normalizeText) {
		ofxGgmlChatLlmTtsAdapters::appendMetadataEntry(
			metadata,
			"normalizeTextHint",
			"Piper CLI normalization is not controlled by this toggle");
	}
	ofxGgmlChatLlmTtsAdapters::appendMetadataEntry(
		metadata,
		"samplingHint",
		"Piper ignores LLM-style token sampling controls");

	return {};
}

inline std::string resolvePiperExecutable(const std::string & executableHint) {
	const std::string trimmed =
		ofxGgmlChatLlmTtsAdapters::trimCopy(executableHint);
	if (!trimmed.empty()) {
		return trimmed;
	}

#ifdef _WIN32
	static const std::vector<std::string> kCanonicalNames = {
		"piper.bat",
		"piper.exe",
		"piper"
	};
#else
	static const std::vector<std::string> kCanonicalNames = {
		"piper"
	};
#endif

	std::vector<std::filesystem::path> candidates;
	for (const auto & root : executableSearchRoots()) {
		for (const auto & fileName : kCanonicalNames) {
			candidates.push_back(root / fileName);
		}
	}

	const std::string resolved = findFirstExistingExecutable(candidates);
	return resolved.empty() ? defaultExecutableHint() : resolved;
}

inline std::shared_ptr<ofxGgmlTtsBackend> createBackend(
	const RuntimeOptions & options = {},
	const std::string & displayName = "Piper TTS") {
	return ofxGgmlTtsInference::createPiperTtsBridgeBackend(
		[options, displayName](const ofxGgmlTtsRequest & request) {
			ofxGgmlTtsResult result;
			result.backendName = displayName;

			result.error = validateRequestForPiper(request, result.metadata);
			if (!result.error.empty()) {
				return result;
			}

			const std::string modelPath =
				ofxGgmlChatLlmTtsAdapters::trimCopy(request.modelPath);
			const std::string voiceName = voiceNameFromModelPath(modelPath);
			const std::string dataDir = dataDirFromModelPath(modelPath);
			std::string outputPath =
				ofxGgmlChatLlmTtsAdapters::trimCopy(request.outputPath);
			if (outputPath.empty()) {
				outputPath = ofxGgmlChatLlmTtsAdapters::makeTempOutputPath(".wav");
			}

			const std::filesystem::path outputDir =
				std::filesystem::path(outputPath).parent_path();
			if (!outputDir.empty()) {
				std::error_code dirEc;
				std::filesystem::create_directories(outputDir, dirEc);
				if (dirEc) {
					result.error =
						"failed to create output directory: " + outputDir.string();
					return result;
				}
			}

			const std::string executable =
				resolvePiperExecutable(options.executablePath);
			if (looksLikeChatLlmExecutable(executable)) {
				result.error =
					"Piper TTS is currently pointed at a chatllm.cpp executable: " +
					executable +
					". Clear the Executable field or click 'Use local' in the Piper profile so it can launch Piper instead.";
				result.metadata.emplace_back("resolvedExecutable", executable);
				return result;
			}
			const auto started = std::chrono::steady_clock::now();
			const std::string inputPath = makeTempInputPath(".txt");
			{
				std::ofstream inputFile(inputPath, std::ios::binary);
				if (!inputFile) {
					result.error = "failed to create Piper input file: " + inputPath;
					return result;
				}
				inputFile << request.text;
				if (!inputFile.good()) {
					result.error = "failed to write Piper input file: " + inputPath;
					return result;
				}
			}

			std::vector<std::string> args;
			args.reserve(16);
			args.push_back(executable);
			if (looksLikePythonLauncher(executable)) {
				args.push_back("-m");
				args.push_back("piper");
			}
			args.push_back("-m");
			args.push_back(voiceName);
			if (!dataDir.empty()) {
				args.push_back("--data-dir");
				args.push_back(dataDir);
			}
			args.push_back("-f");
			args.push_back(outputPath);
			args.push_back("--input-file");
			args.push_back(inputPath);

			const std::string speakerHint =
				ofxGgmlChatLlmTtsAdapters::trimCopy(request.speakerPath);
			if (!speakerHint.empty()) {
				args.push_back("--speaker");
				args.push_back(speakerHint);
			}

			std::string rawOutput;
			std::string launchError;
			int exitCode = -1;
			if (!ofxGgmlChatLlmTtsAdapters::runCommandCapture(
				args,
				rawOutput,
				exitCode,
				true,
				&launchError)) {
				result.error = "failed to start Piper TTS process";
				if (!launchError.empty()) {
					result.error += ": " + launchError;
				}
				result.error +=
					". Set Executable to piper, piper.exe, or a Python launcher with the Piper module installed.";
				std::error_code cleanupEc;
				std::filesystem::remove(std::filesystem::path(inputPath), cleanupEc);
				return result;
			}

			result.rawOutput = rawOutput;
			result.elapsedMs = std::chrono::duration<float, std::milli>(
				std::chrono::steady_clock::now() - started).count();
			result.speakerPath = speakerHint;
			result.metadata.emplace_back("resolvedExecutable", executable);
			result.metadata.emplace_back("voiceName", voiceName);
			result.metadata.emplace_back("dataDir", dataDir);
			result.metadata.emplace_back("modelPath", modelPath);
			result.metadata.emplace_back("outputPath", outputPath);
			std::error_code cleanupEc;
			std::filesystem::remove(std::filesystem::path(inputPath), cleanupEc);

			std::error_code existsEc;
			const bool hasOutputFile =
				std::filesystem::exists(std::filesystem::path(outputPath), existsEc) &&
				!existsEc;
			if (exitCode != 0 || !hasOutputFile) {
				const std::string trimmedOutput =
					ofxGgmlChatLlmTtsAdapters::trimCopy(rawOutput);
				if (!trimmedOutput.empty()) {
					result.error = "Piper failed via " + executable + ": " + trimmedOutput;
				} else if (exitCode != 0) {
					result.error =
						"Piper failed via " + executable +
						" with exit code " + std::to_string(exitCode);
				} else {
					result.error = "Piper did not produce an output file.";
				}
				return result;
			}

			result.success = true;
			result.audioFiles.push_back({outputPath, 0, 0, 0.0f});
			return result;
		},
		displayName);
}

inline void attachBackend(
	ofxGgmlTtsInference & inference,
	const RuntimeOptions & options = {},
	const std::string & displayName = "Piper TTS") {
	inference.setBackend(createBackend(options, displayName));
}

} // namespace ofxGgmlPiperTtsAdapters
