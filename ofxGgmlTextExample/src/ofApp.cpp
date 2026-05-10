#include "ofApp.h"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <memory>
#include <sstream>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace {

std::vector<std::string> wrapText(const std::string & text, std::size_t width) {
	std::vector<std::string> wrapped;
	std::istringstream words(text);
	std::string word;
	std::string line;
	while (words >> word) {
		const std::string next = line.empty() ? word : line + " " + word;
		if (next.size() > width && !line.empty()) {
			wrapped.push_back(line);
			line = word;
		} else {
			line = next;
		}
	}
	if (!line.empty()) {
		wrapped.push_back(line);
	}
	return wrapped;
}

std::string toString(const std::filesystem::path & path) {
	return path.lexically_normal().string();
}

bool pathExists(const std::filesystem::path & path) {
	std::error_code error;
	return std::filesystem::is_regular_file(path, error);
}

std::filesystem::path executableDirectory() {
#if defined(_WIN32)
	std::wstring buffer(MAX_PATH, L'\0');
	DWORD length = GetModuleFileNameW(
		nullptr,
		buffer.data(),
		static_cast<DWORD>(buffer.size()));
	while (length == buffer.size()) {
		buffer.resize(buffer.size() * 2);
		length = GetModuleFileNameW(
			nullptr,
			buffer.data(),
			static_cast<DWORD>(buffer.size()));
	}
	if (length > 0) {
		buffer.resize(length);
		return std::filesystem::path(buffer).parent_path();
	}
#elif defined(__APPLE__)
	uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size);
	std::string buffer(size, '\0');
	if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
		return std::filesystem::path(buffer.c_str()).parent_path();
	}
#else
	std::string buffer(4096, '\0');
	const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
	if (length > 0) {
		buffer.resize(static_cast<std::size_t>(length));
		return std::filesystem::path(buffer).parent_path();
	}
#endif
	std::error_code error;
	return std::filesystem::current_path(error);
}

void addUniquePath(
	std::vector<std::filesystem::path> & paths,
	const std::filesystem::path & path) {
	if (path.empty()) {
		return;
	}
	const std::filesystem::path normalized = path.lexically_normal();
	for (const auto & existing : paths) {
		if (existing == normalized) {
			return;
		}
	}
	paths.push_back(normalized);
}

std::vector<std::filesystem::path> searchRoots() {
	std::vector<std::filesystem::path> roots;
	std::error_code error;
	addUniquePath(roots, executableDirectory());
	addUniquePath(roots, std::filesystem::current_path(error));

	const std::size_t initialCount = roots.size();
	for (std::size_t i = 0; i < initialCount; ++i) {
		std::filesystem::path parent = roots[i];
		for (int depth = 0; depth < 5 && !parent.empty(); ++depth) {
			addUniquePath(roots, parent);
			parent = parent.parent_path();
		}
	}
	return roots;
}

std::string findFirstFile(const std::vector<std::filesystem::path> & candidates) {
	for (const auto & candidate : candidates) {
		if (pathExists(candidate)) {
			return toString(candidate);
		}
	}
	return {};
}

std::string findFirstFileByExtension(
	const std::vector<std::filesystem::path> & roots,
	const std::vector<std::filesystem::path> & relativeDirectories,
	const std::string & extension) {
	for (const auto & root : roots) {
		for (const auto & relative : relativeDirectories) {
			const std::filesystem::path directory = (root / relative).lexically_normal();
			std::error_code error;
			if (!std::filesystem::is_directory(directory, error)) {
				continue;
			}
			for (const auto & entry : std::filesystem::directory_iterator(directory, error)) {
				if (error) {
					break;
				}
				if (entry.is_regular_file(error) && entry.path().extension() == extension) {
					return toString(entry.path());
				}
			}
		}
	}
	return {};
}

std::string discoverLlamaCli() {
#if defined(_WIN32)
	const std::vector<std::string> executableNames = {
		"llama-cli.exe",
		"main.exe",
		"llama.exe"
	};
#else
	const std::vector<std::string> executableNames = {
		"llama-cli",
		"main",
		"llama"
	};
#endif
	const std::vector<std::filesystem::path> relativeDirectories = {
		"",
		"bin",
		"data",
		"data/bin",
		"tools",
		"models",
		"libs/llama/bin",
		"libs/llama.cpp/build/bin",
		"libs/llama.cpp/build/bin/Release",
		"libs/llama.cpp/build/bin/Debug"
	};

	std::vector<std::filesystem::path> candidates;
	for (const auto & root : searchRoots()) {
		for (const auto & relative : relativeDirectories) {
			for (const auto & name : executableNames) {
				candidates.push_back(root / relative / name);
			}
		}
	}
	return findFirstFile(candidates);
}

std::string discoverTextModel() {
	return findFirstFileByExtension(
		searchRoots(),
		{
			"",
			"data",
			"data/models",
			"models",
			"ofxGgmlTextExample/bin/data",
			"ofxGgmlTextExample/bin/data/models",
			"ofxGgmlTextExample/models"
		},
		".gguf");
}

} // namespace

void ofApp::setup() {
	ofSetWindowTitle("ofxGgml text example");
	ofBackground(12);
	gui.setup(nullptr, false);

	settings.executablePath = normalizeEnvPath(envValue("OFXGGML_LLAMA_CLI"));
	settings.serverUrl = normalizeEnvPath(envValue("OFXGGML_TEXT_SERVER_URL"));
	settings.serverModel = normalizeEnvPath(envValue("OFXGGML_TEXT_SERVER_MODEL"));
	if (settings.serverUrl.empty()) {
		settings.serverUrl = "http://127.0.0.1:8080";
	}
	settings.useServerBackend = true;
	modelPath = normalizeEnvPath(envValue("OFXGGML_TEXT_MODEL"));
	autoConfigureTextBackend(settings, modelPath);
	prompt = "Write one concise sentence about local inference in openFrameworks.";

	const std::string backend = normalizeEnvPath(envValue("OFXGGML_TEXT_BACKEND"));
	if (backend == "cli") {
		settings.useServerBackend = false;
		generator.setBackend(std::make_shared<ofxGgmlLlamaCliTextBackend>());
	} else {
		generator.setBackend(std::make_shared<ofxGgmlLlamaServerTextBackend>(settings.serverUrl));
	}
	std::copy(prompt.begin(), prompt.end(), promptBuffer.begin());
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		status = "ready";
		rebuildLinesLocked();
	}
}

void ofApp::draw() {
	std::string statusSnapshot;
	std::string backendSnapshot;
	std::string serverUrlSnapshot;
	std::string serverModelSnapshot;
	std::string executableSnapshot;
	std::string modelPathSnapshot;
	std::string promptSnapshot;
	std::string outputSnapshot;
	bool runningSnapshot = false;
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		statusSnapshot = status;
		backendSnapshot = settings.useServerBackend ? "llama-server" : "llama-cli";
		serverUrlSnapshot = settings.serverUrl.empty() ? "(unset)" : settings.serverUrl;
		serverModelSnapshot = settings.serverModel.empty() ? "(auto)" : settings.serverModel;
		executableSnapshot = settings.executablePath.empty() ? "(optional)" : settings.executablePath;
		modelPathSnapshot = modelPath.empty() ? "(server-managed)" : modelPath;
		promptSnapshot = prompt;
		outputSnapshot = output;
		runningSnapshot = running;
	}

	bool shouldRun = false;
	bool shouldCancel = false;

	ofBackground(12);
	gui.begin();
	ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(880.0f, 520.0f), ImGuiCond_Once);
	if (ImGui::Begin("ofxGgml Text Example")) {
		if (ImGui::Button("Run")) {
			shouldRun = true;
		}
		ImGui::SameLine();
		if (!runningSnapshot) {
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.45f);
		}
		if (ImGui::Button("Cancel") && runningSnapshot) {
			shouldCancel = true;
		}
		if (!runningSnapshot) {
			ImGui::PopStyleVar();
		}

		ImGui::Separator();
		const ImVec4 statusColor = runningSnapshot
			? ImVec4(0.45f, 0.75f, 1.0f, 1.0f)
			: ImVec4(0.70f, 0.92f, 0.70f, 1.0f);
		ImGui::TextColored(statusColor, "%s", statusSnapshot.c_str());
		ImGui::Text("State: %s", runningSnapshot ? "running" : "idle");
		ImGui::Text("Backend: %s", backendSnapshot.c_str());
		ImGui::TextWrapped("Server: %s", serverUrlSnapshot.c_str());
		ImGui::TextWrapped("Server model: %s", serverModelSnapshot.c_str());
		ImGui::TextWrapped("Executable: %s", executableSnapshot.c_str());
		ImGui::TextWrapped("Model: %s", modelPathSnapshot.c_str());

		ImGui::Spacing();
		ImGui::TextUnformatted("Prompt");
		ImGui::Separator();
		if (!runningSnapshot) {
			ImGui::InputTextMultiline(
				"##prompt",
				promptBuffer.data(),
				promptBuffer.size(),
				ImVec2(0.0f, 70.0f));
		} else {
			ImGui::TextWrapped("%s", promptSnapshot.c_str());
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("Output");
		ImGui::Separator();
		ImGui::BeginChild("ofxGgmlTextOutput", ImVec2(0.0f, 170.0f), true);
		if (outputSnapshot.empty()) {
			ImGui::TextDisabled("(none)");
		} else {
			ImGui::TextWrapped("%s", outputSnapshot.c_str());
		}
		ImGui::EndChild();
	}
	ImGui::End();
	gui.end();
	gui.draw();

	if (shouldRun) {
		startPrompt();
	}
	if (shouldCancel) {
		requestCancel();
	}
}

void ofApp::keyPressed(int key) {
	if (key == 'r' || key == 'R') {
		startPrompt();
		return;
	}
	if (key == 'c' || key == 'C') {
		requestCancel();
	}
}

void ofApp::exit() {
	cancelRequested = true;
	if (worker.joinable()) {
		worker.join();
	}
}

void ofApp::startPrompt() {
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		if (running) {
			status = "text request is already running";
			rebuildLinesLocked();
			return;
		}
	}

	if (worker.joinable()) {
		worker.join();
	}

	{
		std::lock_guard<std::mutex> lock(stateMutex);
		output.clear();
		prompt = normalizeEnvPath(promptBuffer.data());
		if (prompt.empty()) {
			status = "type a prompt first";
			rebuildLinesLocked();
			return;
		}
		status = "checking text backend configuration...";
		running = true;
		cancelRequested = false;
		rebuildLinesLocked();
	}

	worker = std::thread(&ofApp::runPromptWorker, this);
}

void ofApp::requestCancel() {
	cancelRequested = true;
	std::lock_guard<std::mutex> lock(stateMutex);
	if (running) {
		status = "cancelling after the next output chunk...";
		rebuildLinesLocked();
	}
}

void ofApp::runPromptWorker() {
	auto fail = [this](std::string message) {
		std::lock_guard<std::mutex> lock(stateMutex);
		status = std::move(message);
		running = false;
		rebuildLinesLocked();
	};

	ofxGgmlTextGenerationSettings requestSettings;
	std::string requestModelPath;
	std::string requestPrompt;
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		requestSettings = settings;
		requestModelPath = modelPath;
		requestPrompt = prompt;
	}

	if (requestSettings.useServerBackend) {
		if (requestSettings.serverUrl.empty()) {
			fail("No llama-server URL configured. Set OFXGGML_TEXT_SERVER_URL.");
			return;
		}
	} else {
		if (requestSettings.executablePath.empty()) {
			fail("No llama.cpp CLI found. Set OFXGGML_LLAMA_CLI or use OFXGGML_TEXT_BACKEND=server.");
			return;
		}
		if (!fileExists(requestSettings.executablePath)) {
			fail("OFXGGML_LLAMA_CLI was not found: " + requestSettings.executablePath);
			return;
		}
		if (requestModelPath.empty()) {
			fail("No GGUF model found. Set OFXGGML_TEXT_MODEL or place one under bin/data/models.");
			return;
		}
		if (!fileExists(requestModelPath)) {
			fail("OFXGGML_TEXT_MODEL was not found: " + requestModelPath);
			return;
		}
	}

	{
		std::lock_guard<std::mutex> lock(stateMutex);
		status = requestSettings.useServerBackend
			? "requesting llama-server..."
			: "running llama.cpp CLI...";
		rebuildLinesLocked();
	}

	ofxGgmlTextRequest request;
	request.modelPath = requestModelPath;
	request.prompt = requestPrompt;
	request.settings = requestSettings;
	request.settings.maxTokens = 64;
	request.settings.temperature = 0.7f;
	request.settings.gpuLayers = -1;

	auto appendServerTextChunk = [this](const std::string & chunk) {
		if (cancelRequested) {
			return false;
		}
		std::lock_guard<std::mutex> lock(stateMutex);
		output += chunk;
		status = "receiving text output...";
		rebuildLinesLocked();
		return !cancelRequested;
	};
	auto cancelOnlyChunk = [this](const std::string &) {
		return !cancelRequested;
	};
	ofxGgmlTextChunkCallback onTextChunk;
	if (requestSettings.useServerBackend) {
		onTextChunk = appendServerTextChunk;
	} else {
		onTextChunk = cancelOnlyChunk;
	}

	auto result = generator.generate(request, onTextChunk);
	if (requestSettings.useServerBackend && !result.success && !cancelRequested) {
		const std::string serverError = result.error;
		const bool canFallbackToCli =
			!requestSettings.executablePath.empty() &&
			fileExists(requestSettings.executablePath) &&
			!requestModelPath.empty() &&
			fileExists(requestModelPath);
		if (canFallbackToCli) {
			{
				std::lock_guard<std::mutex> lock(stateMutex);
				output.clear();
				status = "llama-server unavailable; falling back to llama.cpp CLI...";
				rebuildLinesLocked();
			}
			ofxGgmlTextRequest fallbackRequest = request;
			fallbackRequest.settings.useServerBackend = false;
			ofxGgmlLlamaCliTextBackend fallbackBackend;
			result = fallbackBackend.generate(fallbackRequest, cancelOnlyChunk);
			if (!result.success) {
				result.error = "llama-server failed: " + serverError +
					"; CLI fallback failed: " + result.error;
			}
		}
	}

	std::lock_guard<std::mutex> lock(stateMutex);
	if (cancelRequested) {
		output.clear();
		status = "cancelled";
	} else if (result.success) {
		output = result.text;
		status = "complete via " + result.backendName + " in " +
			std::to_string(static_cast<int>(result.elapsedMs)) + " ms";
	} else {
		if (output.empty() && !result.rawOutput.empty()) {
			output = result.rawOutput;
		}
		status = "text error: " + result.error;
	}
	running = false;
	rebuildLinesLocked();
}

void ofApp::rebuildLinesLocked() {
	lines.clear();
	lines.push_back("ofxGgml text example");
	lines.push_back(status);
	lines.push_back(std::string("state: ") + (running ? "running" : "idle"));
	lines.push_back("keys: R run again, C cancel");
	lines.push_back("backend: " + std::string(settings.useServerBackend ? "llama-server" : "llama-cli"));
	lines.push_back("server: " + (settings.serverUrl.empty() ? "(unset)" : settings.serverUrl));
	lines.push_back("server model: " + (settings.serverModel.empty() ? "(auto)" : settings.serverModel));
	lines.push_back("executable: " + (settings.executablePath.empty() ? "(optional)" : settings.executablePath));
	lines.push_back("model: " + (modelPath.empty() ? "(server-managed)" : modelPath));
	lines.push_back("");
	lines.push_back("prompt:");
	for (const auto & line : wrapText(prompt, 96)) {
		lines.push_back("  " + line);
	}
	lines.push_back("");
	lines.push_back("output:");
	if (output.empty()) {
		lines.push_back("  (none)");
	} else {
		for (const auto & line : wrapText(output, 96)) {
			lines.push_back("  " + line);
		}
	}
}

void ofApp::autoConfigureTextBackend(
	ofxGgmlTextGenerationSettings & settings,
	std::string & modelPath) {
	if (settings.executablePath.empty()) {
		settings.executablePath = discoverLlamaCli();
	}
	if (modelPath.empty()) {
		modelPath = discoverTextModel();
	}
}

std::string ofApp::normalizeEnvPath(const std::string & path) {
	std::size_t first = 0;
	while (first < path.size() &&
		std::isspace(static_cast<unsigned char>(path[first]))) {
		++first;
	}
	std::size_t last = path.size();
	while (last > first &&
		std::isspace(static_cast<unsigned char>(path[last - 1]))) {
		--last;
	}
	std::string normalized = path.substr(first, last - first);
	if (normalized.size() >= 2 && normalized.front() == '"' && normalized.back() == '"') {
		normalized = normalized.substr(1, normalized.size() - 2);
	}
	return normalized;
}

bool ofApp::fileExists(const std::string & path) {
	return !path.empty() && ofFile::doesFileExist(path, false);
}

std::string ofApp::envValue(const char * name) {
#if defined(_WIN32)
	char * value = nullptr;
	std::size_t length = 0;
	if (_dupenv_s(&value, &length, name) != 0 || !value) {
		return {};
	}
	std::string result(value, length > 0 ? length - 1 : 0);
	free(value);
	return result;
#else
	const char * value = std::getenv(name);
	return value ? std::string(value) : std::string();
#endif
}
