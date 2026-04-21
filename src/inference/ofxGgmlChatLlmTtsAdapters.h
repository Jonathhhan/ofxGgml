#pragma once

#include "ofFileUtils.h"
#include "ofxGgmlTtsInference.h"
#include "core/ofxGgmlWindowsUtf8.h"
#include "support/ofxGgmlProcessSecurity.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ofxGgmlChatLlmTtsAdapters {

struct RuntimeOptions {
	std::string executablePath;
	std::string defaultSpeakerName = "EN-FEMALE-1-NEUTRAL";
};

using MetadataEntries = std::vector<std::pair<std::string, std::string>>;

inline std::string trimCopy(const std::string & value) {
	size_t start = 0;
	size_t end = value.size();
	while (start < end &&
		std::isspace(static_cast<unsigned char>(value[start]))) {
		++start;
	}
	while (end > start &&
		std::isspace(static_cast<unsigned char>(value[end - 1]))) {
		--end;
	}
	return value.substr(start, end - start);
}

inline std::string lowerCopy(const std::string & value) {
	std::string lowered = value;
	for (char & c : lowered) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return lowered;
}

inline std::string summarizeModelLoadFailure(
	const std::string & rawOutput,
	const std::string & modelPath) {
	const std::string trimmedOutput = trimCopy(rawOutput);
	const std::string loweredOutput = lowerCopy(trimmedOutput);
	const std::string fileName = std::filesystem::path(modelPath).filename().string();

	if (loweredOutput.find("bad magic") != std::string::npos ||
		loweredOutput.find("model file is broken") != std::string::npos) {
		if (trimmedOutput.empty()) {
			return "The selected TTS model file was rejected by chatllm.cpp. "
				"It may not be a converted chatllm.cpp model artifact such as .bin or .ggmm.";
		}
		return "chatllm.cpp rejected the selected TTS model. "
			"Raw loader output: " + trimmedOutput;
	}

	return trimmedOutput;
}

inline void appendMetadataEntry(
	MetadataEntries & metadata,
	const std::string & key,
	const std::string & value) {
	const std::string trimmedKey = trimCopy(key);
	const std::string trimmedValue = trimCopy(value);
	if (trimmedKey.empty() || trimmedValue.empty()) {
		return;
	}
	metadata.emplace_back(trimmedKey, trimmedValue);
}

inline bool modelPathLooksLikeOuteTts(const std::string & modelPath) {
	const std::string loweredPath = lowerCopy(trimCopy(modelPath));
	return loweredPath.find("outetts") != std::string::npos;
}

inline std::string loweredModelExtension(const std::string & modelPath) {
	return lowerCopy(std::filesystem::path(trimCopy(modelPath)).extension().string());
}

inline std::string validateRequestForChatLlmTts(
	const ofxGgmlTtsRequest & request,
	MetadataEntries & metadata) {
	metadata.clear();
	appendMetadataEntry(metadata, "backendFamily", "chatllm.cpp");
	appendMetadataEntry(metadata, "backendModelFamily", "converted chatllm.cpp OuteTTS model");

	const std::string modelPath = trimCopy(request.modelPath);
	if (modelPath.empty()) {
		return "chatllm.cpp model path is empty";
	}

	const std::string extension = loweredModelExtension(modelPath);
	if (extension == ".gguf" || extension == ".safetensors") {
		return
			"chatllm.cpp TTS does not load raw model exports like .gguf or .safetensors here. "
			"Convert the Hugging Face OuteTTS model with chatllm.cpp's convert.py and load the generated .bin/.ggmm file instead.";
	}

	if (!modelPathLooksLikeOuteTts(modelPath)) {
		appendMetadataEntry(
			metadata,
			"compatibilityHint",
			"chatllm.cpp TTS is wired for converted OuteTTS artifacts produced by convert.py; unrelated files may fail to load.");
	}
	appendMetadataEntry(metadata, "conversionHint", "Use chatllm.cpp convert.py output (.bin/.ggmm), not raw GGUF");

	if (request.task == ofxGgmlTtsTask::CloneVoice &&
		trimCopy(request.speakerPath).empty()) {
		return
			"chatllm.cpp OuteTTS expects a prepared speaker.json profile for voice cloning. "
			"Creating speaker profiles from reference audio is not wired into this adapter yet.";
	}

	if (request.task == ofxGgmlTtsTask::ContinueSpeech) {
		return "Continue Speech is not wired into the chatllm.cpp adapter yet.";
	}

	appendMetadataEntry(metadata, "task", ofxGgmlTtsInference::taskLabel(request.task));
	appendMetadataEntry(metadata, "languageHint", trimCopy(request.language));
	if (!trimCopy(request.speakerReferencePath).empty()) {
		appendMetadataEntry(
			metadata,
			"speakerReferenceIgnored",
			trimCopy(request.speakerReferencePath));
	}
	if (request.streamAudio) {
		appendMetadataEntry(
			metadata,
			"streamAudioIgnored",
			"chatllm.cpp adapter exports a completed audio file");
	}
	if (!request.normalizeText) {
		appendMetadataEntry(metadata, "normalizeTextHint", "disabled");
	}

	return {};
}

inline bool hasPathSeparator(const std::string & value) {
	return value.find('\\') != std::string::npos ||
		value.find('/') != std::string::npos;
}

inline bool isDefaultExecutableHint(
	const std::string & executableHint,
	const std::vector<std::string> & canonicalNames) {
	const std::string trimmed = trimCopy(executableHint);
	if (trimmed.empty()) {
		return true;
	}
	if (hasPathSeparator(trimmed)) {
		return false;
	}
	const std::string fileName = lowerCopy(
		std::filesystem::path(trimmed).filename().string());
	return std::find(
		canonicalNames.begin(),
		canonicalNames.end(),
		fileName) != canonicalNames.end();
}

inline void appendUniquePath(
	std::vector<std::filesystem::path> & paths,
	const std::filesystem::path & candidate) {
	if (candidate.empty()) {
		return;
	}
	std::error_code ec;
	const std::filesystem::path normalized = candidate.lexically_normal();
	for (const auto & existing : paths) {
		if (existing.lexically_normal() == normalized) {
			return;
		}
	}
	paths.push_back(normalized);
}

inline std::vector<std::filesystem::path> executableSearchRoots() {
	std::vector<std::filesystem::path> roots;
	const std::filesystem::path exeDir(ofFilePath::getCurrentExeDir());
	appendUniquePath(roots, exeDir);
	appendUniquePath(roots, exeDir / ".." / ".." / "libs" / "chatllm" / "bin");
	appendUniquePath(roots, exeDir / ".." / ".." / "build" / "chatllm.cpp-build" / "bin");
	appendUniquePath(roots, exeDir / ".." / ".." / "build" / "chatllm.cpp-build" / "bin" / "Release");
	appendUniquePath(roots, exeDir / ".." / ".." / "build" / "chatllm-bld" / "bin");
	appendUniquePath(roots, exeDir / ".." / ".." / "build" / "chatllm-bld" / "bin" / "Release");

	std::error_code ec;
	const std::filesystem::path cwd = std::filesystem::current_path(ec);
	if (!ec) {
		appendUniquePath(roots, cwd);
		appendUniquePath(roots, cwd / "libs" / "chatllm" / "bin");
		appendUniquePath(roots, cwd / "build" / "chatllm.cpp-build" / "bin");
		appendUniquePath(roots, cwd / "build" / "chatllm.cpp-build" / "bin" / "Release");
		appendUniquePath(roots, cwd / "build" / "chatllm-bld" / "bin");
		appendUniquePath(roots, cwd / "build" / "chatllm-bld" / "bin" / "Release");
	}
	return roots;
}

inline std::string preferredLocalExecutablePath() {
#ifdef _WIN32
	const std::string executableName = "chatllm.exe";
#else
	const std::string executableName = "chatllm";
#endif
	const std::filesystem::path exeDir(ofFilePath::getCurrentExeDir());
	return (exeDir / ".." / ".." / "libs" / "chatllm" / "bin" / executableName)
		.lexically_normal()
		.string();
}

inline bool isPreferredLocalExecutablePath(const std::string & executableHint) {
	const std::string trimmed = trimCopy(executableHint);
	if (trimmed.empty()) {
		return false;
	}
	const std::filesystem::path lhs = std::filesystem::path(trimmed).lexically_normal();
	const std::filesystem::path rhs = std::filesystem::path(preferredLocalExecutablePath()).lexically_normal();
	return lhs == rhs;
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

#ifdef _WIN32
using ofxGgmlProcessSecurity::getEnvVarString;
using ofxGgmlProcessSecurity::quoteWindowsArg;
using ofxGgmlProcessSecurity::isWindowsBatchScript;
using ofxGgmlProcessSecurity::resolveWindowsLaunchPath;
#endif

/// Wrapper around ofxGgmlProcessSecurity::runCommandCapture with launchError support.
inline bool runCommandCapture(
	const std::vector<std::string> & args,
	std::string & output,
	int & exitCode,
	bool mergeStderr = true,
	std::string * launchError = nullptr) {
	if (launchError) {
		launchError->clear();
	}
	if (args.empty() || args.front().empty()) {
		if (launchError) {
			*launchError = "no executable was provided";
		}
		return false;
	}

	const bool success = ofxGgmlProcessSecurity::runCommandCapture(
		args, output, exitCode, mergeStderr);

	if (!success && launchError) {
		*launchError = "command execution failed";
	}

	return success;
}

inline std::string makeTempOutputPath(const char * extension = ".wav") {
	std::error_code ec;
	std::filesystem::path base = std::filesystem::temp_directory_path(ec);
	if (ec) {
		base = std::filesystem::current_path();
	}
	const auto stamp =
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	return (base / ("ofxggml_tts_" + std::to_string(stamp) + extension)).string();
}

inline std::string resolveChatLlmExecutable(const std::string & executableHint) {
#ifdef _WIN32
	static const std::vector<std::string> kCanonicalNames = {
		"chatllm.exe",
		"chatllm",
		"main.exe",
		"main"
	};
#else
	static const std::vector<std::string> kCanonicalNames = {
		"chatllm",
		"main"
	};
#endif

	const std::string trimmedHint = trimCopy(executableHint);
	const std::string fallback = trimmedHint.empty()
		? kCanonicalNames.front()
		: trimmedHint;
	if (!isDefaultExecutableHint(fallback, kCanonicalNames)) {
		return fallback;
	}

	std::vector<std::filesystem::path> candidates;
	const auto roots = executableSearchRoots();
	for (const auto & root : roots) {
		for (const auto & fileName : kCanonicalNames) {
			candidates.push_back(root / fileName);
		}
	}

	const std::string resolved = findFirstExistingExecutable(candidates);
	return resolved.empty() ? fallback : resolved;
}

inline bool isExplicitExecutablePath(const std::string & executableHint) {
	const std::string trimmed = trimCopy(executableHint);
	if (trimmed.empty()) {
		return false;
	}
	const std::filesystem::path path(trimmed);
	return path.is_absolute() || path.has_parent_path() || hasPathSeparator(trimmed);
}

inline std::shared_ptr<ofxGgmlTtsBackend> createBackend(
	const RuntimeOptions & options = {},
	const std::string & displayName = "ChatLLM TTS") {
	return ofxGgmlTtsInference::createChatLlmTtsBridgeBackend(
		[options, displayName](const ofxGgmlTtsRequest & request) {
			ofxGgmlTtsResult result;
			result.backendName = displayName;

			result.error = validateRequestForChatLlmTts(request, result.metadata);
			if (!result.error.empty()) {
				return result;
			}
			const std::string modelPath = trimCopy(request.modelPath);

			std::string outputPath = trimCopy(request.outputPath);
			if (outputPath.empty()) {
				outputPath = makeTempOutputPath(".wav");
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

			const std::string executable = resolveChatLlmExecutable(
				options.executablePath);
			if (isExplicitExecutablePath(options.executablePath)) {
				std::error_code execEc;
				if (!std::filesystem::exists(std::filesystem::path(executable), execEc) || execEc) {
					if (isPreferredLocalExecutablePath(options.executablePath)) {
						result.error =
							"chatllm.cpp executable was not found in the addon-local runtime. "
							"Build it with scripts/build-chatllm.ps1 or set Executable to a working chatllm.cpp binary.";
					} else {
						result.error =
							"chatllm.cpp executable not found: " + trimCopy(options.executablePath);
					}
					return result;
				}
			}
			const auto started = std::chrono::steady_clock::now();

			std::vector<std::string> args;
			args.reserve(24);
			args.push_back(executable);
			args.push_back("-m");
			args.push_back(modelPath);
			args.push_back("-p");
			args.push_back(request.text);
			args.push_back("--tts_export");
			args.push_back(outputPath);
			args.push_back("--temp");
			args.push_back(std::to_string(request.temperature));
			args.push_back("--repeat_penalty");
			args.push_back(std::to_string(request.repetitionPenalty));
			args.push_back("--penalty_window");
			args.push_back(std::to_string(std::max(0, request.repetitionRange)));
			args.push_back("--top_k");
			args.push_back(std::to_string(std::max(0, request.topK)));
			args.push_back("--top_p");
			args.push_back(std::to_string(request.topP));

			const std::string speakerPath = trimCopy(request.speakerPath);
			if (!speakerPath.empty()) {
				args.push_back("--set");
				args.push_back("speaker");
				args.push_back(speakerPath);
			}

			if (!trimCopy(request.language).empty()) {
				result.metadata.emplace_back(
					"languageHint",
					trimCopy(request.language));
			}
			if (!trimCopy(request.speakerReferencePath).empty()) {
				result.metadata.emplace_back(
					"speakerReferenceIgnored",
					trimCopy(request.speakerReferencePath));
			}
			if (request.streamAudio) {
				result.metadata.emplace_back(
					"streamAudioIgnored",
					"chatllm.cpp adapter exports a completed audio file");
			}
			if (!request.normalizeText) {
				result.metadata.emplace_back(
					"normalizeTextHint",
					"disabled");
			}

			std::string rawOutput;
			std::string launchError;
			int exitCode = -1;
			if (!runCommandCapture(args, rawOutput, exitCode, true, &launchError)) {
				result.error = "failed to start chatllm.cpp TTS process";
				if (!launchError.empty()) {
					result.error += ": " + launchError;
				}
				return result;
			}

			result.rawOutput = rawOutput;
			result.elapsedMs = std::chrono::duration<float, std::milli>(
				std::chrono::steady_clock::now() - started).count();
			result.speakerPath = speakerPath;
			result.metadata.emplace_back("modelPath", modelPath);
			result.metadata.emplace_back("outputPath", outputPath);

			std::error_code existsEc;
			const bool hasOutputFile =
				std::filesystem::exists(std::filesystem::path(outputPath), existsEc) &&
				!existsEc;
			if (exitCode != 0 || !hasOutputFile) {
				result.error = summarizeModelLoadFailure(rawOutput, modelPath);
				if (result.error.empty()) {
					if (exitCode != 0) {
						result.error =
							"chatllm.cpp TTS failed with exit code " +
							std::to_string(exitCode);
					} else {
						result.error =
							"chatllm.cpp TTS did not produce an output file";
					}
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
	const std::string & displayName = "ChatLLM TTS") {
	inference.setBackend(createBackend(options, displayName));
}

} // namespace ofxGgmlChatLlmTtsAdapters
