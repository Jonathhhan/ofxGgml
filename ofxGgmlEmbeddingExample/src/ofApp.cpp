#include "ofApp.h"

#include "imgui_stdlib.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <string_view>
#include <memory>
#include <sstream>
#include <utility>

namespace {

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
	return text.substr(first, last - first);
}

ImVec2 fitWindowSize(float preferredWidth, float preferredHeight) {
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	const float availableWidth = std::max(420.0f, display.x - 32.0f);
	const float availableHeight = std::max(360.0f, display.y - 32.0f);
	return ImVec2(
		std::min(preferredWidth, availableWidth),
		std::min(preferredHeight, availableHeight));
}

std::string toLowerAscii(const std::string & value) {
	std::string lowered = value;
	for (char & c : lowered) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return lowered;
}

bool isLikelyEmbeddingEnvFlag(const std::string & value) {
	const std::string normalized = toLowerAscii(trimText(value));
	if (normalized.empty()) {
		return false;
	}
	static const std::string trueValues[] = { "1", "true", "yes", "on", "y", "enabled" };
	for (const auto & trueValue : trueValues) {
		if (normalized == trueValue) {
			return true;
		}
	}
	return false;
}

bool isLikelyEmbeddingModel(const std::string & modelPath) {
	const std::string normalized = trimText(modelPath);
	if (normalized.empty()) {
		return false;
	}
	const std::string modelName = toLowerAscii(normalized);
	const std::string_view hints[] = {
		"embed", "embedding", "bge", "e5", "nomic", "instructor", "text-embedding",
		"gte", "text2vec", "sentence-transformers", "intfloat", "all-minilm", "mpnet"};
	for (const auto & hint : hints) {
		if (modelName.find(hint) != std::string::npos) {
			return true;
		}
	}
	return false;
}

void logEmbeddingModelHint(const std::string & modelPath, bool & warningLogged) {
	if (modelPath.empty() || isLikelyEmbeddingModel(modelPath)) {
		warningLogged = false;
		return;
	}
	if (warningLogged) {
		return;
	}
	warningLogged = true;
	ofLogWarning("example-emb")
		<< "Model does not look like an embedding GGUF: " << modelPath
		<< "\nHint: pass an embedding model via OFXGGML_EMBEDDING_MODEL or -ModelPath "
			<< "(e.g. BGE / sentence-transformers family) to avoid degraded outputs.";
}

void failIfEmbeddingModelInvalid(
	const std::string & modelPath,
	bool strictEmbeddingModel,
	std::string & errorOut,
	bool & warningLogged
) {
	if (isLikelyEmbeddingModel(modelPath)) {
		warningLogged = false;
		return;
	}
	if (!strictEmbeddingModel) {
		logEmbeddingModelHint(modelPath, warningLogged);
		return;
	}
	warningLogged = false;
	if (modelPath.empty()) {
		errorOut =
			"Strict embedding mode requires a recognized embedding model path (OFXGGML_EMBEDDING_SERVER_MODEL).";
	} else {
		errorOut = "Strict embedding mode rejected non-embedding model: " + modelPath +
			"\nHint: set OFXGGML_EMBEDDING_SERVER_MODEL (or -Model when launching) to an embedding GGUF.";
	}
}

} // namespace

void ofApp::setup() {
	ofSetWindowTitle("ofxGgml embedding example");
	ofBackground(12);
	gui.setup(nullptr, false);

	settings.serverUrl = normalizeEnvText(envValue("OFXGGML_EMBEDDING_SERVER_URL"));
	settings.serverModel = normalizeEnvText(envValue("OFXGGML_EMBEDDING_SERVER_MODEL"));
	if (settings.serverUrl.empty()) {
		settings.serverUrl = "http://127.0.0.1:8081";
	}
	strictEmbeddingModel = isTruthyEnvValue("OFXGGML_EMBEDDING_STRICT_MODEL");
	configureGenerator();

	inputA = "openFrameworks local inference";
	inputB = "interactive creative coding with local AI";
	inputAEdit = inputA;
	inputBEdit = inputB;
	inputAEdit.reserve(4096);
	inputBEdit.reserve(4096);

	std::lock_guard<std::mutex> lock(stateMutex);
	status = "ready";
	if (strictEmbeddingModel) {
		std::string initError;
		failIfEmbeddingModelInvalid(
			settings.serverModel,
			strictEmbeddingModel,
			initError,
			embeddingModelWarningLogged);
		if (!initError.empty()) {
			error = initError;
			status = "embedding error";
		}
	} else {
		logEmbeddingModelHint(settings.serverModel, embeddingModelWarningLogged);
	}
}

void ofApp::draw() {
	std::string statusSnapshot;
	std::string errorSnapshot;
	std::string serverUrlSnapshot;
	std::string serverModelSnapshot;
	std::vector<std::vector<float>> embeddingsSnapshot;
	float similaritySnapshot = 0.0f;
	bool hasSimilaritySnapshot = false;
	bool runningSnapshot = false;
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		statusSnapshot = status;
		errorSnapshot = error;
		serverUrlSnapshot = settings.serverUrl;
		serverModelSnapshot = settings.serverModel;
		embeddingsSnapshot = embeddings;
		similaritySnapshot = similarity;
		hasSimilaritySnapshot = hasSimilarity;
		runningSnapshot = running;
	}

	bool shouldRun = false;

	ofBackground(12);
	gui.begin();
	ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_Once);
	ImGui::SetNextWindowSize(fitWindowSize(900.0f, 480.0f), ImGuiCond_Once);
	if (ImGui::Begin("ofxGgml Embedding Example")) {
		if (runningSnapshot) {
			ImGui::BeginDisabled();
		}
		if (ImGui::Button("Run", ImVec2(72.0f, 0.0f))) {
			shouldRun = true;
		}
		if (runningSnapshot) {
			ImGui::EndDisabled();
		}

		ImGui::Separator();
		const ImVec4 statusColor = runningSnapshot
			? ImVec4(0.45f, 0.75f, 1.0f, 1.0f)
			: ImVec4(0.70f, 0.92f, 0.70f, 1.0f);
		ImGui::TextColored(statusColor, "%s", statusSnapshot.c_str());
		ImGui::Text("State: %s", runningSnapshot ? "running" : "idle");
		ImGui::Text("Backend: llama-server embeddings");

		if (ImGui::CollapsingHeader("Runtime", ImGuiTreeNodeFlags_DefaultOpen)) {
			std::string serverUrlEdit = serverUrlSnapshot;
			std::string serverModelEdit = serverModelSnapshot;
			if (runningSnapshot) {
				ImGui::BeginDisabled();
			}
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText("Server URL", &serverUrlEdit)) {
				std::lock_guard<std::mutex> lock(stateMutex);
				settings.serverUrl = normalizeEnvText(serverUrlEdit);
				configureGenerator();
			}
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText("Server model", &serverModelEdit)) {
				std::lock_guard<std::mutex> lock(stateMutex);
				settings.serverModel = normalizeEnvText(serverModelEdit);
				if (strictEmbeddingModel) {
					std::string validationError;
					failIfEmbeddingModelInvalid(
						settings.serverModel,
						strictEmbeddingModel,
						validationError,
						embeddingModelWarningLogged);
					if (!validationError.empty()) {
						error = validationError;
						status = "embedding error";
					} else {
						error.clear();
						status = "ready";
					}
				} else {
					logEmbeddingModelHint(settings.serverModel, embeddingModelWarningLogged);
				}
			}
			if (runningSnapshot) {
				ImGui::EndDisabled();
			}
			ImGui::TextWrapped(
				"Use scripts/start-llama-server.bat -Embeddings to run a dedicated embedding server.");
			ImGui::Text("Strict model filtering: %s",
				strictEmbeddingModel ? "enabled (reject non-embedding GGUF)" : "disabled");
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("Input A");
		ImGui::Separator();
		ImGui::InputTextMultiline(
			"##embedding-input-a",
			&inputAEdit,
			ImVec2(0.0f, 64.0f),
			runningSnapshot ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None);
		ImGui::Spacing();
		ImGui::TextUnformatted("Input B");
		ImGui::Separator();
		ImGui::InputTextMultiline(
			"##embedding-input-b",
			&inputBEdit,
			ImVec2(0.0f, 64.0f),
			runningSnapshot ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None);

		ImGui::Spacing();
		ImGui::TextUnformatted("Embeddings");
		ImGui::Separator();
		ImGui::BeginChild("ofxGgmlEmbeddingOutput", ImVec2(0.0f, 164.0f), true);
		if (!errorSnapshot.empty()) {
			ImGui::TextWrapped("Error: %s", errorSnapshot.c_str());
		} else if (embeddingsSnapshot.empty()) {
			ImGui::TextDisabled("(none)");
		} else {
			if (hasSimilaritySnapshot) {
				ImGui::Text("Cosine similarity: %.4f", similaritySnapshot);
				ImGui::Separator();
			}
			for (std::size_t i = 0; i < embeddingsSnapshot.size(); ++i) {
				ImGui::Text(
					"%c dimension: %d",
					static_cast<char>('A' + static_cast<int>(i)),
					static_cast<int>(embeddingsSnapshot[i].size()));
				const std::string preview = embeddingPreview(embeddingsSnapshot[i]);
				ImGui::TextWrapped("%s", preview.c_str());
				if (i + 1 < embeddingsSnapshot.size()) {
					ImGui::Spacing();
				}
			}
		}
		ImGui::EndChild();
	}
	ImGui::End();
	gui.end();
	gui.draw();

	if (shouldRun) {
		startEmbedding();
	}
}

void ofApp::keyPressed(int key) {
	if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard) {
		return;
	}
	if (key == 'r' || key == 'R') {
		startEmbedding();
	}
}

void ofApp::exit() {
	if (worker.joinable()) {
		worker.join();
	}
}

void ofApp::startEmbedding() {
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		if (running) {
			status = "embedding request is already running";
			return;
		}
	}

	if (worker.joinable()) {
		worker.join();
	}

	{
		std::lock_guard<std::mutex> lock(stateMutex);
		inputA = trimText(inputAEdit);
		inputB = trimText(inputBEdit);
		if (inputA.empty() || inputB.empty()) {
			status = "type both input texts first";
			error.clear();
			embeddings.clear();
			hasSimilarity = false;
			return;
		}
		status = "requesting embedding...";
		error.clear();
		embeddings.clear();
		hasSimilarity = false;
		running = true;
	}

	worker = std::thread(&ofApp::runEmbeddingWorker, this);
}

void ofApp::runEmbeddingWorker() {
	ofxGgmlEmbeddingSettings requestSettings;
	std::string requestInputA;
	std::string requestInputB;
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		requestSettings = settings;
		requestInputA = inputA;
		requestInputB = inputB;
		std::string validationError;
		failIfEmbeddingModelInvalid(
			requestSettings.serverModel,
			strictEmbeddingModel,
			validationError,
			embeddingModelWarningLogged);
		if (!validationError.empty()) {
			status = "embedding error";
			error = validationError;
			running = false;
			ofLogError("example-emb") << "request blocked\n" << validationError;
			return;
		}
	}

	if (requestSettings.serverUrl.empty()) {
		std::lock_guard<std::mutex> lock(stateMutex);
		status = "embedding error";
		error = "No embedding server URL configured.";
		running = false;
		return;
	}

	ofxGgmlEmbeddingRequest request;
	request.inputs = { requestInputA, requestInputB };
	request.settings = requestSettings;

	ofLogNotice("ofxGgmlEmbeddingExample")
		<< "input A\n" << requestInputA
		<< "\ninput B\n" << requestInputB;

	const auto result = generator.embed(request);

	if (result) {
		std::ostringstream log;
		log << "output\n";
		for (std::size_t i = 0; i < result.embeddings.size(); ++i) {
			log << static_cast<char>('A' + static_cast<int>(i))
				<< " dimension: " << result.embeddings[i].size() << "\n"
				<< embeddingPreview(result.embeddings[i]) << "\n";
		}
		if (result.embeddings.size() >= 2) {
			log << "cosine similarity: "
				<< ofxGgmlEmbeddingUtils::cosineSimilarity(
					result.embeddings[0],
					result.embeddings[1])
				<< "\n";
		}
		ofLogNotice("ofxGgmlEmbeddingExample") << log.str();
	} else {
		ofLogError("ofxGgmlEmbeddingExample") << "output error\n" << result.error;
	}

	std::lock_guard<std::mutex> lock(stateMutex);
	if (result) {
		embeddings = result.embeddings;
		hasSimilarity = embeddings.size() >= 2;
		similarity = hasSimilarity
			? ofxGgmlEmbeddingUtils::cosineSimilarity(embeddings[0], embeddings[1])
			: 0.0f;
		error.clear();
		status = "complete via " + result.backendName + " in " +
			std::to_string(static_cast<int>(result.elapsedMs)) + " ms";
	} else {
		embeddings.clear();
		similarity = 0.0f;
		hasSimilarity = false;
		error = result.error;
		status = "embedding error";
	}
	running = false;
}

bool ofApp::isTruthyEnvValue(const char * name, bool defaultValue) {
	const auto raw = normalizeEnvText(envValue(name));
	if (raw.empty()) {
		return defaultValue;
	}
	return isLikelyEmbeddingEnvFlag(raw);
}

void ofApp::configureGenerator() {
	generator.setBackend(
		std::make_shared<ofxGgmlLlamaServerEmbeddingBackend>(settings.serverUrl));
}

std::string ofApp::normalizeEnvText(const std::string & text) {
	std::string normalized = trimText(text);
	if (normalized.size() >= 2 && normalized.front() == '"' && normalized.back() == '"') {
		normalized = normalized.substr(1, normalized.size() - 2);
	}
	return normalized;
}

std::string ofApp::embeddingPreview(const std::vector<float> & values) {
	std::ostringstream preview;
	preview << std::fixed << std::setprecision(5);
	const std::size_t count = std::min<std::size_t>(values.size(), 32);
	for (std::size_t i = 0; i < count; ++i) {
		if (i > 0) {
			preview << ", ";
		}
		preview << values[i];
	}
	if (values.size() > count) {
		preview << ", ...";
	}
	return preview.str();
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
