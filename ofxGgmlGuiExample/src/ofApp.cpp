#include "ofApp.h"

#include "ImHelpers.h"
#include "utils/ImGuiHelpers.h"
#include "utils/TextPromptHelpers.h"
#include "utils/ModelHelpers.h"
#include "utils/SpeechHelpers.h"
#include "utils/AudioHelpers.h"
#include "utils/ServerHelpers.h"
#include "utils/VisionHelpers.h"
#include "utils/ProcessHelpers.h"
#include "utils/ConsoleHelpers.h"
#include "utils/PathHelpers.h"
#include "utils/BackendHelpers.h"
#include "utils/ScriptCommandHelpers.h"
#include "config/ModelPresets.h"
#include "ofJson.h"
#include "core/ofxGgmlWindowsUtf8.h"
#include "support/ofxGgmlSimpleSrtSubtitleParser.h"
#include "gguf.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <numeric>
#include <regex>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

// ---------------------------------------------------------------------------
// Static data
// ---------------------------------------------------------------------------

const char * ofApp::modeLabels[kModeCount] = {
	"Chat", "Script", "Summarize", "Write", "Translate", "Custom", "Video Essay", "Video", "Vision", "Speech", "TTS", "Image", "CLIP", "MilkDrop", "Easy"
};

const char * const kTextBackendLabels[] = {
	"CLI fallback",
	"llama-server"
};

const char * const kDefaultTextServerUrl = "http://127.0.0.1:8080";
const char * const kDefaultSpeechServerUrl = "http://127.0.0.1:8081";

namespace {
	constexpr std::array<int, 8> kSupportedDiffusionImageSizes = {128, 256, 384, 512, 640, 768, 896, 1024};
	const char * const kVideoStructureSettingLabels[] = {
		"Three-act cinematic",
		"Trailer / short-form",
		"Music video rise",
		"Loopable ambient",
		"Documentary / essay",
		"Product / brand spot"
	};
	const char * const kVideoPacingSettingLabels[] = {
		"Balanced rise",
		"Gentle build",
		"Aggressive escalation",
		"Punchy short-form"
	};

	std::string sanitizeFilenameStem(const std::string & text, const std::string & fallback = "output") {
		std::string sanitized;
		sanitized.reserve(text.size());
		for (unsigned char c : text) {
			if (std::isalnum(c)) {
				sanitized.push_back(static_cast<char>(c));
			} else if (c == '_' || c == '-') {
				sanitized.push_back(static_cast<char>(c));
			} else if (std::isspace(c)) {
				sanitized.push_back('_');
			}
		}
		return sanitized.empty() ? fallback : sanitized;
	}

	std::vector<std::string> collectGeneratedMontageClipPathsImpl(
		const std::string & videoEssayRenderPath,
		const std::string & montageRenderPath,
		std::string * statusOut) {
		struct CandidatePath {
			std::string path;
			std::string label;
		};

		const std::vector<CandidatePath> candidates = {
			{trim(videoEssayRenderPath), "video essay render"},
			{trim(montageRenderPath), "montage playlist render"}
		};

		std::vector<std::string> results;
		std::vector<std::string> notes;
		std::unordered_set<std::string> seen;
		for (const auto & candidate : candidates) {
			if (candidate.path.empty()) {
				continue;
			}
			if (!pathExists(candidate.path)) {
				notes.push_back("Skipped missing " + candidate.label + ": " + candidate.path);
				continue;
			}

			std::error_code ec;
			const std::filesystem::path canonicalPath =
				std::filesystem::weakly_canonical(std::filesystem::path(candidate.path), ec);
			const std::string normalizedPath =
				(ec ? std::filesystem::path(candidate.path) : canonicalPath)
					.lexically_normal()
					.string();
			if (!seen.insert(ofToLower(normalizedPath)).second) {
				continue;
			}
			results.push_back(normalizedPath);
		}

		if (statusOut != nullptr) {
			if (results.empty()) {
				*statusOut =
					notes.empty()
						? std::string("No generated video outputs are available yet. Render a video essay or montage playlist first.")
						: notes.front();
			} else {
				std::ostringstream status;
				status << "Found " << results.size() << " generated video output";
				if (results.size() != 1) {
					status << "s";
				}
				status << ".";
				if (!notes.empty()) {
					status << " " << notes.front();
				}
				*statusOut = status.str();
			}
		}

		return results;
	}

	bool populateMontageClipPlaylistBufferFromGeneratedOutputs(
		char * clipPathBuffer,
		size_t clipPathBufferSize,
		const std::string & videoEssayRenderPath,
		const std::string & montageRenderPath,
		std::string * statusOut) {
		std::string generatedStatus;
		const std::vector<std::string> generatedClipPaths =
			collectGeneratedMontageClipPathsImpl(
				videoEssayRenderPath,
				montageRenderPath,
				&generatedStatus);
		if (generatedClipPaths.empty()) {
			if (statusOut != nullptr) {
				*statusOut = generatedStatus;
			}
			return false;
		}

		std::vector<std::string> mergedClipPaths = extractPathList(clipPathBuffer);
		const size_t existingCount = mergedClipPaths.size();
		std::unordered_set<std::string> seen;
		for (const auto & path : mergedClipPaths) {
			seen.insert(ofToLower(trim(path)));
		}

		size_t addedCount = 0;
		for (const auto & path : generatedClipPaths) {
			if (!seen.insert(ofToLower(path)).second) {
				continue;
			}
			mergedClipPaths.push_back(path);
			++addedCount;
		}

		if (addedCount == 0) {
			if (statusOut != nullptr) {
				*statusOut = "The generated video outputs are already in the montage clip playlist.";
			}
			return true;
		}

		std::ostringstream packed;
		for (size_t i = 0; i < mergedClipPaths.size(); ++i) {
			if (i > 0) {
				packed << '\n';
			}
			packed << mergedClipPaths[i];
		}
		copyStringToBuffer(clipPathBuffer, clipPathBufferSize, packed.str());

		if (statusOut != nullptr) {
			std::ostringstream status;
			status << "Added " << addedCount << " generated video output";
			if (addedCount != 1) {
				status << "s";
			}
			status << " to the montage clip playlist";
			if (existingCount > 0) {
				status << " while keeping " << existingCount << " existing clip";
				if (existingCount != 1) {
					status << "s";
				}
			}
			status << ".";
			*statusOut = status.str();
		}
		return true;
	}

	struct TokenLiteral {
	const char * text;
	size_t len;
};

void stopPreviewAudioPlayback(
	TtsPreviewState & previewState,
	bool clearLoadedPath) {
	previewState.stopPlayback(clearLoadedPath);
}

std::string makeTempTtsSegmentPath(const std::string & label) {
	std::error_code ec;
	std::filesystem::path base = std::filesystem::temp_directory_path(ec);
	if (ec) {
		base = std::filesystem::current_path();
	}
	const auto now = std::chrono::system_clock::now();
	const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
		now.time_since_epoch()).count();
	const uint64_t nonceHi = static_cast<uint64_t>(std::random_device{}());
	const uint64_t nonceLo = static_cast<uint64_t>(std::random_device{}());
	const uint64_t nonce = (nonceHi << 32) | nonceLo;
	std::ostringstream name;
	name << "ofxggml_tts_" << label << "_" << millis << "_" << std::hex << nonce << ".wav";
	return (base / name.str()).string();
}

bool buildDialogWav(
	const std::vector<std::string> & inputPaths,
	const std::string & outputPath,
	ofxGgmlTtsAudioArtifact & artifactOut,
	std::string * errorMessage = nullptr) {
	if (errorMessage != nullptr) {
		*errorMessage = "";
	}
	if (inputPaths.empty() || trim(outputPath).empty()) {
		if (errorMessage != nullptr) {
			*errorMessage = "Dialog audio needs input clips and an output path.";
		}
		return false;
	}

	std::vector<float> mergedSamples;
	int sampleRate = 0;
	int channels = 0;
	for (const auto & path : inputPaths) {
		LoadedWavAudio audio;
		std::string localError;
		if (!loadWavFile(path, audio, &localError)) {
			if (errorMessage != nullptr) {
				*errorMessage = localError;
			}
			return false;
		}
		if (sampleRate == 0) {
			sampleRate = audio.sampleRate;
			channels = audio.channels;
		} else if (sampleRate != audio.sampleRate || channels != audio.channels) {
			if (errorMessage != nullptr) {
				*errorMessage =
					"Chat dialog audio clips used different sample formats and could not be stitched.";
			}
			return false;
		}
		mergedSamples.insert(
			mergedSamples.end(),
			audio.samples.begin(),
			audio.samples.end());
	}

	if (!writeWavFile(outputPath, mergedSamples, sampleRate, channels)) {
		if (errorMessage != nullptr) {
			*errorMessage = "Failed to write stitched chat dialog WAV file.";
		}
		return false;
	}

	artifactOut.path = outputPath;
	artifactOut.sampleRate = sampleRate;
	artifactOut.channels = channels;
	artifactOut.durationSeconds = sampleRate > 0
		? static_cast<float>(
			mergedSamples.size() /
			static_cast<double>(std::max(1, channels) * sampleRate))
		: 0.0f;
	return true;
}

bool ensurePreviewAudioLoaded(
	TtsPreviewState & previewState,
	std::string & statusMessage,
	const std::string & unavailableMessage,
	const std::string & emptyPathMessage,
	const std::string & missingPrefix,
	const std::string & loadFailurePrefix,
	const std::string & readyMessage,
	const std::string & playingMessage,
	int artifactIndex,
	bool autoplay) {
	auto & audioFiles = previewState.audioFiles;
	if (audioFiles.empty()) {
		statusMessage = unavailableMessage;
		return false;
	}

	const int clampedIndex = std::clamp(
		artifactIndex < 0 ? previewState.selectedAudioIndex : artifactIndex,
		0,
		std::max(0, static_cast<int>(audioFiles.size()) - 1));
	previewState.selectedAudioIndex = clampedIndex;

	const std::string audioPath =
		trim(audioFiles[static_cast<size_t>(previewState.selectedAudioIndex)].path);
	if (audioPath.empty()) {
		statusMessage = emptyPathMessage;
		return false;
	}
	if (!std::filesystem::exists(audioPath)) {
		statusMessage = missingPrefix + audioPath;
		return false;
	}

	if (previewState.loadedAudioPath != audioPath || !previewState.isAudioLoaded()) {
		LoadedWavAudio loadedAudio;
		std::string loadError;
		if (!loadWavFile(audioPath, loadedAudio, &loadError)) {
			statusMessage = loadFailurePrefix + audioPath;
			if (!trim(loadError).empty()) {
				statusMessage += " (" + trim(loadError) + ")";
			}
			return false;
		}
		if (loadedAudio.empty()) {
			statusMessage = loadFailurePrefix + audioPath + " (decoded audio was empty)";
			return false;
		}

		std::lock_guard<std::mutex> lock(previewState.playbackMutex);
		previewState.loadedAudioPath = audioPath;
		previewState.playbackSamples = std::move(loadedAudio.samples);
		previewState.playbackSampleRate = loadedAudio.sampleRate;
		previewState.playbackChannels = loadedAudio.channels;
		previewState.playbackPositionFrames = 0.0;
		previewState.playbackLoaded = true;
		previewState.playbackPaused = false;
		previewState.playbackActive = false;
	}

	if (autoplay) {
		previewState.restartPlayback();
		statusMessage = playingMessage;
	} else {
		statusMessage = readyMessage;
	}
	return true;
}

#ifdef _WIN32

#else

#endif

constexpr size_t kExePathBufSize = 4096; // buffer for resolving the executable path
constexpr float kDefaultTemp = 0.7f;
constexpr float kDefaultTopP = 0.9f;
constexpr float kDefaultRepeatPenalty = 1.1f;
constexpr int kExecNotFound = 127; // POSIX convention when execvp fails
constexpr float kSpinnerInterval = 0.15f;       // seconds per spinner frame
constexpr float kDotsAnimationSpeed = 3.0f;     // dots cycle speed multiplier
constexpr size_t kMaxScriptContextFiles = 50;
constexpr size_t kMaxFocusedFileSnippetChars = 2000;
constexpr auto kStreamUiUpdateInterval = std::chrono::milliseconds(50);
constexpr size_t kStreamUiMinGrowth = 256;
const char * const kWaitingLabels[] = {"generating", "generating.", "generating..", "generating..."};

// Llama CLI detection state shared between probe and UI.
// -1 = unknown / needs probe, 0 = probed but not found, 1 = available.
std::atomic<int> llamaCliState{-1};
std::string llamaCliCommand = "llama-completion";

// Child process tracking — allows stopGeneration() to kill a running
// llama-completion process instead of blocking until it finishes.
#ifdef _WIN32
std::atomic<HANDLE> inferenceProcessHandle{nullptr};
#else
std::atomic<pid_t> inferenceProcessPid{0};
#endif

// Kill the currently running inference child process, if any.
void killInferenceProcess() {
#ifdef _WIN32
	HANDLE h = inferenceProcessHandle.exchange(nullptr);
	if (h) {
		TerminateProcess(h, 1);
		// Do not close the handle here — runProcessCapture owns it.
	}
#else
	pid_t pid = inferenceProcessPid.exchange(0);
	if (pid > 0) {
		kill(pid, SIGKILL);
	}
#endif
}

#ifdef _WIN32
std::string quoteWindowsArg(const std::string & arg) {
	bool needsQuotes = arg.find_first_of(" \t\"%^&|<>") != std::string::npos;
	if (!needsQuotes) return arg;
	std::string out = "\"";
	size_t backslashes = 0;
	for (char c : arg) {
		if (c == '\\') {
			backslashes++;
			continue;
		}
		if (c == '"') {
			out.append(backslashes * 2 + 1, '\\');
			out.push_back('"');
			backslashes = 0;
			continue;
		}
		if (backslashes > 0) {
			out.append(backslashes, '\\');
			backslashes = 0;
		}
		out.push_back(c);
	}
	if (backslashes > 0) {
		out.append(backslashes * 2, '\\');
	}
	out.push_back('"');
	return out;
}
#endif

bool runProcessCapture(const std::vector<std::string> & args, std::string & output, int & exitCode,
                       bool trackProcess = false,
                       std::function<void(const std::string &)> onStreamData = nullptr,
                       bool mergeStderr = true) {
	output.clear();
	exitCode = -1;
	if (args.empty() || args[0].empty()) return false;

#ifdef _WIN32
	SECURITY_ATTRIBUTES sa{};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = nullptr;

	HANDLE readPipe = nullptr;
	HANDLE writePipe = nullptr;
	if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return false;
	if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0)) {
		CloseHandle(readPipe);
		CloseHandle(writePipe);
		return false;
	}

	STARTUPINFOA si{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	// Redirect stdin to NUL so that interactive / conversation modes
	// receive EOF and exit automatically after generation.
	HANDLE nullInput = CreateFileA("NUL", GENERIC_READ, 0, &sa,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	si.hStdInput = (nullInput != INVALID_HANDLE_VALUE)
		? nullInput : GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = writePipe;
	HANDLE nullErr = INVALID_HANDLE_VALUE;
	if (mergeStderr) {
		si.hStdError = writePipe;
	} else {
		// Discard stderr so verbose log output from the child process
		// does not pollute the captured stdout.
		nullErr = CreateFileA("NUL", GENERIC_WRITE, 0, &sa,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		si.hStdError = (nullErr != INVALID_HANDLE_VALUE)
			? nullErr : GetStdHandle(STD_ERROR_HANDLE);
	}

	PROCESS_INFORMATION pi{};
	std::string cmdLine;
	for (size_t i = 0; i < args.size(); i++) {
		if (i > 0) cmdLine += " ";
		cmdLine += quoteWindowsArg(args[i]);
	}
	std::vector<char> mutableCmd(cmdLine.begin(), cmdLine.end());
	mutableCmd.push_back('\0');

	BOOL ok = CreateProcessA(
		nullptr,
		mutableCmd.data(),
		nullptr,
		nullptr,
		TRUE,
		CREATE_NO_WINDOW,
		nullptr,
		nullptr,
		&si,
		&pi
	);
	CloseHandle(writePipe);
	if (nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);
	if (nullErr != INVALID_HANDLE_VALUE) CloseHandle(nullErr);
	if (!ok) {
		CloseHandle(readPipe);
		return false;
	}

	if (trackProcess) {
		inferenceProcessHandle.store(pi.hProcess);
	}

	char buffer[4096];
	DWORD bytesRead = 0;
	while (ReadFile(readPipe, buffer, static_cast<DWORD>(sizeof(buffer)), &bytesRead, nullptr) && bytesRead > 0) {
		output.append(buffer, bytesRead);
		if (onStreamData) onStreamData(output);
	}
	CloseHandle(readPipe);

	WaitForSingleObject(pi.hProcess, INFINITE);

	if (trackProcess) {
		inferenceProcessHandle.store(nullptr);
	}

	DWORD code = 1;
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	exitCode = static_cast<int>(code);
	return true;
#else
	int pipefd[2];
	if (pipe(pipefd) != 0) return false;

	pid_t pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return false;
	}

	if (pid == 0) {
		// Redirect stdin to /dev/null so that interactive / conversation
		// modes (e.g. llama-cli) receive EOF and exit automatically
		// after generation instead of blocking for more input.
		int devNull = open("/dev/null", O_RDONLY);
		if (devNull >= 0) {
			dup2(devNull, STDIN_FILENO);
			if (devNull != STDIN_FILENO) close(devNull);
		}
		dup2(pipefd[1], STDOUT_FILENO);
		if (mergeStderr) {
			dup2(pipefd[1], STDERR_FILENO);
		} else {
			// Discard stderr so verbose log output from the child
			// process does not pollute the captured stdout.
			int devNullW = open("/dev/null", O_WRONLY);
			if (devNullW >= 0) {
				dup2(devNullW, STDERR_FILENO);
				close(devNullW);
			} else {
				// Last resort: close stderr so nothing leaks into the pipe.
				close(STDERR_FILENO);
			}
		}
		close(pipefd[0]);
		close(pipefd[1]);

		std::vector<std::string> mutableArgs = args;
		std::vector<char *> argv;
		argv.reserve(mutableArgs.size() + 1);
		for (auto & a : mutableArgs) {
			argv.push_back(a.empty() ? const_cast<char *>("") : &a[0]);
		}
		argv.push_back(nullptr);
		execvp(argv[0], argv.data());
		_exit(127);
	}

	if (trackProcess) {
		inferenceProcessPid.store(pid);
	}

	close(pipefd[1]);
	char buffer[4096];
	ssize_t n = 0;
	while ((n = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
		output.append(buffer, static_cast<size_t>(n));
		if (onStreamData) onStreamData(output);
	}
	close(pipefd[0]);

	int status = 0;
	pid_t wp = waitpid(pid, &status, 0);

	if (trackProcess) {
		inferenceProcessPid.store(0);
	}

	if (wp < 0) {
		exitCode = -1;
		return false;
	}

	if (WIFEXITED(status)) {
		exitCode = WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		exitCode = -1;
	} else {
		exitCode = -1;
	}
	return true;
#endif
}

struct WorkspaceDiffSnapshot {
	bool success = false;
	bool hasChanges = false;
	std::string workspaceRoot;
	std::string repoRoot;
	std::string statusText;
	std::string diffText;
	std::string error;
};

WorkspaceDiffSnapshot captureWorkspaceDiffSnapshot(const std::string & workspaceRoot) {
	WorkspaceDiffSnapshot snapshot;
	snapshot.workspaceRoot = trim(workspaceRoot);
	if (snapshot.workspaceRoot.empty()) {
		snapshot.error = "Workspace change summary requires a loaded local folder.";
		return snapshot;
	}

	std::string output;
	int exitCode = -1;
	if (!runProcessCapture(
			{"git", "-C", snapshot.workspaceRoot, "rev-parse", "--show-toplevel"},
			output,
			exitCode) ||
		exitCode != 0) {
		snapshot.error =
			"Workspace change summary requires a local Git repository.";
		return snapshot;
	}
	snapshot.repoRoot = trim(stripAnsi(output));
	if (snapshot.repoRoot.empty()) {
		snapshot.error = "Unable to determine the Git repository root for this workspace.";
		return snapshot;
	}

	output.clear();
	exitCode = -1;
	if (!runProcessCapture(
			{"git", "-C", snapshot.repoRoot, "status", "--short", "--branch"},
			output,
			exitCode) ||
		exitCode != 0) {
		snapshot.error = "Failed to read Git status for the current workspace.";
		return snapshot;
	}
	snapshot.statusText = trim(stripAnsi(output));

	output.clear();
	exitCode = -1;
	const bool diffOk = runProcessCapture(
		{"git", "-C", snapshot.repoRoot, "diff", "--no-ext-diff", "--minimal", "HEAD", "--"},
		output,
		exitCode);
	if (diffOk && exitCode == 0) {
		snapshot.diffText = trim(stripAnsi(output));
	} else {
		output.clear();
		exitCode = -1;
		if (runProcessCapture(
				{"git", "-C", snapshot.repoRoot, "diff", "--no-ext-diff", "--minimal", "--"},
				output,
				exitCode) &&
			exitCode == 0) {
			snapshot.diffText = trim(stripAnsi(output));
		}
	}

	snapshot.hasChanges =
		(snapshot.statusText.find("##") != std::string::npos &&
			snapshot.statusText.find('\n') != std::string::npos) ||
		(!snapshot.statusText.empty() &&
			snapshot.statusText.rfind("##", 0) != 0) ||
		!snapshot.diffText.empty();
	snapshot.success = true;
	return snapshot;
}

}

// ---------------------------------------------------------------------------
// Probe for llama-completion / llama-cli / llama.
// Checks a user-supplied custom path first, then PATH, then common
// installation directories.  Updates llamaCliState and llamaCliCommand.
// Prefers llama-completion (one-shot text completion) over llama-cli
// (interactive chat mode since llama.cpp PR #17824).
// ---------------------------------------------------------------------------

static bool probeCandidate(const std::string & candidate,
                           const std::vector<std::string> & flags,
                           std::string & probeOut, int & probeExit) {
	for (const auto & flag : flags) {
		if (runProcessCapture({candidate, flag}, probeOut, probeExit)
			&& probeExit != kExecNotFound) {
			return true;
		}
	}
	return false;
}

static void probeLlamaCliImpl(
	const std::function<void(ofLogLevel, const std::string &)> & logger,
	const std::string & customPath = "") {
	std::string probeOut;
	int probeExit = -1;
	const std::vector<std::string> probeFlags = {"--version", "--help"};
	bool found = false;

	// 1. Try user-supplied custom path first.
	if (!customPath.empty()) {
		std::error_code ec;
		if (std::filesystem::exists(customPath, ec) && !ec) {
			if (probeCandidate(customPath, probeFlags, probeOut, probeExit)) {
				llamaCliCommand = customPath;
				found = true;
			}
		} else {
			if (logger) logger(OF_LOG_WARNING, "custom CLI path not found: " + customPath);
		}
	}

	// 2. Prefer addon-local installs first (libs/llama/bin), then PATH.
	// This avoids accidentally picking an older CPU-only llama executable
	// from PATH when a GPU-enabled bundled build is available.
	if (!found) {
		const std::vector<std::string> exeNames = {"llama-completion", "llama-cli", "llama"};
		std::vector<std::string> preferredDirs;

		// Addon-local libs/llama/bin (default install target for build script).
		{
			std::error_code srcEc;
			auto srcPath = std::filesystem::path(__FILE__).parent_path();
			auto addonRoot = std::filesystem::weakly_canonical(srcPath / ".." / "..", srcEc);
			if (!srcEc) {
				preferredDirs.push_back((addonRoot / "libs" / "llama" / "bin").string());
			}
		}

		for (const auto & dir : preferredDirs) {
			for (const auto & exe : exeNames) {
				std::string fullPath = dir +
#ifdef _WIN32
					"\\" + exe + ".exe";
#else
					"/" + exe;
#endif
				std::error_code ec;
				if (!std::filesystem::exists(fullPath, ec) || ec) continue;
				if (probeCandidate(fullPath, probeFlags, probeOut, probeExit)) {
					llamaCliCommand = fullPath;
					found = true;
					break;
				}
			}
			if (found) break;
		}
	}

	// 3. Try bare names via PATH (execvp search).
	// Prefer llama-completion (one-shot text completion) over llama-cli
	// (interactive chat, server-based since llama.cpp PR #17824).
	if (!found) {
		const std::vector<std::string> bareNames = {"llama-completion", "llama-cli", "llama"};
		for (const auto & name : bareNames) {
			if (probeCandidate(name, probeFlags, probeOut, probeExit)) {
				llamaCliCommand = name;
				found = true;
				break;
			}
		}
	}

	// 4. Try common installation directories.
	if (!found) {
		std::vector<std::string> searchDirs;

		// Check the directory of the running executable.
		// This handles the case where llama-completion was installed
		// next to the application binary (common deployment layout).
		{
#ifdef _WIN32
			std::vector<char> exeBuf(kExePathBufSize);
			DWORD len = GetModuleFileNameA(nullptr, exeBuf.data(), static_cast<DWORD>(exeBuf.size()));
			if (len > 0 && len < exeBuf.size()) {
				auto exeDir = std::filesystem::path(std::string(exeBuf.data(), len)).parent_path();
				searchDirs.push_back(exeDir.string());
			}
#elif defined(__linux__) || defined(__FreeBSD__)
			std::vector<char> exeBuf(kExePathBufSize);
			ssize_t exeLen = readlink("/proc/self/exe", exeBuf.data(), exeBuf.size());
			if (exeLen > 0 && static_cast<size_t>(exeLen) < exeBuf.size()) {
				auto exeDir = std::filesystem::path(std::string(exeBuf.data(), static_cast<size_t>(exeLen))).parent_path();
				searchDirs.push_back(exeDir.string());
			}
#elif defined(__APPLE__)
			// On macOS, use _NSGetExecutablePath or the current working
			// directory; the latter is usually the bundle's Contents/MacOS.
			char macBuf[kExePathBufSize];
			uint32_t macBufSize = sizeof(macBuf);
			if (_NSGetExecutablePath(macBuf, &macBufSize) == 0) {
				auto exeDir = std::filesystem::path(macBuf).parent_path();
				searchDirs.push_back(exeDir.string());
			}
#endif
		}

		// Check addon-local libs/llama/bin directory.  This is the
		// default install prefix used by scripts/build-llama-cli.sh on
		// Windows-like shells and can be used on any platform by
		// passing --prefix <addon>/libs/llama to the script.
		{
			std::error_code srcEc;
			auto srcPath = std::filesystem::path(__FILE__).parent_path();  // .../ofxGgmlGuiExample/src
			auto addonRoot = std::filesystem::weakly_canonical(srcPath / ".." / "..", srcEc);
			if (!srcEc) {
				searchDirs.push_back((addonRoot / "libs" / "llama" / "bin").string());
			}
		}

		// Also check relative to the current working directory in
		// case __FILE__ resolved to a path that no longer exists at
		// runtime (common with out-of-tree builds).
		{
			std::error_code ec;
			auto cwd = std::filesystem::current_path(ec);
			if (!ec) {
				searchDirs.push_back(cwd.string());
				// Try one level up from cwd (e.g., cwd is bin/,
				// llama-completion is in ../libs/llama/bin/).
				auto cwdParent = std::filesystem::weakly_canonical(cwd / "..", ec);
				if (!ec) {
					searchDirs.push_back((cwdParent / "libs" / "llama" / "bin").string());
				}
			}
		}

#ifdef _WIN32
		// On Windows, also check common Program Files locations.
		const std::string progFiles = getEnvVarString("ProgramFiles");
		if (!progFiles.empty()) {
			searchDirs.push_back(progFiles + "\\llama.cpp");
			searchDirs.push_back(progFiles + "\\LlamaCpp");
		}
		const std::string localAppData = getEnvVarString("LOCALAPPDATA");
		if (!localAppData.empty()) {
			searchDirs.push_back(localAppData + "\\llama.cpp");
		}
#else
		// Common POSIX install directories that may not be in PATH
		// when launched from a GUI / IDE.
		searchDirs.push_back("/usr/local/bin");
		searchDirs.push_back("/usr/bin");
#ifdef __APPLE__
		searchDirs.push_back("/opt/homebrew/bin");
#endif
		const std::string home = getEnvVarString("HOME");
		if (!home.empty()) {
			searchDirs.push_back(home + "/.local/bin");
		}
		searchDirs.push_back("/snap/bin");
#endif
		const std::vector<std::string> exeNames = {"llama-completion", "llama-cli", "llama"};
		for (const auto & dir : searchDirs) {
			for (const auto & exe : exeNames) {
				std::string fullPath = dir +
#ifdef _WIN32
					"\\" + exe + ".exe";
#else
					"/" + exe;
#endif
				std::error_code ec;
				if (!std::filesystem::exists(fullPath, ec) || ec) continue;
				if (probeCandidate(fullPath, probeFlags, probeOut, probeExit)) {
					llamaCliCommand = fullPath;
					found = true;
					break;
				}
			}
			if (found) break;
		}
	}

	if (!found) {
		llamaCliState.store(0, std::memory_order_relaxed);
		{
			if (logger) logger(
				OF_LOG_VERBOSE,
				"Optional CLI fallback (llama-completion/llama-cli/llama) not found in PATH or common directories.");
		}
		return;
	}
	{
		if (logger) logger(OF_LOG_NOTICE, "detected CLI: " + llamaCliCommand);
	}
	llamaCliState.store(1, std::memory_order_relaxed);
}

ofLogLevel ofApp::mapGgmlLogLevel(int level) const {
	switch (level) {
	case 4: return OF_LOG_ERROR;
	case 3: return OF_LOG_WARNING;
	case 2: return OF_LOG_NOTICE;
	case 1: return OF_LOG_VERBOSE;
	default: return OF_LOG_SILENT;
	}
}

void ofApp::probeLlamaCli(const std::string & customPath) {
	cliCapabilitiesProbed = false;
	probeLlamaCliImpl(
		[this](ofLogLevel lvl, const std::string & msg) { logWithLevel(lvl, msg); },
		customPath);
}

void ofApp::probeCliCapabilities() {
	if (cliCapabilitiesProbed) return;
	cliCapabilitiesProbed = true;
	cliSupportsTopK = true;
	cliSupportsMinP = true;
	cliSupportsMirostat = true;
	cliSupportsSingleTurn = true;

	std::string helpText;
	int exitCode = -1;
	if (!runProcessCapture({llamaCliCommand, "--help"}, helpText, exitCode) || helpText.empty()) {
		return;
	}

	const bool hasTopK = helpText.find("--top-k") != std::string::npos;
	const bool hasMinP = helpText.find("--min-p") != std::string::npos;
	const bool hasMirostat =
		helpText.find("--mirostat") != std::string::npos &&
		helpText.find("--mirostat-lr") != std::string::npos &&
		helpText.find("--mirostat-ent") != std::string::npos;
	const bool hasSingleTurn =
		helpText.find("--single-turn") != std::string::npos;

	cliSupportsTopK = hasTopK;
	cliSupportsMinP = hasMinP;
	cliSupportsMirostat = hasMirostat;
	cliSupportsSingleTurn = hasSingleTurn;

	if (!cliSupportsTopK) {
		logWithLevel(OF_LOG_WARNING, "Detected CLI does not support --top-k; Top-K will be ignored.");
	}
	if (!cliSupportsMinP) {
		logWithLevel(OF_LOG_WARNING, "Detected CLI does not support --min-p; Min-P will be ignored.");
	}
	if (!cliSupportsMirostat) {
		logWithLevel(OF_LOG_WARNING, "Detected CLI does not support Mirostat flags; Mirostat settings will be ignored.");
	}
	if (!cliSupportsSingleTurn) {
		logWithLevel(OF_LOG_WARNING, "Detected CLI does not support --single-turn; EOF shutdown path may be less stable.");
	}
}

bool ofApp::isLlamaCliReady() const {
	return llamaCliState.load(std::memory_order_relaxed) == 1;
}

std::string ofApp::getLlamaCliCommand() const {
	return llamaCliCommand;
}

void ofApp::killActiveInferenceProcess() {
	killInferenceProcess();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ofApp::setup() {
	gConsoleAnsiEnabled = enableConsoleAnsiFormatting();
	ofDisableArbTex();
	ofSetWindowTitle("ofxGgml AI Studio");
	ofSetFrameRate(60);
	ofSetBackgroundColor(ofColor(30, 30, 34));

gui.setup(nullptr, true, ImGuiConfigFlags_None, true);
ImGui::GetIO().IniFilename = "imgui_ggml_studio.ini";
applyLogLevel(logLevel);

// Initialize presets.
loadModelPresets(modelPresets, taskDefaultModelIndices);
loadVideoRenderPresets(videoRenderPresets, recommendedVideoRenderPresetIndex);
selectedModelIndex = std::clamp(selectedModelIndex, 0,
	std::max(0, static_cast<int>(modelPresets.size()) - 1));
selectedVideoRenderPresetIndex = std::clamp(
	selectedVideoRenderPresetIndex,
	0,
	std::max(0, static_cast<int>(videoRenderPresets.size()) - 1));
scriptLanguages = ofxGgmlCodeAssistant::defaultLanguagePresets();
chatLanguages = ofxGgmlChatAssistant::defaultResponseLanguages();
translateLanguages = ofxGgmlTextAssistant::defaultTranslateLanguages();
loadPromptTemplates(promptTemplates);
if (!scriptLanguages.empty()) {
	scriptSource.setPreferredExtension(
		scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExtension);
}

// Default branch for GitHub.
scriptSourceBranch[0] = '\0';

// Session directory.
sessionDir = ofToDataPath("sessions", true);
std::error_code ec;
std::filesystem::create_directories(sessionDir, ec);
lastSessionPath = ofFilePath::join(sessionDir, "autosave.session");

// Log callback.
ggml.setLogCallback([this](int level, const std::string & msg) {
	const ofLogLevel mapped = mapGgmlLogLevel(level);
	if (mapped != OF_LOG_SILENT) {
		logWithLevel(mapped, msg);
	}
});
engineStatus = "Initializing ggml engine...";

// Auto-load last session if available.
deferredAutoLoadSessionPending = true;

{
	const std::string configuredSpeechExecutable = trim(speechExecutable);
	const std::string localSpeechCliExecutable = findLocalSpeechCliExecutable(true);
	if (!localSpeechCliExecutable.empty() &&
		isDefaultWhisperCliExecutableHint(configuredSpeechExecutable)) {
		copyStringToBuffer(
			speechExecutable,
			sizeof(speechExecutable),
			localSpeechCliExecutable);
	}
}

if (useModeTokenBudgets) {
	maxTokens = std::clamp(modeMaxTokens[static_cast<size_t>(activeMode)], 32, 4096);
}

if (trim(diffusionOutputDir).empty()) {
	copyStringToBuffer(
		diffusionOutputDir,
		sizeof(diffusionOutputDir),
		ofToDataPath("generated", true));
}
if (shouldManageLocalSpeechServer(effectiveSpeechServerUrl(speechServerUrl)) &&
	!findLocalSpeechServerExecutable().empty()) {
	startLocalSpeechServer();
}

// Pre-fill example system prompt only if not restored from session.
if (customSystemPrompt[0] == '\0') {
	copyStringToBuffer(
		customSystemPrompt,
		sizeof(customSystemPrompt),
		"You are a helpful assistant. Respond concisely.");
}
setupHoloscanBridge();
// Apply default theme.
applyTheme(themeIndex);
}

void ofApp::update() {
    if (holoscanBridgeRunning) {
		holoscanBridge.update();
		const auto finishedResults = holoscanBridge.consumeFinishedResults();
		if (!finishedResults.empty()) {
			holoscanVisionCompletedFrames += static_cast<int>(finishedResults.size());
			const auto & latest = finishedResults.back();
			holoscanVisionLatestOutput = latest.result.success ? latest.result.text : latest.result.error;
			holoscanVisionStatus =
				(latest.result.success ? "Holoscan vision result ready" : "Holoscan vision request failed") +
				std::string(" for ") +
				(latest.sourceLabel.empty() ? std::string("frame") : latest.sourceLabel) + ".";
		}
		const std::string holoscanError = trim(holoscanBridge.getLastError());
		if (!holoscanError.empty()) {
			holoscanVisionStatus = holoscanError;
		}
  }
  if (!visionPreviewVideoLoadedPath.empty()) {
    visionPreviewVideo.update();
  }
#if OFXGGML_HAS_OFXPROJECTM
  if (milkdropPreviewInitialized) {
		milkdropPreviewPlayer.setBeatSensitivity(milkdropPreviewBeatSensitivity);
		milkdropPreviewPlayer.setPresetDuration(milkdropPreviewPresetDuration);
		std::vector<float> previewSamples;
		int previewFrames = 0;
		int previewChannels = 0;
		{
			std::lock_guard<std::mutex> lock(milkdropPreviewAudioMutex);
			previewSamples.swap(milkdropPreviewAudioSamples);
			previewFrames = milkdropPreviewAudioFrames;
			previewChannels = milkdropPreviewAudioChannels;
			milkdropPreviewAudioFrames = 0;
			milkdropPreviewAudioChannels = 0;
		}
		if (!previewSamples.empty() && previewFrames > 0 && previewChannels > 0) {
			milkdropPreviewPlayer.audio(
				previewSamples.data(),
				previewFrames,
				previewChannels);
		}
		milkdropPreviewPlayer.update();
  }
#endif
#if OFXGGML_HAS_OFXVLC4
  if (aceStepVlcInitialized) {
		aceStepVlcPlayer.update();
  }
  if (montageVlcPreviewInitialized) {
		montageVlcPreviewPlayer.update();
  }
  if (montageClipVlcInitialized) {
		montageClipVlcPlayer.update();
		if (montageClipAutoRecordPending &&
			!montageClipVlcPlayer.isVideoRecording() &&
			montageClipVlcPlayer.getTexture().isAllocated()) {
			std::string error;
			if (startMontageClipVlcRecording(&error)) {
				montageClipPlaylistStatusMessage =
					"Loaded the generated playlist preview and started recording automatically.";
			} else {
				montageClipPlaylistStatusMessage =
					error.empty() ? std::string("Failed to start automatic playlist recording.") : error;
			}
		}
  }
  if (videoEssayVlcPreviewInitialized) {
		videoEssayVlcPreviewPlayer.update();
  }
#endif
  if (montagePreviewTimelinePlaying) {
		const ofxGgmlMontagePreviewTrack * previewTrack = getSelectedMontagePreviewTrack();
		if (previewTrack != nullptr &&
			getSelectedMontagePreviewTimingMode() == ofxGgmlMontagePreviewTimingMode::Montage) {
			const double durationSeconds = ofxGgmlMontagePreviewBridge::getTrackDuration(*previewTrack);
			const float now = ofGetElapsedTimef();
			if (montagePreviewTimelineLastTickTime > 0.0f && durationSeconds > 0.0) {
				montagePreviewTimelineSeconds = std::min(
					durationSeconds,
					montagePreviewTimelineSeconds + static_cast<double>(now - montagePreviewTimelineLastTickTime));
			}
			if (montagePreviewTimelineSeconds >= durationSeconds) {
				montagePreviewTimelineSeconds = durationSeconds;
				montagePreviewTimelinePlaying = false;
			}
			montagePreviewTimelineLastTickTime = now;
		} else {
			montagePreviewTimelinePlaying = false;
		}
  }
  applyLiveSpeechTranscriberSettings();
  speechLiveTranscriber.update();
  if (deferredEngineInitPending) {
    deferredEngineInitPending = false;
    initializeBackendEngine(false);
    deferredPostInitPending = true;
  }

if (deferredPostInitPending) {
	deferredPostInitPending = false;
	probeLlamaCli();
	detectModelLayers();
	if (gpuLayers == 0 && detectedModelLayers > 0) {
		gpuLayers = detectedModelLayers;
	}
	textInferenceBackend = preferredTextBackendForMode(activeMode);
	if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
		if (trim(textServerUrl).empty()) {
			copyStringToBuffer(textServerUrl, sizeof(textServerUrl), kDefaultTextServerUrl);
		}
		applyServerFriendlyDefaultsForMode(activeMode);
	} else {
		textServerStatus = ServerStatusState::Unknown;
		textServerStatusMessage.clear();
		textServerCapabilityHint.clear();
	}
}

updateDeferredTextServerWarmup();
if (deferredAutoLoadSessionPending && deferredAutoLoadSessionArmed) {
	deferredAutoLoadSessionPending = false;
	autoLoadSession();
	const std::string configuredSpeechExecutable = trim(speechExecutable);
	const std::string localSpeechCliExecutable = findLocalSpeechCliExecutable(true);
	if (!localSpeechCliExecutable.empty() &&
		isDefaultWhisperCliExecutableHint(configuredSpeechExecutable)) {
		copyStringToBuffer(
			speechExecutable,
			sizeof(speechExecutable),
			localSpeechCliExecutable);
		}
	}
	reapFinishedWorkerThread();
	applyPendingOutput();
}

void ofApp::draw() {
ofBackground(30, 30, 34);
gui.begin();
drawMenuBar();

const float windowW = static_cast<float>(ofGetWidth());
const float windowH = static_cast<float>(ofGetHeight());
const float menuBarH = ImGui::GetFrameHeight() + 4.0f;
const float statusBarH = 28.0f;
const float sidebarW = 220.0f;

// Sidebar.
ImGui::SetNextWindowPos(ImVec2(0.0f, menuBarH), ImGuiCond_Always);
ImGui::SetNextWindowSize(ImVec2(sidebarW, windowH - menuBarH - statusBarH), ImGuiCond_Always);
drawSidebar();

// Main panel.
ImGui::SetNextWindowPos(ImVec2(sidebarW, menuBarH), ImGuiCond_Always);
ImGui::SetNextWindowSize(ImVec2(windowW - sidebarW, windowH - menuBarH - statusBarH), ImGuiCond_Always);
drawMainPanel();

// Status bar.
ImGui::SetNextWindowPos(ImVec2(0.0f, windowH - statusBarH), ImGuiCond_Always);
ImGui::SetNextWindowSize(ImVec2(windowW, statusBarH), ImGuiCond_Always);
drawStatusBar();

// Optional floating windows.
if (showDeviceInfo) drawDeviceInfoWindow();
if (showLog) drawLogWindow();

if (deferredAutoLoadSessionPending) {
	deferredAutoLoadSessionArmed = true;
}
if (showPerformance) drawPerformanceWindow();

gui.end();
}

void ofApp::exit() {
  autoSaveSession();
  holoscanBridge.shutdown();
  if (!visionPreviewVideoLoadedPath.empty()) {
    visionPreviewVideo.stop();
    visionPreviewVideo.close();
  }
#if OFXGGML_HAS_OFXVLC4
	closeAceStepVlcPreview();
	closeMontageVlcPreview();
	closeMontageClipVlcPreview();
	closeVideoEssayVlcPreview();
#endif
#if OFXGGML_HAS_OFXPROJECTM
	milkdropPreviewInitialized = false;
#endif
	stopChatTtsPlayback(true);
	stopTtsPanelPlayback(true);
	stopSummaryTtsPlayback(true);
	stopTranslateTtsPlayback(true);
	stopVideoEssayTtsPlayback(true);
	stopSpeechRecording(false);
	if (speechInputStream.getSoundStream() != nullptr) {
		speechInputStream.stop();
		speechInputStream.close();
	}
	if (ttsOutputStream.getSoundStream() != nullptr) {
		ttsOutputStream.stop();
		ttsOutputStream.close();
	}
	speechInputStreamConfigured = false;
	ttsOutputStreamConfigured = false;
	stopLocalTextServer(false);
	stopLocalSpeechServer(false);
	stopLocalAceStepServer(false);
	stopGeneration(true);
	ggml.close();
	gui.exit();
}

void ofApp::keyPressed(int key) {
// Global shortcuts.
if (key == OF_KEY_F1) showDeviceInfo = !showDeviceInfo;
if (key == OF_KEY_F2) showLog = !showLog;
if (key == OF_KEY_F3) showPerformance = !showPerformance;
if (key == 27) stopGeneration();  // Escape cancels generation
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------

void ofApp::clearAllOutputs() {
	chatMessages.clear();
	scriptMessages.clear();
	scriptOutput.clear();
	summarizeOutput.clear();
	summarizeTtsPreview.clearPreviewArtifacts();
	writeOutput.clear();
	translateOutput.clear();
	voiceTranslatorStatus.clear();
	voiceTranslatorTranscript.clear();
	translateTtsPreview.clearPreviewArtifacts();
	customOutput.clear();
	citationOutput.clear();
	visionOutput.clear();
	visionSampledVideoFrames.clear();
	speechOutput.clear();
	chatLastAssistantReply.clear();
	chatTtsPreview.clearPreviewArtifacts();
	ttsOutput.clear();
	diffusionOutput.clear();
	musicToImagePromptOutput.clear();
	musicToImageStatus.clear();
	clipOutput.clear();
	citationResults.clear();
	speechDetectedLanguage.clear();
	speechTranscriptPath.clear();
	speechSrtPath.clear();
	speechSegmentCount = 0;
	ttsBackendName.clear();
	ttsElapsedMs = 0.0f;
	ttsResolvedSpeakerPath.clear();
	ttsAudioFiles.clear();
	ttsMetadata.clear();
	stopTtsPanelPlayback(true);
	stopSummaryTtsPlayback(true);
	stopTranslateTtsPlayback(true);
	diffusionBackendName.clear();
	diffusionElapsedMs = 0.0f;
	diffusionGeneratedImages.clear();
	diffusionMetadata.clear();
	clipBackendName.clear();
	clipElapsedMs = 0.0f;
	clipEmbeddingDimension = 0;
	clipHits.clear();
}

void ofApp::drawMenuBar() {
if (ImGui::BeginMainMenuBar()) {
if (ImGui::BeginMenu("File")) {
if (ImGui::MenuItem("Save Session", "Ctrl+S")) {
ofFileDialogResult result = ofSystemSaveDialog(
"session.txt", "Save Session");
if (result.bSuccess) {
saveSession(result.getPath());
}
}
if (ImGui::MenuItem("Load Session", "Ctrl+L")) {
ofFileDialogResult result = ofSystemLoadDialog(
"Load Session", false, sessionDir);
if (result.bSuccess) {
loadSession(result.getPath());
}
}
ImGui::Separator();
if (ImGui::MenuItem("Clear All Output")) {
	clearAllOutputs();
}
ImGui::Separator();
if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
ofExit(0);
}
ImGui::EndMenu();
}
if (ImGui::BeginMenu("View")) {
ImGui::MenuItem("Device Info    (F1)", nullptr, &showDeviceInfo);
ImGui::MenuItem("Engine Log     (F2)", nullptr, &showLog);
ImGui::MenuItem("Performance    (F3)", nullptr, &showPerformance);
ImGui::EndMenu();
}
ImGui::EndMainMenuBar();
}
}

// ---------------------------------------------------------------------------
// Sidebar — mode selection + model preset + quick settings
// ---------------------------------------------------------------------------

void ofApp::drawSidebar() {
ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

if (ImGui::Begin("##Sidebar", nullptr, flags)) {
const float compactSidebarFieldWidth = std::min(260.0f, ImGui::GetContentRegionAvail().x);
ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "AI Studio");
ImGui::Separator();
ImGui::Spacing();
ImGui::Text("Mode:");
ImGui::SetNextItemWidth(-1);
const int activeModeIndex = std::clamp(
	static_cast<int>(activeMode),
	0,
	kModeCount - 1);
if (ImGui::BeginCombo("##ModeSel", modeLabels[activeModeIndex])) {
	for (int i = 0; i < kModeCount; i++) {
		const bool isSelected = (activeModeIndex == i);
		if (ImGui::Selectable(modeLabels[i], isSelected)) {
			activeMode = static_cast<AiMode>(i);
			if (useModeTokenBudgets) {
				maxTokens = std::clamp(modeMaxTokens[static_cast<size_t>(i)], 32, 4096);
			}
			syncTextBackendForActiveMode(false, false);
		}
		if (isSelected) {
			ImGui::SetItemDefaultFocus();
		}
	}
	ImGui::EndCombo();
}
ImGui::TextDisabled("Stored backend and token defaults follow the selected mode.");

drawSectionSeparator();

// Model preset selector.
ImGui::Text(activeMode == AiMode::LongVideo ? "Planner model:" : "Model:");
ImGui::SetNextItemWidth(-1);
const bool useServerBackend =
	(textInferenceBackend == TextInferenceBackend::LlamaServer);
if (!modelPresets.empty()) {
ImGui::SetNextItemWidth(compactSidebarFieldWidth);
if (ImGui::BeginCombo("##ModelSel", modelPresets[static_cast<size_t>(selectedModelIndex)].name.c_str())) {
for (int i = 0; i < static_cast<int>(modelPresets.size()); i++) {
bool isSelected = (selectedModelIndex == i);
const auto & preset = modelPresets[static_cast<size_t>(i)];
std::string label = preset.name + "  " + preset.sizeMB;
if (ImGui::Selectable(label.c_str(), isSelected)) {
selectedModelIndex = i;
detectModelLayers();
// Default to all GPU layers when switching models.
if (detectedModelLayers > 0) {
gpuLayers = detectedModelLayers;
}
}
if (ImGui::IsItemHovered()) {
showWrappedTooltipf("%s\nBest for: %s\nFile: %s",
	preset.description.c_str(),
	preset.bestFor.c_str(),
	preset.filename.c_str());
}
if (isSelected) ImGui::SetItemDefaultFocus();
}
ImGui::EndCombo();
}
}

if (!modelPresets.empty()) {
	const int recommendedIdx = std::clamp(
		taskDefaultModelIndices[static_cast<int>(activeMode)],
		0, static_cast<int>(modelPresets.size()) - 1);
		ImGui::BeginDisabled(recommendedIdx == selectedModelIndex);
		if (ImGui::Button("Use preset", ImVec2(-1, 0))) {
		selectedModelIndex = recommendedIdx;
		customModelPath[0] = '\0';
		detectModelLayers();
		if (detectedModelLayers > 0) {
			gpuLayers = detectedModelLayers;
		}
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip("Switch to the catalog default for this mode.");
	}

	ImGui::InputTextWithHint(
		"##CustomModelPath",
		"Custom GGUF path override (optional)",
		customModelPath,
		sizeof(customModelPath));
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip(
			"When set, this GGUF path overrides the selected preset for local review, script, and CLI-backed flows.");
	}

	if (ImGui::Button("Browse GGUF...", ImVec2(-1, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select GGUF model", false);
		if (result.bSuccess) {
			copyStringToBuffer(customModelPath, sizeof(customModelPath), result.getPath());
			detectModelLayers();
			if (detectedModelLayers > 0) {
				gpuLayers = detectedModelLayers;
			}
			autoSaveSession();
		}
	}
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip("Pick any local GGUF file without adding it to the catalog.");
	}

	const std::string customOverridePath = trim(customModelPath);
	if (!customOverridePath.empty()) {
		if (ImGui::Button("Clear custom model", ImVec2(-1, 0))) {
			customModelPath[0] = '\0';
			detectModelLayers();
			if (detectedModelLayers > 0) {
				gpuLayers = detectedModelLayers;
			}
			autoSaveSession();
		}
		if (ImGui::IsItemHovered()) {
			showWrappedTooltip("Return to using the selected preset path.");
		}
		ImGui::TextDisabled("Using custom GGUF override");
		if (ImGui::IsItemHovered()) {
			showWrappedTooltip(customOverridePath);
		}
	}

	const auto & selectedPreset = modelPresets[static_cast<size_t>(selectedModelIndex)];
	const std::string modelPath = getSelectedModelPath();
	if (!modelPath.empty()) {
		const std::string modelFileName = ofFilePath::getFileName(modelPath);
		std::error_code modelEc;
		if (std::filesystem::exists(modelPath, modelEc) && !modelEc) {
			ImGui::TextDisabled("Local GGUF: %s", modelFileName.c_str());
			if (ImGui::IsItemHovered()) {
				showWrappedTooltip(modelPath);
			}
			if (useServerBackend) {
				ImGui::TextDisabled("Used for review / embeddings");
				drawHelpMarker("When llama-server is active, the local GGUF can still be useful for review and embedding-related flows.");
			}
		} else if (useServerBackend) {
			ImGui::TextDisabled("Local GGUF optional");
			drawHelpMarker("Normal text generation can run through llama-server without a local GGUF. This local file remains optional.");
			ImGui::TextDisabled("Suggested file: %s", modelFileName.c_str());
			if (ImGui::IsItemHovered()) {
				showWrappedTooltip(modelPath);
			}
			ImGui::BeginDisabled(selectedPreset.url.empty());
			if (ImGui::SmallButton("Download in browser")) {
				ofLaunchBrowser(selectedPreset.url);
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered()) {
				showWrappedTooltip("Opens the preset download URL in your browser.");
			}
		} else {
			ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f),
				"Model missing: %s", modelFileName.c_str());
			if (ImGui::IsItemHovered()) {
				showWrappedTooltip(modelPath);
			}
			ImGui::TextDisabled("Target path");
			drawHelpMarker(modelPath.c_str());
			ImGui::BeginDisabled(selectedPreset.url.empty());
			if (ImGui::SmallButton("Download in browser")) {
				ofLaunchBrowser(selectedPreset.url);
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered()) {
				showWrappedTooltip("Opens the preset download URL in your browser.");
			}
			ImGui::SameLine();
			const int presetNumber = selectedModelIndex + 1;
			std::string downloadCmd = "./scripts/download-model.sh --preset " + ofToString(presetNumber);
			if (ImGui::SmallButton("Copy download command")) {
				copyToClipboard(downloadCmd);
			}
			if (ImGui::IsItemHovered()) {
				showWrappedTooltipf("Copies a shell command to fetch preset %d into the shared addon models/ folder.", presetNumber);
			}
}
	}
}

if (activeMode == AiMode::Diffusion) {
	if (diffusionProfiles.empty()) {
		diffusionProfiles = ofxGgmlDiffusionInference::defaultProfiles();
	}
	selectedDiffusionProfileIndex = std::clamp(
		selectedDiffusionProfileIndex,
		0,
		std::max(0, static_cast<int>(diffusionProfiles.size()) - 1));
	const ofxGgmlImageGenerationModelProfile activeDiffusionProfile =
		diffusionProfiles.empty()
			? ofxGgmlImageGenerationModelProfile{}
			: diffusionProfiles[static_cast<size_t>(selectedDiffusionProfileIndex)];
	const auto activeDiffusionTask =
		static_cast<ofxGgmlImageGenerationTask>(std::clamp(diffusionTaskIndex, 0, 6));
	const bool diffusionNeedsInitImage =
		activeDiffusionTask == ofxGgmlImageGenerationTask::ImageToImage ||
		activeDiffusionTask == ofxGgmlImageGenerationTask::InstructImage ||
		activeDiffusionTask == ofxGgmlImageGenerationTask::Variation ||
		activeDiffusionTask == ofxGgmlImageGenerationTask::Restyle ||
		activeDiffusionTask == ofxGgmlImageGenerationTask::Inpaint ||
		activeDiffusionTask == ofxGgmlImageGenerationTask::Upscale;
	const bool diffusionNeedsMaskImage =
		activeDiffusionTask == ofxGgmlImageGenerationTask::Inpaint;
	const std::string recommendedDiffusionModelPath =
		suggestedModelPath(
			activeDiffusionProfile.modelPath,
			activeDiffusionProfile.modelFileHint);
	const std::string recommendedDiffusionDownloadUrl =
		suggestedModelDownloadUrl(
			activeDiffusionProfile.modelRepoHint,
			activeDiffusionProfile.modelFileHint);

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	ImGui::Text("Image Assets");
	if (!activeDiffusionProfile.name.empty()) {
		ImGui::TextDisabled("Profile: %s", activeDiffusionProfile.name.c_str());
	}
	if (!recommendedDiffusionModelPath.empty()) {
		ImGui::TextDisabled(
			pathExists(recommendedDiffusionModelPath)
				? "Recommended model is available"
				: "Recommended model is not downloaded");
		ImGui::BeginDisabled(trim(diffusionModelPath) == recommendedDiffusionModelPath);
		if (ImGui::Button("Use recommended image model", ImVec2(-1, 0))) {
			copyStringToBuffer(
				diffusionModelPath,
				sizeof(diffusionModelPath),
				recommendedDiffusionModelPath);
			autoSaveSession();
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered()) {
			showWrappedTooltip(
				"Use the current diffusion profile's recommended model path under the shared addon models folder.");
		}
		ImGui::BeginDisabled(recommendedDiffusionDownloadUrl.empty());
		if (ImGui::Button("Download recommended image model", ImVec2(-1, 0))) {
			ofLaunchBrowser(recommendedDiffusionDownloadUrl);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered()) {
			showWrappedTooltip("Open the recommended diffusion model download page in your browser.");
		}
	}

	ImGui::InputTextWithHint(
		"##DiffusionModelPathSidebar",
		"Diffusion model path",
		diffusionModelPath,
		sizeof(diffusionModelPath));
	if (ImGui::Button("Browse diffusion model...", ImVec2(-1, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select diffusion model", false);
		if (result.bSuccess) {
			copyStringToBuffer(diffusionModelPath, sizeof(diffusionModelPath), result.getPath());
			autoSaveSession();
		}
	}

	ImGui::InputTextWithHint(
		"##DiffusionVaePathSidebar",
		"VAE path (optional)",
		diffusionVaePath,
		sizeof(diffusionVaePath));
	if (ImGui::Button("Browse VAE...", ImVec2(-1, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select VAE file", false);
		if (result.bSuccess) {
			copyStringToBuffer(diffusionVaePath, sizeof(diffusionVaePath), result.getPath());
			autoSaveSession();
		}
	}

	if (diffusionNeedsInitImage) {
		ImGui::InputTextWithHint(
			"##DiffusionInitPathSidebar",
			"Init image",
			diffusionInitImagePath,
			sizeof(diffusionInitImagePath));
		if (ImGui::Button("Browse init image...", ImVec2(-1, 0))) {
			ofFileDialogResult result = ofSystemLoadDialog("Select init image", false);
			if (result.bSuccess) {
				copyStringToBuffer(
					diffusionInitImagePath,
					sizeof(diffusionInitImagePath),
					result.getPath());
				autoSaveSession();
			}
		}
	}

	if (diffusionNeedsMaskImage) {
		ImGui::InputTextWithHint(
			"##DiffusionMaskPathSidebar",
			"Mask image",
			diffusionMaskImagePath,
			sizeof(diffusionMaskImagePath));
		if (ImGui::Button("Browse mask image...", ImVec2(-1, 0))) {
			ofFileDialogResult result = ofSystemLoadDialog("Select mask image", false);
			if (result.bSuccess) {
				copyStringToBuffer(
					diffusionMaskImagePath,
					sizeof(diffusionMaskImagePath),
					result.getPath());
				autoSaveSession();
			}
		}
	}
}

ImGui::Spacing();
const bool modeSupportsTextBackend = aiModeSupportsTextBackend(activeMode);
ImGui::Text(modeSupportsTextBackend ? "Text Backend (this mode):" : "Text Backend:");
ImGui::SetNextItemWidth(compactSidebarFieldWidth);
ImGui::BeginDisabled(!modeSupportsTextBackend);
int textBackendIndex = static_cast<int>(textInferenceBackend);
if (ImGui::Combo("##TextBackend", &textBackendIndex,
	kTextBackendLabels, IM_ARRAYSIZE(kTextBackendLabels))) {
	textInferenceBackend = clampTextInferenceBackend(textBackendIndex);
	rememberTextBackendForMode(activeMode, textInferenceBackend);
	if (textInferenceBackend == TextInferenceBackend::LlamaServer &&
		trim(textServerUrl).empty()) {
		copyStringToBuffer(
			textServerUrl,
			sizeof(textServerUrl),
			kDefaultTextServerUrl);
	}
	announceTextBackendChange();
}
ImGui::EndDisabled();
drawHelpMarker(modeSupportsTextBackend
	? "Stored separately per text mode. Switching tabs restores that mode's backend."
	: "Vision, Speech, and Diffusion use their own pipelines. Switch to a text mode to change its backend.");
if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
	ImGui::SetNextItemWidth(compactSidebarFieldWidth);
	const bool serverUrlChanged = ImGui::InputText("Server URL", textServerUrl, sizeof(textServerUrl));
	ImGui::SetNextItemWidth(compactSidebarFieldWidth);
	ImGui::InputText("Server model", textServerModel, sizeof(textServerModel));
	if (serverUrlChanged) {
		textServerStatus = ServerStatusState::Unknown;
		textServerStatusMessage.clear();
		textServerCapabilityHint.clear();
		ensureTextServerReady(
			false,
			shouldManageLocalTextServer(effectiveTextServerUrl(textServerUrl)));
	}
	ImGui::TextDisabled("Persistent server backend");
	drawHelpMarker("Uses a warm OpenAI-compatible llama-server for Chat, Script, and text modes. Leave Server model empty to use the model already loaded by the server.");
	ImGui::TextDisabled("Auto-tuned on setup");
	drawHelpMarker("Server-friendly defaults are applied automatically, and the app starts the local server during setup when the configured URL is local.");
	const std::string localServerExe = findLocalTextServerExecutable();
	if (!localServerExe.empty()) {
		ImGui::TextDisabled("Local server exe: %s", ofFilePath::getFileName(localServerExe).c_str());
		if (ImGui::IsItemHovered()) {
			showWrappedTooltip(localServerExe);
		}
	} else {
		ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.35f, 1.0f), "Local server executable not found.");
		drawHelpMarker("Build it with scripts/build-llama-server.ps1 on Windows or scripts/build-llama-cli.sh on Linux/macOS when you want a managed local server.");
	}
	if (textServerStatus != ServerStatusState::Unknown && !textServerStatusMessage.empty()) {
		const ImVec4 statusColor =
			(textServerStatus == ServerStatusState::Reachable)
				? ImVec4(0.35f, 0.8f, 0.45f, 1.0f)
				: ImVec4(0.9f, 0.45f, 0.35f, 1.0f);
		ImGui::TextColored(statusColor, "%s", textServerStatusMessage.c_str());
		if (!textServerCapabilityHint.empty()) {
			drawHelpMarker(textServerCapabilityHint.c_str());
		}
	}
} else {
	if (llamaCliState.load(std::memory_order_relaxed) == 1) {
		ImGui::TextDisabled("CLI fallback available");
		drawHelpMarker(llamaCliCommand.c_str());
	} else {
		ImGui::TextDisabled("CLI fallback not installed");
		drawHelpMarker("Build it only if you want a local non-server fallback.");
	}
}

drawSectionSeparator();
ImGui::Text("Settings");
ImGui::Spacing();

if (ImGui::Button("Balanced", ImVec2(ImGui::GetContentRegionAvail().x / 3.0f - 3.0f, 0))) {
	maxTokens = 512;
	temperature = 0.7f;
	topP = 0.9f;
	topK = 40;
	minP = 0.0f;
	repeatPenalty = 1.1f;
	mirostatMode = 0;
}
ImGui::SameLine();
if (ImGui::Button("Code", ImVec2(ImGui::GetContentRegionAvail().x / 2.0f - 2.0f, 0))) {
	maxTokens = 1024;
	temperature = 0.25f;
	topP = 0.92f;
	topK = 60;
	minP = 0.05f;
	repeatPenalty = 1.05f;
	mirostatMode = 0;
}
ImGui::SameLine();
if (ImGui::Button("Creative", ImVec2(-1, 0))) {
	maxTokens = 768;
	temperature = 1.0f;
	topP = 0.95f;
	topK = 80;
	minP = 0.0f;
	repeatPenalty = 1.05f;
	mirostatMode = 0;
}

ImGui::SetNextItemWidth(-1);
if (ImGui::SliderInt("##MaxTok", &maxTokens, 32, 4096, "Tokens: %d")) {
	modeMaxTokens[static_cast<size_t>(activeMode)] = maxTokens;
}
ImGui::Checkbox("Per-mode token budgets", &useModeTokenBudgets);
if (ImGui::IsItemHovered()) {
	showWrappedTooltip("Remember and auto-apply a separate Max Tokens value per mode.");
}
ImGui::SetNextItemWidth(-1);
ImGui::SliderFloat("##Temp", &temperature, 0.0f, 2.0f, "Temp: %.2f");
ImGui::SetNextItemWidth(-1);
ImGui::SliderFloat("##TopP", &topP, 0.0f, 1.0f, "Top-P: %.2f");
ImGui::SetNextItemWidth(-1);
ImGui::SliderInt("##TopK", &topK, 0, 200, "Top-K: %d");
ImGui::SetNextItemWidth(-1);
ImGui::SliderFloat("##MinP", &minP, 0.0f, 1.0f, "Min-P: %.2f");
ImGui::SetNextItemWidth(-1);
ImGui::SliderFloat("##RepeatPenalty", &repeatPenalty, 1.0f, 2.0f, "Repeat Penalty: %.2f");
ImGui::SetNextItemWidth(-1);
ImGui::SliderInt("##Seed", &seed, -1, 99999, "Seed: %d");
ImGui::Checkbox("Stop on natural boundary", &stopAtNaturalBoundary);
if (ImGui::IsItemHovered()) {
	showWrappedTooltip("Trims cut-off output to sentence or line boundaries when generation ends abruptly.");
}
ImGui::Checkbox("Auto-continue cutoffs (Script)", &autoContinueCutoff);
if (ImGui::IsItemHovered()) {
	showWrappedTooltip("When Script output appears cut off, run one automatic continuation pass.");
}
ImGui::BeginDisabled(useServerBackend);
ImGui::Checkbox("Use prompt cache", &usePromptCache);
ImGui::EndDisabled();
if (ImGui::IsItemHovered()) {
	showWrappedTooltip(useServerBackend
		? "Prompt cache is a local CLI optimization and is ignored for llama-server."
		: "Reuse llama prompt cache between requests for faster follow-up responses.");
}
const char * liveContextLabels[] = {
	"Live context: Offline",
	"Live context: Loaded sources only",
	"Live context: Live context",
	"Live context: Strict citations"
};
int liveContextModeIndex = static_cast<int>(liveContextMode);
ImGui::SetNextItemWidth(-1);
if (ImGui::Combo("##LiveContextMode", &liveContextModeIndex, liveContextLabels, 4)) {
	liveContextMode = static_cast<LiveContextMode>(liveContextModeIndex);
}
if (ImGui::IsItemHovered()) {
	showWrappedTooltip(
		"Offline disables external context. Loaded sources only uses the URLs you provide. "
		"Live context also allows automatic news, weather, and search grounding.");
}
const bool autoLiveLookupsEnabled =
	liveContextMode == LiveContextMode::LiveContext ||
	liveContextMode == LiveContextMode::LiveContextStrictCitations;
ImGui::BeginDisabled(!autoLiveLookupsEnabled);
ImGui::Checkbox("Allow prompt URLs", &liveContextAllowPromptUrls);
ImGui::Checkbox("Allow domain providers", &liveContextAllowDomainProviders);
ImGui::Checkbox("Allow generic search", &liveContextAllowGenericSearch);
ImGui::EndDisabled();

const char * mirostatLabels[] = { "Mirostat: Off", "Mirostat", "Mirostat 2.0" };
ImGui::SetNextItemWidth(-1);
ImGui::Combo("##MirostatMode", &mirostatMode, mirostatLabels, 3);
if (mirostatMode > 0) {
	ImGui::SetNextItemWidth(-1);
	ImGui::SliderFloat("##MirostatTau", &mirostatTau, 0.0f, 10.0f, "Mirostat Tau: %.1f");
	ImGui::SetNextItemWidth(-1);
	ImGui::SliderFloat("##MirostatEta", &mirostatEta, 0.0f, 1.0f, "Mirostat Eta: %.2f");
}

ImGui::SetNextItemWidth(-1);
ImGui::BeginDisabled(useServerBackend);
ImGui::SliderInt("##Threads", &numThreads, 1, 32, "Threads: %d");
ImGui::EndDisabled();
if (useServerBackend && ImGui::IsItemHovered()) {
	showWrappedTooltip("Thread count is only used by the local CLI path.");
}
ImGui::SetNextItemWidth(-1);
ImGui::SliderInt(
	"##ContextSize",
	&contextSize,
	256,
	16384,
	useServerBackend ? "Prompt budget: %d" : "Context: %d");
if (ImGui::IsItemHovered()) {
	showWrappedTooltip(useServerBackend
		? "Used as a local prompt-trimming heuristic before sending text to llama-server."
		: "Maximum local context window requested for llama-completion.");
}
ImGui::SetNextItemWidth(-1);
ImGui::BeginDisabled(useServerBackend);
ImGui::SliderInt("##BatchSize", &batchSize, 32, 4096, "Batch: %d");
ImGui::EndDisabled();
if (useServerBackend && ImGui::IsItemHovered()) {
	showWrappedTooltip("Batch size is only used by the local CLI path.");
}

if (!backendNames.empty()) {
	ImGui::BeginDisabled(useServerBackend);
	selectedBackendIndex = std::clamp(selectedBackendIndex, 0, static_cast<int>(backendNames.size()) - 1);
	const std::string currentBackendLabel = backendNames[static_cast<size_t>(selectedBackendIndex)];
	if (ImGui::BeginCombo("Backend", currentBackendLabel.c_str())) {
		for (int i = 0; i < static_cast<int>(backendNames.size()); i++) {
			const bool isSelected = (selectedBackendIndex == i);
			if (ImGui::Selectable(backendNames[static_cast<size_t>(i)].c_str(), isSelected)) {
				if (selectedBackendIndex != i) {
					selectedBackendIndex = i;
					reinitBackend();
				}
			}
			if (isSelected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();
}

ImGui::SetNextItemWidth(-1);
{
// GPU layers control the llama-completion CLI process, which has
// its own GPU support — always allow the user to adjust them.
int sliderMax = detectedModelLayers > 0 ? detectedModelLayers : 128;
ImGui::BeginDisabled(useServerBackend);
ImGui::SliderInt("##GPULayers", &gpuLayers, 0, sliderMax, "GPU Layers: %d");
if (ImGui::IsItemHovered()) {
if (detectedModelLayers > 0) {
showWrappedTooltipf("Model has %d layers\nGPU offloading via llama-completion", detectedModelLayers);
}
}
// None / All quick buttons.
if (ImGui::Button("None##gpu", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 2, 0))) {
gpuLayers = 0;
}
ImGui::SameLine();
if (ImGui::Button("All##gpu", ImVec2(-1, 0))) {
gpuLayers = detectedModelLayers > 0 ? detectedModelLayers : 128;
}
ImGui::EndDisabled();
}

if (activeMode == AiMode::LongVideo) {
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	ImGui::Text("Video Settings");

	ImGui::SetNextItemWidth(-1);
	if (ImGui::SliderFloat(
		"##VideoTargetDurationSidebar",
		&longVideoTargetDurationSeconds,
		8.0f,
		360.0f,
		"Duration: %.0f s")) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(-1);
	if (ImGui::SliderInt(
		"##VideoSegmentsSidebar",
		&longVideoChunkCount,
		1,
		16,
		"Segments: %d")) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(-1);
	if (ImGui::Combo(
		"##VideoStructureSidebar",
		&longVideoStructureIndex,
		kVideoStructureSettingLabels,
		IM_ARRAYSIZE(kVideoStructureSettingLabels))) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(-1);
	if (ImGui::Combo(
		"##VideoPacingSidebar",
		&longVideoPacingIndex,
		kVideoPacingSettingLabels,
		IM_ARRAYSIZE(kVideoPacingSettingLabels))) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(-1);
	if (ImGui::SliderInt(
		"##VideoWidthSidebar",
		&longVideoWidth,
		128,
		1280,
		"Width: %d")) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(-1);
	if (ImGui::SliderInt(
		"##VideoHeightSidebar",
		&longVideoHeight,
		128,
		1280,
		"Height: %d")) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(-1);
	if (ImGui::SliderInt(
		"##VideoFpsSidebar",
		&longVideoFps,
		1,
		30,
		"FPS: %d")) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(-1);
	if (ImGui::SliderInt(
		"##VideoFramesPerSegmentSidebar",
		&longVideoFramesPerChunk,
		8,
		160,
		"Frames / segment: %d")) {
		autoSaveSession();
	}
	ImGui::SetNextItemWidth(-1);
	if (ImGui::InputInt("Seed##VideoSidebar", &longVideoSeed)) {
		autoSaveSession();
	}
	if (ImGui::Checkbox(
		"Use prompt inheritance##VideoSidebar",
		&longVideoUsePromptInheritance)) {
		autoSaveSession();
	}
	if (ImGui::Checkbox(
		"Favor loopable ending##VideoSidebar",
		&longVideoFavorLoopableEnding)) {
		autoSaveSession();
	}

	ImGui::Spacing();
		ImGui::Text("Video Render Model");
		ImGui::TextDisabled("Presets are optional. Any local model path you choose takes priority.");
		if (!videoRenderPresets.empty()) {
		ImGui::SetNextItemWidth(-1);
		if (ImGui::BeginCombo(
				"##VideoRenderPreset",
				videoRenderPresets[static_cast<size_t>(selectedVideoRenderPresetIndex)].name.c_str())) {
			for (int i = 0; i < static_cast<int>(videoRenderPresets.size()); ++i) {
				const bool isSelected = (selectedVideoRenderPresetIndex == i);
				const auto & preset = videoRenderPresets[static_cast<size_t>(i)];
				std::string label = preset.name;
				if (!preset.family.empty()) {
					label += "  [" + preset.family + "]";
				}
				if (ImGui::Selectable(label.c_str(), isSelected)) {
					selectedVideoRenderPresetIndex = i;
					autoSaveSession();
				}
				if (ImGui::IsItemHovered()) {
					showWrappedTooltipf(
						"%s\nBackend: %s\nBest for: %s\nFile: %s",
						preset.description.c_str(),
						preset.backend.c_str(),
						preset.bestFor.c_str(),
						preset.filename.c_str());
				}
				if (isSelected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		ImGui::BeginDisabled(recommendedVideoRenderPresetIndex == selectedVideoRenderPresetIndex);
		if (ImGui::Button("Use preset render model", ImVec2(-1, 0))) {
			selectedVideoRenderPresetIndex = recommendedVideoRenderPresetIndex;
			customVideoRenderModelPath[0] = '\0';
			autoSaveSession();
		}
		ImGui::EndDisabled();

		ImGui::InputTextWithHint(
			"##CustomVideoRenderModelPath",
			"Any local video model path (optional override)",
			customVideoRenderModelPath,
			sizeof(customVideoRenderModelPath));
		if (ImGui::Button("Browse video model...", ImVec2(-1, 0))) {
			ofFileDialogResult result = ofSystemLoadDialog("Select video render model", false);
			if (result.bSuccess) {
				copyStringToBuffer(
					customVideoRenderModelPath,
					sizeof(customVideoRenderModelPath),
					result.getPath());
				autoSaveSession();
			}
		}
		if (!trim(customVideoRenderModelPath).empty()) {
			if (ImGui::Button("Clear video model override", ImVec2(-1, 0))) {
				customVideoRenderModelPath[0] = '\0';
				autoSaveSession();
			}
		}

		const auto & selectedRenderPreset =
			videoRenderPresets[static_cast<size_t>(selectedVideoRenderPresetIndex)];
		const std::string selectedRenderModelPath = getSelectedVideoRenderModelPath();
		if (!selectedRenderModelPath.empty()) {
			const std::string selectedFileName =
				ofFilePath::getFileName(selectedRenderModelPath);
			std::error_code renderModelEc;
			if (std::filesystem::exists(selectedRenderModelPath, renderModelEc) &&
				!renderModelEc) {
				ImGui::TextDisabled("Local video model: %s", selectedFileName.c_str());
			} else {
				ImGui::TextDisabled("Suggested video model: %s", selectedFileName.c_str());
			}
			if (ImGui::IsItemHovered()) {
				showWrappedTooltip(selectedRenderModelPath);
			}
		}
		if (!selectedRenderPreset.family.empty()) {
			ImGui::TextDisabled("Family: %s", selectedRenderPreset.family.c_str());
		}
		if (!selectedRenderPreset.url.empty()) {
			if (ImGui::Button("Download video model in browser", ImVec2(-1, 0))) {
				ofLaunchBrowser(selectedRenderPreset.url);
			}
		}
	}
}

ImGui::Separator();
int currentLogIdx = std::clamp(logLevelIndex(logLevel), 0,
	static_cast<int>(kLogLevelOptions.size()) - 1);
const char * currentLogLabel = kLogLevelOptions[static_cast<size_t>(currentLogIdx)].label;
if (ImGui::BeginCombo("Log Level", currentLogLabel)) {
	for (size_t i = 0; i < kLogLevelOptions.size(); i++) {
		const bool isSelected = (currentLogIdx == static_cast<int>(i));
		if (ImGui::Selectable(kLogLevelOptions[i].label, isSelected)) {
			if (!isSelected) {
				applyLogLevel(kLogLevelOptions[i].level);
				logWithLevel(OF_LOG_NOTICE,
					std::string("Log level set to ") + kLogLevelOptions[i].label);
				currentLogIdx = static_cast<int>(i);
			}
		}
		if (isSelected) ImGui::SetItemDefaultFocus();
	}
	ImGui::EndCombo();
}
ImGui::Checkbox("Show Engine Log", &showLog);

const char * themeLabels[] = { "Dark", "Light", "Classic" };
if (ImGui::Combo("Theme", &themeIndex, themeLabels, 3)) {
	applyTheme(themeIndex);
}

drawSectionSeparator();

// Engine status indicator.
if (engineReady) {
ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Engine: OK");
} else {
ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.2f, 1.0f), "Engine: Error");
}
ImGui::Text("Backend: %s", ggml.getBackendName().c_str());

if (generating.load()) {
ImGui::Spacing();
const char * spinner = "|/-\\";
int spinIdx = static_cast<int>(ImGui::GetTime() / kSpinnerInterval) % 4;
float elapsed = ofGetElapsedTimef() - generationStartTime;
char genLabel[64];
snprintf(genLabel, sizeof(genLabel), "%c Generating... (%.1fs)", spinner[spinIdx], elapsed);
ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", genLabel);
if (ImGui::Button("Stop", ImVec2(-1, 0))) {
stopGeneration();
}
}
}
ImGui::End();
}

// ---------------------------------------------------------------------------
// Main panel — dispatches to mode-specific panels
// ---------------------------------------------------------------------------

void ofApp::drawMainPanel() {
ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

if (ImGui::Begin("##MainPanel", nullptr, flags)) {
switch (activeMode) {
case AiMode::Chat:      drawChatPanel();      break;
case AiMode::Easy:      drawEasyPanel();      break;
case AiMode::Script:    drawScriptPanel();    break;
case AiMode::Summarize: drawSummarizePanel(); break;
	case AiMode::Write:     drawWritePanel();     break;
	case AiMode::Translate: drawTranslatePanel(); break;
	case AiMode::Custom:    drawCustomPanel();    break;
	case AiMode::VideoEssay: drawVideoEssayPanel(); break;
	case AiMode::LongVideo: drawLongVideoPanel(); break;
	case AiMode::Vision:    drawVisionPanel();    break;
	case AiMode::Speech:    drawSpeechPanel();    break;
	case AiMode::Tts:       drawTtsPanel();       break;
	case AiMode::Diffusion: drawDiffusionPanel(); break;
	case AiMode::Clip:      drawClipPanel();      break;
	case AiMode::MilkDrop:  drawMilkDropPanel();  break;
	}
}
ImGui::End();
}

// ---------------------------------------------------------------------------
// Script panel
// ---------------------------------------------------------------------------

void ofApp::drawScriptPanel() {
drawPanelHeader("Script Assistant", "simple coding chat with optional advanced workspace tools");

ImGui::TextDisabled("UI:");
ImGui::SameLine();
if (drawStyledToggleButton(
		"Simple",
		scriptSimpleUi,
		ImVec4(0.23f, 0.52f, 0.78f, 1.0f))) {
	if (!scriptSimpleUi) {
		scriptSimpleUi = true;
		autoSaveSession();
	}
}
if (ImGui::IsItemHovered()) {
	showWrappedTooltip("ChatPilot-style layout: keep the main coding chat visible and tuck advanced tools behind collapsible sections.");
}
ImGui::SameLine();
if (drawStyledToggleButton(
		"Advanced",
		!scriptSimpleUi,
		ImVec4(0.38f, 0.34f, 0.58f, 1.0f),
		false)) {
	if (scriptSimpleUi) {
		scriptSimpleUi = false;
		autoSaveSession();
	}
}
if (ImGui::IsItemHovered()) {
	showWrappedTooltip("Full Script mode with inline completion, workspace tooling, semantic search, approvals, and project memory surfaced at once.");
}
ImGui::SameLine();
ImGui::TextDisabled(
	"%s",
	scriptSimpleUi
		? "Simple chatpilot-style coding surface"
		: "Full workspace-aware coding surface");

// Language selector and source controls on same row.
ImGui::Text("Language:");
ImGui::SameLine();
ImGui::SetNextItemWidth(140);
	if (!scriptLanguages.empty()) {
	if (ImGui::BeginCombo("##LangSel", scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].name.c_str())) {
	for (int i = 0; i < static_cast<int>(scriptLanguages.size()); i++) {
	bool isSelected = (selectedLanguageIndex == i);
	if (ImGui::Selectable(scriptLanguages[static_cast<size_t>(i)].name.c_str(), isSelected)) {
	if (selectedLanguageIndex != i) {
	selectedLanguageIndex = i;
	if (!scriptLanguages.empty()) {
		scriptSource.setPreferredExtension(
			scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExtension);
	}
	if (scriptSource.getSourceType() == ofxGgmlScriptSourceType::LocalFolder &&
		!scriptSource.getLocalFolderPath().empty()) {
		selectedScriptFileIndex = -1;
		scriptSource.rescan();
	}
	}
	}
	if (isSelected) ImGui::SetItemDefaultFocus();
	}
	ImGui::EndCombo();
	}
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip("Select programming language for code generation and file filtering.");
	}
}

ImGui::SameLine();
ImGui::Text("Source:");
ImGui::SameLine();

// Source type buttons.
ofxGgmlScriptSourceType sourceType = scriptSource.getSourceType();
if (sourceType == ofxGgmlScriptSourceType::None && deferredScriptSourceRestorePending) {
	sourceType = deferredScriptSourceType;
}
bool isNone = (sourceType == ofxGgmlScriptSourceType::None);
bool isLocal = (sourceType == ofxGgmlScriptSourceType::LocalFolder);
bool isGitHub = (sourceType == ofxGgmlScriptSourceType::GitHubRepo);
bool isInternet = (sourceType == ofxGgmlScriptSourceType::Internet);

if (drawStyledToggleButton("None", isNone, ImVec4(0.3f, 0.3f, 0.35f, 1.0f))) {
	clearDeferredScriptSourceRestore();
	scriptSource.clear();
	selectedScriptFileIndex = -1;
}
if (ImGui::IsItemHovered()) {
	showWrappedTooltip("Clear source code reference. Use for standalone code generation.");
}

if (drawStyledToggleButton("Local Folder", isLocal, ImVec4(0.2f, 0.5f, 0.3f, 1.0f))) {
ofFileDialogResult result = ofSystemLoadDialog("Select Script Folder", true);
if (result.bSuccess) {
	clearDeferredScriptSourceRestore();
	selectedScriptFileIndex = -1;
	if (!scriptLanguages.empty()) {
		scriptSource.setPreferredExtension(
			scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExtension);
	}
	scriptSource.setLocalFolder(result.getPath());
}
}
if (ImGui::IsItemHovered()) {
	showWrappedTooltip("Load a local project folder for workspace-aware code assistance.");
}

const auto localWorkspaceInfo = scriptSource.getWorkspaceInfo();
const bool isVisualStudioWorkspace =
	(scriptSource.getSourceType() == ofxGgmlScriptSourceType::LocalFolder) &&
	(localWorkspaceInfo.hasVisualStudioSolution ||
	 !localWorkspaceInfo.visualStudioProjectPaths.empty());
if (drawStyledToggleButton("Visual Studio", isVisualStudioWorkspace, ImVec4(0.45f, 0.35f, 0.7f, 1.0f))) {
	ofFileDialogResult result = ofSystemLoadDialog("Select Visual Studio .sln or .vcxproj", false);
	if (result.bSuccess) {
		clearDeferredScriptSourceRestore();
		selectedScriptFileIndex = -1;
		if (!scriptLanguages.empty()) {
			scriptSource.setPreferredExtension(
				scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExtension);
		}
		scriptSource.setVisualStudioWorkspace(result.getPath());
	}
}
if (ImGui::IsItemHovered()) {
	showWrappedTooltip("Load Visual Studio solution or project for C/C++ workspace context.");
}

if (drawStyledToggleButton("GitHub", isGitHub, ImVec4(0.3f, 0.3f, 0.6f, 1.0f))) {
	clearDeferredScriptSourceRestore();
	selectedScriptFileIndex = -1;
	scriptSource.setGitHubMode();
}
if (ImGui::IsItemHovered()) {
	showWrappedTooltip("Load code from a GitHub repository (owner/repo or full URL).");
}

if (drawStyledToggleButton("Internet", isInternet, ImVec4(0.25f, 0.5f, 0.7f, 1.0f), false)) {
	clearDeferredScriptSourceRestore();
	selectedScriptFileIndex = -1;
	scriptSource.setInternetMode();
}
if (ImGui::IsItemHovered()) {
	showWrappedTooltip("Load code from arbitrary URLs for reference and context.");
}

if (deferredScriptSourceRestorePending &&
	scriptSource.getSourceType() == ofxGgmlScriptSourceType::None) {
	ImGui::Separator();
	if (deferredScriptSourceType == ofxGgmlScriptSourceType::LocalFolder) {
		ImGui::TextDisabled("Saved local workspace is available but not loaded yet.");
		if (!deferredScriptSourcePath.empty()) {
			ImGui::TextWrapped("Path: %s", deferredScriptSourcePath.c_str());
		}
	} else if (deferredScriptSourceType == ofxGgmlScriptSourceType::GitHubRepo) {
		ImGui::TextDisabled("Saved GitHub source is available but not loaded yet.");
		if (std::strlen(scriptSourceGitHub) > 0) {
			ImGui::TextWrapped(
				"Repo: %s%s%s",
				scriptSourceGitHub,
				std::strlen(scriptSourceBranch) > 0 ? " @ " : "",
				std::strlen(scriptSourceBranch) > 0 ? scriptSourceBranch : "");
		}
	} else if (deferredScriptSourceType == ofxGgmlScriptSourceType::Internet) {
		const auto pendingUrls = splitStoredScriptSourceUrls(deferredScriptSourceInternetUrls);
		ImGui::TextDisabled("Saved internet sources are available but not loaded yet.");
		ImGui::TextWrapped("URLs: %d", static_cast<int>(pendingUrls.size()));
	}
	if (ImGui::Button("Load Saved Source", ImVec2(160, 0))) {
		restoreDeferredScriptSourceIfNeeded();
	}
	if (sourceType != ofxGgmlScriptSourceType::None) {
		ImGui::SameLine();
	}
}

// Script source file browser (inline when active).
const auto scriptSourceFiles = scriptSource.getFiles();
const auto drawScriptSourceBrowserAndDetails = [&]() {
	if (scriptSource.getSourceType() != ofxGgmlScriptSourceType::None &&
		!scriptSourceFiles.empty()) {
		ImGui::BeginChild("##ScriptFiles", ImVec2(-1, 80), true);
		if (scriptSource.getSourceType() == ofxGgmlScriptSourceType::LocalFolder) {
			const std::string localPath = scriptSource.getLocalFolderPath();
			ImGui::TextDisabled("Folder: %s", localPath.c_str());
		} else if (scriptSource.getSourceType() == ofxGgmlScriptSourceType::GitHubRepo) {
			const std::string ownerRepo = scriptSource.getGitHubOwnerRepo();
			const std::string branch = scriptSource.getGitHubBranch();
			ImGui::TextDisabled("GitHub: %s (%s)", ownerRepo.c_str(), branch.c_str());
		} else {
			ImGui::TextDisabled("Internet sources");
		}
		for (int i = 0; i < static_cast<int>(scriptSourceFiles.size()); i++) {
			const auto & entry = scriptSourceFiles[static_cast<size_t>(i)];
			ImGui::PushID(i);
			std::string icon = entry.isDirectory ? "[dir] " : "      ";
			bool isSelected = (selectedScriptFileIndex == i);
			if (ImGui::Selectable((icon + entry.name).c_str(), isSelected) &&
				!entry.isDirectory) {
				selectedScriptFileIndex = i;
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
	}

	if (scriptSource.getSourceType() != ofxGgmlScriptSourceType::None) {
		drawScriptSourcePanel();
	}
};

const auto formatScriptSourceSummary = [&]() {
	switch (scriptSource.getSourceType()) {
	case ofxGgmlScriptSourceType::LocalFolder:
		return std::string("Workspace attached: local folder");
	case ofxGgmlScriptSourceType::GitHubRepo:
		return std::string("Workspace attached: GitHub repository");
	case ofxGgmlScriptSourceType::Internet:
		return std::string("Workspace attached: internet reference sources");
	case ofxGgmlScriptSourceType::None:
	default:
		return std::string("No workspace attached yet");
	}
};

if (scriptSimpleUi) {
	ImGui::Separator();
	ImGui::TextDisabled("%s", formatScriptSourceSummary().c_str());
	if (scriptSource.getSourceType() == ofxGgmlScriptSourceType::LocalFolder &&
		!scriptSource.getLocalFolderPath().empty()) {
		ImGui::TextWrapped("Path: %s", scriptSource.getLocalFolderPath().c_str());
	} else if (scriptSource.getSourceType() == ofxGgmlScriptSourceType::GitHubRepo) {
		const std::string ownerRepo = scriptSource.getGitHubOwnerRepo();
		ImGui::TextWrapped(
			"Repo: %s",
			ownerRepo.empty() ? "not loaded yet" : ownerRepo.c_str());
	}
	ImGui::TextDisabled(
		"Files: %d | Focused file: %s",
		static_cast<int>(scriptSourceFiles.size()),
		(selectedScriptFileIndex >= 0 &&
		 selectedScriptFileIndex < static_cast<int>(scriptSourceFiles.size()) &&
		 !scriptSourceFiles[static_cast<size_t>(selectedScriptFileIndex)].isDirectory)
			? scriptSourceFiles[static_cast<size_t>(selectedScriptFileIndex)].name.c_str()
			: "none");
	if (ImGui::CollapsingHeader("Workspace files & source details")) {
		drawScriptSourceBrowserAndDetails();
	}
} else {
	drawScriptSourceBrowserAndDetails();
}

	ImGui::Spacing();

	// --- 3. Chat History (dynamic size) ---
	ImGui::Text("Coding Chat:");
	ImGui::BeginChild("##ScriptChatHistory", ImVec2(-1, -120), true);
	for (const auto & msg : scriptMessages) {
		if (msg.role == "user") {
			ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "You:");
		} else if (msg.role == "system") {
			ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "System:");
		} else {
			ImGui::TextColored(ImVec4(0.6f, 0.7f, 1.0f, 1.0f), "AI:");
		}
		ImGui::TextWrapped("%s", msg.text.c_str());
		ImGui::Spacing();
	}
	if (generating.load() && activeGenerationMode == AiMode::Script) {
		ImGui::TextColored(ImVec4(0.6f, 0.7f, 1.0f, 1.0f), "AI:");
		std::string partial;
		{
			std::lock_guard<std::mutex> lock(streamMutex);
			partial = streamingOutput;
		}
		if (partial.empty()) {
			int dots = static_cast<int>(ImGui::GetTime() * kDotsAnimationSpeed) % 4;
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", kWaitingLabels[dots]);
		} else {
			std::string preview;
			if (!scriptOutput.empty() && partial.size() > scriptOutput.size()) {
				preview = partial.substr(scriptOutput.size());
			} else {
				preview = partial;
			}
			ImGui::TextWrapped("%s", preview.c_str());
		}
	}

	if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40.0f) {
		ImGui::SetScrollHereY(1.0f);
	}
	ImGui::EndChild();

	// --- 4. Multiline Chat Input ---
	ImGui::TextDisabled("Tip: Enter sends, Ctrl+Enter for new line");
	const bool scriptChatSubmitted = ImGui::InputTextMultiline(
		"##ScriptIn", scriptInput, sizeof(scriptInput), ImVec2(-1, 50),
		ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);

	static const char * kScriptAgentModeLabels[] = {"Build", "Plan"};
	ImGui::Text("Agent:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(140);
	if (ImGui::BeginCombo(
		"##ScriptAgentMode",
		kScriptAgentModeLabels[std::clamp(scriptAgentModeIndex, 0, 1)])) {
		for (int i = 0; i < 2; ++i) {
			const bool selected = (scriptAgentModeIndex == i);
			if (ImGui::Selectable(kScriptAgentModeLabels[i], selected)) {
				scriptAgentModeIndex = i;
				autoSaveSession();
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip(
			scriptAgentModeIndex == 0
				? "Build keeps the normal edit-oriented assistant flow and unified-diff bias."
				: "Plan stays read-only and biases the coding agent toward exploration and planning.");
	}
	ImGui::SameLine();
	ImGui::TextDisabled(
		"%s",
		scriptAgentModeIndex == 0
			? "Edit-oriented runtime"
			: "Read-only planning runtime");

const bool hasSelectedFile =
	selectedScriptFileIndex >= 0 &&
	selectedScriptFileIndex < static_cast<int>(scriptSourceFiles.size()) &&
	!scriptSourceFiles[static_cast<size_t>(selectedScriptFileIndex)].isDirectory;
const bool hasUserInput = std::strlen(scriptInput) > 0;
const bool hasWorkspaceSource =
	sourceType == ofxGgmlScriptSourceType::LocalFolder &&
	!scriptSource.getLocalFolderPath().empty();
static char scriptBuildErrors[8192] = {};
static bool restrictWorkspaceToFocusedFile = true;

	auto buildScriptAssistantContext = [this]() {
		ofxGgmlCodeAssistantContext context;
		context.scriptSource = &scriptSource;
		context.projectMemory = &scriptProjectMemory;
		context.focusedFileIndex = selectedScriptFileIndex;
		context.includeRepoContext = scriptIncludeRepoContext;
		context.maxRepoFiles = kMaxScriptContextFiles;
		context.maxFocusedFileChars = kMaxFocusedFileSnippetChars;
		context.activeMode = "Script";
		if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
			context.selectedBackend = "llama-server";
			const std::string serverUrl = trim(textServerUrl);
			if (!serverUrl.empty()) {
				context.selectedBackend += " @ " + serverUrl;
			}
		} else {
			context.selectedBackend = trim(llamaCliCommand).empty()
				? "llama-completion"
				: trim(llamaCliCommand);
			const std::string ggmlBackend = trim(ggml.getBackendName());
			if (!ggmlBackend.empty()) {
				context.selectedBackend += " via " + ggmlBackend;
			}
		}
		context.recentTouchedFiles = recentScriptTouchedFiles;
		context.lastFailureReason = lastScriptFailureReason;
		return context;
	};

auto buildWorkspaceAllowedFiles = [&]() {
	std::vector<std::string> allowedFiles;
	if (restrictWorkspaceToFocusedFile && hasSelectedFile) {
		allowedFiles.push_back(
			scriptSourceFiles[static_cast<size_t>(selectedScriptFileIndex)].fullPath);
	}
	return allowedFiles;
};

	struct ResolvedScriptReference {
		std::string displayLabel;
		std::string resolvedPath;
		std::string resolvedSymbol;
		bool resolved = false;
		bool forceWorkspaceContext = false;
		bool forceGeneralExploration = false;
	};

	auto stripScriptAtReferences = [](const std::string & rawInput) {
		std::string cleaned;
		cleaned.reserve(rawInput.size());
		auto isReferenceBoundary = [](char ch) {
			return std::isspace(static_cast<unsigned char>(ch)) != 0 ||
				ch == ',' || ch == ';' || ch == ')' || ch == '(' ||
				ch == '[' || ch == ']' || ch == '{' || ch == '}' ||
				ch == '"' || ch == '\'';
		};
		for (size_t i = 0; i < rawInput.size(); ++i) {
			if (rawInput[i] == '@' &&
				(i == 0 ||
				 std::isspace(static_cast<unsigned char>(rawInput[i - 1])) ||
				 rawInput[i - 1] == '(' || rawInput[i - 1] == '[' ||
				 rawInput[i - 1] == '{' || rawInput[i - 1] == '"')) {
				size_t end = i + 1;
				while (end < rawInput.size() && !isReferenceBoundary(rawInput[end])) {
					++end;
				}
				if (end > i + 1) {
					i = end - 1;
					continue;
				}
			}
			cleaned.push_back(rawInput[i]);
		}
		return trim(cleaned);
	};

	auto resolveScriptReferences = [&](const std::string & rawInput) {
		std::vector<ResolvedScriptReference> resolved;
		const auto tokens = extractScriptAtReferenceTokens(rawInput);
		for (const auto & token : tokens) {
			ResolvedScriptReference item;
			item.displayLabel = "@" + token.rawToken;
			if (token.normalizedToken == "focused" ||
				token.normalizedToken == "current" ||
				token.normalizedToken == "thisfile") {
				if (hasSelectedFile) {
					item.resolved = true;
					item.resolvedPath =
						scriptSourceFiles[static_cast<size_t>(selectedScriptFileIndex)].fullPath;
					item.displayLabel += " -> focused file";
				} else {
					item.displayLabel += " -> no focused file";
				}
			} else if (
				token.normalizedToken == "recent" ||
				token.normalizedToken == "touched") {
				if (!recentScriptTouchedFiles.empty()) {
					item.resolved = true;
					item.resolvedPath = recentScriptTouchedFiles.front();
					item.displayLabel +=
						" -> recent touched files (" +
						ofToString(static_cast<int>(recentScriptTouchedFiles.size())) + ")";
				} else {
					item.displayLabel += " -> no recent touched files";
				}
			} else if (
				token.normalizedToken == "general" ||
				token.normalizedToken == "explore") {
				item.resolved = true;
				item.forceWorkspaceContext = true;
				item.forceGeneralExploration = true;
				item.displayLabel += " -> broad read-only repo exploration";
			} else if (
				token.normalizedToken == "workspace" ||
				token.normalizedToken == "repo") {
				item.resolved = true;
				item.forceWorkspaceContext = true;
				item.displayLabel += " -> workspace context";
			} else if (
				token.normalizedToken.rfind("symbol:", 0) == 0 ||
				token.normalizedToken.rfind("sym:", 0) == 0) {
				const size_t split = token.normalizedToken.find(':');
				const std::string rawSymbol =
					split == std::string::npos
						? std::string()
						: trim(token.rawToken.substr(split + 1));
				if (!rawSymbol.empty()) {
					item.resolved = true;
					item.resolvedSymbol = rawSymbol;
					item.displayLabel += " -> symbol " + rawSymbol;
				} else {
					item.displayLabel += " -> missing symbol name";
				}
			} else {
				std::vector<std::string> matches;
				for (const auto & entry : scriptSourceFiles) {
					if (entry.isDirectory) {
						continue;
					}
					std::string normalizedName = ofToLower(entry.name);
					std::string normalizedPath = ofToLower(entry.fullPath);
					if (normalizedName == token.normalizedToken ||
						normalizedName.find(token.normalizedToken) != std::string::npos ||
						normalizedPath.find(token.normalizedToken) != std::string::npos) {
						matches.push_back(entry.fullPath);
					}
				}
				if (matches.size() == 1) {
					item.resolved = true;
					item.resolvedPath = matches.front();
					item.displayLabel += " -> " + ofFilePath::getFileName(matches.front());
				} else if (matches.empty()) {
					item.displayLabel += " -> not found";
				} else {
					item.displayLabel +=
						" -> ambiguous (" + ofToString(static_cast<int>(matches.size())) + " matches)";
				}
			}
			resolved.push_back(item);
		}
		return resolved;
	};

auto appendScriptAssistantOutput = [&](const std::string & userLabel,
	const std::string & assistantText) {
	if (!userLabel.empty()) {
		scriptMessages.push_back({"user", userLabel, ofGetElapsedTimef()});
	}
	scriptOutput = assistantText;
	scriptMessages.push_back({"assistant", assistantText, ofGetElapsedTimef()});
};

auto formatSymbolContext = [](const ofxGgmlCodeAssistantSymbolContext & symbolContext) {
	std::ostringstream out;
	out << "Semantic context for: " << symbolContext.query << "\n\n";
	if (symbolContext.definitions.empty()) {
		out << "Definitions: none found\n";
	} else {
		out << "Definitions:\n";
		for (const auto & symbol : symbolContext.definitions) {
			out << "- " << symbol.name;
			if (!symbol.signature.empty()) {
				out << " :: " << symbol.signature;
			}
			out << "\n  " << symbol.filePath << ":" << symbol.line << "\n";
			if (!symbol.preview.empty()) {
				out << "  " << trim(symbol.preview) << "\n";
			}
		}
	}

	if (!symbolContext.relatedReferences.empty()) {
		out << "\nReferences and callers:\n";
		for (const auto & ref : symbolContext.relatedReferences) {
			out << "- " << ref.kind << " in " << ref.filePath << ":" << ref.line;
			if (!ref.callerSymbol.empty()) {
				out << " via " << ref.callerSymbol;
			}
			out << "\n";
			if (!ref.preview.empty()) {
				out << "  " << trim(ref.preview) << "\n";
			}
		}
	}

	return out.str();
};

auto previewWorkspacePlan = [&](const std::string & label) {
	if (sourceType != ofxGgmlScriptSourceType::LocalFolder ||
		scriptSource.getLocalFolderPath().empty()) {
		appendScriptAssistantOutput(label,
			"Workspace preview requires a loaded local folder source.");
		return;
	}

	auto structured = ofxGgmlCodeAssistant::parseStructuredResult(scriptOutput);
	if (structured.unifiedDiff.empty()) {
		structured.unifiedDiff =
			ofxGgmlCodeAssistant::buildUnifiedDiffFromStructuredResult(structured);
	}

	const std::string workspaceRoot = scriptSource.getLocalFolderPath();
	const auto allowedFiles = buildWorkspaceAllowedFiles();

	ofxGgmlWorkspacePatchValidationResult validation;
	ofxGgmlWorkspaceApplyResult applyResult;
	if (!structured.unifiedDiff.empty()) {
		validation = scriptWorkspaceAssistant.validateUnifiedDiff(
			structured.unifiedDiff,
			workspaceRoot,
			allowedFiles);
		applyResult = scriptWorkspaceAssistant.applyUnifiedDiff(
			structured.unifiedDiff,
			workspaceRoot,
			allowedFiles,
			true);
	} else if (!structured.patchOperations.empty()) {
		validation = scriptWorkspaceAssistant.validatePatchOperations(
			structured.patchOperations,
			workspaceRoot,
			allowedFiles);
		applyResult = scriptWorkspaceAssistant.applyPatchOperations(
			structured.patchOperations,
			workspaceRoot,
			allowedFiles,
			true);
	} else {
		appendScriptAssistantOutput(label,
			"No structured patch plan was found in the latest script output.");
		return;
	}

	std::vector<std::string> changedFiles = applyResult.touchedFiles;
	if (changedFiles.empty()) {
		changedFiles = validation.validatedFiles;
	}
	if (changedFiles.empty()) {
		for (const auto & fileIntent : structured.filesToTouch) {
			if (!fileIntent.filePath.empty()) {
				changedFiles.push_back(fileIntent.filePath);
			}
		}
	}
	const auto workspaceInfoSnapshot = scriptSource.getWorkspaceInfo();
	const auto verificationCommands = structured.verificationCommands.empty()
		? scriptWorkspaceAssistant.suggestVerificationCommands(
			changedFiles,
			workspaceRoot,
			&workspaceInfoSnapshot)
		: structured.verificationCommands;
	if (!changedFiles.empty()) {
		recentScriptTouchedFiles = changedFiles;
	}
	if (!verificationCommands.empty()) {
		cachedScriptVerificationCommands = verificationCommands;
	}
	if (!validation.success) {
		lastScriptFailureReason = validation.messages.empty()
			? std::string("Workspace dry run validation failed.")
			: validation.messages.front();
	} else {
		lastScriptFailureReason.clear();
	}

	std::ostringstream out;
	out << label << "\n\n";
	out << "Workspace root: " << workspaceRoot << "\n";
	out << "Validation: " << (validation.success ? "passed" : "failed") << "\n";
	if (!allowedFiles.empty()) {
		out << "Allow-list:\n";
		for (const auto & file : allowedFiles) {
			out << "- " << file << "\n";
		}
	}
	if (!validation.messages.empty()) {
		out << "\nValidation notes:\n";
		for (const auto & message : validation.messages) {
			out << "- " << message << "\n";
		}
	}
	if (!applyResult.messages.empty()) {
		out << "\nDry-run result:\n";
		for (const auto & message : applyResult.messages) {
			out << "- " << message << "\n";
		}
	}
	if (!applyResult.unifiedDiffPreview.empty()) {
		out << "\nUnified diff preview:\n" << applyResult.unifiedDiffPreview << "\n";
	}
	if (!verificationCommands.empty()) {
		out << "\nSuggested verification commands:\n";
		for (const auto & command : verificationCommands) {
			out << "- " << command.label << ": " << command.executable;
			for (const auto & arg : command.arguments) {
				out << " " << arg;
			}
			if (!command.workingDirectory.empty()) {
				out << "  (cwd: " << command.workingDirectory << ")";
			}
			out << "\n";
		}
	}

	appendScriptAssistantOutput(label, out.str());
};

auto submitScriptRequest = [&](ofxGgmlCodeAssistantAction action,
	const std::string & userInput = std::string(),
	const std::string & bodyOverride = std::string(),
	const std::string & labelOverride = std::string(),
	bool clearInputAfter = false,
	bool requestStructuredResult = false,
	bool requestUnifiedDiff = false,
	const std::string & buildErrors = std::string(),
	const std::vector<std::string> & allowedFiles = {}) {
	const auto resolvedReferences = resolveScriptReferences(userInput);
	const std::string cleanedUserInput = stripScriptAtReferences(userInput);
	ofxGgmlCodeAssistantRequest request;
	request.action = action;
	request.userInput = cleanedUserInput.empty() ? userInput : cleanedUserInput;
	request.lastTask = lastScriptRequest;
	request.lastOutput = scriptOutput;
	request.bodyOverride = bodyOverride;
	request.labelOverride = labelOverride;
	request.requestStructuredResult = requestStructuredResult;
	request.requestUnifiedDiff = requestUnifiedDiff;
	request.buildErrors = buildErrors;
	request.allowedFiles = allowedFiles;
	if (scriptAgentModeIndex == 1) {
		std::ostringstream planModeBody;
		planModeBody
			<< "Work in read-only planning mode. Focus on investigation, intended files, "
			<< "risks, and verification steps before proposing any concrete edits.";
		if (!trim(request.bodyOverride).empty()) {
			planModeBody << "\n\n" << trim(request.bodyOverride);
		}
		request.bodyOverride = trim(planModeBody.str());
		request.requestUnifiedDiff = false;
	}
	std::vector<std::string> mergedAllowedFiles = request.allowedFiles;
	std::vector<std::string> targetSymbols;
	std::vector<std::string> referenceNotes;
	bool forceWorkspaceContext = false;
	bool forceGeneralExploration = false;
	for (const auto & reference : resolvedReferences) {
		referenceNotes.push_back(reference.displayLabel);
		if (!reference.resolvedPath.empty()) {
			mergedAllowedFiles.push_back(reference.resolvedPath);
		}
		if (!reference.resolvedSymbol.empty()) {
			targetSymbols.push_back(reference.resolvedSymbol);
		}
		forceWorkspaceContext = forceWorkspaceContext || reference.forceWorkspaceContext;
		forceGeneralExploration =
			forceGeneralExploration || reference.forceGeneralExploration;
	}
	std::sort(mergedAllowedFiles.begin(), mergedAllowedFiles.end());
	mergedAllowedFiles.erase(
		std::unique(mergedAllowedFiles.begin(), mergedAllowedFiles.end()),
		mergedAllowedFiles.end());
	std::sort(targetSymbols.begin(), targetSymbols.end());
	targetSymbols.erase(
		std::unique(targetSymbols.begin(), targetSymbols.end()),
		targetSymbols.end());
	request.allowedFiles = std::move(mergedAllowedFiles);
	if (!targetSymbols.empty()) {
		request.symbolQuery.targetSymbols = targetSymbols;
		request.symbolQuery.query = cleanedUserInput.empty() ? userInput : cleanedUserInput;
		request.symbolQuery.includeCallers = true;
		request.symbolQuery.maxDefinitions = 4;
		request.symbolQuery.maxReferences = 8;
	}
	if (!referenceNotes.empty()) {
		std::ostringstream referenceBody;
		referenceBody << "Explicit @ references from the user:\n";
		for (const auto & note : referenceNotes) {
			referenceBody << "- " << note << "\n";
		}
		if (forceGeneralExploration) {
			referenceBody
				<< "- treat this as broad repository exploration and planning rather than direct patch application\n";
		}
		if (forceWorkspaceContext) {
			referenceBody << "- prefer full loaded workspace context for this request\n";
		}
		if (!trim(bodyOverride).empty()) {
			referenceBody << "\n" << trim(bodyOverride);
		}
		request.bodyOverride = trim(referenceBody.str());
	}
	if (!scriptLanguages.empty() &&
		selectedLanguageIndex >= 0 &&
		selectedLanguageIndex < static_cast<int>(scriptLanguages.size())) {
		request.language = scriptLanguages[static_cast<size_t>(selectedLanguageIndex)];
	}

	ofxGgmlCodeAssistantContext requestContext = buildScriptAssistantContext();
	if (forceWorkspaceContext) {
		requestContext.includeRepoContext = true;
	}
	if (forceGeneralExploration) {
		requestContext.activeMode = "Script @general";
		requestContext.includeRepoContext = true;
		requestContext.attachScriptSourceDocuments = true;
		requestContext.maxRepoFiles = std::max<size_t>(requestContext.maxRepoFiles, 120);
		requestContext.maxFocusedFileChars =
			std::max<size_t>(requestContext.maxFocusedFileChars, 3200);
		requestContext.projectMemoryHeading =
			"Project memory from previous coding requests (use for broad planning and repo exploration):";
	}

	const auto prepared = scriptAssistant.preparePrompt(
		request,
		requestContext);
	const std::string taskText = prepared.body.empty()
		? userInput
		: prepared.body;
	const std::string requestLabel = prepared.requestLabel.empty()
		? taskText
		: prepared.requestLabel;

	scriptMessages.push_back({"user", requestLabel, ofGetElapsedTimef()});
	request.userInput = taskText;
	runScriptAssistantRequest(
		request,
		requestLabel,
		clearInputAfter,
		{},
		&requestContext,
		forceGeneralExploration);
};

auto summarizeLocalChanges = [&](const std::string & focusText) {
	if (sourceType != ofxGgmlScriptSourceType::LocalFolder ||
		scriptSource.getLocalFolderPath().empty()) {
		appendScriptAssistantOutput(
			"Summarize local changes",
			"Local change summaries require a loaded local folder workspace.");
		return;
	}

	const auto snapshot = captureWorkspaceDiffSnapshot(scriptSource.getLocalFolderPath());
	if (!snapshot.success) {
		appendScriptAssistantOutput("Summarize local changes", snapshot.error);
		return;
	}
	if (!snapshot.hasChanges) {
		appendScriptAssistantOutput(
			"Summarize local changes",
			"No local Git changes were detected in the current workspace.");
		return;
	}

	std::ostringstream body;
	body << "Summarize the following local Git changes professionally for reviewers. "
		<< "Focus on user-visible impact, important files, notable risks, and verification notes.\n";
	if (!trim(focusText).empty()) {
		body << "Review focus: " << trim(focusText) << "\n";
	}
	body << "\nRepository root: " << snapshot.repoRoot << "\n";
	if (!snapshot.statusText.empty()) {
		body << "\nGit status:\n"
			<< truncatePromptPayload(snapshot.statusText, 2000) << "\n";
	}
	if (!snapshot.diffText.empty()) {
		body << "\nUnified diff:\n"
			<< truncatePromptPayload(snapshot.diffText, 12000) << "\n";
	}

	submitScriptRequest(
		ofxGgmlCodeAssistantAction::SummarizeChanges,
		focusText,
		body.str(),
		"Summarize local changes.",
		false);
};

auto executeScriptSlashCommand = [&](const ScriptSlashCommand & command) {
	switch (command.kind) {
	case ScriptSlashCommandKind::Help:
		appendScriptAssistantOutput("Slash command help", buildScriptCommandHelpText());
		return true;
	case ScriptSlashCommandKind::ReviewAll: {
		if (sourceType != ofxGgmlScriptSourceType::None && !scriptSourceFiles.empty()) {
			const std::string reviewQuery = trim(command.argument).empty()
				? ofxGgmlCodeReview::defaultReviewQuery()
				: trim(command.argument);
			scriptMessages.push_back({
				"user",
				"Review all files in loaded " +
					std::string(sourceType == ofxGgmlScriptSourceType::LocalFolder ? "folder" : "repo") +
					". Focus: " + reviewQuery,
				ofGetElapsedTimef()});
			runHierarchicalReview(reviewQuery);
			return true;
		}
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::Review,
			command.argument,
			"",
			"",
			false);
		return true;
	}
	case ScriptSlashCommandKind::ReviewFix: {
		std::ostringstream body;
		body << "Review the current workspace and return an actionable fix plan. "
			<< "Only include concrete, evidence-backed issues. "
			<< "Prefer a structured plan with a unified diff when a clear next fix is visible.";
		if (!trim(command.argument).empty()) {
			body << "\n\nFocus: " << trim(command.argument);
		}
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::Review,
			command.argument,
			body.str(),
			"Review with fix plan.",
			false,
			true,
			true,
			"",
			buildWorkspaceAllowedFiles());
		return true;
	}
	case ScriptSlashCommandKind::NextEdit:
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::NextEdit,
			command.argument,
			"",
			"",
			false,
			true,
			true,
			"",
			buildWorkspaceAllowedFiles());
		return true;
	case ScriptSlashCommandKind::SummarizeChanges:
		summarizeLocalChanges(command.argument);
		return true;
	case ScriptSlashCommandKind::Tests: {
		std::ostringstream body;
		body << "Propose the highest-value tests for this workspace and request. "
			<< "Prefer concrete test names, likely files, and verification commands. "
			<< "If no additional tests are needed, say so clearly.";
		if (!trim(command.argument).empty()) {
			body << "\n\nFocus: " << trim(command.argument);
		}
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::Generate,
			command.argument,
			body.str(),
			"Plan tests.",
			false,
			true,
			false,
			"",
			buildWorkspaceAllowedFiles());
		return true;
	}
	case ScriptSlashCommandKind::FixPlan:
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::Edit,
			command.argument,
			"",
			"",
			false,
			true,
			true,
			"",
			buildWorkspaceAllowedFiles());
		return true;
	case ScriptSlashCommandKind::Explain:
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::Explain,
			command.argument,
			"",
			"",
			false);
		return true;
	case ScriptSlashCommandKind::Docs:
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::GroundedDocs,
			command.argument,
			"",
			"",
			false);
		return true;
	case ScriptSlashCommandKind::None:
	default:
		return false;
	}
};

struct ScriptActionSpec {
	const char * label;
	ImVec2 size;
	ofxGgmlCodeAssistantAction action;
};

const ScriptActionSpec actionSpecs[] = {
	{ "Generate", ImVec2(100, 0), ofxGgmlCodeAssistantAction::Generate },
	{ "Explain", ImVec2(100, 0), ofxGgmlCodeAssistantAction::Explain },
	{ "Debug", ImVec2(100, 0), ofxGgmlCodeAssistantAction::Debug },
	{ "Optimize", ImVec2(100, 0), ofxGgmlCodeAssistantAction::Optimize },
	{ "Refactor", ImVec2(100, 0), ofxGgmlCodeAssistantAction::Refactor },
	{ "Next Edit", ImVec2(100, 0), ofxGgmlCodeAssistantAction::NextEdit },
	{ "Review Mode", ImVec2(100, 0), ofxGgmlCodeAssistantAction::Review },
};

bool canSendScriptChat = !generating.load() && hasUserInput;
const ScriptSlashCommand slashCommand = hasUserInput
	? parseScriptSlashCommand(scriptInput)
	: ScriptSlashCommand{};
const auto scriptReferences = hasUserInput
	? resolveScriptReferences(scriptInput)
	: std::vector<ResolvedScriptReference>{};
const std::string currentScriptInput = std::string(scriptInput);
const size_t lastScriptTokenStart = currentScriptInput.find_last_of(" \t\r\n");
const std::string lastScriptToken = trim(
	lastScriptTokenStart == std::string::npos
		? currentScriptInput
		: currentScriptInput.substr(lastScriptTokenStart + 1));
const bool scriptHasAutocompleteToken =
	!lastScriptToken.empty() && lastScriptToken.front() == '@';

auto replaceTrailingScriptToken = [&](const std::string & replacement, bool appendTrailingSpace = true) {
	std::string updatedInput = currentScriptInput;
	const size_t tokenStart = lastScriptTokenStart == std::string::npos
		? 0
		: lastScriptTokenStart + 1;
	updatedInput = updatedInput.substr(0, tokenStart) + replacement;
	if (appendTrailingSpace) {
		updatedInput += " ";
	}
	copyStringToBuffer(scriptInput, sizeof(scriptInput), updatedInput);
};

struct ScriptAutocompleteSuggestion {
	std::string label;
	std::string replacement;
	std::string tooltip;
};
std::vector<ScriptAutocompleteSuggestion> autocompleteSuggestions;
std::string autocompleteHint;
if (scriptHasAutocompleteToken) {
	const std::string tokenBody = trim(lastScriptToken.substr(1));
	const std::string tokenBodyLower = ofToLower(tokenBody);
	const auto pushAutocompleteSuggestion =
		[&](const std::string & label,
			const std::string & replacement,
			const std::string & tooltip) {
			if (autocompleteSuggestions.size() >= 8) {
				return;
			}
			for (const auto & existing : autocompleteSuggestions) {
				if (existing.replacement == replacement) {
					return;
				}
			}
			autocompleteSuggestions.push_back({label, replacement, tooltip});
		};

	const struct {
		const char * token;
		const char * tooltip;
	} builtInReferences[] = {
		{"@focused", "Reference the currently selected file."},
		{"@recent", "Reference recent touched files."},
		{"@general", "Broaden into read-only workspace exploration and planning."},
		{"@workspace", "Bias toward the full loaded workspace."},
		{"@symbol:", "Search semantic symbol context, for example @symbol:update."}
	};

	if (tokenBodyLower.rfind("symbol:", 0) == 0 || tokenBodyLower.rfind("sym:", 0) == 0) {
		const size_t separator = tokenBody.find(':');
		const std::string symbolNeedle = separator == std::string::npos
			? std::string()
			: trim(tokenBody.substr(separator + 1));
		if (symbolNeedle.empty()) {
			autocompleteHint = "Type after @symbol: to search indexed workspace symbols.";
		} else if (!hasWorkspaceSource) {
			autocompleteHint = "Load a local workspace to autocomplete semantic symbols.";
		} else {
			static std::string cachedSymbolNeedle;
			static std::vector<std::string> cachedSymbolSuggestions;
			if (cachedSymbolNeedle != symbolNeedle) {
				cachedSymbolNeedle = symbolNeedle;
				cachedSymbolSuggestions.clear();
				ofxGgmlCodeAssistantSymbolQuery query;
				query.query = symbolNeedle;
				query.targetSymbols = {symbolNeedle};
				query.includeCallers = true;
				query.maxDefinitions = 6;
				query.maxReferences = 6;
				const auto symbolContext =
					scriptAssistant.buildSymbolContext(query, buildScriptAssistantContext());
				for (const auto & symbol : symbolContext.definitions) {
					const std::string symbolName = trim(symbol.name);
					if (symbolName.empty()) {
						continue;
					}
					if (std::find(
							cachedSymbolSuggestions.begin(),
							cachedSymbolSuggestions.end(),
							symbolName) == cachedSymbolSuggestions.end()) {
						cachedSymbolSuggestions.push_back(symbolName);
					}
				}
			}
			for (const auto & symbolName : cachedSymbolSuggestions) {
				pushAutocompleteSuggestion(
					"@" + std::string("symbol:") + symbolName,
					"@symbol:" + symbolName,
					"Use semantic symbol grounding for " + symbolName + ".");
			}
			if (autocompleteSuggestions.empty()) {
				autocompleteHint = "No symbol matches found yet. Try a broader name.";
			}
		}
	} else {
		for (const auto & builtInReference : builtInReferences) {
			const std::string token = builtInReference.token;
			if (tokenBody.empty() ||
				ofToLower(token.substr(1)).find(tokenBodyLower) != std::string::npos) {
				pushAutocompleteSuggestion(token, token, builtInReference.tooltip);
			}
		}
		for (const auto & entry : scriptSourceFiles) {
			if (entry.isDirectory) {
				continue;
			}
			const std::string entryNameLower = ofToLower(entry.name);
			const std::string entryPathLower = ofToLower(entry.fullPath);
			if (tokenBody.empty() ||
				entryNameLower.find(tokenBodyLower) != std::string::npos ||
				entryPathLower.find(tokenBodyLower) != std::string::npos) {
				pushAutocompleteSuggestion(
					"@" + entry.name,
					"@" + entry.name,
					entry.fullPath);
			}
		}
		if (autocompleteSuggestions.empty()) {
			autocompleteHint = "No loaded file or reference matches the current @ token.";
		}
	}
}

const bool showScriptAutocompletePopup =
	scriptHasAutocompleteToken && !autocompleteSuggestions.empty();
if (!showScriptAutocompletePopup) {
	scriptAutocompleteSelectedIndex = 0;
	scriptAutocompleteLastToken.clear();
} else {
	if (scriptAutocompleteLastToken != lastScriptToken) {
		scriptAutocompleteLastToken = lastScriptToken;
		scriptAutocompleteSelectedIndex = 0;
	} else {
		scriptAutocompleteSelectedIndex =
			ofClamp(scriptAutocompleteSelectedIndex, 0, static_cast<int>(autocompleteSuggestions.size()) - 1);
	}
}

bool scriptAutocompleteAccepted = false;
if (showScriptAutocompletePopup) {
	if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
		scriptAutocompleteSelectedIndex =
			(scriptAutocompleteSelectedIndex + 1) % static_cast<int>(autocompleteSuggestions.size());
	}
	if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
		scriptAutocompleteSelectedIndex =
			(scriptAutocompleteSelectedIndex - 1 + static_cast<int>(autocompleteSuggestions.size())) %
			static_cast<int>(autocompleteSuggestions.size());
	}
	if (scriptChatSubmitted || ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
		const auto & selectedSuggestion =
			autocompleteSuggestions[static_cast<size_t>(scriptAutocompleteSelectedIndex)];
		replaceTrailingScriptToken(selectedSuggestion.replacement);
		scriptAutocompleteAccepted = true;
	}
}
const bool effectiveScriptChatSubmitted = scriptChatSubmitted && !scriptAutocompleteAccepted;

auto clearScriptConversation = [&]() {
	scriptMessages.clear();
	scriptOutput.clear();
	scriptInlineCompletionOutput.clear();
	scriptInlineCompletionTargetPath.clear();
	lastScriptFailureReason.clear();
	recentScriptTouchedFiles.clear();
	scriptAssistantEvents.clear();
	scriptAssistantToolCalls.clear();
	scriptAssistantSession = {};
	scriptCodingAgent.resetSession();
	lastScriptOutputLikelyCutoff = false;
	lastScriptOutputTail.clear();
};

struct ScriptQuickChip {
	const char * label;
	const char * commandText;
	const char * tooltip;
};
const ScriptQuickChip quickChips[] = {
	{ "/review", "/review ", "Review the loaded workspace or current request." },
	{ "/reviewfix", "/reviewfix ", "Review and ask for an actionable fix plan." },
	{ "/nextedit", "/nextedit ", "Predict the most likely next change." },
	{ "/tests", "/tests ", "Plan the highest-value tests." },
	{ "@focused", "@focused ", "Reference the currently selected file." },
	{ "@recent", "@recent ", "Reference recent touched files." },
	{ "@general", "@general ", "Broaden into read-only workspace exploration." },
	{ "@workspace", "@workspace ", "Bias toward whole-workspace context." },
	{ "@symbol:", "@symbol:", "Reference a symbol, for example @symbol:update." }
};

const bool showScriptPromptHelpers =
	!scriptSimpleUi || ImGui::CollapsingHeader("Prompt helpers & references");
if (showScriptPromptHelpers) {
	if (slashCommand.kind != ScriptSlashCommandKind::None) {
		ImGui::TextDisabled("Detected slash action. Send will execute a structured Script command.");
	}

	ImGui::TextDisabled("Quick actions:");
	for (size_t chipIndex = 0; chipIndex < std::size(quickChips); ++chipIndex) {
		ImGui::PushID(static_cast<int>(chipIndex));
		if (ImGui::SmallButton(quickChips[chipIndex].label)) {
			std::string updatedInput = trim(std::string(scriptInput));
			if (!updatedInput.empty()) {
				updatedInput += " ";
			}
			updatedInput += quickChips[chipIndex].commandText;
			copyStringToBuffer(scriptInput, sizeof(scriptInput), updatedInput);
		}
		if (ImGui::IsItemHovered()) {
			showWrappedTooltip(quickChips[chipIndex].tooltip);
		}
		ImGui::PopID();
		if (chipIndex + 1 < std::size(quickChips)) {
			ImGui::SameLine();
		}
	}

	if (scriptHasAutocompleteToken) {
		ImGui::TextDisabled("Autocomplete:");
		if (showScriptAutocompletePopup) {
			if (ImGui::BeginChild("##ScriptAutocompletePopup", ImVec2(-1, 110), true)) {
				for (size_t suggestionIndex = 0;
					 suggestionIndex < autocompleteSuggestions.size();
					 ++suggestionIndex) {
					const auto & suggestion = autocompleteSuggestions[suggestionIndex];
					const bool isSelected =
						static_cast<int>(suggestionIndex) == scriptAutocompleteSelectedIndex;
					ImGui::PushID(static_cast<int>(suggestionIndex + 200));
					if (ImGui::Selectable(suggestion.label.c_str(), isSelected)) {
						scriptAutocompleteSelectedIndex = static_cast<int>(suggestionIndex);
						replaceTrailingScriptToken(suggestion.replacement);
					}
					if (isSelected) {
						ImGui::SetScrollHereY(0.5f);
					}
					if (ImGui::IsItemHovered() && !suggestion.tooltip.empty()) {
						showWrappedTooltip(suggestion.tooltip);
					}
					ImGui::PopID();
				}
			}
			ImGui::EndChild();
			ImGui::TextDisabled("Use Up/Down + Enter, Tab, or click a suggestion.");
		} else if (!autocompleteHint.empty()) {
			ImGui::TextDisabled("%s", autocompleteHint.c_str());
		}
	}

	if (!scriptReferences.empty()) {
		ImGui::TextDisabled("Detected @ references:");
		for (const auto & reference : scriptReferences) {
			ImGui::BulletText("%s", reference.displayLabel.c_str());
		}
	}
} else if (slashCommand.kind != ScriptSlashCommandKind::None) {
	ImGui::TextDisabled("Slash command detected. Open Prompt helpers to inspect it.");
}

ImGui::BeginDisabled(generating.load());
if (canSendScriptChat) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.3f, 1.0f));
if ((ImGui::Button("Send to Chat", ImVec2(110, 0)) || effectiveScriptChatSubmitted) && canSendScriptChat) {
	if (slashCommand.kind != ScriptSlashCommandKind::None) {
		if (executeScriptSlashCommand(slashCommand)) {
			std::memset(scriptInput, 0, sizeof(scriptInput));
		}
	} else {
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::Ask,
			scriptInput,
			"",
			"",
			true);
	}
}
if (canSendScriptChat) ImGui::PopStyleColor();
ImGui::SameLine();
	if (ImGui::Button("Clear Chat", ImVec2(90, 0))) {
		clearScriptConversation();
	}
ImGui::SameLine();

ImGui::BeginDisabled(generating.load() || !lastScriptOutputLikelyCutoff || lastScriptOutputTail.empty());
if (ImGui::Button("Continue cutoff", ImVec2(120, 0))) {
	submitScriptRequest(
		ofxGgmlCodeAssistantAction::ContinueCutoff,
		"",
		ofxGgmlInference::buildCutoffContinuationRequest(lastScriptOutputTail),
		"Continue from cutoff.");
}
ImGui::EndDisabled();
if (!scriptSimpleUi) {
	ImGui::SameLine();
}

if (scriptSimpleUi) {
	ImGui::Spacing();
	ImGui::TextDisabled("Primary actions:");
	ImGui::BeginDisabled(generating.load());
	if (ImGui::Button("Explain", ImVec2(90, 0)) && hasUserInput) {
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::Explain,
			scriptInput,
			"",
			"",
			false);
	}
	ImGui::SameLine();
	if (hasWorkspaceSource) {
		if (ImGui::Button("Grounded Edit", ImVec2(120, 0)) && hasUserInput) {
			submitScriptRequest(
				ofxGgmlCodeAssistantAction::Edit,
				scriptInput,
				"",
				"",
				false,
				true,
				true,
				"",
				buildWorkspaceAllowedFiles());
		}
		ImGui::SameLine();
		if (ImGui::Button("Review", ImVec2(90, 0))) {
			if (hasUserInput) {
				submitScriptRequest(
					ofxGgmlCodeAssistantAction::Review,
					scriptInput,
					"",
					"",
					false);
			} else {
				scriptMessages.push_back({
					"user",
					"Review all files in loaded " +
						std::string(sourceType == ofxGgmlScriptSourceType::LocalFolder
							? "folder"
							: "repo") +
						".",
					ofGetElapsedTimef()});
				runHierarchicalReview();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Plan Tests", ImVec2(100, 0))) {
			std::ostringstream body;
			body << "Propose the highest-value tests for this workspace and request. "
				<< "Prefer concrete test names, likely files, and verification commands. "
				<< "If no additional tests are needed, say so clearly.";
			if (hasUserInput) {
				body << "\n\nFocus: " << trim(scriptInput);
			}
			submitScriptRequest(
				ofxGgmlCodeAssistantAction::Generate,
				scriptInput,
				body.str(),
				"Plan tests.",
				false,
				true,
				false,
				"",
				buildWorkspaceAllowedFiles());
		}
	} else {
		if (ImGui::Button("Load workspace", ImVec2(130, 0))) {
			ofFileDialogResult result = ofSystemLoadDialog("Select Script Folder", true);
			if (result.bSuccess) {
				clearDeferredScriptSourceRestore();
				selectedScriptFileIndex = -1;
				if (!scriptLanguages.empty()) {
					scriptSource.setPreferredExtension(
						scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExtension);
				}
				scriptSource.setLocalFolder(result.getPath());
			}
		}
	}
	ImGui::EndDisabled();
	ImGui::TextDisabled(
		"%s",
		hasWorkspaceSource
			? "Keep the main coding chat focused here; open Advanced Script Controls only when you need reviews, semantic tools, or inline completion."
			: "Attach a workspace for grounded edits, file-aware reviews, and semantic symbol lookup.");
}

// Review all files button (enabled when folder/repo is loaded)
if (!scriptSimpleUi &&
	sourceType != ofxGgmlScriptSourceType::None &&
	!scriptSourceFiles.empty()) {
	if (ImGui::Button("Review All Files", ImVec2(120, 0))) {
		scriptMessages.push_back({"user", "Review all files in loaded " + std::string(sourceType == ofxGgmlScriptSourceType::LocalFolder ? "folder" : "repo") + ".", ofGetElapsedTimef()});
		runHierarchicalReview();
	}
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip("Run embedding-powered, multi-pass review over the loaded folder/repository.\nRecommended: use the Script-mode recommended model plus Review Preset.");
	}
	ImGui::SameLine();
	if (ImGui::Button("Review Fix Plan", ImVec2(120, 0))) {
		std::ostringstream body;
		body << "Review the current workspace and return an actionable fix plan. "
			<< "Only include concrete, evidence-backed issues. Prefer a structured plan and unified diff.";
		if (hasUserInput) {
			body << "\n\nFocus: " << trim(scriptInput);
		}
		submitScriptRequest(
			ofxGgmlCodeAssistantAction::Review,
			scriptInput,
			body.str(),
			"Review with fix plan.",
			true,
			true,
			true,
			"",
			buildWorkspaceAllowedFiles());
	}
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip("Review the loaded workspace and return an actionable fix plan with structured edits.");
	}
	ImGui::SameLine();
	if (ImGui::Button("Review Preset", ImVec2(110, 0))) {
		applyScriptReviewPreset();
	}
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip("Switch to the recommended Script model and review-tuned generation settings.");
	}
}

	const bool hasScriptOutput = !scriptOutput.empty();
	const bool hasLastTask = !lastScriptRequest.empty();
	const bool hasBuildErrors = trim(scriptBuildErrors).size() > 0;
	const std::string focusedScriptFileLabel =
		(hasSelectedFile && selectedScriptFileIndex >= 0 &&
		 selectedScriptFileIndex < static_cast<int>(scriptSourceFiles.size()))
			? scriptSourceFiles[static_cast<size_t>(selectedScriptFileIndex)].name
			: std::string();
	const std::string scriptBackendLabel = [&]() {
		if (textInferenceBackend == TextInferenceBackend::LlamaServer) {
			const std::string serverUrl = trim(textServerUrl);
			return serverUrl.empty()
				? std::string("llama-server")
				: std::string("llama-server @ ") + serverUrl;
		}
		const std::string cliPath = trim(llamaCliCommand);
		return cliPath.empty() ? std::string("llama-completion") : cliPath;
	}();
	const std::string scriptSourceLabel = [&]() {
		switch (sourceType) {
		case ofxGgmlScriptSourceType::LocalFolder: return std::string("local workspace");
		case ofxGgmlScriptSourceType::GitHubRepo: return std::string("GitHub repo");
		case ofxGgmlScriptSourceType::Internet: return std::string("internet sources");
		case ofxGgmlScriptSourceType::None:
		default:
			return std::string("none");
		}
	}();

	ImGui::Separator();
	ImGui::TextDisabled(
		"Backend: %s | Source: %s | Focused file: %s",
		scriptBackendLabel.c_str(),
		scriptSourceLabel.c_str(),
		focusedScriptFileLabel.empty() ? "none" : focusedScriptFileLabel.c_str());
	if (!scriptSimpleUi || ImGui::CollapsingHeader("Assistant internals")) {
		ImGui::TextDisabled(
			"Repo context: %s | Recent touched files: %d",
			scriptIncludeRepoContext ? "on" : "off",
			static_cast<int>(recentScriptTouchedFiles.size()));
		if (!lastScriptFailureReason.empty()) {
			ImGui::TextColored(
				ImVec4(0.95f, 0.68f, 0.35f, 1.0f),
				"Last failure: %s",
				lastScriptFailureReason.c_str());
		}
	}

	const auto describeScriptAssistantEventKind =
		[](ofxGgmlCodeAssistantEventKind kind) {
			switch (kind) {
			case ofxGgmlCodeAssistantEventKind::SessionStarted:
				return "Session started";
			case ofxGgmlCodeAssistantEventKind::PromptPrepared:
				return "Prompt prepared";
			case ofxGgmlCodeAssistantEventKind::OutputChunk:
				return "Streaming output";
			case ofxGgmlCodeAssistantEventKind::StructuredResultReady:
				return "Structured result";
			case ofxGgmlCodeAssistantEventKind::ToolProposed:
				return "Tool proposed";
			case ofxGgmlCodeAssistantEventKind::ApprovalRequested:
				return "Approval requested";
			case ofxGgmlCodeAssistantEventKind::ApprovalGranted:
				return "Approval granted";
			case ofxGgmlCodeAssistantEventKind::ApprovalDenied:
				return "Approval denied";
			case ofxGgmlCodeAssistantEventKind::Completed:
				return "Completed";
			case ofxGgmlCodeAssistantEventKind::Error:
				return "Error";
			}
			return "Event";
		};
	int approvalRequestCount = 0;
	int verificationSuggestionCount = 0;
	for (const auto & toolCall : scriptAssistantToolCalls) {
		if (toolCall.requiresApproval) {
			approvalRequestCount++;
		}
		if (toolCall.toolName == "run_verification") {
			verificationSuggestionCount++;
		}
	}
	const bool hasAssistantWorkflowData =
		!scriptAssistantEvents.empty() || !scriptAssistantToolCalls.empty();
	if (hasAssistantWorkflowData &&
		(!scriptSimpleUi || ImGui::CollapsingHeader("Assistant workflow"))) {
		ImGui::Spacing();
		ImGui::TextDisabled(
			"Assistant workflow: rev %llu | tools: %d | approvals: %d | verification: %d",
			static_cast<unsigned long long>(scriptAssistantSession.revision),
			static_cast<int>(scriptAssistantToolCalls.size()),
			approvalRequestCount,
			verificationSuggestionCount);
		if (ImGui::BeginChild("##ScriptAssistantWorkflow", ImVec2(-1, 120), true)) {
			if (!scriptAssistantToolCalls.empty()) {
				ImGui::TextDisabled("Proposed tools:");
				for (const auto & toolCall : scriptAssistantToolCalls) {
					std::string line = toolCall.toolName;
					if (!toolCall.summary.empty()) {
						line += ": " + toolCall.summary;
					}
					if (toolCall.requiresApproval) {
						line += toolCall.approved ? " [approved]" : " [approval denied]";
					}
					if (toolCall.toolName == "run_verification") {
						line += " [verification]";
					}
					ImGui::BulletText("%s", line.c_str());
				}
			}
			if (!scriptAssistantEvents.empty()) {
				if (!scriptAssistantToolCalls.empty()) {
					ImGui::Spacing();
				}
				ImGui::TextDisabled("Recent assistant events:");
				const size_t maxEvents = std::min<size_t>(scriptAssistantEvents.size(), 8);
				for (size_t index = scriptAssistantEvents.size() - maxEvents;
					 index < scriptAssistantEvents.size();
					 ++index) {
					const auto & event = scriptAssistantEvents[index];
					std::string line = describeScriptAssistantEventKind(event.kind);
					if (!event.message.empty()) {
						line += ": " + event.message;
					} else if (!event.toolCall.summary.empty()) {
						line += ": " + event.toolCall.summary;
					} else if (!event.toolCall.toolName.empty()) {
						line += ": " + event.toolCall.toolName;
					}
					ImGui::BulletText("%s", line.c_str());
				}
			}
		}
		ImGui::EndChild();
	}

	ofxGgmlCodeAssistantToolCall pendingApprovalToolCall;
	bool hasPendingApproval = false;
	{
		std::lock_guard<std::mutex> approvalLock(scriptAssistantApprovalMutex);
		hasPendingApproval = scriptAssistantApprovalPending;
		if (hasPendingApproval) {
			pendingApprovalToolCall = scriptAssistantPendingApprovalToolCall;
		}
	}
	const auto submitPendingToolApproval = [&](bool approved) {
		std::string toolName;
		uint64_t requestId = 0;
		bool shouldUpdateStatus = false;
		bool shouldNotify = false;
		{
			std::lock_guard<std::mutex> approvalLock(scriptAssistantApprovalMutex);
			if (!scriptAssistantApprovalPending ||
				scriptAssistantApprovalDecisionReady) {
				// Always notify even on early return to wake waiting threads
				shouldNotify = true;
			} else {
				requestId = scriptAssistantApprovalRequestId;
				toolName = scriptAssistantPendingApprovalToolCall.toolName;
				scriptAssistantApprovalDecisionApproved = approved;
				scriptAssistantApprovalDecisionReady = true;
				scriptAssistantApprovalDecisionRequestId = requestId;
				scriptAssistantApprovalPending = false;
				shouldUpdateStatus = true;
				shouldNotify = true;
			}
		}
		if (shouldUpdateStatus) {
			{
				std::lock_guard<std::mutex> streamLock(streamMutex);
				streamingOutput =
					(approved ? "Approved tool request" : "Denied tool request") +
					(toolName.empty() ? std::string(".") : ": " + toolName + ".");
			}
			generatingStatus = approved
				? "Running approved tool request..."
				: "Skipping denied tool request...";
		}
		if (shouldNotify) {
			scriptAssistantApprovalCv.notify_all();
		}
	};
	if (hasPendingApproval) {
		ImGui::Spacing();
		ImGui::TextColored(
			ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
			"Approval requested: Script assistant is waiting for your decision.");
		if (!pendingApprovalToolCall.toolName.empty()) {
			ImGui::TextWrapped(
				"Tool: %s",
				pendingApprovalToolCall.toolName.c_str());
		}
		if (!pendingApprovalToolCall.summary.empty()) {
			ImGui::TextWrapped(
				"Reason: %s",
				pendingApprovalToolCall.summary.c_str());
		}
		if (!pendingApprovalToolCall.payload.empty()) {
			ImGui::BeginChild("##ScriptApprovalPayload", ImVec2(-1, 90), true);
			ImGui::TextWrapped("%s", pendingApprovalToolCall.payload.c_str());
			ImGui::EndChild();
		}
		if (ImGui::Button("Approve Tool", ImVec2(120, 0))) {
			submitPendingToolApproval(true);
		}
		ImGui::SameLine();
		if (ImGui::Button("Deny Tool", ImVec2(120, 0))) {
			submitPendingToolApproval(false);
		}
	}

	const auto requestFocusedInlineCompletion = [&]() {
		if (!hasSelectedFile) {
			appendScriptAssistantOutput(
				"Inline completion",
				"Select a focused file before requesting editor-style inline completion.");
			return;
		}
		std::string focusedFileContent;
		if (!scriptSource.loadFileContent(selectedScriptFileIndex, focusedFileContent)) {
			appendScriptAssistantOutput(
				"Inline completion",
				"Could not load the focused file for inline completion.");
			return;
		}
		std::string inlineInstruction = trim(scriptInlineInstruction);
		if (inlineInstruction.empty()) {
			inlineInstruction = trim(scriptInput);
		}
		if (inlineInstruction.empty()) {
			inlineInstruction =
				"Continue the next most useful code for this file while preserving style, structure, and current conventions.";
		}
		runScriptInlineCompletionRequest(
			scriptSourceFiles[static_cast<size_t>(selectedScriptFileIndex)].fullPath,
			focusedFileContent,
			"",
			inlineInstruction);
	};

	const bool showAdvancedScriptControls =
		!scriptSimpleUi || ImGui::CollapsingHeader("Advanced Script Controls");
	if (showAdvancedScriptControls) {
		ImGui::Spacing();
		ImGui::TextDisabled("Assistant intents:");
		auto drawIntentButton =
			[&](const char * label,
				const char * tooltip,
				bool enabled,
				const auto & onClick) {
				ImGui::BeginDisabled(!enabled);
				if (ImGui::SmallButton(label) && enabled) {
					onClick();
				}
				if (ImGui::IsItemHovered() && tooltip != nullptr && tooltip[0] != '\0') {
					showWrappedTooltip(tooltip);
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
			};

		if (hasPendingApproval) {
			drawIntentButton(
				"Approve",
				"Allow the currently proposed tool call.",
				true,
				[&]() { submitPendingToolApproval(true); });
			drawIntentButton(
				"Deny",
				"Reject the currently proposed tool call.",
				true,
				[&]() { submitPendingToolApproval(false); });
			ImGui::TextDisabled("Pending tool approval comes first.");
		} else if (!hasWorkspaceSource) {
			drawIntentButton(
				"Load workspace",
				"Pick a local project so Script mode can ground edits and reviews.",
				!generating.load(),
				[&]() {
					ofFileDialogResult result = ofSystemLoadDialog("Select Script Folder", true);
					if (result.bSuccess) {
						clearDeferredScriptSourceRestore();
						selectedScriptFileIndex = -1;
						if (!scriptLanguages.empty()) {
							scriptSource.setPreferredExtension(
								scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExtension);
						}
						scriptSource.setLocalFolder(result.getPath());
					}
				});
			drawIntentButton(
				"Explain prompt",
				"Turn the current Script prompt into an explanation request.",
				!generating.load() && hasUserInput,
				[&]() {
					submitScriptRequest(
						ofxGgmlCodeAssistantAction::Explain,
						scriptInput,
						"",
						"",
						false);
				});
			ImGui::TextDisabled("Load a workspace to unlock grounded plans, symbol search, and dry runs.");
		} else if (hasBuildErrors) {
			drawIntentButton(
				"Fix build",
				"Use compiler output to propose the safest next build fix.",
				!generating.load(),
				[&]() {
					submitScriptRequest(
						ofxGgmlCodeAssistantAction::FixBuild,
						scriptInput,
						"",
						"",
						false,
						true,
						true,
						scriptBuildErrors,
						buildWorkspaceAllowedFiles());
				});
			drawIntentButton(
				"Review fix",
				"Review the workspace and return a grounded fix plan.",
				!generating.load(),
				[&]() {
					std::ostringstream body;
					body << "Review the current workspace and return an actionable fix plan. "
						<< "Only include concrete, evidence-backed issues. Prefer a structured plan and unified diff.";
					if (hasUserInput) {
						body << "\n\nFocus: " << trim(scriptInput);
					}
					submitScriptRequest(
						ofxGgmlCodeAssistantAction::Review,
						scriptInput,
						body.str(),
						"Review with fix plan.",
						true,
						true,
						true,
						"",
						buildWorkspaceAllowedFiles());
				});
			drawIntentButton(
				"Dry run",
				"Preview the latest structured patch plan against the workspace.",
				!generating.load(),
				[&]() { previewWorkspacePlan("Workspace dry run"); });
			ImGui::TextDisabled("Compiler output is loaded, so build repair is the strongest next move.");
		} else if (hasSelectedFile && !hasUserInput) {
			drawIntentButton(
				"Explain file",
				"Seed the Script prompt from the currently focused file.",
				!generating.load(),
				[&]() {
					copyStringToBuffer(
						scriptInput,
						sizeof(scriptInput),
						("Explain how " + focusedScriptFileLabel +
							" works, including key functions, data flow, and risks.").c_str());
				});
			drawIntentButton(
				"Inline continue",
				"Use editor-style continuation against the focused file instead of a chat request.",
				!generating.load(),
				[&]() { requestFocusedInlineCompletion(); });
			drawIntentButton(
				"Symbol context",
				"Inspect semantic symbol context around the focused file or prompt.",
				!generating.load(),
				[&]() {
					ofxGgmlCodeAssistantSymbolQuery query;
					query.query = focusedScriptFileLabel;
					query.includeCallers = true;
					query.maxDefinitions = 6;
					query.maxReferences = 8;
					const auto symbolContext =
						scriptAssistant.buildSymbolContext(query, buildScriptAssistantContext());
					appendScriptAssistantOutput("Inspect symbol context", formatSymbolContext(symbolContext));
				});
			ImGui::TextDisabled("No chat prompt yet, so start from the focused file.");
		} else if (hasWorkspaceSource && hasUserInput) {
			drawIntentButton(
				"Grounded edit",
				"Build a structured edit plan using the loaded workspace.",
				!generating.load(),
				[&]() {
					submitScriptRequest(
						ofxGgmlCodeAssistantAction::Edit,
						scriptInput,
						"",
						"",
						false,
						true,
						true,
						"",
						buildWorkspaceAllowedFiles());
				});
			drawIntentButton(
				"Review",
				"Review the loaded workspace using the current Script prompt as focus.",
				!generating.load(),
				[&]() {
					submitScriptRequest(
						ofxGgmlCodeAssistantAction::Review,
						scriptInput,
						"",
						"",
						false);
				});
			drawIntentButton(
				"Tests",
				"Plan the highest-value verification steps for the current request.",
				!generating.load(),
				[&]() {
					std::ostringstream body;
					body << "Propose the highest-value tests for this workspace and request. "
						<< "Prefer concrete test names, likely files, and verification commands. "
						<< "If no additional tests are needed, say so clearly.";
					if (hasUserInput) {
						body << "\n\nFocus: " << trim(scriptInput);
					}
					submitScriptRequest(
						ofxGgmlCodeAssistantAction::Generate,
						scriptInput,
						body.str(),
						"Plan tests.",
						false,
						true,
						false,
						"",
						buildWorkspaceAllowedFiles());
				});
			ImGui::TextDisabled("You have both a workspace and a request, so use grounded planning instead of freeform chat.");
		} else {
			drawIntentButton(
				"Review workspace",
				"Run the hierarchical workspace review flow.",
				!generating.load(),
				[&]() { runHierarchicalReview(); });
			drawIntentButton(
				"Next edit",
				"Predict the highest-value next change for the workspace.",
				!generating.load(),
				[&]() {
					submitScriptRequest(
						ofxGgmlCodeAssistantAction::NextEdit,
						scriptInput,
						"",
						"",
						false,
						true,
						true,
						"",
						buildWorkspaceAllowedFiles());
				});
			drawIntentButton(
				"Change summary",
				"Summarize local Git changes professionally for reviewers.",
				!generating.load() &&
					sourceType == ofxGgmlScriptSourceType::LocalFolder &&
					!scriptSource.getLocalFolderPath().empty(),
				[&]() { summarizeLocalChanges(scriptInput); });
			ImGui::TextDisabled("No prompt yet. Start with a review or next-edit prediction.");
		}

		ImGui::Spacing();
		ImGui::TextDisabled("Suggested next step:");
		if (!hasWorkspaceSource) {
			if (ImGui::Button("Load local workspace...", ImVec2(170, 0))) {
				ofFileDialogResult result = ofSystemLoadDialog("Select Script Folder", true);
				if (result.bSuccess) {
					clearDeferredScriptSourceRestore();
					selectedScriptFileIndex = -1;
					if (!scriptLanguages.empty()) {
						scriptSource.setPreferredExtension(
							scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExtension);
					}
					scriptSource.setLocalFolder(result.getPath());
				}
			}
			ImGui::SameLine();
			ImGui::TextDisabled("Load a local project to unlock grounded edit plans and dry runs.");
		} else if (hasBuildErrors) {
			if (drawDisabledButton("Fix Build Plan", generating.load(), ImVec2(150, 0))) {
				submitScriptRequest(
					ofxGgmlCodeAssistantAction::FixBuild,
					scriptInput,
					"",
					"",
					false,
					true,
					true,
					scriptBuildErrors,
					buildWorkspaceAllowedFiles());
			}
			ImGui::SameLine();
			ImGui::TextDisabled("You already have compiler output loaded, so start with a grounded build fix.");
		} else if (hasScriptOutput) {
			const auto lastStructuredOutput = ofxGgmlCodeAssistant::parseStructuredResult(scriptOutput);
			if (!lastStructuredOutput.unifiedDiff.empty() || !lastStructuredOutput.patchOperations.empty()) {
				if (drawDisabledButton("Workspace Dry Run", generating.load(), ImVec2(150, 0))) {
					previewWorkspacePlan("Workspace dry run");
				}
				ImGui::SameLine();
				ImGui::TextDisabled("Preview the latest structured edit plan before applying anything manually.");
			} else if (hasSelectedFile && !hasUserInput) {
				if (ImGui::Button("Explain focused file", ImVec2(150, 0))) {
					copyStringToBuffer(
						scriptInput,
						sizeof(scriptInput),
						("Explain how " + focusedScriptFileLabel + " works, including key functions, data flow, and risks.").c_str());
				}
				ImGui::SameLine();
				ImGui::TextDisabled("Seed the input from the selected file instead of starting from a blank prompt.");
			}
		} else if (hasWorkspaceSource && hasUserInput) {
			if (drawDisabledButton("Grounded Edit Plan", generating.load(), ImVec2(150, 0))) {
				submitScriptRequest(
					ofxGgmlCodeAssistantAction::Edit,
					scriptInput,
					"",
					"",
					false,
					true,
					true,
					"",
					buildWorkspaceAllowedFiles());
			}
			ImGui::SameLine();
			ImGui::TextDisabled("You have a workspace and a request loaded, so the strongest next step is a structured edit plan.");
		} else if (hasWorkspaceSource) {
			if (drawDisabledButton("Review workspace", generating.load(), ImVec2(150, 0))) {
				runHierarchicalReview();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("No prompt yet. Start with a workspace review to surface the next useful change.");
		}

		if (hasScriptOutput && sourceType == ofxGgmlScriptSourceType::LocalFolder) {
			ImGui::SameLine();
			if (ImGui::Button("Save Output", ImVec2(120, 0))) {
				std::string filename = buildScriptFilename();
				scriptSource.saveToLocalSource(filename, scriptOutput);
			}
		}

		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::TextDisabled("Editor-style inline completion");
		if (hasSelectedFile) {
			ImGui::TextWrapped(
				"Focused file: %s",
				scriptSourceFiles[static_cast<size_t>(selectedScriptFileIndex)].name.c_str());
		} else {
			ImGui::TextDisabled("Select a file in the workspace list to use inline completion.");
		}
		if (ImGui::SmallButton("Use Script Prompt##Inline")) {
			const std::string fallbackInstruction = trim(std::string(scriptInput));
			if (!fallbackInstruction.empty()) {
				copyStringToBuffer(
					scriptInlineInstruction,
					sizeof(scriptInlineInstruction),
					fallbackInstruction);
			}
		}
		if (ImGui::IsItemHovered()) {
			showWrappedTooltip("Seed the inline-completion instruction from the current Script prompt.");
		}
		ImGui::SameLine();
		ImGui::TextDisabled("If blank, inline completion falls back to the current Script prompt.");
		ImGui::InputText(
			"Inline instruction",
			scriptInlineInstruction,
			sizeof(scriptInlineInstruction));
		ImGui::BeginDisabled(generating.load() || !hasSelectedFile);
		if (ImGui::Button("Inline Complete", ImVec2(130, 0))) {
			requestFocusedInlineCompletion();
		}
		if (ImGui::IsItemHovered()) {
			showWrappedTooltip("Run editor-style continuation against the focused file instead of a chat request.");
		}
		ImGui::EndDisabled();
		if (!scriptInlineCompletionOutput.empty()) {
			ImGui::SameLine();
			if (ImGui::SmallButton("Copy Completion##Inline")) {
				copyToClipboard(scriptInlineCompletionOutput);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Use in Chat##Inline")) {
				copyStringToBuffer(scriptInput, sizeof(scriptInput), scriptInlineCompletionOutput);
			}
			const std::string inlineTargetLabel = trim(scriptInlineCompletionTargetPath);
			if (!inlineTargetLabel.empty()) {
				ImGui::TextDisabled("Target: %s", inlineTargetLabel.c_str());
			}
			if (ImGui::BeginChild("##InlineCompletionPreview", ImVec2(-1, 120), true)) {
				ImGui::TextWrapped("%s", scriptInlineCompletionOutput.c_str());
			}
			ImGui::EndChild();
		}

		ImGui::Spacing();
		ImGui::TextDisabled("Quick Actions:");
		ImGui::TextDisabled("%s", "Tip: /review /reviewfix /nextedit /summary /tests /fix /docs");
		if (ImGui::IsItemHovered()) {
			showWrappedTooltip(buildScriptCommandHelpText());
		}
		ImGui::BeginDisabled(generating.load() || (!hasUserInput && !hasSelectedFile));
		for (size_t i = 0; i < std::size(actionSpecs); i++) {
			const auto & spec = actionSpecs[i];
			if (ImGui::Button(spec.label, spec.size)) {
				if (spec.action == ofxGgmlCodeAssistantAction::NextEdit) {
					submitScriptRequest(
						spec.action,
						scriptInput,
						"",
						"",
						true,
						true,
						true,
						"",
						buildWorkspaceAllowedFiles());
				} else {
					submitScriptRequest(
						spec.action,
						scriptInput,
						"",
						"",
						true);
				}
			}
			if (i + 1 < std::size(actionSpecs)) {
				ImGui::SameLine();
			}
		}
		ImGui::EndDisabled();
		ImGui::BeginDisabled(
			generating.load() ||
			sourceType != ofxGgmlScriptSourceType::LocalFolder ||
			scriptSource.getLocalFolderPath().empty());
		if (ImGui::Button("Change Summary", ImVec2(120, 0))) {
			summarizeLocalChanges(scriptInput);
			std::memset(scriptInput, 0, sizeof(scriptInput));
		}
		if (ImGui::IsItemHovered()) {
			showWrappedTooltip("Summarize local Git changes professionally for reviewers.");
		}
		ImGui::EndDisabled();

		if (ImGui::CollapsingHeader("Semantic & Workspace")) {
			ImGui::Checkbox("Restrict workspace previews to focused file", &restrictWorkspaceToFocusedFile);
			if (ImGui::IsItemHovered()) {
				showWrappedTooltip("When enabled, structured edit previews stay inside the currently selected file.");
			}

			ImGui::BeginDisabled(generating.load() || (!hasUserInput && !hasSelectedFile));
			if (ImGui::Button("Symbol Context", ImVec2(120, 0))) {
				ofxGgmlCodeAssistantSymbolQuery query;
				query.query = hasUserInput ? std::string(scriptInput) : lastScriptRequest;
				query.includeCallers = true;
				query.maxDefinitions = 6;
				query.maxReferences = 8;
				const auto symbolContext =
					scriptAssistant.buildSymbolContext(query, buildScriptAssistantContext());
				appendScriptAssistantOutput("Inspect symbol context", formatSymbolContext(symbolContext));
			}
			ImGui::SameLine();
			if (ImGui::Button("Edit Plan", ImVec2(100, 0))) {
				submitScriptRequest(
					ofxGgmlCodeAssistantAction::Edit,
					scriptInput,
					"",
					"",
					true,
					true,
					true,
					"",
					buildWorkspaceAllowedFiles());
			}
			ImGui::SameLine();
			if (ImGui::Button("Grounded Docs", ImVec2(120, 0))) {
				submitScriptRequest(
					ofxGgmlCodeAssistantAction::GroundedDocs,
					scriptInput,
					"",
					"",
					true);
			}
			ImGui::EndDisabled();

			ImGui::InputTextMultiline(
				"Build errors / compiler output",
				scriptBuildErrors,
				sizeof(scriptBuildErrors),
				ImVec2(-1, 90));
			ImGui::BeginDisabled(generating.load() || std::strlen(scriptBuildErrors) == 0);
			if (ImGui::Button("Fix Build Plan", ImVec2(120, 0))) {
				submitScriptRequest(
					ofxGgmlCodeAssistantAction::FixBuild,
					scriptInput,
					"",
					"",
					false,
					true,
					true,
					scriptBuildErrors,
					buildWorkspaceAllowedFiles());
			}
			ImGui::SameLine();
			if (ImGui::Button("Workspace Dry Run", ImVec2(140, 0))) {
				previewWorkspacePlan("Workspace dry run");
			}
			ImGui::EndDisabled();
		}

		if (ImGui::CollapsingHeader("Tools & Memory")) {
			bool useProjectMemory = scriptProjectMemory.isEnabled();
			if (ImGui::Checkbox("Use project memory", &useProjectMemory)) {
				scriptProjectMemory.setEnabled(useProjectMemory);
			}
			ImGui::SameLine();
			ImGui::Checkbox("Include repo context", &scriptIncludeRepoContext);
			if (ImGui::IsItemHovered()) showWrappedTooltip("Include loaded file list and selected file snippet in script prompts");

			ImGui::BeginDisabled(generating.load() || (!hasScriptOutput && !hasLastTask));
			if (ImGui::Button("Continue Task", ImVec2(120, 0))) {
				submitScriptRequest(ofxGgmlCodeAssistantAction::ContinueTask);
			}
			if (ImGui::IsItemHovered()) showWrappedTooltip("Continue from the latest coding response without rewriting your full prompt.");
			ImGui::SameLine();
			if (ImGui::Button("Shorter", ImVec2(80, 0))) {
				submitScriptRequest(ofxGgmlCodeAssistantAction::Shorter);
			}
			ImGui::SameLine();
			if (ImGui::Button("More Detail", ImVec2(90, 0))) {
				submitScriptRequest(ofxGgmlCodeAssistantAction::MoreDetail);
			}
			ImGui::SameLine();
			if (ImGui::Button("Long Code Preset", ImVec2(120, 0))) {
				maxTokens = std::max(maxTokens, 2048);
				contextSize = std::max(contextSize, 8192);
				temperature = 0.35f;
				topP = 0.92f;
				topK = std::max(topK, 60);
				minP = std::max(minP, 0.05f);
				repeatPenalty = 1.05f;
			}

			if (ImGui::Button("Reuse Last Task", ImVec2(120, 0))) {
				if (hasLastTask) {
					copyStringToBuffer(scriptInput, sizeof(scriptInput), lastScriptRequest);
				}
			}
			ImGui::EndDisabled();

			if (ImGui::SmallButton("Clear Project Memory")) scriptProjectMemory.clear();
			ImGui::SameLine();
			ImGui::TextDisabled("Learned context (%d chars)", static_cast<int>(scriptProjectMemory.getMemoryText().size()));
			ImGui::BeginChild("##ProjectMemory", ImVec2(-1, 80), true);
			ImGui::TextWrapped("%s", scriptProjectMemory.getMemoryText().c_str());
			ImGui::EndChild();
		}
	}
}

// ---------------------------------------------------------------------------
// Script source panel — GitHub repo connection UI
// ---------------------------------------------------------------------------

void ofApp::drawScriptSourcePanel() {
ImGui::Spacing();
const ofxGgmlScriptSourceType sourceType = scriptSource.getSourceType();
const auto workspaceInfo = scriptSource.getWorkspaceInfo();

if (sourceType == ofxGgmlScriptSourceType::LocalFolder) {
	ImGui::TextColored(ImVec4(0.6f, 0.85f, 0.6f, 1.0f), "Local Workspace:");
	ImGui::SameLine();
	if (ImGui::Button("Rescan", ImVec2(70, 0))) {
		scriptSource.rescan();
	}
	ImGui::SameLine();
	if (ImGui::Button("Load VS Workspace", ImVec2(140, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Select Visual Studio .sln or .vcxproj", false);
		if (result.bSuccess) {
			selectedScriptFileIndex = -1;
			scriptSource.setVisualStudioWorkspace(result.getPath());
		}
	}

	const std::string localRoot = scriptSource.getLocalFolderPath();
	if (!localRoot.empty()) {
		ImGui::TextWrapped("Root: %s", localRoot.c_str());
	}
	if (workspaceInfo.hasVisualStudioSolution) {
		ImGui::TextWrapped("Solution: %s", workspaceInfo.visualStudioSolutionPath.c_str());
	}
	if (!workspaceInfo.visualStudioProjectPaths.empty()) {
		ImGui::TextWrapped(
			"Visual Studio projects: %d",
			static_cast<int>(workspaceInfo.visualStudioProjectPaths.size()));
	}
	if (!workspaceInfo.visualStudioProjects.empty()) {
		if (ImGui::BeginCombo(
				"VS Project",
				workspaceInfo.selectedVisualStudioProjectPath.empty()
					? workspaceInfo.visualStudioProjects.front().relativePath.c_str()
					: workspaceInfo.selectedVisualStudioProjectPath.c_str())) {
			for (const auto & project : workspaceInfo.visualStudioProjects) {
				const bool isSelected =
					project.relativePath == workspaceInfo.selectedVisualStudioProjectPath;
				const std::string label = project.name.empty()
					? project.relativePath
					: (project.name + " (" + project.relativePath + ")");
				if (ImGui::Selectable(label.c_str(), isSelected)) {
					scriptSource.configureVisualStudioWorkspace(
						project.relativePath,
						workspaceInfo.selectedVisualStudioConfiguration,
						workspaceInfo.selectedVisualStudioPlatform);
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}
	if (!workspaceInfo.visualStudioConfigurations.empty()) {
		if (ImGui::BeginCombo(
				"Configuration",
				workspaceInfo.selectedVisualStudioConfiguration.c_str())) {
			for (const auto & configuration : workspaceInfo.visualStudioConfigurations) {
				const bool isSelected =
					configuration == workspaceInfo.selectedVisualStudioConfiguration;
				if (ImGui::Selectable(configuration.c_str(), isSelected)) {
					scriptSource.configureVisualStudioWorkspace(
						workspaceInfo.selectedVisualStudioProjectPath,
						configuration,
						workspaceInfo.selectedVisualStudioPlatform);
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}
	if (!workspaceInfo.visualStudioPlatforms.empty()) {
		if (ImGui::BeginCombo(
				"Platform",
				workspaceInfo.selectedVisualStudioPlatform.c_str())) {
			for (const auto & platform : workspaceInfo.visualStudioPlatforms) {
				const bool isSelected =
					platform == workspaceInfo.selectedVisualStudioPlatform;
				if (ImGui::Selectable(platform.c_str(), isSelected)) {
					scriptSource.configureVisualStudioWorkspace(
						workspaceInfo.selectedVisualStudioProjectPath,
						workspaceInfo.selectedVisualStudioConfiguration,
						platform);
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}
	if (workspaceInfo.hasCompilationDatabase) {
		ImGui::TextWrapped("compile_commands.json: %s", workspaceInfo.compilationDatabasePath.c_str());
	}
	if (workspaceInfo.hasCMakeProject) {
		ImGui::TextWrapped("CMakeLists.txt: %s", workspaceInfo.cmakeListsPath.c_str());
	}
	if (workspaceInfo.hasOpenFrameworksProject) {
		ImGui::TextWrapped("addons.make: %s", workspaceInfo.addonsMakePath.c_str());
	}
	if (!workspaceInfo.defaultBuildDirectory.empty()) {
		ImGui::TextWrapped("Preferred build dir: %s", workspaceInfo.defaultBuildDirectory.c_str());
	}
	if (!workspaceInfo.msbuildPath.empty()) {
		ImGui::TextWrapped("MSBuild: %s", workspaceInfo.msbuildPath.c_str());
	}
	ImGui::TextDisabled(
		"Auto-rescan: %s",
		workspaceInfo.localBackgroundMonitoringEnabled ? "active" : "off");

	if (cachedScriptVerificationRoot != localRoot ||
		cachedScriptVerificationGeneration != workspaceInfo.workspaceGeneration) {
		cachedScriptVerificationCommands =
			scriptWorkspaceAssistant.suggestVerificationCommands(
				{},
				localRoot,
				&workspaceInfo);
		cachedScriptVerificationRoot = localRoot;
		cachedScriptVerificationGeneration = workspaceInfo.workspaceGeneration;
	}
	if (!cachedScriptVerificationCommands.empty()) {
		ImGui::Spacing();
		ImGui::TextDisabled("Suggested verification:");
		for (const auto & command : cachedScriptVerificationCommands) {
			std::string line = command.executable;
			for (const auto & arg : command.arguments) {
				line += " " + arg;
			}
			ImGui::BulletText("%s", line.c_str());
		}
	}
} else if (sourceType == ofxGgmlScriptSourceType::GitHubRepo) {
	ImGui::TextColored(ImVec4(0.6f, 0.7f, 1.0f, 1.0f), "GitHub Repository:");
	ImGui::SetNextItemWidth(250);
	ImGui::InputText("##GHRepo", scriptSourceGitHub, sizeof(scriptSourceGitHub));
	ImGui::SameLine();
	ImGui::Text("Branch:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(100);
	ImGui::InputText("##GHBranch", scriptSourceBranch, sizeof(scriptSourceBranch));
	ImGui::SetNextItemWidth(240);
	ImGui::InputText(
		"Token (optional)##GHToken",
		scriptSourceGitHubToken,
		sizeof(scriptSourceGitHubToken),
		ImGuiInputTextFlags_Password);
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		scriptSource.setGitHubAuthToken(scriptSourceGitHubToken);
	}
	ImGui::SameLine();
	if (ImGui::Button("Fetch", ImVec2(60, 0))) {
		if (std::strlen(scriptSourceGitHub) > 0) {
			selectedScriptFileIndex = -1;
			if (!scriptLanguages.empty()) {
				scriptSource.setPreferredExtension(
					scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExtension);
			}
			scriptSource.setGitHubAuthToken(scriptSourceGitHubToken);
			if (scriptSource.setGitHubRepoFromInput(
				scriptSourceGitHub,
				scriptSourceBranch)) {
				scriptSource.fetchGitHubRepo();
			}
		}
	}
	ImGui::TextDisabled("Accepts owner/repo, GitHub repo URLs, /tree/<branch> URLs, and file URLs. Leave branch empty for auto-detect.");
	if (!workspaceInfo.gitHubDefaultBranch.empty()) {
		ImGui::TextWrapped("Default branch: %s", workspaceInfo.gitHubDefaultBranch.c_str());
	}
	if (!workspaceInfo.gitHubResolvedCommitSha.empty()) {
		ImGui::TextWrapped("Pinned commit: %s", workspaceInfo.gitHubResolvedCommitSha.c_str());
	}
	if (!workspaceInfo.gitHubFocusedPath.empty()) {
		ImGui::TextWrapped("Focused path: %s", workspaceInfo.gitHubFocusedPath.c_str());
	}
	if (!workspaceInfo.gitHubDiagnostic.empty()) {
		ImGui::TextColored(
			ImVec4(0.95f, 0.7f, 0.35f, 1.0f),
			"%s",
			workspaceInfo.gitHubDiagnostic.c_str());
	}
} else if (sourceType == ofxGgmlScriptSourceType::Internet) {
	ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.9f, 1.0f), "Internet Sources (URLs):");
	ImGui::SetNextItemWidth(360);
	ImGui::InputText("##InternetUrl", scriptSourceInternetUrl, sizeof(scriptSourceInternetUrl));
	ImGui::SameLine();
	if (ImGui::Button("Add URL", ImVec2(80, 0))) {
		if (std::strlen(scriptSourceInternetUrl) > 0) {
			scriptSource.addInternetUrl(scriptSourceInternetUrl);
			scriptSource.rescan();
			scriptSourceInternetUrl[0] = '\0';
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear URLs", ImVec2(90, 0))) {
		scriptSource.setInternetUrls({});
		selectedScriptFileIndex = -1;
	}
	if (ImGui::IsItemHovered()) {
		showWrappedTooltip("Remove all added URLs.");
	}
	if (selectedScriptFileIndex >= 0) {
		ImGui::SameLine();
		if (ImGui::Button("Remove Selected", ImVec2(130, 0))) {
			if (scriptSource.removeInternetUrl(static_cast<size_t>(selectedScriptFileIndex))) {
				selectedScriptFileIndex = -1;
				scriptSource.rescan();
			}
		}
	}
}

const std::string status = scriptSource.getStatus();
if (!status.empty()) {
	ImGui::SameLine();
	ImGui::TextDisabled("%s", status.c_str());
}
ImGui::Spacing();
}

std::string ofApp::buildScriptFilename() const {
std::string ext = ".txt";
if (!scriptLanguages.empty()) {
ext = scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExtension;
}
auto now = std::chrono::system_clock::now();
auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
now.time_since_epoch()).count();
return "generated_" + ofToString(ms) + ext;
}

// ---------------------------------------------------------------------------
// Summarize panel
// ---------------------------------------------------------------------------

void ofApp::ensureVisionPreviewResources() {
	ensureLocalImagePreview(
		trim(visionImagePath),
		visionPreviewImage,
		visionPreviewImageLoadedPath,
		visionPreviewImageError);

	const std::string videoPath = trim(visionVideoPath);
	if (videoPath != visionPreviewVideoLoadedPath) {
		if (!visionPreviewVideoLoadedPath.empty()) {
			visionPreviewVideo.stop();
			visionPreviewVideo.close();
		}
		visionPreviewVideoLoadedPath.clear();
		visionPreviewVideoError.clear();
		visionPreviewVideoReady = false;
		if (!videoPath.empty()) {
			if (visionPreviewVideo.load(videoPath)) {
				visionPreviewVideo.setLoopState(OF_LOOP_NONE);
				visionPreviewVideo.play();
				visionPreviewVideo.setPaused(true);
				visionPreviewVideo.setPosition(0.0f);
				visionPreviewVideo.update();
				visionPreviewVideoLoadedPath = videoPath;
				visionPreviewVideoReady = visionPreviewVideo.isLoaded();
			} else {
			visionPreviewVideoError = "Unable to load video preview.";
		}
	}
	}

	std::string outputPreviewPath;
	if (!visionSampledVideoFrames.empty()) {
		outputPreviewPath = trim(visionSampledVideoFrames.front().imagePath);
	}
	ensureLocalImagePreview(
		outputPreviewPath,
		visionOutputPreviewImage,
		visionOutputPreviewLoadedPath,
		visionOutputPreviewError);
}

int ofApp::clampSupportedDiffusionImageSize(int value) {
	int bestValue = kSupportedDiffusionImageSizes.front();
	int bestDistance = std::abs(value - bestValue);
	for (int candidate : kSupportedDiffusionImageSizes) {
		const int distance = std::abs(value - candidate);
		if (distance < bestDistance) {
			bestValue = candidate;
			bestDistance = distance;
		}
	}
	return bestValue;
}

ofxGgmlInferenceSettings ofApp::buildCurrentTextInferenceSettings(AiMode mode) const {
	constexpr float kDefaultTemp = 0.7f;
	constexpr float kDefaultTopP = 0.9f;
	constexpr float kDefaultRepeatPenalty = 1.1f;

	ofxGgmlInferenceSettings settings;
	settings.maxTokens = std::clamp(maxTokens, 1, 8192);
	settings.temperature = std::isfinite(temperature)
		? std::clamp(temperature, 0.0f, 2.0f)
		: kDefaultTemp;
	settings.topP = std::isfinite(topP)
		? std::clamp(topP, 0.0f, 1.0f)
		: kDefaultTopP;
	settings.topK = std::clamp(topK, 0, 200);
	settings.minP = std::isfinite(minP)
		? std::clamp(minP, 0.0f, 1.0f)
		: 0.0f;
	settings.repeatPenalty = std::isfinite(repeatPenalty)
		? std::clamp(repeatPenalty, 1.0f, 2.0f)
		: kDefaultRepeatPenalty;
	settings.contextSize = std::clamp(contextSize, 256, 16384);
	settings.batchSize = std::clamp(batchSize, 32, 4096);
	settings.threads = std::clamp(numThreads, 1, 128);
	settings.gpuLayers = std::clamp(gpuLayers, 0, detectedModelLayers > 0 ? detectedModelLayers : 128);
	settings.seed = seed;
	settings.simpleIo = true;
	settings.singleTurn = true;
	settings.autoProbeCliCapabilities = true;
	settings.trimPromptToContext = true;
	settings.allowBatchFallback = true;
	settings.autoContinueCutoff = (mode == AiMode::Script) && autoContinueCutoff;
	settings.stopAtNaturalBoundary = stopAtNaturalBoundary;
	const std::string modelPath = getSelectedModelPath();
	settings.autoPromptCache = usePromptCache;
	settings.promptCachePath = usePromptCache ? promptCachePathFor(modelPath, mode) : std::string();
	settings.mirostat = mirostatMode;
	settings.mirostatTau = mirostatTau;
	settings.mirostatEta = mirostatEta;
	settings.useServerBackend = (textInferenceBackend == TextInferenceBackend::LlamaServer);
	if (settings.useServerBackend) {
		settings.serverUrl = effectiveTextServerUrl(textServerUrl);
		settings.serverModel = trim(textServerModel);
	} else if (!backendNames.empty() &&
		selectedBackendIndex >= 0 &&
		selectedBackendIndex < static_cast<int>(backendNames.size())) {
		const std::string & selected = backendNames[static_cast<size_t>(selectedBackendIndex)];
		if (selected != "CPU") {
			settings.device = selected;
		}
	}
	if (!settings.useServerBackend && settings.device.empty()) {
		const std::string backend = ggml.getBackendName();
		if (!backend.empty() && backend != "CPU" && backend != "none") {
			settings.device = backend;
		}
	}
	return settings;
}

void ofApp::ensureLocalImagePreview(
	const std::string & imagePath,
	ofImage & previewImage,
	std::string & loadedPath,
	std::string & errorMessage) {
	if (imagePath != loadedPath) {
		previewImage.clear();
		loadedPath.clear();
		errorMessage.clear();
		if (!imagePath.empty()) {
			if (previewImage.load(imagePath)) {
				loadedPath = imagePath;
			} else {
				loadedPath = imagePath;
				errorMessage = "Unable to load image preview.";
			}
		}
	}
}

void ofApp::drawMediaTexturePreview(const ofBaseHasTexture & previewTexture, const char * childId) {
	const float availWidth = std::max(160.0f, ImGui::GetContentRegionAvail().x);
	const float maxWidth = std::min(availWidth, 420.0f);
	const float texWidth = std::max(1.0f, static_cast<float>(previewTexture.getTexture().getWidth()));
	const float texHeight = std::max(1.0f, static_cast<float>(previewTexture.getTexture().getHeight()));
	const float scale = std::min(maxWidth / texWidth, 240.0f / texHeight);
	const ImVec2 drawSize(
		std::max(1.0f, texWidth * scale),
		std::max(1.0f, texHeight * scale));

	ImGui::BeginChild(childId, ImVec2(0, drawSize.y + 12.0f), true);
	ofxImGui::AddImage(previewTexture, glm::vec2(drawSize.x, drawSize.y));
	ImGui::EndChild();
}

void ofApp::drawLocalImagePreview(
	const char * label,
	const std::string & imagePath,
	ofImage & previewImage,
	const std::string & errorMessage,
	const char * childId) {
	if (imagePath.empty()) {
		return;
	}
	if (!errorMessage.empty()) {
		ImGui::TextDisabled("%s", errorMessage.c_str());
		return;
	}
	if (!previewImage.isAllocated() || !previewImage.getTexture().isAllocated()) {
		ImGui::TextDisabled("Image preview will appear here.");
		return;
	}
	ImGui::TextDisabled(
		"%s: %d x %d",
		label,
		previewImage.getWidth(),
		previewImage.getHeight());
	drawMediaTexturePreview(previewImage, childId);
}

void ofApp::drawVisionImagePreview(const std::string & imagePath) {
	drawLocalImagePreview(
		"Image preview",
		imagePath,
		visionPreviewImage,
		visionPreviewImageError,
		"##VisionImagePreview");
}

int ofApp::findActiveMontageSourceCueIndex() const {
	if (!montageSubtitlePlaybackEnabled ||
		montageSourceSubtitleTrack.cues.empty() ||
		!visionPreviewVideoReady ||
		!visionPreviewVideo.isLoaded()) {
		return -1;
	}

	const double durationSeconds = std::max(0.0, static_cast<double>(visionPreviewVideo.getDuration()));
	const double currentSeconds = std::max(
		0.0,
		std::min(
			durationSeconds,
			static_cast<double>(visionPreviewVideo.getPosition()) * durationSeconds));
	for (size_t i = 0; i < montageSourceSubtitleTrack.cues.size(); ++i) {
		const auto & cue = montageSourceSubtitleTrack.cues[i];
		if (currentSeconds >= cue.startSeconds && currentSeconds <= cue.endSeconds) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

ofxGgmlMontagePreviewTimingMode ofApp::getSelectedMontagePreviewTimingMode() const {
	return montagePreviewTimingModeIndex == 1
		? ofxGgmlMontagePreviewTimingMode::Montage
		: ofxGgmlMontagePreviewTimingMode::Source;
}

const ofxGgmlMontagePreviewTrack * ofApp::getSelectedMontagePreviewTrack() const {
	if (montagePreviewBundle.montageTrack.cues.empty() &&
		montagePreviewBundle.sourceTrack.cues.empty()) {
		return nullptr;
	}
	return &ofxGgmlMontagePreviewBridge::selectTrack(
		montagePreviewBundle,
		getSelectedMontagePreviewTimingMode());
}

double ofApp::getSelectedMontagePreviewTimeSeconds() const {
	if (getSelectedMontagePreviewTimingMode() == ofxGgmlMontagePreviewTimingMode::Source) {
		if (!visionPreviewVideoReady || !visionPreviewVideo.isLoaded()) {
			return 0.0;
		}
		const double durationSeconds = std::max(0.0, static_cast<double>(visionPreviewVideo.getDuration()));
		return std::max(
			0.0,
			std::min(
				durationSeconds,
				static_cast<double>(visionPreviewVideo.getPosition()) * durationSeconds));
	}
	return std::max(0.0, montagePreviewTimelineSeconds);
}

int ofApp::findActiveMontagePreviewCueIndex() const {
	const ofxGgmlMontagePreviewTrack * track = getSelectedMontagePreviewTrack();
	if (!montageSubtitlePlaybackEnabled || track == nullptr) {
		return -1;
	}
	if (getSelectedMontagePreviewTimingMode() == ofxGgmlMontagePreviewTimingMode::Source &&
		(!visionPreviewVideoReady || !visionPreviewVideo.isLoaded())) {
		return -1;
	}
	return ofxGgmlMontagePreviewBridge::findCueAtTime(
		*track,
		getSelectedMontagePreviewTimeSeconds());
}

double ofApp::getVideoEssaySectionStartSeconds(int sectionIndex) const {
	if (sectionIndex < 0) {
		return 0.0;
	}
	for (const auto & cue : videoEssayVoiceCues) {
		if (cue.sectionIndex == sectionIndex) {
			return std::max(0.0, cue.startSeconds);
		}
	}
	return 0.0;
}

std::string ofApp::exportVideoEssaySubtitleTrack(std::string * errorOut) const {
	const std::string srtText = trim(videoEssaySrtText);
	if (srtText.empty()) {
		if (errorOut) {
			*errorOut = "No video essay SRT is available yet.";
		}
		return {};
	}

	const std::filesystem::path exportDir =
		std::filesystem::path(ofToDataPath("cache/video_essay_preview", true));
	std::error_code ec;
	std::filesystem::create_directories(exportDir, ec);
	if (ec) {
		if (errorOut) {
			*errorOut = "Failed to create the video essay preview cache directory.";
		}
		return {};
	}

	const std::string safeTopic =
		sanitizeFilenameStem(trim(videoEssayTopic).empty() ? "video_essay" : trim(videoEssayTopic), "video_essay");
	const std::filesystem::path outputPath =
		exportDir / (safeTopic + "_essay_preview.srt");
	std::ofstream outputFile(outputPath, std::ios::binary | std::ios::trunc);
	if (!outputFile.is_open()) {
		if (errorOut) {
			*errorOut = "Failed to export the video essay subtitle preview.";
		}
		return {};
	}
	outputFile.write(srtText.data(), static_cast<std::streamsize>(srtText.size()));
	outputFile.close();
	return outputPath.string();
}

std::string ofApp::exportSelectedMontagePreviewTrack(
	ofxGgmlMontagePreviewTextFormat format,
	std::string * errorOut) const {
	const ofxGgmlMontagePreviewTrack * track = getSelectedMontagePreviewTrack();
	if (track == nullptr) {
		if (errorOut) {
			*errorOut = "No montage preview track is available yet.";
		}
		return {};
	}
	const std::filesystem::path exportDir =
		std::filesystem::path(ofToDataPath("cache/montage_preview", true));
	const std::filesystem::path outputPath =
		exportDir / ofxGgmlMontagePreviewBridge::suggestSubtitleFileName(*track, format);
	std::string error;
	if (!ofxGgmlMontagePreviewBridge::exportTrack(*track, outputPath.string(), format, &error)) {
		if (errorOut) {
			*errorOut = error;
		}
		return {};
	}
	return outputPath.string();
}

std::string ofApp::exportMontageClipPlaylistManifest(std::string * errorOut) const {
	const std::vector<std::string> clipPaths = extractPathList(montageClipPaths);
	if (clipPaths.empty()) {
		if (errorOut) {
			*errorOut = "Add at least one clip path before exporting a montage playlist manifest.";
		}
		return {};
	}

	ofJson manifest = ofJson::object();
	manifest["title"] = trim(montageEdlTitle).empty() ? "MONTAGE" : trim(montageEdlTitle);
	manifest["goal"] = trim(montageGoal);
	manifest["timingMode"] =
		getSelectedMontagePreviewTimingMode() == ofxGgmlMontagePreviewTimingMode::Source
			? "source"
			: "montage";
	manifest["audioPath"] = trim(montageRenderAudioPath);
	manifest["clips"] = ofJson::array();

	for (size_t i = 0; i < clipPaths.size(); ++i) {
		const std::string normalizedPath = trim(clipPaths[i]);
		std::error_code ec;
		const bool exists = !normalizedPath.empty() &&
			std::filesystem::exists(std::filesystem::path(normalizedPath), ec) &&
			!ec;
		ofJson clipJson = {
			{"index", static_cast<int>(i)},
			{"path", normalizedPath},
			{"exists", exists}
		};
		manifest["clips"].push_back(clipJson);
	}

	const std::filesystem::path exportDir =
		std::filesystem::path(ofToDataPath("cache/montage_export", true));
	std::error_code dirEc;
	std::filesystem::create_directories(exportDir, dirEc);
	if (dirEc) {
		if (errorOut) {
			*errorOut = "Failed to create the montage export cache directory.";
		}
		return {};
	}

	const std::string safeTitle =
		sanitizeFilenameStem(trim(montageEdlTitle).empty() ? "montage" : trim(montageEdlTitle), "montage");
	const std::filesystem::path outputPath = exportDir / (safeTitle + "_playlist.json");
	std::ofstream outputFile(outputPath, std::ios::binary | std::ios::trunc);
	if (!outputFile.is_open()) {
		if (errorOut) {
			*errorOut = "Failed to write the montage playlist manifest.";
		}
		return {};
	}
	outputFile << manifest.dump(2);
	outputFile.close();
	return outputPath.string();
}

std::vector<std::string> ofApp::collectGeneratedMontageClipPaths(std::string * statusOut) const {
	return collectGeneratedMontageClipPathsImpl(
		videoEssayLastRenderedVideoPath,
		montageClipRenderOutputPath,
		statusOut);
}

bool ofApp::populateMontageClipPlaylistFromGeneratedOutputs(std::string * statusOut) {
	return populateMontageClipPlaylistBufferFromGeneratedOutputs(
		montageClipPaths,
		sizeof(montageClipPaths),
		videoEssayLastRenderedVideoPath,
		montageClipRenderOutputPath,
		statusOut);
}

#if OFXGGML_HAS_OFXVLC4
bool ofApp::ensureAceStepVlcPreviewInitialized(std::string * errorOut) {
	if (aceStepVlcInitialized) {
		return true;
	}

	try {
		aceStepVlcPlayer.init(0, nullptr);
		aceStepVlcPlayer.setVolume(100);
		aceStepVlcInitialized = true;
		aceStepVlcError.clear();
		return true;
	} catch (const std::exception & e) {
		aceStepVlcError =
			std::string("Failed to initialize ofxVlc4 AceStep audio preview: ") + e.what();
		if (errorOut != nullptr) {
			*errorOut = aceStepVlcError;
		}
		return false;
	}
}

bool ofApp::loadAceStepVlcPreview(int trackIndex, std::string * errorOut) {
	if (!ensureAceStepVlcPreviewInitialized(errorOut)) {
		return false;
	}

	if (aceStepGeneratedTracks.empty()) {
		aceStepVlcError = "Generate music first before loading the ofxVlc4 audio preview.";
		if (errorOut != nullptr) {
			*errorOut = aceStepVlcError;
		}
		return false;
	}

	trackIndex = std::clamp(
		trackIndex,
		0,
		std::max(0, static_cast<int>(aceStepGeneratedTracks.size()) - 1));
	const std::string audioPath =
		trim(aceStepGeneratedTracks[static_cast<size_t>(trackIndex)].path);
	if (audioPath.empty()) {
		aceStepVlcError = "The selected generated track does not have a valid audio path.";
		if (errorOut != nullptr) {
			*errorOut = aceStepVlcError;
		}
		return false;
	}

	const bool reloadAudio = aceStepVlcLoadedAudioPath != audioPath;
	if (reloadAudio) {
		aceStepVlcPlayer.stop();
		aceStepVlcPlayer.clearMediaSlaves();
		aceStepVlcPlayer.clearPlaylist();
		if (aceStepVlcPlayer.addPathToPlaylist(audioPath) <= 0) {
			aceStepVlcError = "ofxVlc4 could not load the selected generated music track.";
			if (errorOut != nullptr) {
				*errorOut = aceStepVlcError;
			}
			return false;
		}
		aceStepVlcPlayer.playIndex(0);
		aceStepVlcLoadedAudioPath = audioPath;
		aceStepSelectedTrackIndex = trackIndex;
	}

	aceStepVlcError.clear();
	if (errorOut != nullptr) {
		errorOut->clear();
	}
	return true;
}

void ofApp::closeAceStepVlcPreview() {
	if (!aceStepVlcInitialized) {
		return;
	}

	aceStepVlcPlayer.close();
	aceStepVlcInitialized = false;
	aceStepVlcLoadedAudioPath.clear();
}

void ofApp::drawAceStepVlcPreview() {
	if (!aceStepVlcInitialized) {
		return;
	}

	if (!aceStepVlcError.empty()) {
		ImGui::TextDisabled("%s", aceStepVlcError.c_str());
	}

	const float durationSeconds = std::max(0.0f, aceStepVlcPlayer.getLength() / 1000.0f);
	float previewPosition = std::clamp(aceStepVlcPlayer.getPosition(), 0.0f, 1.0f);
	const float currentSeconds = previewPosition * durationSeconds;
	if (ImGui::Button(
			aceStepVlcPlayer.isPlaying() ? "Pause audio preview" : "Play audio preview",
			ImVec2(170, 0))) {
		aceStepVlcPlayer.togglePlayPause();
	}
	ImGui::SameLine();
	if (ImGui::Button("Restart audio preview", ImVec2(170, 0))) {
		aceStepVlcPlayer.play();
		aceStepVlcPlayer.setPosition(0.0f);
	}
	ImGui::SameLine();
	if (ImGui::Button("Stop audio preview", ImVec2(150, 0))) {
		aceStepVlcPlayer.stop();
		aceStepVlcPlayer.setPosition(0.0f);
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%.2fs / %.2fs", currentSeconds, durationSeconds);

	ImGui::SetNextItemWidth(-1);
	if (ImGui::SliderFloat("Audio preview position##AceStep", &previewPosition, 0.0f, 1.0f, "%.3f")) {
		aceStepVlcPlayer.setPosition(previewPosition);
	}
	if (!aceStepVlcLoadedAudioPath.empty()) {
		ImGui::TextDisabled("%s", aceStepVlcLoadedAudioPath.c_str());
	}
}

bool ofApp::ensureVideoEssayVlcPreviewInitialized(std::string * errorOut) {
	if (videoEssayVlcPreviewInitialized) {
		return true;
	}

	try {
		videoEssayVlcPreviewPlayer.init(0, nullptr);
		videoEssayVlcPreviewPlayer.setVolume(0);
		videoEssayVlcPreviewPlayer.setSubtitleDelayMs(0);
		videoEssayVlcPreviewPlayer.setSubtitleTextScale(1.0f);
		videoEssayVlcPreviewInitialized = true;
		videoEssayVlcPreviewError.clear();
		return true;
	} catch (const std::exception & e) {
		videoEssayVlcPreviewError = std::string("Failed to initialize ofxVlc4 video essay preview: ") + e.what();
		if (errorOut) {
			*errorOut = videoEssayVlcPreviewError;
		}
		return false;
	}
}

bool ofApp::loadVideoEssayVlcPreview(std::string * errorOut) {
	if (!ensureVideoEssayVlcPreviewInitialized(errorOut)) {
		return false;
	}

	const std::string videoPath = trim(videoEssaySourceVideoPath);
	if (videoPath.empty()) {
		videoEssayVlcPreviewError = "Select a source video before loading the video essay preview.";
		if (errorOut) {
			*errorOut = videoEssayVlcPreviewError;
		}
		return false;
	}

	std::string subtitleError;
	const std::string subtitlePath = exportVideoEssaySubtitleTrack(&subtitleError);
	if (subtitlePath.empty()) {
		videoEssayVlcPreviewError =
			subtitleError.empty() ? std::string("Failed to prepare the video essay SRT for ofxVlc4.")
								  : subtitleError;
		if (errorOut) {
			*errorOut = videoEssayVlcPreviewError;
		}
		return false;
	}

	videoEssayVlcPreviewSubtitlePath = subtitlePath;

	const bool reloadVideo = videoEssayVlcPreviewLoadedVideoPath != videoPath;
	if (reloadVideo) {
		videoEssayVlcPreviewPlayer.stop();
		videoEssayVlcPreviewPlayer.clearMediaSlaves();
		videoEssayVlcPreviewPlayer.clearPlaylist();
		if (videoEssayVlcPreviewPlayer.addPathToPlaylist(videoPath) <= 0) {
			videoEssayVlcPreviewError = "ofxVlc4 could not load the selected video essay source video.";
			if (errorOut) {
				*errorOut = videoEssayVlcPreviewError;
			}
			return false;
		}
		videoEssayVlcPreviewPlayer.playIndex(0);
		videoEssayVlcPreviewPlayer.setVolume(0);
		videoEssayVlcPreviewLoadedVideoPath = videoPath;
		videoEssayVlcPreviewLoadedSubtitlePath.clear();
	}

	if (reloadVideo || videoEssayVlcPreviewLoadedSubtitlePath != subtitlePath) {
		videoEssayVlcPreviewPlayer.clearMediaSlaves();
		if (!videoEssayVlcPreviewPlayer.addSubtitleSlave(subtitlePath)) {
			videoEssayVlcPreviewError = "ofxVlc4 could not attach the video essay subtitle slave.";
			if (errorOut) {
				*errorOut = videoEssayVlcPreviewError;
			}
			return false;
		}
		const auto subtitleTracks = videoEssayVlcPreviewPlayer.getSubtitleTracks();
		if (!subtitleTracks.empty()) {
			videoEssayVlcPreviewPlayer.selectSubtitleTrackById(subtitleTracks.back().id);
		}
		videoEssayVlcPreviewLoadedSubtitlePath = subtitlePath;
	}

	videoEssayVlcPreviewError.clear();
	if (errorOut) {
		errorOut->clear();
	}
	return true;
}

void ofApp::closeVideoEssayVlcPreview() {
	if (!videoEssayVlcPreviewInitialized) {
		return;
	}
	videoEssayVlcPreviewPlayer.close();
	videoEssayVlcPreviewInitialized = false;
	videoEssayVlcPreviewLoadedVideoPath.clear();
	videoEssayVlcPreviewLoadedSubtitlePath.clear();
}

bool ofApp::startVideoEssayVlcRecording(std::string * errorOut) {
	if (!loadVideoEssayVlcPreview(errorOut)) {
		return false;
	}
	if (videoEssayVlcPreviewPlayer.isVideoRecording()) {
		if (errorOut) {
			*errorOut = "Video essay preview recording is already active.";
		}
		return false;
	}

	const ofTexture & previewTexture = videoEssayVlcPreviewPlayer.getTexture();
	if (!previewTexture.isAllocated()) {
		videoEssayVlcPreviewError = "Wait for the video essay preview to produce a frame before recording.";
		if (errorOut) {
			*errorOut = videoEssayVlcPreviewError;
		}
		return false;
	}

	const std::filesystem::path outputDir =
		std::filesystem::path(ofToDataPath("generated/video_essay", true));
	std::error_code ec;
	std::filesystem::create_directories(outputDir, ec);
	if (ec) {
		videoEssayVlcPreviewError = "Failed to create the video essay render directory.";
		if (errorOut) {
			*errorOut = videoEssayVlcPreviewError;
		}
		return false;
	}

	const std::string safeTopic =
		sanitizeFilenameStem(trim(videoEssayTopic).empty() ? "video_essay" : trim(videoEssayTopic), "video_essay");
	const std::string recordingBasePath =
		(outputDir / (safeTopic + "_" + ofGetTimestampString("%Y%m%d-%H%M%S"))).string();
	if (!videoEssayVlcPreviewPlayer.startTextureRecordingSession(recordingBasePath, previewTexture)) {
		videoEssayVlcPreviewError = "ofxVlc4 could not start texture recording for the video essay preview.";
		if (errorOut) {
			*errorOut = videoEssayVlcPreviewError;
		}
		return false;
	}

	videoEssayLastRenderedVideoPath.clear();
	videoEssayVlcPreviewStatusMessage = "Recording the video essay VLC preview...";
	if (errorOut) {
		errorOut->clear();
	}
	return true;
}

bool ofApp::stopVideoEssayVlcRecording(std::string * errorOut) {
	if (!videoEssayVlcPreviewInitialized || !videoEssayVlcPreviewPlayer.isVideoRecording()) {
		if (errorOut) {
			*errorOut = "Video essay preview recording is not active.";
		}
		return false;
	}

	if (!videoEssayVlcPreviewPlayer.stopRecordingSession()) {
		videoEssayVlcPreviewError = "ofxVlc4 could not stop the video essay recording session cleanly.";
		if (errorOut) {
			*errorOut = videoEssayVlcPreviewError;
		}
		return false;
	}

	const std::string recordedVideoPath = trim(videoEssayVlcPreviewPlayer.getLastVideoRecordingPath());
	if (recordedVideoPath.empty()) {
		videoEssayVlcPreviewError = "The video essay preview recording did not produce a video file.";
		if (errorOut) {
			*errorOut = videoEssayVlcPreviewError;
		}
		return false;
	}

	const std::string narrationAudioPath =
		videoEssayTtsPreview.audioFiles.empty()
			? std::string()
			: trim(videoEssayTtsPreview.audioFiles[static_cast<size_t>(
				std::clamp(
					videoEssayTtsPreview.selectedAudioIndex,
					0,
					std::max(0, static_cast<int>(videoEssayTtsPreview.audioFiles.size()) - 1)))].path);

	if (!narrationAudioPath.empty() && std::filesystem::exists(narrationAudioPath)) {
		const std::filesystem::path outputDir =
			std::filesystem::path(ofToDataPath("generated/video_essay", true));
		const std::string safeTopic =
			sanitizeFilenameStem(trim(videoEssayTopic).empty() ? "video_essay" : trim(videoEssayTopic), "video_essay");
		const std::filesystem::path outputPath =
			outputDir / (safeTopic + "_render_" + ofGetTimestampString("%Y%m%d-%H%M%S") + ".mp4");
		std::string muxError;
		if (!ofxVlc4::muxRecordingFilesToMp4(
				recordedVideoPath,
				narrationAudioPath,
				outputPath.string(),
				30000,
				&muxError)) {
			videoEssayVlcPreviewError =
				muxError.empty()
					? std::string("Failed to mux the video essay preview with the narration audio.")
					: muxError;
			if (errorOut) {
				*errorOut = videoEssayVlcPreviewError;
			}
			return false;
		}
		videoEssayLastRenderedVideoPath = outputPath.string();
		videoEssayVlcPreviewStatusMessage = "Rendered narrated video essay preview: " + outputPath.string();
	} else {
		videoEssayLastRenderedVideoPath = recordedVideoPath;
		videoEssayVlcPreviewStatusMessage =
			"Recorded video essay preview without narration mux: " + recordedVideoPath;
	}

	videoEssayVlcPreviewError.clear();
	if (errorOut) {
		errorOut->clear();
	}
	return true;
}

void ofApp::drawVideoEssayVlcPreview() {
	if (!videoEssayVlcPreviewInitialized) {
		return;
	}

	if (!videoEssayVlcPreviewError.empty()) {
		ImGui::TextDisabled("%s", videoEssayVlcPreviewError.c_str());
	}

	const ofTexture & previewTexture = videoEssayVlcPreviewPlayer.getTexture();
	if (previewTexture.isAllocated()) {
		const float availWidth = std::max(160.0f, ImGui::GetContentRegionAvail().x);
		const float maxWidth = std::min(availWidth, 420.0f);
		const float texWidth = std::max(1.0f, static_cast<float>(previewTexture.getWidth()));
		const float texHeight = std::max(1.0f, static_cast<float>(previewTexture.getHeight()));
		const float scale = std::min(maxWidth / texWidth, 240.0f / texHeight);
		const glm::vec2 drawSize(
			std::max(1.0f, texWidth * scale),
			std::max(1.0f, texHeight * scale));
		ImGui::BeginChild("##VideoEssayVlcPreview", ImVec2(0, drawSize.y + 12.0f), true);
		ofxImGui::AddImage(previewTexture, drawSize);
		ImGui::EndChild();
	} else {
		ImGui::TextDisabled("ofxVlc4 preview will appear here after the video essay media attaches.");
	}

	const float durationSeconds = std::max(0.0f, videoEssayVlcPreviewPlayer.getLength() / 1000.0f);
	float previewPosition = std::clamp(videoEssayVlcPreviewPlayer.getPosition(), 0.0f, 1.0f);
	const float currentSeconds = previewPosition * durationSeconds;
	if (ImGui::Button(
			videoEssayVlcPreviewPlayer.isPlaying() ? "Pause VLC preview##VideoEssay" : "Play VLC preview##VideoEssay",
			ImVec2(170, 0))) {
		videoEssayVlcPreviewPlayer.togglePlayPause();
	}
	ImGui::SameLine();
	if (ImGui::Button("Restart VLC preview##VideoEssay", ImVec2(170, 0))) {
		videoEssayVlcPreviewPlayer.play();
		videoEssayVlcPreviewPlayer.setPosition(0.0f);
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%.2fs / %.2fs", currentSeconds, durationSeconds);

	ImGui::SetNextItemWidth(-1);
	if (ImGui::SliderFloat("VLC preview position##VideoEssay", &previewPosition, 0.0f, 1.0f, "%.3f")) {
		videoEssayVlcPreviewPlayer.setPosition(previewPosition);
	}

	int subtitleDelayMs = videoEssayVlcPreviewPlayer.getSubtitleDelayMs();
	ImGui::SetNextItemWidth(180);
	if (ImGui::SliderInt("VLC subtitle delay##VideoEssay", &subtitleDelayMs, -5000, 5000, "%d ms")) {
		videoEssayVlcPreviewPlayer.setSubtitleDelayMs(subtitleDelayMs);
	}
	ImGui::SameLine();
	float subtitleTextScale = videoEssayVlcPreviewPlayer.getSubtitleTextScale();
	ImGui::SetNextItemWidth(180);
	if (ImGui::SliderFloat("VLC subtitle scale##VideoEssay", &subtitleTextScale, 0.5f, 3.0f, "%.2fx")) {
		videoEssayVlcPreviewPlayer.setSubtitleTextScale(subtitleTextScale);
	}

	const auto subtitleState = videoEssayVlcPreviewPlayer.getSubtitleStateInfo();
	if (subtitleState.trackSelected && !subtitleState.selectedTrackLabel.empty()) {
		ImGui::TextDisabled("Active VLC subtitle track: %s", subtitleState.selectedTrackLabel.c_str());
	} else {
		ImGui::TextDisabled("No VLC subtitle track selected yet.");
	}
	if (!videoEssayVlcPreviewLoadedSubtitlePath.empty()) {
		ImGui::TextDisabled("%s", videoEssayVlcPreviewLoadedSubtitlePath.c_str());
	}
}

bool ofApp::ensureMontageVlcPreviewInitialized(std::string * errorOut) {
	if (montageVlcPreviewInitialized) {
		return true;
	}

	try {
		montageVlcPreviewPlayer.init(0, nullptr);
		montageVlcPreviewPlayer.setVolume(0);
		montageVlcPreviewPlayer.setSubtitleDelayMs(0);
		montageVlcPreviewPlayer.setSubtitleTextScale(1.0f);
		montageVlcPreviewInitialized = true;
		montageVlcPreviewError.clear();
		return true;
	} catch (const std::exception & e) {
		montageVlcPreviewError = std::string("Failed to initialize ofxVlc4 preview: ") + e.what();
		if (errorOut) {
			*errorOut = montageVlcPreviewError;
		}
		return false;
	}
}

bool ofApp::loadMontageVlcPreview(std::string * errorOut) {
	if (!ensureMontageVlcPreviewInitialized(errorOut)) {
		return false;
	}

	const std::string videoPath = trim(visionVideoPath);
	if (videoPath.empty()) {
		montageVlcPreviewError = "Select a source video before loading the ofxVlc4 preview.";
		if (errorOut) {
			*errorOut = montageVlcPreviewError;
		}
		return false;
	}

	std::string subtitleError;
	const std::string subtitlePath =
		exportSelectedMontagePreviewTrack(ofxGgmlMontagePreviewTextFormat::Srt, &subtitleError);
	if (subtitlePath.empty()) {
		montageVlcPreviewError =
			subtitleError.empty() ? std::string("Failed to prepare the active subtitle track for ofxVlc4.")
								  : subtitleError;
		if (errorOut) {
			*errorOut = montageVlcPreviewError;
		}
		return false;
	}

	montagePreviewSubtitleSlavePath = subtitlePath;

	const bool reloadVideo = montageVlcPreviewLoadedVideoPath != videoPath;
	if (reloadVideo) {
		montageVlcPreviewPlayer.stop();
		montageVlcPreviewPlayer.clearMediaSlaves();
		montageVlcPreviewPlayer.clearPlaylist();
		if (montageVlcPreviewPlayer.addPathToPlaylist(videoPath) <= 0) {
			montageVlcPreviewError = "ofxVlc4 could not load the selected source video.";
			if (errorOut) {
				*errorOut = montageVlcPreviewError;
			}
			return false;
		}
		montageVlcPreviewPlayer.playIndex(0);
		montageVlcPreviewPlayer.setVolume(0);
		montageVlcPreviewLoadedVideoPath = videoPath;
		montageVlcPreviewLoadedSubtitlePath.clear();
	}

	if (reloadVideo || montageVlcPreviewLoadedSubtitlePath != subtitlePath) {
		montageVlcPreviewPlayer.clearMediaSlaves();
		if (!montageVlcPreviewPlayer.addSubtitleSlave(subtitlePath)) {
			montageVlcPreviewError = "ofxVlc4 could not attach the exported subtitle slave.";
			if (errorOut) {
				*errorOut = montageVlcPreviewError;
			}
			return false;
		}
		const auto subtitleTracks = montageVlcPreviewPlayer.getSubtitleTracks();
		if (!subtitleTracks.empty()) {
			montageVlcPreviewPlayer.selectSubtitleTrackById(subtitleTracks.back().id);
		}
		montageVlcPreviewLoadedSubtitlePath = subtitlePath;
	}

	montageVlcPreviewError.clear();
	if (errorOut) {
		errorOut->clear();
	}
	return true;
}

void ofApp::closeMontageVlcPreview() {
	if (!montageVlcPreviewInitialized) {
		return;
	}
	montageVlcPreviewPlayer.close();
	montageVlcPreviewInitialized = false;
	montageVlcPreviewLoadedVideoPath.clear();
	montageVlcPreviewLoadedSubtitlePath.clear();
}

void ofApp::drawMontageVlcPreview() {
	if (!montageVlcPreviewInitialized) {
		return;
	}

	if (!montageVlcPreviewError.empty()) {
		ImGui::TextDisabled("%s", montageVlcPreviewError.c_str());
	}

	const ofTexture & previewTexture = montageVlcPreviewPlayer.getTexture();
	if (previewTexture.isAllocated()) {
		const float availWidth = std::max(160.0f, ImGui::GetContentRegionAvail().x);
		const float maxWidth = std::min(availWidth, 420.0f);
		const float texWidth = std::max(1.0f, static_cast<float>(previewTexture.getWidth()));
		const float texHeight = std::max(1.0f, static_cast<float>(previewTexture.getHeight()));
		const float scale = std::min(maxWidth / texWidth, 240.0f / texHeight);
		const glm::vec2 drawSize(
			std::max(1.0f, texWidth * scale),
			std::max(1.0f, texHeight * scale));
		ImGui::BeginChild("##MontageVlcPreview", ImVec2(0, drawSize.y + 12.0f), true);
		ofxImGui::AddImage(previewTexture, drawSize);
		ImGui::EndChild();
	} else {
		ImGui::TextDisabled("ofxVlc4 preview will appear here after the media attaches.");
	}

	const float durationSeconds = std::max(0.0f, montageVlcPreviewPlayer.getLength() / 1000.0f);
	float previewPosition = std::clamp(montageVlcPreviewPlayer.getPosition(), 0.0f, 1.0f);
	const float currentSeconds = previewPosition * durationSeconds;
	if (ImGui::Button(montageVlcPreviewPlayer.isPlaying() ? "Pause VLC preview" : "Play VLC preview", ImVec2(150, 0))) {
		montageVlcPreviewPlayer.togglePlayPause();
	}
	ImGui::SameLine();
	if (ImGui::Button("Restart VLC preview", ImVec2(150, 0))) {
		montageVlcPreviewPlayer.play();
		montageVlcPreviewPlayer.setPosition(0.0f);
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%.2fs / %.2fs", currentSeconds, durationSeconds);

	ImGui::SetNextItemWidth(-1);
	if (ImGui::SliderFloat("VLC preview position", &previewPosition, 0.0f, 1.0f, "%.3f")) {
		montageVlcPreviewPlayer.setPosition(previewPosition);
	}

	int subtitleDelayMs = montageVlcPreviewPlayer.getSubtitleDelayMs();
	ImGui::SetNextItemWidth(180);
	if (ImGui::SliderInt("VLC subtitle delay", &subtitleDelayMs, -5000, 5000, "%d ms")) {
		montageVlcPreviewPlayer.setSubtitleDelayMs(subtitleDelayMs);
	}
	ImGui::SameLine();
	float subtitleTextScale = montageVlcPreviewPlayer.getSubtitleTextScale();
	ImGui::SetNextItemWidth(180);
	if (ImGui::SliderFloat("VLC subtitle scale", &subtitleTextScale, 0.5f, 3.0f, "%.2fx")) {
		montageVlcPreviewPlayer.setSubtitleTextScale(subtitleTextScale);
	}

	const auto subtitleState = montageVlcPreviewPlayer.getSubtitleStateInfo();
	if (subtitleState.trackSelected && !subtitleState.selectedTrackLabel.empty()) {
		ImGui::TextDisabled("Active VLC subtitle track: %s", subtitleState.selectedTrackLabel.c_str());
	} else {
		ImGui::TextDisabled("No VLC subtitle track selected yet.");
	}
	if (!montageVlcPreviewLoadedSubtitlePath.empty()) {
		ImGui::TextDisabled("%s", montageVlcPreviewLoadedSubtitlePath.c_str());
	}
}

bool ofApp::ensureMontageClipVlcPreviewInitialized(std::string * errorOut) {
	if (montageClipVlcInitialized) {
		return true;
	}

	try {
		montageClipVlcPlayer.init(0, nullptr);
		montageClipVlcInitialized = true;
		montageClipVlcError.clear();
		return true;
	} catch (const std::exception & e) {
		montageClipVlcError =
			std::string("Failed to initialize ofxVlc4 montage clip preview: ") + e.what();
		if (errorOut) {
			*errorOut = montageClipVlcError;
		}
		return false;
	}
}

bool ofApp::loadMontageClipVlcPreview(std::string * errorOut) {
	if (!ensureMontageClipVlcPreviewInitialized(errorOut)) {
		return false;
	}

	const std::vector<std::string> clipPaths = extractPathList(montageClipPaths);
	if (clipPaths.empty()) {
		montageClipVlcError = "Add at least one clip path before loading the montage clip preview.";
		if (errorOut) {
			*errorOut = montageClipVlcError;
		}
		return false;
	}

	montageClipVlcPlayer.stop();
	montageClipVlcPlayer.clearMediaSlaves();
	montageClipVlcPlayer.clearPlaylist();

	size_t addedCount = 0;
	for (const auto & clipPath : clipPaths) {
		std::error_code ec;
		if (!std::filesystem::exists(std::filesystem::path(clipPath), ec) || ec) {
			continue;
		}
		if (montageClipVlcPlayer.addPathToPlaylist(clipPath) > 0) {
			++addedCount;
		}
	}

	if (addedCount == 0) {
		montageClipVlcError = "ofxVlc4 could not load any montage clip paths from the current list.";
		if (errorOut) {
			*errorOut = montageClipVlcError;
		}
		return false;
	}

	montageClipVlcPlayer.playIndex(0);
	montageClipVlcError.clear();
	if (errorOut) {
		errorOut->clear();
	}
	return true;
}

void ofApp::closeMontageClipVlcPreview() {
	montageClipAutoRecordPending = false;
	if (!montageClipVlcInitialized) {
		return;
	}
	montageClipVlcPlayer.close();
	montageClipVlcInitialized = false;
}

void ofApp::drawMontageClipVlcPreview() {
	if (!montageClipVlcInitialized) {
		return;
	}

	if (!montageClipVlcError.empty()) {
		ImGui::TextDisabled("%s", montageClipVlcError.c_str());
	}

	const ofTexture & previewTexture = montageClipVlcPlayer.getTexture();
	if (previewTexture.isAllocated()) {
		const float availWidth = std::max(160.0f, ImGui::GetContentRegionAvail().x);
		const float maxWidth = std::min(availWidth, 420.0f);
		const float texWidth = std::max(1.0f, static_cast<float>(previewTexture.getWidth()));
		const float texHeight = std::max(1.0f, static_cast<float>(previewTexture.getHeight()));
		const float scale = std::min(maxWidth / texWidth, 240.0f / texHeight);
		const glm::vec2 drawSize(
			std::max(1.0f, texWidth * scale),
			std::max(1.0f, texHeight * scale));
		ImGui::BeginChild("##MontageClipVlcPreview", ImVec2(0, drawSize.y + 12.0f), true);
		ofxImGui::AddImage(previewTexture, drawSize);
		ImGui::EndChild();
	} else {
		ImGui::TextDisabled("Playlist preview will appear here after the first clip attaches.");
	}

	const float durationSeconds = std::max(0.0f, montageClipVlcPlayer.getLength() / 1000.0f);
	float previewPosition = std::clamp(montageClipVlcPlayer.getPosition(), 0.0f, 1.0f);
	const float currentSeconds = previewPosition * durationSeconds;
	if (ImGui::Button(
			montageClipVlcPlayer.isPlaying() ? "Pause clip playlist" : "Play clip playlist",
			ImVec2(150, 0))) {
		montageClipVlcPlayer.togglePlayPause();
	}
	ImGui::SameLine();
	if (ImGui::Button("Previous clip", ImVec2(120, 0))) {
		montageClipVlcPlayer.previousMediaListItem();
	}
	ImGui::SameLine();
	if (ImGui::Button("Next clip", ImVec2(120, 0))) {
		montageClipVlcPlayer.nextMediaListItem();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%.2fs / %.2fs", currentSeconds, durationSeconds);

	ImGui::SetNextItemWidth(-1);
	if (ImGui::SliderFloat("Clip playlist position", &previewPosition, 0.0f, 1.0f, "%.3f")) {
		montageClipVlcPlayer.setPosition(previewPosition);
	}

	const auto playlistState = montageClipVlcPlayer.getPlaylistStateInfo();
	if (!playlistState.items.empty()) {
		ImGui::TextDisabled(
			"Playlist: %d item(s), current index %d",
			static_cast<int>(playlistState.items.size()),
			playlistState.currentIndex);
	}
}

bool ofApp::startMontageClipVlcRecording(std::string * errorOut) {
	montageClipAutoRecordPending = false;
	const bool hasLoadedPreview =
		montageClipVlcInitialized &&
		!montageClipVlcPlayer.getPlaylistStateInfo().items.empty();
	if (!hasLoadedPreview && !loadMontageClipVlcPreview(errorOut)) {
		return false;
	}
	if (montageClipVlcPlayer.isVideoRecording()) {
		if (errorOut) {
			*errorOut = "Montage clip playlist recording is already active.";
		}
		return false;
	}

	const ofTexture & previewTexture = montageClipVlcPlayer.getTexture();
	if (!previewTexture.isAllocated()) {
		montageClipVlcError = "The montage clip playlist preview has no texture to record yet.";
		if (errorOut) {
			*errorOut = montageClipVlcError;
		}
		return false;
	}

	const std::filesystem::path outputDir =
		std::filesystem::path(ofToDataPath("generated/montage_export", true));
	std::error_code dirEc;
	std::filesystem::create_directories(outputDir, dirEc);
	if (dirEc) {
		montageClipVlcError = "Failed to create the montage export output directory.";
		if (errorOut) {
			*errorOut = montageClipVlcError;
		}
		return false;
	}

	const std::string safeTitle =
		sanitizeFilenameStem(trim(montageEdlTitle).empty() ? "montage" : trim(montageEdlTitle), "montage");
	const std::string recordingBasePath =
		(outputDir / (safeTitle + "_" + ofGetTimestampString("%Y%m%d-%H%M%S"))).string();
	if (!montageClipVlcPlayer.startTextureRecordingSession(recordingBasePath, previewTexture)) {
		montageClipVlcError = "ofxVlc4 could not start texture recording for the montage clip playlist.";
		if (errorOut) {
			*errorOut = montageClipVlcError;
		}
		return false;
	}

	montageClipRenderOutputPath.clear();
	montageClipPlaylistStatusMessage = "Recording the montage clip playlist preview...";
	if (errorOut) {
		errorOut->clear();
	}
	return true;
}

bool ofApp::startMontageGeneratedClipPreviewAndRecording(std::string * errorOut) {
	std::string populateStatus;
	if (!populateMontageClipPlaylistFromGeneratedOutputs(&populateStatus)) {
		montageClipPlaylistStatusMessage = populateStatus;
		if (errorOut != nullptr) {
			*errorOut = populateStatus;
		}
		return false;
	}

	std::string manifestError;
	const std::string manifestPath = exportMontageClipPlaylistManifest(&manifestError);
	if (!manifestPath.empty()) {
		montageClipPlaylistManifestPath = manifestPath;
	}

	std::string loadError;
	if (!loadMontageClipVlcPreview(&loadError)) {
		montageClipPlaylistStatusMessage =
			loadError.empty() ? std::string("Failed to load the generated clip playlist preview.") : loadError;
		if (errorOut != nullptr) {
			*errorOut = montageClipPlaylistStatusMessage;
		}
		return false;
	}

	if (!montageClipVlcPlayer.getTexture().isAllocated()) {
		montageClipAutoRecordPending = true;
		montageClipPlaylistStatusMessage =
			populateStatus +
			" Loaded the playlist preview. Recording will start automatically when the first frame is ready.";
		if (errorOut != nullptr) {
			errorOut->clear();
		}
		return true;
	}

	std::string recordError;
	if (!startMontageClipVlcRecording(&recordError)) {
		montageClipPlaylistStatusMessage =
			recordError.empty() ? std::string("Failed to start recording the generated clip playlist preview.") : recordError;
		if (errorOut != nullptr) {
			*errorOut = montageClipPlaylistStatusMessage;
		}
		return false;
	}

	montageClipPlaylistStatusMessage =
		populateStatus +
		" Loaded the playlist preview and started recording. Use 'Stop + Render playlist' to finalize the mp4.";
	if (errorOut != nullptr) {
		errorOut->clear();
	}
	return true;
}

bool ofApp::stopMontageClipVlcRecording(std::string * errorOut) {
	montageClipAutoRecordPending = false;
	if (!montageClipVlcInitialized || !montageClipVlcPlayer.isVideoRecording()) {
		if (errorOut) {
			*errorOut = "Montage clip playlist recording is not active.";
		}
		return false;
	}

	if (!montageClipVlcPlayer.stopRecordingSession()) {
		montageClipVlcError = "ofxVlc4 could not stop the montage clip playlist recording cleanly.";
		if (errorOut) {
			*errorOut = montageClipVlcError;
		}
		return false;
	}

	const std::string recordedVideoPath = trim(montageClipVlcPlayer.getLastVideoRecordingPath());
	if (recordedVideoPath.empty()) {
		montageClipVlcError = "The montage clip playlist recording did not produce a video file.";
		if (errorOut) {
			*errorOut = montageClipVlcError;
		}
		return false;
	}

	const std::string audioPath = trim(montageRenderAudioPath);
	if (!audioPath.empty()) {
		const std::filesystem::path outputDir =
			std::filesystem::path(ofToDataPath("generated/montage_export", true));
		const std::string safeTitle =
			sanitizeFilenameStem(trim(montageEdlTitle).empty() ? "montage" : trim(montageEdlTitle), "montage");
		const std::filesystem::path outputPath =
			outputDir / (safeTitle + "_playlist_render_" + ofGetTimestampString("%Y%m%d-%H%M%S") + ".mp4");
		std::string muxError;
		if (!ofxVlc4::muxRecordingFilesToMp4(
				recordedVideoPath,
				audioPath,
				outputPath.string(),
				30000,
				&muxError)) {
			montageClipVlcError =
				muxError.empty()
					? std::string("Failed to mux the montage clip playlist with the selected audio track.")
					: muxError;
			if (errorOut) {
				*errorOut = montageClipVlcError;
			}
			return false;
		}
		montageClipRenderOutputPath = outputPath.string();
		montageClipPlaylistStatusMessage =
			"Rendered montage clip playlist with muxed audio: " + outputPath.string();
	} else {
		montageClipRenderOutputPath = recordedVideoPath;
		montageClipPlaylistStatusMessage =
			"Recorded montage clip playlist without extra audio mux: " + recordedVideoPath;
	}

	montageClipVlcError.clear();
	if (errorOut) {
		errorOut->clear();
	}
	return true;
}
#endif

void ofApp::rebuildMontageSubtitleTrackFromText() {
	montageSubtitleTrack = {};
	if (trim(montageSrtText).empty()) {
		montagePreviewBundle.montageTrack = {};
		selectedMontageCueIndex = -1;
		return;
	}

	std::vector<ofxGgmlSimpleSrtCue> cues;
	std::string error;
	if (!ofxGgmlSimpleSrtSubtitleParser::parseText(montageSrtText, cues, error)) {
		montagePreviewBundle.montageTrack = {};
		selectedMontageCueIndex = -1;
		return;
	}

	montageSubtitleTrack.title = trim(montageEdlTitle).empty() ? "MONTAGE" : trim(montageEdlTitle);
	montageSubtitleTrack.cues.reserve(cues.size());
	for (size_t i = 0; i < cues.size(); ++i) {
		const auto & cue = cues[i];
		ofxGgmlMontageSubtitleCue montageCue;
		montageCue.index = static_cast<int>(i + 1);
		montageCue.startSeconds = std::max(0.0, static_cast<double>(cue.startMs) / 1000.0);
		montageCue.endSeconds = std::max(montageCue.startSeconds, static_cast<double>(cue.endMs) / 1000.0);
		montageCue.text = cue.text;
		montageSubtitleTrack.cues.push_back(std::move(montageCue));
	}

	selectedMontageCueIndex = montageSubtitleTrack.cues.empty()
		? -1
		: std::clamp(selectedMontageCueIndex, 0, static_cast<int>(montageSubtitleTrack.cues.size()) - 1);
	montagePreviewBundle.sourceVideoPath = trim(visionVideoPath);
	montagePreviewBundle.montageTrack.title = montageSubtitleTrack.title;
	montagePreviewBundle.montageTrack.timingMode = ofxGgmlMontagePreviewTimingMode::Montage;
	montagePreviewBundle.montageTrack.cues = montageSubtitleTrack.cues;
}

void ofApp::drawVisionVideoPreview(const std::string & videoPath) {
	if (videoPath.empty()) {
		return;
	}
	if (!visionPreviewVideoError.empty()) {
		ImGui::TextDisabled("%s", visionPreviewVideoError.c_str());
		return;
	}
	if (!visionPreviewVideoReady || !visionPreviewVideo.isLoaded() ||
		!visionPreviewVideo.getTexture().isAllocated()) {
		ImGui::TextDisabled("Video preview will appear here after the file loads.");
		return;
	}
	ImGui::TextDisabled(
		"Video preview: %d x %d",
		visionPreviewVideo.getWidth(),
		visionPreviewVideo.getHeight());
	drawMediaTexturePreview(visionPreviewVideo, "##VisionVideoPreview");

	const float durationSeconds = std::max(0.0f, visionPreviewVideo.getDuration());
	float previewPosition = std::clamp(visionPreviewVideo.getPosition(), 0.0f, 1.0f);
	const float currentSeconds = previewPosition * durationSeconds;
	if (ImGui::Button(visionPreviewVideo.isPaused() ? "Play preview" : "Pause preview", ImVec2(120, 0))) {
		visionPreviewVideo.setPaused(!visionPreviewVideo.isPaused());
		if (!visionPreviewVideo.isPaused()) {
			visionPreviewVideo.play();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Restart preview", ImVec2(120, 0))) {
		visionPreviewVideo.play();
		visionPreviewVideo.setPosition(0.0f);
		if (visionPreviewVideo.isPaused()) {
			visionPreviewVideo.update();
		}
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%.2fs / %.2fs", currentSeconds, durationSeconds);

	ImGui::SetNextItemWidth(-1);
	if (ImGui::SliderFloat("Preview position", &previewPosition, 0.0f, 1.0f, "%.3f")) {
		visionPreviewVideo.setPosition(previewPosition);
		visionPreviewVideo.update();
	}

	if (montageSubtitlePlaybackEnabled &&
		getSelectedMontagePreviewTimingMode() == ofxGgmlMontagePreviewTimingMode::Source &&
		!montageSourceSubtitleTrack.cues.empty()) {
		const int activeCueIndex = findActiveMontagePreviewCueIndex();
		if (activeCueIndex >= 0 &&
			activeCueIndex < static_cast<int>(montageSourceSubtitleTrack.cues.size())) {
			const auto & cue = montageSourceSubtitleTrack.cues[static_cast<size_t>(activeCueIndex)];
			ImGui::Separator();
			ImGui::TextDisabled("Live source-timed subtitle");
			ImGui::TextWrapped("%s", cue.text.c_str());
		}
	}
}

void ofApp::ensureDiffusionPreviewResources() {
	ensureLocalImagePreview(
		trim(diffusionInitImagePath),
		diffusionInitPreviewImage,
		diffusionInitPreviewLoadedPath,
		diffusionInitPreviewError);
	ensureLocalImagePreview(
		trim(diffusionMaskImagePath),
		diffusionMaskPreviewImage,
		diffusionMaskPreviewLoadedPath,
		diffusionMaskPreviewError);

	std::string outputPreviewPath;
	for (const auto & image : diffusionGeneratedImages) {
		if (image.selected && !trim(image.path).empty()) {
			outputPreviewPath = trim(image.path);
			break;
		}
	}
	if (outputPreviewPath.empty() && !diffusionGeneratedImages.empty()) {
		outputPreviewPath = trim(diffusionGeneratedImages.front().path);
	}
	ensureLocalImagePreview(
		outputPreviewPath,
		diffusionOutputPreviewImage,
		diffusionOutputPreviewLoadedPath,
		diffusionOutputPreviewError);
}

void ofApp::drawDiffusionImagePreview(
	const char * label,
	const std::string & imagePath,
	ofImage & previewImage,
	const std::string & errorMessage,
	const char * childId) {
	drawLocalImagePreview(label, imagePath, previewImage, errorMessage, childId);
}

void ofApp::runSpeechInference() {
	if (generating.load()) return;

	if (speechProfiles.empty()) {
		speechProfiles = ofxGgmlSpeechInference::defaultProfiles();
	}
	selectedSpeechProfileIndex = std::clamp(
		selectedSpeechProfileIndex,
		0,
		std::max(0, static_cast<int>(speechProfiles.size()) - 1));

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::Speech;
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = "Preparing speech transcription...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	const ofxGgmlSpeechModelProfile profileBase =
		speechProfiles.empty()
			? ofxGgmlSpeechModelProfile{}
			: speechProfiles[static_cast<size_t>(selectedSpeechProfileIndex)];
	const std::string audioPath = trim(speechAudioPath);
	const std::string executable = trim(speechExecutable);
	const std::string modelPath = trim(speechModelPath);
	const std::string serverUrl = trim(speechServerUrl);
	const std::string serverModel = trim(speechServerModel);
	const std::string prompt = trim(speechPrompt);
	const std::string languageHint = trim(speechLanguageHint);
	const int taskIndex = std::clamp(speechTaskIndex, 0, 1);
	const bool returnTimestamps = speechReturnTimestamps;

	workerThread = std::thread([this, profileBase, audioPath, executable, modelPath, serverUrl, serverModel, prompt, languageHint, taskIndex, returnTimestamps]() {
		auto setPending = [this](const std::string & text) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingOutput = text;
			pendingRole = "assistant";
			pendingMode = AiMode::Speech;
		};

		try {
			SpeechExecutionPlan plan;
			std::string planError;
			if (!buildSpeechExecutionPlan(
					profileBase,
					audioPath,
					executable,
					modelPath,
					serverUrl,
					serverModel,
					prompt,
					languageHint,
					taskIndex,
					returnTimestamps,
					plan,
					planError)) {
				setPending("[Error] " + planError);
				generating.store(false);
				return;
			}

			const ofxGgmlSpeechResult result = executeSpeechExecutionPlan(
				plan,
				[this](const std::string & status) {
					std::lock_guard<std::mutex> lock(streamMutex);
					streamingOutput = status;
				},
				[this](ofLogLevel level, const std::string & message) {
					logWithLevel(level, message);
				});
			if (cancelRequested.load()) {
				setPending("[Cancelled] Speech request cancelled.");
			} else if (result.success) {
				std::string displayText = result.text;
				if (!result.segments.empty()) {
					const std::string segmentText = formatSpeechSegments(result.segments);
					if (!segmentText.empty()) {
						displayText += "\n\nTimestamp segments:\n" + segmentText;
					}
				}
				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingSpeechDetectedLanguage = result.detectedLanguage;
					pendingSpeechTranscriptPath = result.transcriptPath;
					pendingSpeechSrtPath = result.srtPath;
					pendingSpeechSegmentCount = static_cast<int>(result.segments.size());
				}
				setPending(displayText);
				logWithLevel(
					OF_LOG_NOTICE,
					"Speech request completed in " +
						ofxGgmlHelpers::formatDurationMs(result.elapsedMs) +
						" via " + result.backendName);
			} else {
				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingSpeechDetectedLanguage.clear();
					pendingSpeechTranscriptPath.clear();
					pendingSpeechSrtPath.clear();
					pendingSpeechSegmentCount = 0;
				}
				setPending("[Error] " + result.error);
				if (!result.rawOutput.empty()) {
					logWithLevel(OF_LOG_WARNING, "Speech raw output: " + result.rawOutput);
				}
			}
		} catch (const std::exception & e) {
			{
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingSpeechDetectedLanguage.clear();
				pendingSpeechTranscriptPath.clear();
				pendingSpeechSrtPath.clear();
				pendingSpeechSegmentCount = 0;
			}
			setPending(std::string("[Error] Speech inference failed: ") + e.what());
		} catch (...) {
			{
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingSpeechDetectedLanguage.clear();
				pendingSpeechTranscriptPath.clear();
				pendingSpeechSrtPath.clear();
				pendingSpeechSegmentCount = 0;
			}
			setPending("[Error] Unknown failure during speech inference.");
		}

		{
			std::lock_guard<std::mutex> lock(streamMutex);
			streamingOutput.clear();
		}
		generating.store(false);
	});
}

void ofApp::runVoiceTranslatorWorkflow(bool useAudioInput) {
	if (generating.load()) return;

	if (translateLanguages.empty()) {
		translateLanguages = ofxGgmlTextAssistant::defaultTranslateLanguages();
	}
	ensureTtsProfilesLoaded();

	const auto resolveTranslateLanguageName = [&](int index, const std::string & fallback) {
		if (index >= 0 && index < static_cast<int>(translateLanguages.size())) {
			return translateLanguages[static_cast<size_t>(index)].name;
		}
		return fallback;
	};

	const std::string sourceLanguage = resolveTranslateLanguageName(
		translateSourceLang,
		"Auto detect");
	std::string targetLanguage = resolveTranslateLanguageName(
		translateTargetLang,
		"English");
	if (targetLanguage == "Auto detect") {
		targetLanguage = "English";
	}

	const std::string inputText = trim(
		std::strlen(translateInput) > 0
			? std::string(translateInput)
			: translateOutput);
	const std::string audioPath = trim(voiceTranslatorAudioPath);
	if (useAudioInput) {
		if (audioPath.empty()) {
			voiceTranslatorStatus = "Select an audio file or buffered recording first.";
			return;
		}
	} else if (inputText.empty()) {
		voiceTranslatorStatus = "Enter text to translate first.";
		return;
	}

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::Translate;
	generationStartTime = ofGetElapsedTimef();
	voiceTranslatorStatus = useAudioInput
		? "Preparing audio translation workflow..."
		: "Preparing translated voice workflow...";

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = voiceTranslatorStatus;
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	const ofxGgmlSpeechModelProfile speechProfileBase =
		speechProfiles.empty()
			? ofxGgmlSpeechModelProfile{}
			: speechProfiles[static_cast<size_t>(std::clamp(
				selectedSpeechProfileIndex,
				0,
				std::max(0, static_cast<int>(speechProfiles.size()) - 1)))];
	const std::string speechExecutableValue = trim(speechExecutable);
	const std::string speechModelPathValue = trim(speechModelPath);
	const std::string speechServerUrlValue = trim(speechServerUrl);
	const std::string speechServerModelValue = trim(speechServerModel);
	const std::string speechPromptValue = trim(speechPrompt);
	const std::string speechLanguageHintValue = trim(speechLanguageHint);

	const ofxGgmlTtsModelProfile ttsProfileBase = getSelectedTtsProfile();
	const std::string ttsModelPathValue = trim(ttsModelPath);
	const std::string ttsSpeakerPathValue = trim(ttsSpeakerPath);
	const std::string ttsSpeakerReferenceValue = trim(ttsSpeakerReferencePath);
	const std::string ttsOutputPathValue = trim(ttsOutputPath);
	const std::string ttsPromptAudioPathValue = trim(ttsPromptAudioPath);
	const std::string ttsLanguageValue = trim(ttsLanguage);
	const int ttsTaskIndexValue = std::clamp(ttsTaskIndex, 0, 2);
	const int requestSeed = ttsSeed;
	const int requestMaxTokens = std::max(0, ttsMaxTokens);
	const float requestTemperature = std::isfinite(ttsTemperature)
		? std::clamp(ttsTemperature, 0.0f, 2.0f)
		: 0.4f;
	const float requestPenalty = std::isfinite(ttsRepetitionPenalty)
		? std::clamp(ttsRepetitionPenalty, 1.0f, 3.0f)
		: 1.1f;
	const int requestRange = std::clamp(ttsRepetitionRange, 0, 512);
	const int requestTopK = std::clamp(ttsTopK, 0, 200);
	const float requestTopP = std::isfinite(ttsTopP)
		? std::clamp(ttsTopP, 0.0f, 1.0f)
		: 0.9f;
	const float requestMinP = std::isfinite(ttsMinP)
		? std::clamp(ttsMinP, 0.0f, 1.0f)
		: 0.05f;
	const bool requestStreamAudio = ttsStreamAudio;
	const bool requestNormalizeText = ttsNormalizeText;
	const auto ttsBackend = createConfiguredTtsBackend(
		ttsProfileBase,
		trim(ttsExecutablePath));
	const ofxGgmlInferenceSettings translateSettings =
		buildCurrentTextInferenceSettings(AiMode::Translate);
	const std::string modelPath = getSelectedModelPath();

	workerThread = std::thread(
		[this,
		 useAudioInput,
		 inputText,
		 audioPath,
		 sourceLanguage,
		 targetLanguage,
		 speechProfileBase,
		 speechExecutableValue,
		 speechModelPathValue,
		 speechServerUrlValue,
		 speechServerModelValue,
		 speechPromptValue,
		 speechLanguageHintValue,
		 ttsProfileBase,
		 ttsModelPathValue,
		 ttsSpeakerPathValue,
		 ttsSpeakerReferenceValue,
		 ttsOutputPathValue,
		 ttsPromptAudioPathValue,
		 ttsLanguageValue,
		 ttsTaskIndexValue,
		 requestSeed,
		 requestMaxTokens,
		 requestTemperature,
		 requestPenalty,
		 requestRange,
		 requestTopK,
		 requestTopP,
		 requestMinP,
		 requestStreamAudio,
		 requestNormalizeText,
		 ttsBackend,
		 translateSettings,
		 modelPath]() {
			auto setPendingTranslation = [this](const std::string & translatedText) {
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingOutput = translatedText;
				pendingRole = "assistant";
				pendingMode = AiMode::Translate;
			};
			auto clearPendingTtsArtifacts = [this]() {
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingTtsBackendName.clear();
				pendingTtsElapsedMs = 0.0f;
				pendingTtsResolvedSpeakerPath.clear();
				pendingTtsAudioFiles.clear();
				pendingTtsMetadata.clear();
			};
			auto setVoiceTranslatorStatus = [this](
				const std::string & status,
				const std::string & transcript) {
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingVoiceTranslatorStatus = status;
				pendingVoiceTranslatorTranscript = transcript;
				pendingVoiceTranslatorDirty = true;
			};
			auto setStreamingStatus = [this](const std::string & status) {
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput = status;
			};

			try {
				std::string transcript = inputText;
				std::string detectedSourceLanguage = sourceLanguage;

				if (useAudioInput) {
					SpeechExecutionPlan speechPlan;
					std::string speechPlanError;
					if (!buildSpeechExecutionPlan(
							speechProfileBase,
							audioPath,
							speechExecutableValue,
							speechModelPathValue,
							speechServerUrlValue,
							speechServerModelValue,
							speechPromptValue,
							sourceLanguage == "Auto detect"
								? speechLanguageHintValue
								: sourceLanguage,
							static_cast<int>(ofxGgmlSpeechTask::Transcribe),
							false,
							speechPlan,
							speechPlanError)) {
						clearPendingTtsArtifacts();
						setPendingTranslation("[Error] " + speechPlanError);
						setVoiceTranslatorStatus("[Error] " + speechPlanError, "");
						generating.store(false);
						return;
					}

					const ofxGgmlSpeechResult speechResult = executeSpeechExecutionPlan(
						speechPlan,
						[&](const std::string & status) {
							setStreamingStatus(status);
						},
						[this](ofLogLevel level, const std::string & message) {
							logWithLevel(level, message);
						});
					if (cancelRequested.load()) {
						clearPendingTtsArtifacts();
						setPendingTranslation("[Cancelled] Voice translator cancelled.");
						setVoiceTranslatorStatus("[Cancelled] Voice translator cancelled.", "");
						generating.store(false);
						return;
					}
					if (!speechResult.success) {
						clearPendingTtsArtifacts();
						const std::string errorMessage = speechResult.error.empty()
							? std::string("Speech transcription failed.")
							: speechResult.error;
						setPendingTranslation("[Error] " + errorMessage);
						setVoiceTranslatorStatus("[Error] " + errorMessage, "");
						generating.store(false);
						return;
					}

					transcript = trim(speechResult.text);
					if (transcript.empty()) {
						clearPendingTtsArtifacts();
						setPendingTranslation("[Error] Speech transcription returned no text.");
						setVoiceTranslatorStatus("[Error] Speech transcription returned no text.", "");
						generating.store(false);
						return;
					}

					if (!trim(speechResult.detectedLanguage).empty()) {
						detectedSourceLanguage = trim(speechResult.detectedLanguage);
					}
				}

				ofxGgmlTextAssistantRequest translateRequest;
				translateRequest.task = ofxGgmlTextTask::Translate;
				translateRequest.inputText = transcript;
				translateRequest.sourceLanguage = detectedSourceLanguage;
				translateRequest.targetLanguage = targetLanguage;
				translateRequest.labelOverride = "Voice translator";

				setStreamingStatus("Translating to " + targetLanguage + "...");
				const ofxGgmlTextAssistantResult translateResult = textAssistant.run(
					modelPath,
					translateRequest,
					translateSettings,
					[this](const std::string & partial) {
						if (cancelRequested.load()) {
							return false;
						}
						std::lock_guard<std::mutex> lock(streamMutex);
						streamingOutput = partial;
						return true;
					});
				if (cancelRequested.load()) {
					clearPendingTtsArtifacts();
					setPendingTranslation("[Cancelled] Voice translator cancelled.");
					setVoiceTranslatorStatus("[Cancelled] Voice translator cancelled.", transcript);
					generating.store(false);
					return;
				}
				if (!translateResult.inference.success) {
					clearPendingTtsArtifacts();
					const std::string errorMessage = translateResult.inference.error.empty()
						? std::string("Translation failed.")
						: translateResult.inference.error;
					setPendingTranslation("[Error] " + errorMessage);
					setVoiceTranslatorStatus("[Error] " + errorMessage, transcript);
					generating.store(false);
					return;
				}

				const std::string translatedText = trim(translateResult.inference.text);
				if (translatedText.empty()) {
					clearPendingTtsArtifacts();
					setPendingTranslation("[Error] Translation returned no text.");
					setVoiceTranslatorStatus("[Error] Translation returned no text.", transcript);
					generating.store(false);
					return;
				}

				setPendingTranslation(translatedText);

				if (!voiceTranslatorSpeakOutput) {
					clearPendingTtsArtifacts();
					setVoiceTranslatorStatus(
						"Translated text ready in " + targetLanguage + ". Voice synthesis skipped.",
						transcript);
					generating.store(false);
					return;
				}

				const auto task = static_cast<ofxGgmlTtsTask>(ttsTaskIndexValue);
				if (!ttsBackend) {
					clearPendingTtsArtifacts();
					setVoiceTranslatorStatus(
						"[Error] TTS backend is not available for translated voice output.",
						transcript);
					generating.store(false);
					return;
				}
				if (task == ofxGgmlTtsTask::CloneVoice && ttsSpeakerReferenceValue.empty()) {
					clearPendingTtsArtifacts();
					setVoiceTranslatorStatus(
						"[Error] Select a reference audio file for Clone Voice before running translated speech.",
						transcript);
					generating.store(false);
					return;
				}
				if (task == ofxGgmlTtsTask::ContinueSpeech && ttsPromptAudioPathValue.empty()) {
					clearPendingTtsArtifacts();
					setVoiceTranslatorStatus(
						"[Error] Select prompt audio for Continue Speech before running translated speech.",
						transcript);
					generating.store(false);
					return;
				}

				const std::string effectiveModelPath = ttsModelPathValue.empty()
					? suggestedModelPath(ttsProfileBase.modelPath, ttsProfileBase.modelFileHint)
					: ttsModelPathValue;
				const std::string effectiveSpeakerPath = ttsSpeakerPathValue.empty()
					? suggestedModelPath(ttsProfileBase.speakerPath, ttsProfileBase.speakerFileHint)
					: ttsSpeakerPathValue;
				std::string effectiveOutputPath = ttsOutputPathValue;
				if (effectiveOutputPath.empty()) {
					effectiveOutputPath = ofToDataPath("generated/voice_translator.wav", true);
				}

				const std::filesystem::path outputDir =
					std::filesystem::path(effectiveOutputPath).parent_path();
				if (!outputDir.empty()) {
					std::error_code dirEc;
					std::filesystem::create_directories(outputDir, dirEc);
					if (dirEc) {
						clearPendingTtsArtifacts();
						setVoiceTranslatorStatus(
							"[Error] Failed to create translated voice output directory: " + outputDir.string(),
							transcript);
						generating.store(false);
						return;
					}
				}

				ofxGgmlTtsRequest ttsRequest;
				ttsRequest.task = task;
				ttsRequest.text = translatedText;
				ttsRequest.modelPath = effectiveModelPath;
				ttsRequest.speakerPath = effectiveSpeakerPath;
				ttsRequest.speakerReferencePath = ttsSpeakerReferenceValue;
				ttsRequest.language = ttsLanguageValue.empty()
					? targetLanguage
					: ttsLanguageValue;
				ttsRequest.outputPath = effectiveOutputPath;
				ttsRequest.promptAudioPath = ttsPromptAudioPathValue;
				ttsRequest.seed = requestSeed;
				ttsRequest.maxTokens = requestMaxTokens;
				ttsRequest.temperature = requestTemperature;
				ttsRequest.repetitionPenalty = requestPenalty;
				ttsRequest.repetitionRange = requestRange;
				ttsRequest.topK = requestTopK;
				ttsRequest.topP = requestTopP;
				ttsRequest.minP = requestMinP;
				ttsRequest.streamAudio = requestStreamAudio;
				ttsRequest.normalizeText = requestNormalizeText;

				setStreamingStatus("Synthesizing translated voice...");
				const ofxGgmlTtsResult ttsResult = ttsBackend->synthesize(ttsRequest);
				if (cancelRequested.load()) {
					clearPendingTtsArtifacts();
					setVoiceTranslatorStatus("[Cancelled] Voice translator cancelled.", transcript);
					generating.store(false);
					return;
				}
				if (!ttsResult.success) {
					clearPendingTtsArtifacts();
					const std::string errorMessage = ttsResult.error.empty()
						? std::string("Translated voice synthesis failed.")
						: ttsResult.error;
					setVoiceTranslatorStatus("[Error] " + errorMessage, transcript);
					generating.store(false);
					return;
				}

				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingTtsBackendName = ttsResult.backendName;
					pendingTtsElapsedMs = ttsResult.elapsedMs;
					pendingTtsResolvedSpeakerPath = ttsResult.speakerPath;
					pendingTtsAudioFiles = ttsResult.audioFiles;
					pendingTtsMetadata = ttsResult.metadata;
				}

				std::ostringstream status;
				status << "Translated to " << targetLanguage << " and synthesized voice";
				if (!ttsResult.backendName.empty()) {
					status << " via " << ttsResult.backendName;
				}
				if (ttsResult.elapsedMs > 0.0f) {
					status << " in " << ofxGgmlHelpers::formatDurationMs(ttsResult.elapsedMs);
				}
				status << ".";
				if (!ttsResult.audioFiles.empty()) {
					status << " Output: " << ttsResult.audioFiles.front().path;
				}
				setVoiceTranslatorStatus(status.str(), transcript);
			} catch (const std::exception & e) {
				clearPendingTtsArtifacts();
				setPendingTranslation(std::string("[Error] Voice translator failed: ") + e.what());
				setVoiceTranslatorStatus(
					std::string("[Error] Voice translator failed: ") + e.what(),
					std::string());
			} catch (...) {
				clearPendingTtsArtifacts();
				setPendingTranslation("[Error] Voice translator failed.");
				setVoiceTranslatorStatus(
					"[Error] Voice translator failed.",
					std::string());
			}

			generating.store(false);
		});
}

void ofApp::runTtsInferenceForText(
	const std::string & textValue,
	const std::string & statusLabel,
	bool mirrorIntoTtsInput,
	const std::string & modelPathOverride,
	const std::string & speakerPathOverride) {
	if (generating.load()) return;

	ensureTtsProfilesLoaded();

	if (mirrorIntoTtsInput) {
		copyStringToBuffer(ttsInput, sizeof(ttsInput), textValue);
	}

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::Tts;
	generationStartTime = ofGetElapsedTimef();

	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = "Preparing TTS synthesis request...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	const ofxGgmlTtsModelProfile profileBase = getSelectedTtsProfile();
	const std::string text = trim(textValue);
	const std::string executablePath = trim(ttsExecutablePath);
	const std::string modelPath = trim(modelPathOverride).empty()
		? trim(ttsModelPath)
		: trim(modelPathOverride);
	const std::string speakerPath = trim(speakerPathOverride).empty()
		? trim(ttsSpeakerPath)
		: trim(speakerPathOverride);
	const std::string speakerReferencePath = trim(ttsSpeakerReferencePath);
	const std::string outputPath = trim(ttsOutputPath);
	const std::string promptAudioPath = trim(ttsPromptAudioPath);
	const std::string language = trim(ttsLanguage);
	const int taskIndex = std::clamp(ttsTaskIndex, 0, 2);
	const int requestSeed = ttsSeed;
	const int requestMaxTokens = std::max(0, ttsMaxTokens);
	const float requestTemperature = std::isfinite(ttsTemperature)
		? std::clamp(ttsTemperature, 0.0f, 2.0f)
		: 0.4f;
	const float requestPenalty = std::isfinite(ttsRepetitionPenalty)
		? std::clamp(ttsRepetitionPenalty, 1.0f, 3.0f)
		: 1.1f;
	const int requestRange = std::clamp(ttsRepetitionRange, 0, 512);
	const int requestTopK = std::clamp(ttsTopK, 0, 200);
	const float requestTopP = std::isfinite(ttsTopP)
		? std::clamp(ttsTopP, 0.0f, 1.0f)
		: 0.9f;
	const float requestMinP = std::isfinite(ttsMinP)
		? std::clamp(ttsMinP, 0.0f, 1.0f)
		: 0.05f;
	const bool requestStreamAudio = ttsStreamAudio;
	const bool requestNormalizeText = ttsNormalizeText;
	const auto backend = createConfiguredTtsBackend(profileBase, executablePath);
	const std::string backendLabel =
		backend ? backend->backendName() : std::string("TTS backend");

	workerThread = std::thread([this, backend, backendLabel, profileBase, statusLabel, text, modelPath, speakerPath, speakerReferencePath, outputPath, promptAudioPath, language, taskIndex, requestSeed, requestMaxTokens, requestTemperature, requestPenalty, requestRange, requestTopK, requestTopP, requestMinP, requestStreamAudio, requestNormalizeText]() {
		auto setPending = [this](const std::string & textValue) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingOutput = textValue;
			pendingRole = "assistant";
			pendingMode = AiMode::Tts;
		};

		auto clearPendingTtsArtifacts = [this]() {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingTtsBackendName.clear();
			pendingTtsElapsedMs = 0.0f;
			pendingTtsResolvedSpeakerPath.clear();
			pendingTtsAudioFiles.clear();
			pendingTtsMetadata.clear();
		};

		try {
			const auto task = static_cast<ofxGgmlTtsTask>(taskIndex);
			if (text.empty()) {
				clearPendingTtsArtifacts();
				setPending("[Error] Enter text to synthesize first.");
				generating.store(false);
				return;
			}
			if (task == ofxGgmlTtsTask::CloneVoice && speakerReferencePath.empty()) {
				clearPendingTtsArtifacts();
				setPending("[Error] Select a reference audio file for Clone Voice.");
				generating.store(false);
				return;
			}
			if (task == ofxGgmlTtsTask::ContinueSpeech && promptAudioPath.empty()) {
				clearPendingTtsArtifacts();
				setPending("[Error] Select prompt audio for Continue Speech.");
				generating.store(false);
				return;
			}

			const std::string effectiveModelPath = modelPath.empty()
				? suggestedModelPath(profileBase.modelPath, profileBase.modelFileHint)
				: modelPath;
			const std::string effectiveSpeakerPath = speakerPath.empty()
				? suggestedModelPath(profileBase.speakerPath, profileBase.speakerFileHint)
				: speakerPath;
			std::string effectiveOutputPath = outputPath;
			if (effectiveOutputPath.empty()) {
				effectiveOutputPath = ofToDataPath("generated/tts_output.wav", true);
			}

			const std::filesystem::path outputDir =
				std::filesystem::path(effectiveOutputPath).parent_path();
			if (!outputDir.empty()) {
				std::error_code dirEc;
				std::filesystem::create_directories(outputDir, dirEc);
				if (dirEc) {
					clearPendingTtsArtifacts();
					setPending("[Error] Failed to create TTS output directory: " + outputDir.string());
					generating.store(false);
					return;
				}
			}

			ofxGgmlTtsRequest request;
			request.task = task;
			request.text = text;
			request.modelPath = effectiveModelPath;
			request.speakerPath = effectiveSpeakerPath;
			request.speakerReferencePath = speakerReferencePath;
			request.language = language;
			request.outputPath = effectiveOutputPath;
			request.promptAudioPath = promptAudioPath;
			request.seed = requestSeed;
			request.maxTokens = requestMaxTokens;
			request.temperature = requestTemperature;
			request.repetitionPenalty = requestPenalty;
			request.repetitionRange = requestRange;
			request.topK = requestTopK;
			request.topP = requestTopP;
			request.minP = requestMinP;
			request.streamAudio = requestStreamAudio;
			request.normalizeText = requestNormalizeText;

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput = "Calling " + backendLabel + "...";
			}

			if (!backend) {
				clearPendingTtsArtifacts();
				setPending("[Error] TTS backend is not available.");
				generating.store(false);
				return;
			}

			const ofxGgmlTtsResult result = backend->synthesize(request);
			if (cancelRequested.load()) {
				clearPendingTtsArtifacts();
				setPending("[Cancelled] TTS synthesis cancelled.");
			} else if (result.success) {
				std::ostringstream summary;
				summary << (trim(statusLabel).empty() ? "Synthesized audio" : trim(statusLabel));
				if (!result.backendName.empty()) {
					summary << " via " << result.backendName;
				}
				if (result.elapsedMs > 0.0f) {
					summary << " in " << ofxGgmlHelpers::formatDurationMs(result.elapsedMs);
				}
				summary << ".";
				if (!result.audioFiles.empty()) {
					summary << "\n\nGenerated audio:";
					for (const auto & artifact : result.audioFiles) {
						summary << "\n- " << artifact.path;
					}
				} else if (!effectiveOutputPath.empty()) {
					summary << "\n\nRequested output: " << effectiveOutputPath;
				}
				if (!result.metadata.empty()) {
					summary << "\n\nMetadata:";
					for (const auto & entry : result.metadata) {
						if (trim(entry.first).empty() || trim(entry.second).empty()) {
							continue;
						}
						summary << "\n- " << entry.first << ": " << entry.second;
					}
				}
				if (!result.rawOutput.empty()) {
					summary << "\n\n" << result.rawOutput;
				}

				{
					std::lock_guard<std::mutex> lock(outputMutex);
					pendingTtsBackendName = result.backendName;
					pendingTtsElapsedMs = result.elapsedMs;
					pendingTtsResolvedSpeakerPath = result.speakerPath;
					pendingTtsAudioFiles = result.audioFiles;
					pendingTtsMetadata = result.metadata;
				}
				setPending(summary.str());
			} else {
				clearPendingTtsArtifacts();
				const std::string message = result.error.empty()
					? "TTS synthesis failed."
					: result.error;
				setPending("[Error] " + message);
			}
		} catch (const std::exception & e) {
			clearPendingTtsArtifacts();
			setPending(std::string("[Error] TTS synthesis failed: ") + e.what());
		} catch (...) {
			clearPendingTtsArtifacts();
			setPending("[Error] TTS synthesis failed.");
		}

		generating.store(false);
	});
}

void ofApp::runTtsInference() {
	runTtsInferenceForText(ttsInput, "Synthesized audio", false);
}

void ofApp::speakLatestChatReply(bool mirrorIntoTtsInput) {
	const std::string reply = trim(chatLastAssistantReply);
	if (reply.empty()) {
		chatTtsPreview.statusMessage = "No assistant reply available to synthesize yet.";
		return;
	}
	chatTtsPreview.request.pending = true;
	chatTtsPreview.statusMessage = "Synthesizing latest chat reply...";
	const std::string botModelPath = chatUseCustomTtsVoice
		? trim(chatTtsModelPath)
		: std::string();
	const std::string botSpeakerPath = chatUseCustomTtsVoice
		? trim(chatTtsSpeakerPath)
		: std::string();
	runTtsInferenceForText(
		reply,
		"Synthesized chat reply",
		mirrorIntoTtsInput,
		botModelPath,
		botSpeakerPath);
}

void ofApp::speakLatestChatExchange(bool mirrorIntoTtsInput) {
	if (generating.load()) {
		return;
	}

	std::string assistantText;
	std::string userText;
	for (auto it = chatMessages.rbegin(); it != chatMessages.rend(); ++it) {
		if (assistantText.empty() && it->role == "assistant") {
			assistantText = trim(it->text);
			continue;
		}
		if (!assistantText.empty() && it->role == "user") {
			userText = trim(it->text);
			break;
		}
	}
	if (assistantText.empty() || userText.empty()) {
		chatTtsPreview.statusMessage = "A user message and assistant reply are both needed for chat dialog playback.";
		return;
	}

	ensureTtsProfilesLoaded();
	if (mirrorIntoTtsInput) {
		copyStringToBuffer(ttsInput, sizeof(ttsInput), assistantText);
	}

	generating.store(true);
	cancelRequested.store(false);
	activeGenerationMode = AiMode::Tts;
	generationStartTime = ofGetElapsedTimef();
	chatTtsPreview.request.pending = true;
	chatTtsPreview.statusMessage = "Synthesizing latest chat exchange...";
	{
		std::lock_guard<std::mutex> lock(streamMutex);
		streamingOutput = "Preparing chat dialog synthesis...";
	}

	if (workerThread.joinable()) {
		workerThread.join();
	}

	const ofxGgmlTtsModelProfile profileBase = getSelectedTtsProfile();
	const std::string executablePath = trim(ttsExecutablePath);
	const std::string botModelPath = chatUseCustomTtsVoice ? trim(chatTtsModelPath) : std::string();
	const std::string botSpeakerPath = chatUseCustomTtsVoice ? trim(chatTtsSpeakerPath) : std::string();
	const std::string userModelPath = chatUseUserTtsVoice ? trim(chatUserTtsModelPath) : std::string();
	const std::string userSpeakerPath = chatUseUserTtsVoice ? trim(chatUserTtsSpeakerPath) : std::string();
	const std::string speakerReferencePath = trim(ttsSpeakerReferencePath);
	const std::string promptAudioPath = trim(ttsPromptAudioPath);
	const std::string language = trim(ttsLanguage);
	const int requestSeed = ttsSeed;
	const int requestMaxTokens = std::max(0, ttsMaxTokens);
	const float requestTemperature = std::isfinite(ttsTemperature)
		? std::clamp(ttsTemperature, 0.0f, 2.0f)
		: 0.4f;
	const float requestPenalty = std::isfinite(ttsRepetitionPenalty)
		? std::clamp(ttsRepetitionPenalty, 1.0f, 3.0f)
		: 1.1f;
	const int requestRange = std::clamp(ttsRepetitionRange, 0, 512);
	const int requestTopK = std::clamp(ttsTopK, 0, 200);
	const float requestTopP = std::isfinite(ttsTopP)
		? std::clamp(ttsTopP, 0.0f, 1.0f)
		: 0.9f;
	const float requestMinP = std::isfinite(ttsMinP)
		? std::clamp(ttsMinP, 0.0f, 1.0f)
		: 0.05f;
	const bool requestNormalizeText = ttsNormalizeText;
	const auto backend = createConfiguredTtsBackend(profileBase, executablePath);
	const std::string backendLabel =
		backend ? backend->backendName() : std::string("TTS backend");
	const std::string configuredOutputPath = trim(ttsOutputPath);

	workerThread = std::thread([this,
		backend,
		backendLabel,
		profileBase,
		assistantText,
		userText,
		botModelPath,
		botSpeakerPath,
		userModelPath,
		userSpeakerPath,
		speakerReferencePath,
		promptAudioPath,
		language,
		requestSeed,
		requestMaxTokens,
		requestTemperature,
		requestPenalty,
		requestRange,
		requestTopK,
		requestTopP,
		requestMinP,
		requestNormalizeText,
		configuredOutputPath]() {
		auto setPending = [this](const std::string & textValue) {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingOutput = textValue;
			pendingRole = "assistant";
			pendingMode = AiMode::Tts;
		};

		auto clearPendingTtsArtifacts = [this]() {
			std::lock_guard<std::mutex> lock(outputMutex);
			pendingTtsBackendName.clear();
			pendingTtsElapsedMs = 0.0f;
			pendingTtsResolvedSpeakerPath.clear();
			pendingTtsAudioFiles.clear();
			pendingTtsMetadata.clear();
		};

		auto synthesizeSegment =
			[&](const std::string & text,
				const std::string & modelOverride,
				const std::string & speakerOverride,
				const std::string & outputPath) -> ofxGgmlTtsResult {
				ofxGgmlTtsRequest request;
				request.task = ofxGgmlTtsTask::Synthesize;
				request.text = text;
				request.modelPath = modelOverride.empty()
					? suggestedModelPath(profileBase.modelPath, profileBase.modelFileHint)
					: modelOverride;
				request.speakerPath = speakerOverride.empty()
					? suggestedModelPath(profileBase.speakerPath, profileBase.speakerFileHint)
					: speakerOverride;
				request.speakerReferencePath = speakerReferencePath;
				request.language = language;
				request.outputPath = outputPath;
				request.promptAudioPath = promptAudioPath;
				request.seed = requestSeed;
				request.maxTokens = requestMaxTokens;
				request.temperature = requestTemperature;
				request.repetitionPenalty = requestPenalty;
				request.repetitionRange = requestRange;
				request.topK = requestTopK;
				request.topP = requestTopP;
				request.minP = requestMinP;
				request.streamAudio = false;
				request.normalizeText = requestNormalizeText;
				return backend->synthesize(request);
			};

		try {
			if (!backend) {
				clearPendingTtsArtifacts();
				setPending("[Error] TTS backend is not available.");
				generating.store(false);
				return;
			}

			const std::string userOutputPath = makeTempTtsSegmentPath("chat_user");
			const std::string botOutputPath = makeTempTtsSegmentPath("chat_bot");
			std::string dialogOutputPath = configuredOutputPath;
			if (dialogOutputPath.empty()) {
				dialogOutputPath = makeTempTtsSegmentPath("chat_dialog");
			}

			const std::filesystem::path outputDir =
				std::filesystem::path(dialogOutputPath).parent_path();
			if (!outputDir.empty()) {
				std::error_code dirEc;
				std::filesystem::create_directories(outputDir, dirEc);
				if (dirEc) {
					clearPendingTtsArtifacts();
					setPending("[Error] Failed to create chat dialog output directory: " + outputDir.string());
					generating.store(false);
					return;
				}
			}

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput = "Synthesizing user side of the chat...";
			}
			const ofxGgmlTtsResult userResult =
				synthesizeSegment(userText, userModelPath, userSpeakerPath, userOutputPath);
			if (!userResult.success) {
				clearPendingTtsArtifacts();
				setPending("[Error] " +
					(userResult.error.empty() ? std::string("Failed to synthesize the user side of the chat dialog.") : userResult.error));
				generating.store(false);
				return;
			}

			{
				std::lock_guard<std::mutex> lock(streamMutex);
				streamingOutput = "Synthesizing bot side of the chat...";
			}
			const ofxGgmlTtsResult botResult =
				synthesizeSegment(assistantText, botModelPath, botSpeakerPath, botOutputPath);
			if (!botResult.success) {
				clearPendingTtsArtifacts();
				setPending("[Error] " +
					(botResult.error.empty() ? std::string("Failed to synthesize the bot side of the chat dialog.") : botResult.error));
				generating.store(false);
				return;
			}

			std::vector<ofxGgmlTtsAudioArtifact> audioFiles;
			std::vector<std::pair<std::string, std::string>> metadata;
			ofxGgmlTtsAudioArtifact dialogArtifact;
			std::string stitchError;
			if (buildDialogWav({userOutputPath, botOutputPath}, dialogOutputPath, dialogArtifact, &stitchError)) {
				audioFiles.push_back(dialogArtifact);
			} else {
				metadata.emplace_back("dialogStitchWarning", stitchError);
			}
			audioFiles.push_back({userOutputPath, 0, 0, 0.0f});
			audioFiles.push_back({botOutputPath, 0, 0, 0.0f});
			metadata.emplace_back("dialogUserText", userText);
			metadata.emplace_back("dialogBotText", assistantText);
			if (!userResult.speakerPath.empty()) {
				metadata.emplace_back("dialogUserSpeaker", userResult.speakerPath);
			}
			if (!botResult.speakerPath.empty()) {
				metadata.emplace_back("dialogBotSpeaker", botResult.speakerPath);
			}

			{
				std::lock_guard<std::mutex> lock(outputMutex);
				pendingTtsBackendName = botResult.backendName.empty() ? backendLabel : botResult.backendName;
				pendingTtsElapsedMs = userResult.elapsedMs + botResult.elapsedMs;
				pendingTtsResolvedSpeakerPath = botResult.speakerPath;
				pendingTtsAudioFiles = audioFiles;
				pendingTtsMetadata = metadata;
			}

			std::ostringstream summary;
			summary << "Synthesized chat dialog";
			if (!botResult.backendName.empty()) {
				summary << " via " << botResult.backendName;
			}
			if ((userResult.elapsedMs + botResult.elapsedMs) > 0.0f) {
				summary << " in "
					<< ofxGgmlHelpers::formatDurationMs(userResult.elapsedMs + botResult.elapsedMs);
			}
			summary << ".";
			if (!audioFiles.empty()) {
				summary << "\n\nGenerated audio:";
				for (const auto & artifact : audioFiles) {
					summary << "\n- " << artifact.path;
				}
			}
			setPending(summary.str());
		} catch (const std::exception & e) {
			clearPendingTtsArtifacts();
			setPending(std::string("[Error] Chat dialog synthesis failed: ") + e.what());
		} catch (...) {
			clearPendingTtsArtifacts();
			setPending("[Error] Chat dialog synthesis failed.");
		}

		generating.store(false);
	});
}

void ofApp::speakLatestSummary(bool mirrorIntoTtsInput) {
	const std::string summary = trim(summarizeOutput);
	if (summary.empty()) {
		summarizeTtsPreview.statusMessage = "No summary is available to synthesize yet.";
		return;
	}
	summarizeTtsPreview.request.pending = true;
	summarizeTtsPreview.statusMessage = "Synthesizing current summary...";
	runTtsInferenceForText(summary, "Synthesized summary", mirrorIntoTtsInput);
}

void ofApp::speakTranslatedReply(bool mirrorIntoTtsInput) {
	const std::string translatedText = trim(translateOutput);
	if (translatedText.empty()) {
		translateTtsPreview.statusMessage = "No translated output is available to synthesize yet.";
		return;
	}
	translateTtsPreview.request.pending = true;
	translateTtsPreview.statusMessage = "Synthesizing translated voice output...";
	runTtsInferenceForText(
		translatedText,
		"Translated voice output",
		mirrorIntoTtsInput,
		translateUseCustomTtsVoice ? trim(translateTtsModelPath) : std::string(),
		translateUseCustomTtsVoice ? trim(translateTtsSpeakerPath) : std::string());
}

void ofApp::speakVideoEssayReply(bool mirrorIntoTtsInput) {
	const std::string narration = trim(videoEssayScript);
	if (narration.empty()) {
		videoEssayTtsPreview.statusMessage = "No video essay narration is available to synthesize yet.";
		return;
	}
	videoEssayTtsPreview.request.pending = true;
	videoEssayTtsPreview.statusMessage = "Synthesizing video essay voiceover...";
	runTtsInferenceForText(
		narration,
		"Video essay narration",
		mirrorIntoTtsInput,
		videoEssayUseCustomTtsVoice ? trim(videoEssayTtsModelPath) : std::string(),
		videoEssayUseCustomTtsVoice ? trim(videoEssayTtsSpeakerPath) : std::string());
}

bool ofApp::ensureChatTtsAudioLoaded(int artifactIndex, bool autoplay) {
	if (!ensureTtsOutputStreamReady()) {
		chatTtsPreview.statusMessage = "Failed to open the audio output stream for chat preview playback.";
		return false;
	}
	return ensurePreviewAudioLoaded(
		chatTtsPreview,
		chatTtsPreview.statusMessage,
		"No synthesized chat audio is available yet.",
		"The selected chat audio artifact has no file path.",
		"Chat audio file is missing: ",
		"Failed to load chat audio preview: ",
		"Ready to play synthesized chat reply.",
		"Playing synthesized chat reply.",
		artifactIndex,
		autoplay);
}

void ofApp::stopChatTtsPlayback(bool clearLoadedPath) {
	stopPreviewAudioPlayback(chatTtsPreview, clearLoadedPath);
}

bool ofApp::ensureTtsPanelAudioLoaded(int artifactIndex, bool autoplay) {
	if (!ensureTtsOutputStreamReady()) {
		ttsPanelPreview.statusMessage =
			"Failed to open the audio output stream for TTS playback.";
		return false;
	}
	return ensurePreviewAudioLoaded(
		ttsPanelPreview,
		ttsPanelPreview.statusMessage,
		"No synthesized TTS audio is available yet.",
		"The selected TTS audio artifact has no file path.",
		"TTS audio file is missing: ",
		"Failed to load TTS audio preview: ",
		"Ready to play synthesized TTS output.",
		"Playing synthesized TTS output.",
		artifactIndex,
		autoplay);
}

void ofApp::stopTtsPanelPlayback(bool clearLoadedPath) {
	stopPreviewAudioPlayback(ttsPanelPreview, clearLoadedPath);
}

bool ofApp::ensureSummaryTtsAudioLoaded(int artifactIndex, bool autoplay) {
	if (!ensureTtsOutputStreamReady()) {
		summarizeTtsPreview.statusMessage =
			"Failed to open the audio output stream for summary preview playback.";
		return false;
	}
	return ensurePreviewAudioLoaded(
		summarizeTtsPreview,
		summarizeTtsPreview.statusMessage,
		"No synthesized summary audio is available yet.",
		"The selected summary audio artifact has no file path.",
		"Summary audio file is missing: ",
		"Failed to load summary audio preview: ",
		"Ready to play synthesized summary.",
		"Playing synthesized summary.",
		artifactIndex,
		autoplay);
}

void ofApp::stopSummaryTtsPlayback(bool clearLoadedPath) {
	stopPreviewAudioPlayback(summarizeTtsPreview, clearLoadedPath);
}

bool ofApp::ensureTranslateTtsAudioLoaded(int artifactIndex, bool autoplay) {
	if (!ensureTtsOutputStreamReady()) {
		translateTtsPreview.statusMessage =
			"Failed to open the audio output stream for translated voice preview playback.";
		return false;
	}
	return ensurePreviewAudioLoaded(
		translateTtsPreview,
		translateTtsPreview.statusMessage,
		"No translated voice audio is available yet.",
		"The selected translated voice artifact has no file path.",
		"Translated voice file is missing: ",
		"Failed to load translated voice preview: ",
		"Ready to play translated voice output.",
		"Playing translated voice output.",
		artifactIndex,
		autoplay);
}

void ofApp::stopTranslateTtsPlayback(bool clearLoadedPath) {
	stopPreviewAudioPlayback(translateTtsPreview, clearLoadedPath);
}

bool ofApp::ensureVideoEssayTtsAudioLoaded(int artifactIndex, bool autoplay) {
	if (!ensureTtsOutputStreamReady()) {
		videoEssayTtsPreview.statusMessage =
			"Failed to open the audio output stream for video essay voiceover playback.";
		return false;
	}
	return ensurePreviewAudioLoaded(
		videoEssayTtsPreview,
		videoEssayTtsPreview.statusMessage,
		"No video essay voiceover audio is available yet.",
		"The selected video essay voiceover artifact has no file path.",
		"Video essay voiceover file is missing: ",
		"Failed to load video essay voiceover preview: ",
		"Ready to play video essay voiceover.",
		"Playing video essay voiceover.",
		artifactIndex,
		autoplay);
}

void ofApp::stopVideoEssayTtsPlayback(bool clearLoadedPath) {
	stopPreviewAudioPlayback(videoEssayTtsPreview, clearLoadedPath);
}

bool ofApp::runRealInference(AiMode mode, const std::string & prompt, std::string & output, std::string & error,
	std::function<void(const std::string &)> onStreamData,
	bool preserveLlamaInstructions,
	bool suppressFallbackWarning) {
	output.clear();
	error.clear();

	const bool preferredServerBackend =
		(textInferenceBackend == TextInferenceBackend::LlamaServer);
	const std::string modelPath = getSelectedModelPath();
	bool useServerBackend = preferredServerBackend;
	auto prepareCliBackend = [&](std::string * cliError = nullptr) -> bool {
		std::string localError;
		if (modelPath.empty()) {
			localError = "No model preset selected.";
		} else if (!std::filesystem::exists(modelPath)) {
			localError = "Model file not found: " + modelPath;
		}
		if (!localError.empty()) {
			if (cliError) {
				*cliError = localError;
			} else {
				error = localError;
			}
			return false;
		}
		if (llamaCliState.load(std::memory_order_relaxed) != 1) {
			probeLlamaCli();
			if (llamaCliState.load(std::memory_order_relaxed) != 1) {
				localError = "Optional CLI fallback is not installed. Build it with scripts/build-llama-cli.sh if you want a local non-server fallback.";
				if (cliError) {
					*cliError = localError;
				} else {
					error = localError;
				}
				return false;
			}
		}
		llmInference.setCompletionExecutable(llamaCliCommand);
		llmInference.probeCompletionCapabilities(true);
		return true;
	};
	if (useServerBackend && !ensureTextServerReady(false, true)) {
		const std::string serverError = !textServerStatusMessage.empty()
			? textServerStatusMessage
			: "Server-backed inference is not ready.";
		std::string cliError;
		if (prepareCliBackend(&cliError)) {
			useServerBackend = false;
			if (shouldLog(OF_LOG_NOTICE)) {
				logWithLevel(
					OF_LOG_NOTICE,
					"Server-backed inference is unavailable; falling back to local llama-completion for this request.");
			}
		} else {
			error = serverError;
			if (!cliError.empty()) {
				error += " CLI fallback unavailable: " + cliError;
			}
			return false;
		}
	}
	if (!useServerBackend && !prepareCliBackend()) {
		return false;
	}

	ofxGgmlInferenceSettings inferenceSettings;
	inferenceSettings.maxTokens = std::clamp(maxTokens, 1, 8192);
	inferenceSettings.temperature = std::isfinite(temperature)
		? std::clamp(temperature, 0.0f, 2.0f)
		: kDefaultTemp;
	inferenceSettings.topP = std::isfinite(topP)
		? std::clamp(topP, 0.0f, 1.0f)
		: kDefaultTopP;
	inferenceSettings.topK = std::clamp(topK, 0, 200);
	inferenceSettings.minP = std::isfinite(minP)
		? std::clamp(minP, 0.0f, 1.0f)
		: 0.0f;
	inferenceSettings.repeatPenalty = std::isfinite(repeatPenalty)
		? std::clamp(repeatPenalty, 1.0f, 2.0f)
		: kDefaultRepeatPenalty;
	inferenceSettings.contextSize = std::clamp(contextSize, 256, 16384);
	inferenceSettings.batchSize = std::clamp(batchSize, 32, 4096);
	inferenceSettings.threads = std::clamp(numThreads, 1, 128);
	inferenceSettings.gpuLayers = std::clamp(gpuLayers, 0, detectedModelLayers > 0 ? detectedModelLayers : 128);
	inferenceSettings.seed = seed;
	inferenceSettings.simpleIo = true;
	inferenceSettings.singleTurn = true;
	inferenceSettings.autoProbeCliCapabilities = true;
	inferenceSettings.trimPromptToContext = true;
	inferenceSettings.allowBatchFallback = true;
	inferenceSettings.autoContinueCutoff = (mode == AiMode::Script) && autoContinueCutoff;
	inferenceSettings.stopAtNaturalBoundary = stopAtNaturalBoundary;
	inferenceSettings.autoPromptCache = usePromptCache;
	inferenceSettings.promptCachePath = usePromptCache ? promptCachePathFor(modelPath, mode) : std::string();
	inferenceSettings.mirostat = mirostatMode;
	inferenceSettings.mirostatTau = mirostatTau;
	inferenceSettings.mirostatEta = mirostatEta;
	inferenceSettings.useServerBackend = useServerBackend;
	if (useServerBackend) {
		inferenceSettings.serverUrl = effectiveTextServerUrl(textServerUrl);
		inferenceSettings.serverModel = trim(textServerModel);
	}

	if (!useServerBackend &&
		!backendNames.empty() &&
		selectedBackendIndex >= 0 &&
		selectedBackendIndex < static_cast<int>(backendNames.size())) {
		const std::string & selected = backendNames[static_cast<size_t>(selectedBackendIndex)];
		if (selected != "CPU") {
			inferenceSettings.device = selected;
		}
	}
	if (!useServerBackend && inferenceSettings.device.empty()) {
		const std::string backend = ggml.getBackendName();
		if (!backend.empty() && backend != "CPU" && backend != "none") {
			inferenceSettings.device = backend;
		}
	}
	if (!useServerBackend &&
		inferenceSettings.gpuLayers == 0 &&
		inferenceSettings.device != "CPU") {
		inferenceSettings.gpuLayers = detectedModelLayers > 0 ? detectedModelLayers : 999;
	}

	std::string streamedText;
	auto chunkBridge = [&](const std::string & chunk) -> bool {
		if (cancelRequested.load()) {
			return false;
		}
		streamedText += chunk;
		if (onStreamData) {
			onStreamData(streamedText);
		}
		return true;
	};

	const ofxGgmlInferenceResult inferenceResult = llmInference.generate(
		modelPath,
		prompt,
		inferenceSettings,
		chunkBridge);
	bool useLegacyFallback = false;
	if (!inferenceResult.success) {
		if (!streamedText.empty() && !preserveLlamaInstructions) {
			output = ofxGgmlInference::sanitizeGeneratedText(
				streamedText,
				prompt);
			if (!output.empty()) {
				logWithLevel(OF_LOG_WARNING,
					"Generation ended with an error, but streamed output is available; using streamed text.");
				return true;
			}
		}
		error = inferenceResult.error.empty()
			? "Inference failed."
			: inferenceResult.error;
		useLegacyFallback = true;
	}
	if (!useLegacyFallback) {
		if (preserveLlamaInstructions) {
			output = trim(stripAnsi(streamedText.empty() ? inferenceResult.text : streamedText));
		} else if (!streamedText.empty()) {
			output = ofxGgmlInference::sanitizeGeneratedText(streamedText, prompt);
		} else {
			output = trim(inferenceResult.text);
		}
		if (!output.empty()) {
			return true;
		}
		if (error.empty()) {
			error = useServerBackend
				? "llama-server returned empty output."
				: "llama-completion returned empty output.";
		}
		useLegacyFallback = true;
		if (!useLegacyFallback) {
			return false;
		}
	}
	if (!suppressFallbackWarning && shouldLog(OF_LOG_WARNING)) {
		logWithLevel(OF_LOG_WARNING,
			useServerBackend
				? "Server-backed inference produced no usable output; falling back to local llama-completion."
				: "Modern inference path produced no usable output; falling back to legacy CLI execution.");
	}

	// Probe for llama-completion/llama-cli/llama if not already found.
	// Unlike earlier revisions this no longer permanently caches a
	// "not-found" result so the user can install the tools without
	// restarting the app.

	if (!prepareCliBackend()) {
		return false;
	}
	probeCliCapabilities();

	std::error_code tempEc;
	std::string dataDir = std::filesystem::temp_directory_path(tempEc).string();
	if (tempEc || dataDir.empty()) {
		dataDir = ofToDataPath("", true);
	}
	std::random_device rd;
	const uint64_t nonceHi = static_cast<uint64_t>(rd());
	const uint64_t nonceLo = static_cast<uint64_t>(rd());
	const uint64_t nonce = (nonceHi << 32) | nonceLo;
	const std::string id = ofToString(ofGetSystemTimeMillis()) + "_" + ofToString(nonce);
	const std::string promptPath = ofFilePath::join(dataDir, "llama_prompt_" + id + ".txt");

	{
		std::ofstream promptFile(promptPath);
		if (!promptFile.is_open()) {
			error = "Failed to create prompt file.";
			return false;
		}
		promptFile << prompt;
	}

	const int safeMaxTokens = std::clamp(maxTokens, 1, 8192);
	const float safeTemp = (std::isfinite(temperature) ? std::clamp(temperature, 0.0f, 2.0f) : kDefaultTemp);
	const float safeTopP = (std::isfinite(topP) ? std::clamp(topP, 0.0f, 1.0f) : kDefaultTopP);
const int safeTopK = std::clamp(topK, 0, 200);
const float safeMinP = (std::isfinite(minP) ? std::clamp(minP, 0.0f, 1.0f) : 0.0f);
	const float safeRepeatPenalty = (std::isfinite(repeatPenalty) ? std::clamp(repeatPenalty, 1.0f, 2.0f) : kDefaultRepeatPenalty);
	const int safeThreads = std::clamp(numThreads, 1, 128);
	const int safeContext = std::clamp(contextSize, 256, 16384);
	int effectiveBatch = std::clamp(batchSize, 32, 4096);
	const int maxGpuLayers = detectedModelLayers > 0 ? detectedModelLayers : 128;
	const int safeGpuLayers = std::clamp(gpuLayers, 0, maxGpuLayers);
	std::string cliDevice;
	if (!backendNames.empty() &&
		selectedBackendIndex >= 0 &&
		selectedBackendIndex < static_cast<int>(backendNames.size())) {
		const std::string & selected = backendNames[static_cast<size_t>(selectedBackendIndex)];
		if (selected != "CPU") {
			cliDevice = selected;
		}
	}
	if (cliDevice.empty()) {
		const std::string backend = ggml.getBackendName();
		if (!backend.empty() && backend != "CPU" && backend != "none") {
			cliDevice = backend;
		}
	}
	if (!cliDevice.empty() && effectiveBatch > 256) {
		if (shouldLog(OF_LOG_NOTICE)) {
			logWithLevel(OF_LOG_NOTICE,
				"Reducing batch size from " + ofToString(effectiveBatch) +
				" to 256 for CUDA/Vulkan stability.");
		}
		effectiveBatch = 256;
	}
	// GPU layers control the llama-completion CLI process, which has
	// its own GPU support independent of the addon's ggml engine.
	int effectiveGpuLayers = safeGpuLayers;
	if (effectiveGpuLayers == 0) {
		bool cpuBackendSelected = false;
		if (!backendNames.empty() &&
			selectedBackendIndex >= 0 &&
			selectedBackendIndex < static_cast<int>(backendNames.size())) {
			cpuBackendSelected = (backendNames[static_cast<size_t>(selectedBackendIndex)] == "CPU");
		}
		if (!cpuBackendSelected) {
			effectiveGpuLayers = (detectedModelLayers > 0) ? detectedModelLayers : 999;
			if (shouldLog(OF_LOG_NOTICE)) {
				logWithLevel(OF_LOG_NOTICE,
					(detectedModelLayers > 0)
						? ("GPU layers was 0; using detected model layer count (" +
							ofToString(detectedModelLayers) + ") for llama CLI offload.")
						: "GPU layers was 0 and model layer metadata is unavailable; using -ngl 999 for llama CLI offload.");
			}
		}
	}

	std::ostringstream tempStr, topPStr, repeatPenaltyStr;
	tempStr << std::fixed << std::setprecision(3) << safeTemp;
	topPStr << std::fixed << std::setprecision(3) << safeTopP;
	repeatPenaltyStr << std::fixed << std::setprecision(3) << safeRepeatPenalty;

	auto makeArgs = [&](bool shortFlags) {
		std::vector<std::string> out;
		out.reserve(40);
		out.emplace_back(llamaCliCommand);
		out.emplace_back("-m");
		out.emplace_back(modelPath);
		out.emplace_back(shortFlags ? "-f" : "--file");
		out.emplace_back(promptPath);
		out.emplace_back("-n");
		out.emplace_back(ofToString(safeMaxTokens));
		out.emplace_back("-c");
		out.emplace_back(ofToString(safeContext));
		out.emplace_back("-b");
		out.emplace_back(ofToString(effectiveBatch));
		out.emplace_back("-ngl");
		out.emplace_back(ofToString(effectiveGpuLayers));
		if (!cliDevice.empty()) {
			out.emplace_back("--device");
			out.emplace_back(cliDevice);
			out.emplace_back("--split-mode");
			out.emplace_back("none");
		}
		out.emplace_back("--temp");
		out.emplace_back(tempStr.str());
		out.emplace_back("--top-p");
		out.emplace_back(topPStr.str());
		if (safeTopK > 0 && cliSupportsTopK) {
			out.emplace_back("--top-k");
			out.emplace_back(ofToString(safeTopK));
		}
		if (safeMinP > 0.0f && cliSupportsMinP) {
			std::ostringstream minPStr;
			minPStr << std::fixed << std::setprecision(3) << safeMinP;
			out.emplace_back("--min-p");
			out.emplace_back(minPStr.str());
		}
		out.emplace_back("--repeat-penalty");
		out.emplace_back(repeatPenaltyStr.str());
		out.emplace_back(shortFlags ? "-t" : "--threads");
		out.emplace_back(ofToString(safeThreads));
		(void)usePromptCache;
		out.emplace_back("--no-display-prompt");
		out.emplace_back("--simple-io");
		if (cliSupportsSingleTurn) {
			out.emplace_back("--single-turn");
		}
		if (seed >= 0) {
			out.emplace_back("--seed");
			out.emplace_back(ofToString(seed));
		}
		if ((mirostatMode == 1 || mirostatMode == 2) && cliSupportsMirostat) {
			out.emplace_back("--mirostat");
			out.emplace_back(ofToString(mirostatMode));
			std::ostringstream tauStr, etaStr;
			tauStr << std::fixed << std::setprecision(3) << std::clamp(mirostatTau, 0.0f, 20.0f);
			etaStr << std::fixed << std::setprecision(3) << std::clamp(mirostatEta, 0.0f, 1.0f);
			out.emplace_back("--mirostat-lr");
			out.emplace_back(etaStr.str());
			out.emplace_back("--mirostat-ent");
			out.emplace_back(tauStr.str());
		}
		return out;
	};
	std::vector<std::string> args = makeArgs(false);

	// Print the command line to console for debugging.
	if (shouldLog(OF_LOG_VERBOSE)) {
		std::string cmdLine;
		for (size_t i = 0; i < args.size(); i++) {
			if (i > 0) cmdLine += " ";
			cmdLine += args[i];
		}
		logWithLevel(OF_LOG_VERBOSE, "Running: " + cmdLine);
	}

	std::string raw;
	int ret = -1;
	const auto tCliStart = std::chrono::steady_clock::now();
	const bool started = runProcessCapture(args, raw, ret, true, onStreamData, false);
	const auto tCliEnd = std::chrono::steady_clock::now();
	const float cliElapsedMs = std::chrono::duration<float, std::milli>(tCliEnd - tCliStart).count();
	if (shouldLog(OF_LOG_VERBOSE)) {
		logWithLevel(OF_LOG_VERBOSE, std::string("Process ") +
			(started ? "started" : "failed to start") + ", exit code: " + ofToString(ret));
	}
	if (started && ret != 0) {
		const bool crashLikeExit =
			ret == -1073740791 || // Windows STATUS_STACK_BUFFER_OVERRUN
			ret == -1073741819;   // Windows STATUS_ACCESS_VIOLATION
		if (crashLikeExit) {
			if (shouldLog(OF_LOG_WARNING)) {
				logWithLevel(OF_LOG_WARNING,
					"llama-completion crashed; retrying once with lower batch size.");
			}
			effectiveBatch = std::min(effectiveBatch, 128);
			raw.clear();
			ret = -1;
			runProcessCapture(makeArgs(false), raw, ret, true, onStreamData, false);
		}

		// With stderr separated, error messages about unknown flags
		// are no longer in `raw`.  If the process failed quickly and
		// produced no usable stdout, retry with short-style flags as
		// a fallback in case the installed CLI version uses a
		// different option syntax.
		if (trim(raw).empty()) {
			args = makeArgs(true);
			runProcessCapture(args, raw, ret, true, onStreamData, false);
		}
		// If still no stdout output after the short-flag retry, do a
		// diagnostic run with stderr captured so the user sees the
		// actual error message from the CLI tool.
		if (ret != 0 && trim(raw).empty()) {
			std::string diagOut;
			int diagRet = -1;
			runProcessCapture(makeArgs(false), diagOut, diagRet, false, nullptr, true);
			if (!trim(diagOut).empty()) {
				raw = diagOut;
			}
		}
	}

	// Exit code 130 (128 + SIGINT) is expected when the interactive-
	// mode CLI receives EOF on stdin and shuts down.  Treat it as
	// success when we captured valid generated output.
	// Also tolerate specific crash-on-exit codes that can occur
	// during cleanup after generation already produced output.
	if (started && ret != 0 && !trim(stripAnsi(raw)).empty()) {
		const bool benignExit =
			ret == 130                   // SIGINT (EOF on stdin)
			|| ret == 1                  // generic error (may occur during cleanup)
			|| ret == -1                 // signal-killed on POSIX
			|| (ret >= 128 && ret < 160) // POSIX signal exits (128+signal)
			;
		if (benignExit) {
			ret = 0;
		}
	}

	std::error_code ec;
	std::filesystem::remove(promptPath, ec);
	if (ec) {
		logWithLevel(OF_LOG_WARNING, "failed to remove temp prompt file: " + promptPath);
	}

	if (!started || ret == kExecNotFound) {
		// Binary truly missing — invalidate cache so next call re-probes.
		llamaCliState.store(-1, std::memory_order_relaxed);
		error = "llama-completion/llama-cli/llama not found in PATH.";
		return false;
	}

	if (ret != 0) {
		const std::string trimmedRaw = trim(stripAnsi(raw));
		const std::string codeDesc = describeExitCode(ret);
		if (!codeDesc.empty()) {
			error = llamaCliCommand + " crashed: " + codeDesc + ".";
			if (!trimmedRaw.empty()) {
				error += "\nOutput:\n" + trimmedRaw;
			}
			error += "\nTry reducing context size, setting GPU layers to 0 (CPU-only), or updating your GPU drivers.";
		} else {
			error = llamaCliCommand + " exited with code " + ofToString(ret) + ".";
			if (!trimmedRaw.empty()) {
				error += " Output:\n" + trimmedRaw;
			}
		}
		return false;
	}

	if (shouldLog(OF_LOG_NOTICE)) {
		logWithLevel(OF_LOG_NOTICE,
			"llama-completion run: " + ofToString(cliElapsedMs, 1) +
				" ms, output " + ofToString(trim(stripAnsi(raw)).size()) + " chars");
	}

	output = trim(stripAnsi(raw));
	if (preserveLlamaInstructions) {
		if (output.empty()) {
			error = llamaCliCommand + " returned empty output.";
			return false;
		}
		return true;
	}
	output = ofxGgmlInference::sanitizeGeneratedText(output, prompt);

	if (output.empty()) {
		error = llamaCliCommand + " returned empty output.";
		return false;
	}

	return true;
}

// ---------------------------------------------------------------------------
// Backend reinitialization — called when the user changes backend or device.
// ---------------------------------------------------------------------------

void ofApp::initializeBackendEngine(bool announceReinit) {
	ggml.close();

	ofxGgmlSettings settings;
	settings.threads = numThreads;
	if (selectedBackendIndex >= 0 &&
		selectedBackendIndex < static_cast<int>(backendNames.size())) {
		settings.preferredBackendName = backendNames[selectedBackendIndex];
	}
	setVulkanRuntimeDisabled(
		shouldDisableVulkanForCurrentSelection(backendNames, selectedBackendIndex));
	settings.graphSize = static_cast<size_t>(contextSize);

	auto result = ggml.setup(settings);
	engineReady = result.isOk();
	if (engineReady) {
		engineStatus = "Ready (" + ggml.getBackendName() + ")";
		devices = ggml.listDevices();
		lastBackendUsed = ggml.getBackendName();
		backendNames.clear();
		for (const auto & d : devices) {
			backendNames.push_back(d.name);
		}
		syncSelectedBackendIndex();
	} else {
		engineStatus = "Failed to initialize ggml engine";
		devices.clear();
	}

	if (announceReinit) {
		logWithLevel(OF_LOG_NOTICE, "Backend reinitialized: " + engineStatus);
	}
}

void ofApp::reinitBackend() {
 if (generating.load()) return;
 initializeBackendEngine(true);
}

void ofApp::syncSelectedBackendIndex() {
	if (backendNames.empty()) {
		selectedBackendIndex = 0;
		return;
	}
	std::string actualName = ggml.getBackendName();
	int matchIdx = -1;
	for (int i = 0; i < static_cast<int>(backendNames.size()); i++) {
		if (backendNames[i] == actualName) {
			matchIdx = i;
			break;
		}
	}
	selectedBackendIndex = (matchIdx >= 0) ? matchIdx : 0;
}

ofxGgmlRealtimeInfoSettings ofApp::buildLiveContextSettings(
	const std::string & rawUrls,
	const std::string & heading,
	bool enableAutoLiveContext) const {
	ofxGgmlRealtimeInfoSettings settings;
	settings.heading = heading;
	settings.explicitUrls = extractHttpUrls(rawUrls);
	settings.allowPromptUrlFetch = liveContextAllowPromptUrls;
	settings.allowDomainProviders = liveContextAllowDomainProviders;
	settings.allowGenericSearch = liveContextAllowGenericSearch;

	switch (liveContextMode) {
	case LiveContextMode::Offline:
		settings.enabled = false;
		settings.explicitUrls.clear();
		settings.requestCitations = false;
		settings.allowPromptUrlFetch = false;
		settings.allowDomainProviders = false;
		settings.allowGenericSearch = false;
		break;
	case LiveContextMode::LoadedSourcesOnly:
		settings.enabled = false;
		settings.requestCitations = true;
		settings.allowPromptUrlFetch = false;
		settings.allowDomainProviders = false;
		settings.allowGenericSearch = false;
		break;
	case LiveContextMode::LiveContext:
		settings.enabled = enableAutoLiveContext;
		settings.requestCitations = false;
		break;
	case LiveContextMode::LiveContextStrictCitations:
		settings.enabled = enableAutoLiveContext;
		settings.requestCitations = true;
		break;
	}

	return settings;
}

// ---------------------------------------------------------------------------
// Theme application
// ---------------------------------------------------------------------------

void ofApp::applyTheme(int index) {
switch (index) {
case 0:  // Dark
ImGui::StyleColorsDark();
break;
case 1:  // Light
ImGui::StyleColorsLight();
break;
case 2:  // Classic
ImGui::StyleColorsClassic();
break;
default:
ImGui::StyleColorsDark();
break;
}
}

// ---------------------------------------------------------------------------
// Clipboard helper
// ---------------------------------------------------------------------------

bool ofApp::ensureClipBackendConfigured(
	const std::string & modelPath,
	int verbosity,
	bool normalizeEmbeddings) {
#if OFXGGML_HAS_CLIPCPP
	const std::string trimmedModelPath = trim(modelPath);
	if (trimmedModelPath.empty()) {
		return false;
	}
	const int clampedVerbosity = std::clamp(verbosity, 0, 2);
	const bool needsReload =
		!clipInference.getBackend() ||
		clipInference.getBackend()->backendName() != "clip.cpp" ||
		configuredClipBackendModelPath != trimmedModelPath ||
		configuredClipBackendVerbosity != clampedVerbosity ||
		configuredClipBackendNormalize != normalizeEmbeddings;
	if (needsReload) {
		ofxGgmlClipCppAdapters::RuntimeOptions options;
		options.verbosity = clampedVerbosity;
		options.normalizeByDefault = normalizeEmbeddings;
		ofxGgmlClipCppAdapters::attachBackend(
			clipInference,
			trimmedModelPath,
			options,
			"clip.cpp");
		configuredClipBackendModelPath = trimmedModelPath;
		configuredClipBackendVerbosity = clampedVerbosity;
		configuredClipBackendNormalize = normalizeEmbeddings;
	}
	return clipInference.getBackend() != nullptr;
#else
	(void)modelPath;
	(void)verbosity;
	(void)normalizeEmbeddings;
	return clipInference.getBackend() != nullptr;
#endif
}

bool ofApp::ensureDiffusionBackendConfigured() {
#if OFXGGML_HAS_OFXSTABLEDIFFUSION
	if (!stableDiffusionEngine) {
		stableDiffusionEngine = std::make_shared<ofxStableDiffusion>();
	}
	const auto existingBackend = diffusionInference.getBackend();
	const auto existingBridge =
		std::dynamic_pointer_cast<ofxGgmlStableDiffusionBridgeBackend>(existingBackend);
	const bool backendNeedsAttach =
		!existingBackend ||
		!existingBridge ||
		!existingBridge->isConfigured() ||
		existingBackend->backendName() != "ofxStableDiffusion";
	if (backendNeedsAttach) {
		ofxGgmlStableDiffusionAdapters::RuntimeOptions runtimeOptions;
		runtimeOptions.clipInference =
			std::shared_ptr<ofxGgmlClipInference>(&clipInference, [](ofxGgmlClipInference *) {});
		diffusionInference.setBackend(
			ofxGgmlStableDiffusionAdapters::createImageBackend(
				stableDiffusionEngine,
				runtimeOptions));
	}
	return diffusionInference.getBackend() != nullptr;
#else
	return diffusionInference.getBackend() != nullptr;
#endif
}

bool ofApp::ensureDiffusionClipBackendConfigured() {
	return ensureClipBackendConfigured(trim(clipModelPath), clipVerbosity, clipNormalizeEmbeddings);
}

std::string ofApp::getPreferredDiffusionReuseImagePath() const {
	for (const auto & image : diffusionGeneratedImages) {
		if (image.selected && !trim(image.path).empty()) {
			return trim(image.path);
		}
	}
	if (!diffusionGeneratedImages.empty()) {
		return trim(diffusionGeneratedImages.front().path);
	}
	return trim(visionImagePath);
}

void ofApp::setDiffusionInitImagePath(const std::string & path, bool promoteTask) {
	const std::string trimmedPath = trim(path);
	if (trimmedPath.empty()) {
		return;
	}
	copyStringToBuffer(diffusionInitImagePath, sizeof(diffusionInitImagePath), trimmedPath);
	if (promoteTask &&
		static_cast<ofxGgmlImageGenerationTask>(std::clamp(diffusionTaskIndex, 0, 6)) ==
			ofxGgmlImageGenerationTask::TextToImage) {
		diffusionTaskIndex = static_cast<int>(ofxGgmlImageGenerationTask::ImageToImage);
	}
	autoSaveSession();
}

void ofApp::copyDiffusionOutputsToClipPaths() {
	std::ostringstream joined;
	for (size_t i = 0; i < diffusionGeneratedImages.size(); ++i) {
		if (i > 0) {
			joined << "\n";
		}
		joined << diffusionGeneratedImages[i].path;
	}
	copyStringToBuffer(clipImagePaths, sizeof(clipImagePaths), joined.str());
	autoSaveSession();
}

std::string ofApp::getSelectedModelPath() const {
	const std::string customPath = trim(customModelPath);
	if (!customPath.empty()) {
		return customPath;
	}
	if (modelPresets.empty()) return "";
	if (selectedModelIndex < 0 || selectedModelIndex >= static_cast<int>(modelPresets.size())) return "";
	if (cachedModelPathIndex == selectedModelIndex &&
		!cachedModelPath.empty() &&
		pathExists(cachedModelPath)) {
		return cachedModelPath;
	}
	const auto & preset = modelPresets[static_cast<size_t>(selectedModelIndex)];
	cachedModelPath = resolveModelPathHint(preset.filename);
	cachedModelPathIndex = selectedModelIndex;
	return cachedModelPath;
}

std::string ofApp::getSelectedVideoRenderModelPath() const {
	const std::string customPath = trim(customVideoRenderModelPath);
	if (!customPath.empty()) {
		return customPath;
	}
	if (videoRenderPresets.empty()) return "";
	if (selectedVideoRenderPresetIndex < 0 ||
		selectedVideoRenderPresetIndex >= static_cast<int>(videoRenderPresets.size())) return "";
	if (cachedVideoRenderModelPathIndex == selectedVideoRenderPresetIndex &&
		!cachedVideoRenderModelPath.empty() &&
		pathExists(cachedVideoRenderModelPath)) {
		return cachedVideoRenderModelPath;
	}
	const auto & preset = videoRenderPresets[static_cast<size_t>(selectedVideoRenderPresetIndex)];
	cachedVideoRenderModelPath = resolveModelPathHint(preset.filename);
	cachedVideoRenderModelPathIndex = selectedVideoRenderPresetIndex;
	return cachedVideoRenderModelPath;
}

std::string ofApp::getSelectedVideoRenderModelLabel() const {
	const std::string customPath = trim(customVideoRenderModelPath);
	if (!customPath.empty()) {
		const std::string fileName = ofFilePath::getFileName(customPath);
		return fileName.empty() ? std::string("Custom video model") : fileName;
	}
	if (videoRenderPresets.empty() ||
		selectedVideoRenderPresetIndex < 0 ||
		selectedVideoRenderPresetIndex >= static_cast<int>(videoRenderPresets.size())) {
		return std::string();
	}
	return videoRenderPresets[static_cast<size_t>(selectedVideoRenderPresetIndex)].name;
}

void ofApp::applyScriptReviewPreset() {
	if (!modelPresets.empty()) {
		selectedModelIndex = std::clamp(
			taskDefaultModelIndices[static_cast<int>(AiMode::Script)],
			0,
			static_cast<int>(modelPresets.size()) - 1);
		detectModelLayers();
		if (detectedModelLayers > 0) {
			gpuLayers = detectedModelLayers;
		}
	}
	maxTokens = std::clamp(maxTokens, 512, 768);
	contextSize = std::clamp(contextSize, 4096, 6144);
	batchSize = 256;
	temperature = 0.2f;
	topP = 0.9f;
	topK = std::max(topK, 50);
	minP = std::max(minP, 0.05f);
	repeatPenalty = 1.03f;
	autoContinueCutoff = true;
	usePromptCache = true;
}

void ofApp::detectModelLayers() {
	detectedModelLayers = 0;
	const std::string modelPath = getSelectedModelPath();
	if (modelPath.empty()) {
		return;
	}

	std::error_code ec;
	if (!std::filesystem::exists(modelPath, ec) || ec) {
		return;
	}

	detectedModelLayers = detectGgufLayerCountMetadata(modelPath);
	if (detectedModelLayers > 0 && shouldLog(OF_LOG_VERBOSE)) {
		logWithLevel(
			OF_LOG_VERBOSE,
			"Detected " + ofToString(detectedModelLayers) + " layers from GGUF metadata.");
	}
}
