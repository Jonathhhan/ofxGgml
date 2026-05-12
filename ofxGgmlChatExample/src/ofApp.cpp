#include "ofApp.h"

#include "imgui_stdlib.h"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string_view>
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

std::string toLowerAscii(std::string value) {
	std::transform(
		value.begin(),
		value.end(),
		value.begin(),
		[](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
	return value;
}

std::string trimText(const std::string & text) {
	std::size_t first = 0;
	while (first < text.size() &&
		std::isspace(static_cast<unsigned char>(text[first]))) {
		++first;
	}
	std::size_t last = text.size();
	while (last > first &&
		std::isspace(static_cast<unsigned char>(text[last - 1]))) {
		--last;
	}
	std::string normalized = text.substr(first, last - first);
	if (normalized.size() >= 2 && normalized.front() == '"' && normalized.back() == '"') {
		normalized = normalized.substr(1, normalized.size() - 2);
	}
	return normalized;
}

bool isLikelyTextModel(const std::string & modelPath) {
	const std::string modelName = toLowerAscii(trimText(modelPath));
	if (modelName.empty()) {
		return false;
	}
	const std::string_view nonTextHints[] = {
		"embed",
		"embedding",
		"bge",
		"e5",
		"nomic",
		"gte",
		"instructor",
		"text-embedding",
		"sentence-transformers",
		"sentence_transformers",
		"jina",
		"intfloat",
		"all-minilm",
		"mpnet"
	};
	for (const auto & hint : nonTextHints) {
		if (modelName.find(hint) != std::string::npos) {
			return false;
		}
	}
	return true;
}

void logTextModelHint(const std::string & modelPath, bool & warningLogged) {
	if (modelPath.empty() || isLikelyTextModel(modelPath)) {
		warningLogged = false;
		return;
	}
	if (warningLogged) {
		return;
	}
	warningLogged = true;
	ofLogWarning("ofxGgmlChatExample")
		<< "Model looks like embedding GGUF: " << modelPath
		<< "\nHint: use a text-oriented GGUF in llama-server/cli for chat text output.";
}

void failIfTextModelInvalid(
	const std::string & modelPath,
	bool strictTextModel,
	std::string & errorOut,
	bool & warningLogged) {
	if (modelPath.empty() || isLikelyTextModel(modelPath)) {
		warningLogged = false;
		return;
	}
	if (!strictTextModel) {
		logTextModelHint(modelPath, warningLogged);
		return;
	}
	warningLogged = false;
	errorOut = "Strict text model mode rejected non-text model: " + modelPath + "\n"
		"Pass OFXGGML_TEXT_MODEL (or -Model) with a text-oriented GGUF, "
		"or set OFXGGML_TEXT_STRICT_MODEL=0.";
}

std::string toString(const std::filesystem::path & path) {
	return path.lexically_normal().string();
}

std::string filenameForDisplay(const std::string & path) {
	const std::filesystem::path filePath(path);
	const std::string filename = filePath.filename().string();
	return filename.empty() ? path : filename;
}

ImVec2 fitWindowSize(float preferredWidth, float preferredHeight) {
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	const float availableWidth = std::max(360.0f, display.x - 32.0f);
	const float availableHeight = std::max(320.0f, display.y - 32.0f);
	return ImVec2(
		std::min(preferredWidth, availableWidth),
		std::min(preferredHeight, availableHeight));
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
		for (int depth = 0; depth < 6 && !parent.empty(); ++depth) {
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

std::vector<std::string> findFilesByExtension(
	const std::vector<std::filesystem::path> & roots,
	const std::vector<std::filesystem::path> & relativeDirectories,
	const std::string & extension) {
	std::vector<std::string> files;
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
					files.push_back(toString(entry.path()));
				}
			}
		}
	}
	std::sort(files.begin(), files.end());
	files.erase(std::unique(files.begin(), files.end()), files.end());
	return files;
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
	const auto models = findFilesByExtension(
		searchRoots(),
		{
			"",
			"data",
			"data/models",
			"models",
			"../models",
			"ofxGgmlChatExample/bin/data",
			"ofxGgmlChatExample/bin/data/models",
			"ofxGgmlChatExample/models",
			"ofxGgmlTextExample/bin/data",
			"ofxGgmlTextExample/bin/data/models",
			"ofxGgmlTextExample/models"
		},
		".gguf");
	return models.empty() ? std::string() : models.front();
}

std::vector<std::string> discoverTextModels() {
	return findFilesByExtension(
		searchRoots(),
		{
			"",
			"data",
			"data/models",
			"models",
			"../models",
			"ofxGgmlChatExample/bin/data",
			"ofxGgmlChatExample/bin/data/models",
			"ofxGgmlChatExample/models",
			"ofxGgmlTextExample/bin/data",
			"ofxGgmlTextExample/bin/data/models",
			"ofxGgmlTextExample/models"
		},
		".gguf");
}

} // namespace

void ofApp::setup() {
	ofSetWindowTitle("ofxGgml chat example");
	ofSetFrameRate(60);
	ofBackground(12);
	gui.setup(nullptr, false);

	settings.executablePath = normalizeEnvPath(envValue("OFXGGML_LLAMA_CLI"));
	settings.serverUrl = normalizeEnvPath(envValue("OFXGGML_TEXT_SERVER_URL"));
	settings.serverModel = normalizeEnvPath(envValue("OFXGGML_TEXT_SERVER_MODEL"));
	if (settings.serverUrl.empty()) {
		settings.serverUrl = "http://127.0.0.1:8080";
	}
	settings.useServerBackend = true;
	settings.maxTokens = 256;
	settings.temperature = 0.7f;
	settings.topP = 0.95f;
	settings.gpuLayers = -1;
	settings.contextSize = 4096;

	modelPath = normalizeEnvPath(envValue("OFXGGML_TEXT_MODEL"));
	autoConfigureTextBackend(settings, modelPath);
	refreshModelChoices();
	const std::string backend = normalizeEnvPath(envValue("OFXGGML_TEXT_BACKEND"));
	if (backend == "cli") {
		settings.useServerBackend = false;
	}
	strictTextModel = isTruthyEnvValue("OFXGGML_TEXT_STRICT_MODEL");
	configureGenerator();
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		if (strictTextModel) {
			if (!modelPath.empty()) {
				std::string validationError;
				failIfTextModelInvalid(modelPath, strictTextModel, validationError, textModelWarningLogged);
				if (!validationError.empty()) {
					status = "chat error: " + validationError;
				}
			} else {
				std::string validationError;
				failIfTextModelInvalid(settings.serverModel, strictTextModel, validationError, textModelWarningLogged);
				if (!validationError.empty()) {
					status = "chat error: " + validationError;
				}
			}
		}
	}

	const std::string defaultSystem =
		"You are a concise local assistant running inside an openFrameworks example.";
	std::copy(defaultSystem.begin(), defaultSystem.end(), systemBuffer.begin());
	status = "ready";
}

void ofApp::draw() {
	bool shouldSend = false;
	bool shouldCancel = false;
	bool shouldClear = false;
	std::vector<ChatEntry> chatSnapshot;
	std::string statusSnapshot;
	std::string backendSnapshot;
	std::string serverUrlSnapshot;
	std::string serverModelSnapshot;
	std::string executableSnapshot;
	std::string modelPathSnapshot;
	std::vector<std::string> modelChoicesSnapshot;
	bool runningSnapshot = false;
	bool useServer = false;
	int selectedModelIndexSnapshot = -1;
	int maxTokens = 0;
	float temperature = 0.0f;
	float topP = 0.0f;
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		chatSnapshot = chat;
		statusSnapshot = status;
		useServer = settings.useServerBackend;
		backendSnapshot = settings.useServerBackend ? "llama-server" : "llama-cli";
		serverUrlSnapshot = settings.serverUrl.empty() ? "(unset)" : settings.serverUrl;
		serverModelSnapshot = settings.serverModel.empty() ? "(auto)" : settings.serverModel;
		executableSnapshot = settings.executablePath.empty() ? "(optional)" : settings.executablePath;
		modelPathSnapshot = modelPath.empty() ? "(server-managed)" : modelPath;
		modelChoicesSnapshot = modelChoices;
		selectedModelIndexSnapshot = selectedModelIndex;
		maxTokens = settings.maxTokens;
		temperature = settings.temperature;
		topP = settings.topP;
		runningSnapshot = running;
	}

	ofBackground(12);
	gui.begin();
	ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_Once);
	ImGui::SetNextWindowSize(fitWindowSize(1040.0f, 640.0f), ImGuiCond_Once);
	if (ImGui::Begin("ofxGgml Chat Example")) {
		ImGui::TextColored(
			runningSnapshot ? ImVec4(0.45f, 0.75f, 1.0f, 1.0f) : ImVec4(0.70f, 0.92f, 0.70f, 1.0f),
			"%s",
			statusSnapshot.c_str());
		ImGui::SameLine();
		ImGui::TextDisabled("Backend: %s", backendSnapshot.c_str());

		ImGui::Separator();
		if (runningSnapshot) {
			ImGui::BeginDisabled();
		}
		if (ImGui::Checkbox("Use llama-server", &useServer)) {
			std::lock_guard<std::mutex> lock(stateMutex);
			if (!running) {
				settings.useServerBackend = useServer;
				configureGenerator();
			}
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		if (ImGui::SliderInt("Max tokens", &maxTokens, 16, 1024)) {
			std::lock_guard<std::mutex> lock(stateMutex);
			settings.maxTokens = maxTokens;
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::SliderFloat("Temp", &temperature, 0.0f, 1.5f, "%.2f")) {
			std::lock_guard<std::mutex> lock(stateMutex);
			settings.temperature = temperature;
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::SliderFloat("Top-p", &topP, 0.1f, 1.0f, "%.2f")) {
			std::lock_guard<std::mutex> lock(stateMutex);
			settings.topP = topP;
		}
		if (runningSnapshot) {
			ImGui::EndDisabled();
		}

		if (ImGui::CollapsingHeader("Runtime", ImGuiTreeNodeFlags_DefaultOpen)) {
			std::string serverUrlEdit = serverUrlSnapshot == "(unset)" ? std::string() : serverUrlSnapshot;
			std::string serverModelEdit = serverModelSnapshot == "(auto)" ? std::string() : serverModelSnapshot;
			std::string modelPathEdit = modelPathSnapshot == "(server-managed)" ? std::string() : modelPathSnapshot;
			if (runningSnapshot) {
				ImGui::BeginDisabled();
			}
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText("Server URL", &serverUrlEdit)) {
				std::lock_guard<std::mutex> lock(stateMutex);
				settings.serverUrl = normalizeEnvPath(serverUrlEdit);
				if (settings.useServerBackend) {
					configureGenerator();
				}
			}
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText("Server model", &serverModelEdit)) {
				std::lock_guard<std::mutex> lock(stateMutex);
				settings.serverModel = normalizeEnvPath(serverModelEdit);
				if (strictTextModel) {
					std::string validationError;
					failIfTextModelInvalid(settings.serverModel, strictTextModel, validationError, textModelWarningLogged);
					if (!validationError.empty()) {
						status = "chat error: " + validationError;
					}
				} else {
					logTextModelHint(settings.serverModel, textModelWarningLogged);
				}
			}
			const std::string preview = selectedModelIndexSnapshot >= 0 &&
				selectedModelIndexSnapshot < static_cast<int>(modelChoicesSnapshot.size())
				? filenameForDisplay(modelChoicesSnapshot[static_cast<std::size_t>(selectedModelIndexSnapshot)])
				: "(custom / server-managed)";
			if (ImGui::BeginCombo("Local GGUF", preview.c_str())) {
				for (std::size_t i = 0; i < modelChoicesSnapshot.size(); ++i) {
					const bool selected = static_cast<int>(i) == selectedModelIndexSnapshot;
					const std::string label = filenameForDisplay(modelChoicesSnapshot[i]);
					if (ImGui::Selectable(label.c_str(), selected)) {
						std::lock_guard<std::mutex> lock(stateMutex);
						selectedModelIndex = static_cast<int>(i);
						modelPath = modelChoices[static_cast<std::size_t>(selectedModelIndex)];
					}
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("%s", modelChoicesSnapshot[i].c_str());
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText("Local model path", &modelPathEdit)) {
				std::lock_guard<std::mutex> lock(stateMutex);
				modelPath = normalizeEnvPath(modelPathEdit);
				if (strictTextModel) {
					std::string validationError;
					failIfTextModelInvalid(modelPath, strictTextModel, validationError, textModelWarningLogged);
					if (!validationError.empty()) {
						status = "chat error: " + validationError;
					}
				} else {
					logTextModelHint(modelPath, textModelWarningLogged);
				}
				selectedModelIndex = -1;
				for (std::size_t i = 0; i < modelChoices.size(); ++i) {
					if (modelChoices[i] == modelPath) {
						selectedModelIndex = static_cast<int>(i);
						break;
					}
				}
			}
			if (ImGui::Button("Refresh models", ImVec2(120.0f, 0.0f))) {
				refreshModelChoices();
			}
			if (runningSnapshot) {
				ImGui::EndDisabled();
			}
			ImGui::TextWrapped(
				"Server: %s | Transport: %s | Local model: %s",
				useServer ? "configured" : "off",
				useServer ? "streaming" : "CLI",
				modelPathSnapshot.empty() || modelPathSnapshot == "(server-managed)"
					? "server-managed"
					: (fileExists(modelPathSnapshot) ? "found" : "missing"));
			ImGui::TextWrapped("CLI: %s", executableSnapshot.c_str());
			ImGui::Text("Strict text filtering: %s",
				strictTextModel ? "enabled (reject embedding GGUF patterns)" : "disabled");
		}

		ImGui::TextUnformatted("System");
		if (runningSnapshot) {
			ImGui::BeginDisabled();
		}
		ImGui::InputTextMultiline(
			"##system",
			systemBuffer.data(),
			systemBuffer.size(),
			ImVec2(-1.0f, 54.0f));
		if (runningSnapshot) {
			ImGui::EndDisabled();
		}

		ImGui::BeginChild("chat-history", ImVec2(0.0f, -148.0f), true);
		if (chatSnapshot.empty()) {
			ImGui::TextDisabled("No messages yet.");
		}
		for (const auto & entry : chatSnapshot) {
			const ImVec4 color = entry.role == ofxGgmlTextRole::User
				? ImVec4(0.65f, 0.82f, 1.0f, 1.0f)
				: ImVec4(0.78f, 0.92f, 0.72f, 1.0f);
			ImGui::TextColored(color, "%s", roleName(entry.role));
			ImGui::TextWrapped("%s", entry.content.empty() ? "..." : entry.content.c_str());
			ImGui::Spacing();
		}
		if (scrollToBottom) {
			ImGui::SetScrollHereY(1.0f);
			scrollToBottom = false;
		}
		ImGui::EndChild();

		ImGui::InputTextMultiline(
			"##prompt",
			promptBuffer.data(),
			promptBuffer.size(),
			ImVec2(-1.0f, 74.0f),
			ImGuiInputTextFlags_AllowTabInput);
		const bool ctrlEnter =
			ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
			ImGui::GetIO().KeyCtrl &&
			ImGui::IsKeyPressed(ImGuiKey_Enter);
		if (ImGui::Button("Send", ImVec2(72.0f, 0.0f)) || ctrlEnter) {
			shouldSend = true;
		}
		ImGui::SameLine();
		if (!runningSnapshot) {
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.45f);
		}
		if (ImGui::Button("Cancel", ImVec2(72.0f, 0.0f)) && runningSnapshot) {
			shouldCancel = true;
		}
		if (!runningSnapshot) {
			ImGui::PopStyleVar();
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear", ImVec2(72.0f, 0.0f))) {
			shouldClear = true;
		}
	}
	ImGui::End();
	gui.end();
	gui.draw();

	if (shouldSend) {
		sendPrompt();
	}
	if (shouldCancel) {
		requestCancel();
	}
	if (shouldClear) {
		clearChat();
	}
}

void ofApp::keyPressed(int key) {
	if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard) {
		return;
	}
	if (key == OF_KEY_RETURN &&
		(ofGetKeyPressed(OF_KEY_CONTROL) || ofGetKeyPressed(OF_KEY_COMMAND))) {
		sendPrompt();
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

void ofApp::sendPrompt() {
	const std::string prompt = trimCopy(promptBuffer.data());
	if (prompt.empty()) {
		std::lock_guard<std::mutex> lock(stateMutex);
		status = "type a message first";
		return;
	}

	{
		std::lock_guard<std::mutex> lock(stateMutex);
		if (running) {
			status = "chat request is already running";
			return;
		}
	}
	if (worker.joinable()) {
		worker.join();
	}

	{
		std::lock_guard<std::mutex> lock(stateMutex);
		chat.push_back({ ofxGgmlTextRole::User, prompt });
		chat.push_back({ ofxGgmlTextRole::Assistant, {} });
		pendingAssistantIndex = chat.size() - 1;
		status = "checking chat backend configuration...";
		running = true;
		cancelRequested = false;
		scrollToBottom = true;
	}
	std::fill(promptBuffer.begin(), promptBuffer.end(), '\0');
	worker = std::thread(&ofApp::runChatWorker, this);
}

void ofApp::requestCancel() {
	std::lock_guard<std::mutex> lock(stateMutex);
	cancelRequested = true;
	if (running) {
		status = "cancelling...";
	}
}

void ofApp::clearChat() {
	std::lock_guard<std::mutex> lock(stateMutex);
	if (running) {
		status = "cancel the running request before clearing";
		return;
	}
	chat.clear();
	status = "chat cleared";
	scrollToBottom = true;
}

void ofApp::runChatWorker() {
	auto fail = [this](std::string message) {
		std::lock_guard<std::mutex> lock(stateMutex);
		if (pendingAssistantIndex < chat.size() && chat[pendingAssistantIndex].content.empty()) {
			chat[pendingAssistantIndex].content = message;
		}
		status = std::move(message);
		running = false;
		scrollToBottom = true;
	};

	ofxGgmlTextGenerationSettings requestSettings;
	std::string requestModelPath;
	std::string systemPrompt;
	std::vector<ofxGgmlTextMessage> messages;
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		requestSettings = settings;
		requestModelPath = modelPath;
		systemPrompt = trimCopy(systemBuffer.data());
		for (std::size_t i = 0; i < chat.size(); ++i) {
			if (i == pendingAssistantIndex && chat[i].content.empty()) {
				continue;
			}
			messages.push_back({ chat[i].role, chat[i].content });
		}
	}

	if (requestSettings.useServerBackend) {
		if (requestSettings.serverUrl.empty()) {
			fail("No llama-server URL configured. Set OFXGGML_TEXT_SERVER_URL.");
			return;
		}
		if (strictTextModel) {
			std::string validationError;
			failIfTextModelInvalid(requestSettings.serverModel, strictTextModel, validationError, textModelWarningLogged);
			if (!validationError.empty()) {
				fail("chat error: " + validationError);
				return;
			}
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
		if (strictTextModel) {
			std::string validationError;
			failIfTextModelInvalid(requestModelPath, strictTextModel, validationError, textModelWarningLogged);
			if (!validationError.empty()) {
				fail("chat error: " + validationError);
				return;
			}
		}
	}

	{
		std::lock_guard<std::mutex> lock(stateMutex);
		status = requestSettings.useServerBackend
			? "requesting llama-server..."
			: "running llama.cpp CLI...";
	}

	ofxGgmlTextRequest request;
	request.modelPath = requestModelPath;
	request.systemPrompt = systemPrompt;
	request.messages = std::move(messages);
	request.settings = requestSettings;
	request.settings.gpuLayers = -1;
	request.settings.stream = requestSettings.useServerBackend;

	auto appendServerTextChunk = [this](const std::string & chunk) {
		if (cancelRequested) {
			return false;
		}
		if (chunk.empty()) {
			return true;
		}
		appendAssistantText(chunk);
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

	std::string consolePrompt;
	for (auto it = request.messages.rbegin(); it != request.messages.rend(); ++it) {
		if (it->role == ofxGgmlTextRole::User) {
			consolePrompt = it->content;
			break;
		}
	}
	ofLogNotice("ofxGgmlChatExample") << "prompt\n" << consolePrompt;
	auto result = generator.generate(request, onTextChunk);
	if (result) {
		ofLogNotice("ofxGgmlChatExample") << "output\n" << result.text;
	} else {
		ofLogError("ofxGgmlChatExample") << "output error\n" << result.error;
	}

	std::lock_guard<std::mutex> lock(stateMutex);
	if (pendingAssistantIndex < chat.size()) {
		if (cancelRequested) {
			chat.erase(chat.begin() + static_cast<std::ptrdiff_t>(pendingAssistantIndex));
			status = "cancelled";
		} else if (result) {
			chat[pendingAssistantIndex].content = result.text;
			status = "complete via " + result.backendName + " in " +
				std::to_string(static_cast<int>(result.elapsedMs)) + " ms";
		} else {
			chat[pendingAssistantIndex].content = "Error: " + result.error;
			status = "chat error: " + result.error;
		}
	}
	running = false;
	scrollToBottom = true;
}

void ofApp::configureGenerator() {
	if (settings.useServerBackend) {
		generator.setBackend(std::make_shared<ofxGgmlLlamaServerTextBackend>(settings.serverUrl));
	} else {
		generator.setBackend(std::make_shared<ofxGgmlLlamaCliTextBackend>());
	}
}

void ofApp::refreshModelChoices() {
	std::vector<std::string> discovered = discoverTextModels();
	std::lock_guard<std::mutex> lock(stateMutex);
	modelChoices = std::move(discovered);
	selectedModelIndex = -1;
	for (std::size_t i = 0; i < modelChoices.size(); ++i) {
		if (modelChoices[i] == modelPath) {
			selectedModelIndex = static_cast<int>(i);
			break;
		}
	}
	if (modelPath.empty() && !modelChoices.empty()) {
		selectedModelIndex = 0;
		modelPath = modelChoices.front();
	}
}

void ofApp::appendAssistantText(const std::string & text) {
	std::lock_guard<std::mutex> lock(stateMutex);
	if (pendingAssistantIndex < chat.size()) {
		chat[pendingAssistantIndex].content += text;
	}
	status = "receiving chat output...";
	scrollToBottom = true;
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
	return trimCopy(path);
}

bool ofApp::fileExists(const std::string & path) {
	return !path.empty() && ofFile::doesFileExist(path, false);
}

bool ofApp::isTruthyEnvValue(const char * name, bool defaultValue) {
	const auto raw = normalizeEnvPath(envValue(name));
	if (raw.empty()) {
		return defaultValue;
	}
	const auto lower = toLowerAscii(trimText(raw));
	static const std::string trueValues[] = { "1", "true", "yes", "on", "y", "enabled" };
	for (const auto & trueValue : trueValues) {
		if (lower == trueValue) {
			return true;
		}
	}
	return false;
}

std::string ofApp::trimCopy(const std::string & value) {
	std::size_t first = 0;
	while (first < value.size() &&
		std::isspace(static_cast<unsigned char>(value[first]))) {
		++first;
	}
	std::size_t last = value.size();
	while (last > first &&
		std::isspace(static_cast<unsigned char>(value[last - 1]))) {
		--last;
	}
	std::string normalized = value.substr(first, last - first);
	if (normalized.size() >= 2 && normalized.front() == '"' && normalized.back() == '"') {
		normalized = normalized.substr(1, normalized.size() - 2);
	}
	return normalized;
}

const char * ofApp::roleName(ofxGgmlTextRole role) {
	switch (role) {
	case ofxGgmlTextRole::System: return "System";
	case ofxGgmlTextRole::User: return "You";
	case ofxGgmlTextRole::Assistant: return "Assistant";
	}
	return "Message";
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
