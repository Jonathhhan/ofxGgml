#include "support/ofxGgmlEasy.h"

#include "core/ofxGgmlCore.h"
#include "core/ofxGgmlMetrics.h"
#include "ofJson.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace {

template <typename T, typename Configure>
T & ensureOwned(
	std::unique_ptr<T> & ptr,
	Configure && configure) {
	if (!ptr) {
		ptr = std::make_unique<T>();
		configure(*ptr);
	}
	return *ptr;
}

ofxGgmlInferenceResult makeTextConfigError(const std::string & message) {
	ofxGgmlInferenceResult result;
	result.error = message;
	return result;
}

ofxGgmlChatAssistantResult makeChatConfigError(const std::string & message) {
	ofxGgmlChatAssistantResult result;
	result.inference.error = message;
	return result;
}

ofxGgmlTextAssistantResult makeTextAssistantConfigError(
	const std::string & label,
	const std::string & message) {
	ofxGgmlTextAssistantResult result;
	result.prepared.label = label;
	result.inference.error = message;
	return result;
}

ofxGgmlVisionResult makeVisionConfigError(const std::string & message) {
	ofxGgmlVisionResult result;
	result.error = message;
	return result;
}

ofxGgmlSpeechResult makeSpeechConfigError(const std::string & message) {
	ofxGgmlSpeechResult result;
	result.error = message;
	return result;
}

void applyInferenceExecutables(
	ofxGgmlInference & inference,
	const ofxGgmlEasyTextConfig & config) {
	inference.setCompletionExecutable(config.completionExecutable);
	inference.setEmbeddingExecutable(config.embeddingExecutable);
}

void applyCrawlerBackend(
	ofxGgmlWebCrawler & crawler,
	const ofxGgmlEasyCrawlerConfig & config) {
	crawler.setBackend(
		std::make_shared<ofxGgmlMojoWebCrawlerBackend>(config.executablePath));
}

void applySpeechBackend(
	ofxGgmlSpeechInference & speechInference,
	const ofxGgmlEasySpeechConfig & config) {
	if (config.preferServer && !config.serverUrl.empty()) {
		speechInference.setBackend(
			ofxGgmlSpeechInference::createWhisperServerBackend(
				config.serverUrl,
				config.serverModel));
		return;
	}
	speechInference.setBackend(
		ofxGgmlSpeechInference::createWhisperCliBackend(
			config.cliExecutable.empty()
				? "whisper-cli"
				: config.cliExecutable));
}

std::vector<std::filesystem::path> modelCatalogCandidates(const std::string & explicitPath) {
	if (!explicitPath.empty()) {
		return {std::filesystem::path(explicitPath)};
	}
	const std::filesystem::path cwd = std::filesystem::current_path();
	return {
		cwd / "scripts" / "model-catalog.json",
		cwd / ".." / "scripts" / "model-catalog.json",
		cwd / ".." / ".." / "scripts" / "model-catalog.json",
		cwd / ".." / ".." / ".." / "scripts" / "model-catalog.json"
	};
}

std::optional<std::pair<ofJson, std::filesystem::path>> loadModelCatalogJson(
	const std::string & catalogPath) {
	for (const auto & candidate : modelCatalogCandidates(catalogPath)) {
		std::ifstream in(candidate);
		if (!in) {
			continue;
		}
		const std::string text{
			std::istreambuf_iterator<char>(in),
			std::istreambuf_iterator<char>()};
		const ofJson parsed = ofJson::parse(text, nullptr, false);
		if (!parsed.is_discarded()) {
			return std::make_pair(parsed, std::filesystem::weakly_canonical(candidate));
		}
	}
	return std::nullopt;
}

std::string jsonStringOrEmpty(const ofJson & json, const char * key) {
	if (!json.contains(key) || !json[key].is_string()) {
		return "";
	}
	try {
		return json[key].get<std::string>();
	} catch (...) {
		return "";
	}
}

std::vector<ofxGgmlEasyModelPreset> parseModelPresets(const ofJson & root) {
	std::vector<ofxGgmlEasyModelPreset> presets;
	if (!root.contains("models") || !root["models"].is_array()) {
		return presets;
	}
	for (const auto & item : root["models"]) {
		ofxGgmlEasyModelPreset preset;
		if (item.contains("preset") && item["preset"].is_number_integer()) {
			preset.preset = item["preset"].get<int>();
		}
		preset.name = jsonStringOrEmpty(item, "name");
		preset.filename = jsonStringOrEmpty(item, "filename");
		preset.url = jsonStringOrEmpty(item, "url");
		preset.size = jsonStringOrEmpty(item, "size");
		preset.bestFor = jsonStringOrEmpty(item, "best_for");
		preset.sha256 = jsonStringOrEmpty(item, "sha256");
		if (item.contains("provenance") && item["provenance"].is_object()) {
			const auto & provenance = item["provenance"];
			preset.publisher = jsonStringOrEmpty(provenance, "publisher");
			preset.sourceType = jsonStringOrEmpty(provenance, "source_type");
			preset.sourceUrl = jsonStringOrEmpty(provenance, "source_url");
			preset.verificationStatus = jsonStringOrEmpty(provenance, "verification_status");
			preset.catalogUpdatedAt = jsonStringOrEmpty(provenance, "catalog_updated_at");
		}
		if (preset.catalogUpdatedAt.empty()) {
			preset.catalogUpdatedAt = jsonStringOrEmpty(root, "catalog_updated_at");
		}
		presets.push_back(std::move(preset));
	}
	return presets;
}

std::optional<int> lookupTaskDefault(
	const ofJson & root,
	const std::string & task) {
	if (task.empty() ||
		!root.contains("task_defaults") ||
		!root["task_defaults"].is_object() ||
		!root["task_defaults"].contains(task) ||
		!root["task_defaults"][task].is_number_integer()) {
		return std::nullopt;
	}
	try {
		return root["task_defaults"][task].get<int>();
	} catch (...) {
		return std::nullopt;
	}
}

std::optional<ofxGgmlEasyModelPreset> findPresetByNumber(
	const std::vector<ofxGgmlEasyModelPreset> & presets,
	int presetNumber) {
	for (const auto & preset : presets) {
		if (preset.preset == presetNumber) {
			return preset;
		}
	}
	return std::nullopt;
}

bool pathLooksExplicit(const std::string & path) {
	return path.find('/') != std::string::npos || path.find('\\') != std::string::npos;
}

std::string quoteShellArg(const std::string & text) {
	if (text.empty()) {
		return "\"\"";
	}
	std::string quoted = "\"";
	for (char ch : text) {
		if (ch == '"' || ch == '\\') {
			quoted.push_back('\\');
		}
		quoted.push_back(ch);
	}
	quoted.push_back('"');
	return quoted;
}

std::string configuredModelMetricKey(const ofxGgmlEasyTextConfig & config) {
	if (!config.modelPath.empty()) {
		return config.modelPath;
	}
	if (!config.serverModel.empty()) {
		return config.serverModel;
	}
	return "default";
}

std::string diagnosticSeverityLabel(ofxGgmlEasyDiagnosticSeverity severity) {
	switch (severity) {
		case ofxGgmlEasyDiagnosticSeverity::Info: return "info";
		case ofxGgmlEasyDiagnosticSeverity::Warning: return "warning";
		case ofxGgmlEasyDiagnosticSeverity::Degraded: return "degraded";
		case ofxGgmlEasyDiagnosticSeverity::Blocking: return "blocking";
	}
	return "unknown";
}

void addDiagnosticIssue(
	std::vector<ofxGgmlEasyDiagnosticIssue> & issues,
	ofxGgmlEasyDiagnosticSeverity severity,
	const std::string & component,
	const std::string & message) {
	issues.push_back({severity, component, message});
}

ofJson stringVectorToJson(const std::vector<std::string> & values) {
	ofJson array = ofJson::array();
	for (const auto & value : values) {
		array.push_back(value);
	}
	return array;
}

} // namespace

size_t ofxGgmlEasyDiagnosticsReport::countIssues(
	ofxGgmlEasyDiagnosticSeverity severity) const {
	size_t count = 0;
	for (const auto & issue : issues) {
		if (issue.severity == severity) {
			++count;
		}
	}
	return count;
}

std::string ofxGgmlEasyDiagnosticsReport::toJsonString() const {
	ofJson root;
	root["ready"] = ready;
	root["degraded"] = degraded;

	root["setup"]["ready"] = setup.ready;
	root["setup"]["catalogAvailable"] = setup.catalogAvailable;
	root["setup"]["modelPathExists"] = setup.modelPathExists;
	root["setup"]["prefersServer"] = setup.prefersServer;
	root["setup"]["serverConfigured"] = setup.serverConfigured;
	root["setup"]["resolvedCatalogPath"] = setup.resolvedCatalogPath;
	if (setup.configuredPreset) {
		root["setup"]["configuredPreset"] = {
			{"preset", setup.configuredPreset->preset},
			{"name", setup.configuredPreset->name},
			{"filename", setup.configuredPreset->filename},
			{"verificationStatus", setup.configuredPreset->verificationStatus}
		};
	}
	if (setup.recommendedPreset) {
		root["setup"]["recommendedPreset"] = {
			{"preset", setup.recommendedPreset->preset},
			{"name", setup.recommendedPreset->name},
			{"filename", setup.recommendedPreset->filename},
			{"verificationStatus", setup.recommendedPreset->verificationStatus}
		};
	}
	root["setup"]["errors"] = stringVectorToJson(setup.errors);
	root["setup"]["warnings"] = stringVectorToJson(setup.warnings);
	root["setup"]["recommendations"] = stringVectorToJson(setup.recommendations);

	root["health"]["textConfigured"] = health.textConfigured;
	root["health"]["localRuntimeAttached"] = health.localRuntimeAttached;
	root["health"]["localRuntimeReady"] = health.localRuntimeReady;
	root["health"]["serverExpected"] = health.serverExpected;
	root["health"]["averageLatencyMs"] = health.averageLatencyMs;
	root["health"]["minLatencyMs"] = health.minLatencyMs;
	root["health"]["maxLatencyMs"] = health.maxLatencyMs;
	root["health"]["averageTokensPerSecond"] = health.averageTokensPerSecond;
	root["health"]["retrievalCacheHitRate"] = health.retrievalCacheHitRate;
	root["health"]["tokenCountCacheHitRate"] = health.tokenCountCacheHitRate;
	root["health"]["warnings"] = stringVectorToJson(health.warnings);
	root["health"]["serverProbe"] = {
		{"reachable", health.serverProbe.reachable},
		{"healthOk", health.serverProbe.healthOk},
		{"modelsOk", health.serverProbe.modelsOk},
		{"baseUrl", health.serverProbe.baseUrl},
		{"activeModel", health.serverProbe.activeModel},
		{"error", health.serverProbe.error}
	};
	root["health"]["serverQueue"] = {
		{"available", health.serverQueue.available},
		{"queueLength", health.serverQueue.queueLength},
		{"processingCount", health.serverQueue.processingCount},
		{"completedCount", health.serverQueue.completedCount},
		{"failedCount", health.serverQueue.failedCount},
		{"serverUrl", health.serverQueue.serverUrl},
		{"error", health.serverQueue.error}
	};

	root["issues"] = ofJson::array();
	for (const auto & issue : issues) {
		root["issues"].push_back({
			{"severity", diagnosticSeverityLabel(issue.severity)},
			{"component", issue.component},
			{"message", issue.message}
		});
	}
	return root.dump(2);
}

ofxGgmlEasy::ofxGgmlEasy() = default;

void ofxGgmlEasy::configureText(const ofxGgmlEasyTextConfig & config) {
	m_textConfig = config;
	syncTextBackends();
}

void ofxGgmlEasy::syncTextBackends() {
	if (m_inference) {
		applyInferenceExecutables(*m_inference, m_textConfig);
	}
	if (m_citationSearch) {
		applyInferenceExecutables(m_citationSearch->getInference(), m_textConfig);
	}
	if (m_ragPipeline) {
		applyInferenceExecutables(m_ragPipeline->getInference(), m_textConfig);
	}
	if (m_chatAssistant) {
		m_chatAssistant->setCompletionExecutable(m_textConfig.completionExecutable);
	}
	if (m_textAssistant) {
		m_textAssistant->setCompletionExecutable(m_textConfig.completionExecutable);
	}
	if (m_codingAgent) {
		m_codingAgent->setCompletionExecutable(m_textConfig.completionExecutable);
		m_codingAgent->setEmbeddingExecutable(m_textConfig.embeddingExecutable);
	}
	if (m_mediaPromptGenerator) {
		m_mediaPromptGenerator->setCompletionExecutable(m_textConfig.completionExecutable);
	}
#if OFXGGML_ENABLE_COMPANION_WORKFLOWS
	if (m_musicGenerator) {
		m_musicGenerator->setCompletionExecutable(m_textConfig.completionExecutable);
	}
	if (m_milkDropGenerator) {
		m_milkDropGenerator->setCompletionExecutable(m_textConfig.completionExecutable);
	}
	if (m_videoEssayWorkflow) {
		m_videoEssayWorkflow->getTextAssistant().setCompletionExecutable(
			m_textConfig.completionExecutable);
		applyInferenceExecutables(
			m_videoEssayWorkflow->getCitationSearch().getInference(),
			m_textConfig);
	}
#endif
	if (m_longVideoPlanner) {
		m_longVideoPlanner->getTextAssistant().setCompletionExecutable(
			m_textConfig.completionExecutable);
	}
	if (m_conversationManager) {
		m_conversationManager->getInference().setCompletionExecutable(
			m_textConfig.completionExecutable);
	}
}

void ofxGgmlEasy::configureVision(const ofxGgmlEasyVisionConfig & config) {
	m_visionConfig = config;
}

void ofxGgmlEasy::configureSpeech(const ofxGgmlEasySpeechConfig & config) {
	m_speechConfig = config;
	syncSpeechBackend();
}

void ofxGgmlEasy::configureWebCrawler(const ofxGgmlEasyCrawlerConfig & config) {
	m_crawlerConfig = config;
	syncCrawlerBackend();
}

const ofxGgmlEasyTextConfig & ofxGgmlEasy::getTextConfig() const {
	return m_textConfig;
}

const ofxGgmlEasyVisionConfig & ofxGgmlEasy::getVisionConfig() const {
	return m_visionConfig;
}

const ofxGgmlEasySpeechConfig & ofxGgmlEasy::getSpeechConfig() const {
	return m_speechConfig;
}

const ofxGgmlEasyCrawlerConfig & ofxGgmlEasy::getWebCrawlerConfig() const {
	return m_crawlerConfig;
}

ofxGgmlInference & ofxGgmlEasy::getInference() {
	return ensureInference();
}

const ofxGgmlInference & ofxGgmlEasy::getInference() const {
	return ensureInference();
}

ofxGgmlChatAssistant & ofxGgmlEasy::getChatAssistant() {
	return ensureChatAssistant();
}

const ofxGgmlChatAssistant & ofxGgmlEasy::getChatAssistant() const {
	return ensureChatAssistant();
}

ofxGgmlTextAssistant & ofxGgmlEasy::getTextAssistant() {
	return ensureTextAssistant();
}

const ofxGgmlTextAssistant & ofxGgmlEasy::getTextAssistant() const {
	return ensureTextAssistant();
}

ofxGgmlVisionInference & ofxGgmlEasy::getVisionInference() {
	return ensureVisionInference();
}

const ofxGgmlVisionInference & ofxGgmlEasy::getVisionInference() const {
	return ensureVisionInference();
}

ofxGgmlSpeechInference & ofxGgmlEasy::getSpeechInference() {
	return ensureSpeechInference();
}

const ofxGgmlSpeechInference & ofxGgmlEasy::getSpeechInference() const {
	return ensureSpeechInference();
}

ofxGgmlWebCrawler & ofxGgmlEasy::getWebCrawler() {
	return ensureCitationSearch().getWebCrawler();
}

const ofxGgmlWebCrawler & ofxGgmlEasy::getWebCrawler() const {
	return ensureCitationSearch().getWebCrawler();
}

ofxGgmlCitationSearch & ofxGgmlEasy::getCitationSearch() {
	return ensureCitationSearch();
}

const ofxGgmlCitationSearch & ofxGgmlEasy::getCitationSearch() const {
	return ensureCitationSearch();
}

ofxGgmlVideoPlanner & ofxGgmlEasy::getVideoPlanner() {
	return ensureVideoPlanner();
}

const ofxGgmlVideoPlanner & ofxGgmlEasy::getVideoPlanner() const {
	return ensureVideoPlanner();
}

ofxGgmlMediaPromptGenerator & ofxGgmlEasy::getMediaPromptGenerator() {
	return ensureMediaPromptGenerator();
}

const ofxGgmlMediaPromptGenerator & ofxGgmlEasy::getMediaPromptGenerator() const {
	return ensureMediaPromptGenerator();
}

#if OFXGGML_ENABLE_COMPANION_WORKFLOWS
ofxGgmlMusicGenerator & ofxGgmlEasy::getMusicGenerator() {
	return ensureMusicGenerator();
}

const ofxGgmlMusicGenerator & ofxGgmlEasy::getMusicGenerator() const {
	return ensureMusicGenerator();
}

ofxGgmlAceStepBridge & ofxGgmlEasy::getAceStepBridge() {
	return ensureAceStepBridge();
}

const ofxGgmlAceStepBridge & ofxGgmlEasy::getAceStepBridge() const {
	return ensureAceStepBridge();
}

ofxGgmlMilkDropGenerator & ofxGgmlEasy::getMilkDropGenerator() {
	return ensureMilkDropGenerator();
}

const ofxGgmlMilkDropGenerator & ofxGgmlEasy::getMilkDropGenerator() const {
	return ensureMilkDropGenerator();
}

ofxGgmlVideoEssayWorkflow & ofxGgmlEasy::getVideoEssayWorkflow() {
	return ensureVideoEssayWorkflow();
}

const ofxGgmlVideoEssayWorkflow & ofxGgmlEasy::getVideoEssayWorkflow() const {
	return ensureVideoEssayWorkflow();
}
#endif

ofxGgmlLongVideoPlanner & ofxGgmlEasy::getLongVideoPlanner() {
	return ensureLongVideoPlanner();
}

const ofxGgmlLongVideoPlanner & ofxGgmlEasy::getLongVideoPlanner() const {
	return ensureLongVideoPlanner();
}

ofxGgmlCodingAgent & ofxGgmlEasy::getCodingAgent() {
	return ensureCodingAgent();
}

const ofxGgmlCodingAgent & ofxGgmlEasy::getCodingAgent() const {
	return ensureCodingAgent();
}

ofxGgmlRAGPipeline & ofxGgmlEasy::getRAGPipeline() {
	return ensureRAGPipeline();
}

const ofxGgmlRAGPipeline & ofxGgmlEasy::getRAGPipeline() const {
	return ensureRAGPipeline();
}

ofxGgmlConversationManager & ofxGgmlEasy::getConversationManager() {
	return ensureConversationManager();
}

const ofxGgmlConversationManager & ofxGgmlEasy::getConversationManager() const {
	return ensureConversationManager();
}

std::vector<ofxGgmlEasyModelPreset> ofxGgmlEasy::listTextModelPresets(
	const std::string & catalogPath) const {
	const auto catalog = loadModelCatalogJson(catalogPath);
	if (!catalog) {
		return {};
	}
	return parseModelPresets(catalog->first);
}

std::optional<ofxGgmlEasyModelPreset> ofxGgmlEasy::recommendTextModelForTask(
	const std::string & task,
	const std::string & catalogPath) const {
	const auto catalog = loadModelCatalogJson(catalogPath);
	if (!catalog) {
		return std::nullopt;
	}
	const auto presetNumber = lookupTaskDefault(catalog->first, task);
	if (!presetNumber) {
		return std::nullopt;
	}
	return findPresetByNumber(parseModelPresets(catalog->first), *presetNumber);
}

ofxGgmlEasyModelSetupReport ofxGgmlEasy::inspectTextSetup(
	const std::string & task,
	const std::string & catalogPath) const {
	ofxGgmlEasyModelSetupReport report;
	report.prefersServer = m_textConfig.preferServer;
	report.serverConfigured = !m_textConfig.serverUrl.empty();

	const auto catalog = loadModelCatalogJson(catalogPath);
	std::vector<ofxGgmlEasyModelPreset> presets;
	if (catalog) {
		report.catalogAvailable = true;
		report.resolvedCatalogPath = catalog->second.string();
		presets = parseModelPresets(catalog->first);
	}

	if (!task.empty() && catalog) {
		const auto presetNumber = lookupTaskDefault(catalog->first, task);
		if (presetNumber) {
			report.recommendedPreset = findPresetByNumber(presets, *presetNumber);
		}
	}

	if (m_textConfig.modelPath.empty()) {
		report.errors.push_back(
			"No text model is configured. Call configureText(...) with a GGUF model path.");
	} else {
		const std::filesystem::path modelPath(m_textConfig.modelPath);
		report.modelPathExists = std::filesystem::exists(modelPath);
		if (!report.modelPathExists) {
			report.errors.push_back(
				"Configured text model path does not exist: " + m_textConfig.modelPath);
		}
		for (const auto & preset : presets) {
			if (!preset.filename.empty() && modelPath.filename() == preset.filename) {
				report.configuredPreset = preset;
				break;
			}
		}
		if (modelPath.extension() != ".gguf") {
			report.warnings.push_back(
				"Configured text model does not use a .gguf extension, so llama.cpp-compatible local inference may fail.");
		}
	}

	if (m_textConfig.preferServer && m_textConfig.serverUrl.empty()) {
		report.errors.push_back(
			"preferServer is enabled, but no serverUrl is configured.");
	}
	if (!m_textConfig.preferServer && m_textConfig.completionExecutable.empty()) {
		report.warnings.push_back(
			"No completion executable is configured, so local text inference cannot run until llama-cli tooling is available.");
	}
	if (m_textConfig.preferServer && m_textConfig.serverModel.empty()) {
		report.warnings.push_back(
			"preferServer is enabled without a serverModel. Requests may still work, but explicit model routing is safer.");
	}
	if ((task == "rag" || task == "research" || task == "citation" || task == "script") &&
		m_textConfig.embeddingExecutable.empty() &&
		!(m_textConfig.settings.useServerBackend && !m_textConfig.serverUrl.empty())) {
		report.warnings.push_back(
			"Embedding support is not configured, so hybrid retrieval will fall back to lexical ranking.");
	}

	if (!m_textConfig.completionExecutable.empty() &&
		pathLooksExplicit(m_textConfig.completionExecutable) &&
		!std::filesystem::exists(std::filesystem::path(m_textConfig.completionExecutable))) {
		report.warnings.push_back(
			"Configured completion executable path was not found: " +
			m_textConfig.completionExecutable);
	}

	if (report.configuredPreset && !report.configuredPreset->hasChecksum()) {
		report.warnings.push_back(
			"Configured preset is present in the model catalog but is still missing a published SHA256 checksum.");
	}

	if (report.configuredPreset &&
		!task.empty() &&
		report.recommendedPreset &&
		report.configuredPreset->preset != report.recommendedPreset->preset) {
		report.warnings.push_back(
			"Configured model is usable, but the catalog recommends a different preset for task '" +
			task + "'.");
	}

	if (report.recommendedPreset) {
		report.recommendations.push_back(
			"Recommended preset for '" + task + "': #" +
			std::to_string(report.recommendedPreset->preset) + " " +
			report.recommendedPreset->name);
	}

	if (!report.catalogAvailable) {
		report.warnings.push_back(
			"Model catalog was not found. Pass a catalogPath to enable preset recommendations.");
	}

	if (!report.modelPathExists && report.recommendedPreset) {
		report.recommendations.push_back(
			"Use scripts/download-model.sh --preset " +
			std::to_string(report.recommendedPreset->preset) +
			" to fetch the recommended text model.");
	}

	report.ready = report.errors.empty();
	return report;
}

std::optional<ofxGgmlEasyModelDownloadPlan> ofxGgmlEasy::planTextModelDownload(
	const std::string & task,
	int preset,
	const std::string & catalogPath,
	const std::string & outputDir) const {
	const auto catalog = loadModelCatalogJson(catalogPath);
	if (!catalog) {
		return std::nullopt;
	}

	std::optional<ofxGgmlEasyModelPreset> selectedPreset;
	if (preset > 0) {
		selectedPreset = findPresetByNumber(parseModelPresets(catalog->first), preset);
	} else if (!task.empty()) {
		const auto taskPreset = lookupTaskDefault(catalog->first, task);
		if (taskPreset) {
			selectedPreset = findPresetByNumber(parseModelPresets(catalog->first), *taskPreset);
		}
	}
	if (!selectedPreset) {
		return std::nullopt;
	}

	ofxGgmlEasyModelDownloadPlan plan;
	plan.available = true;
	plan.preset = selectedPreset->preset;
	plan.task = task;
	plan.name = selectedPreset->name;
	plan.filename = selectedPreset->filename;
	plan.url = selectedPreset->url;
	plan.sha256 = selectedPreset->sha256;
	plan.size = selectedPreset->size;
	plan.outputDir = outputDir;
	plan.catalogPath = catalog->second.string();
	plan.downloadScriptPath = (catalog->second.parent_path() / "download-model.sh").string();
	plan.verifiedCatalogEntry = selectedPreset->checksumVerified();
	plan.checksumRequired = selectedPreset->sourceType == "official" ||
		!selectedPreset->sha256.empty();
	plan.catalogTrustSummary = plan.verifiedCatalogEntry
		? "Catalog entry is signed and marked verified-sha256."
		: "Catalog entry is available but not marked verified-sha256.";

	std::ostringstream command;
	command << quoteShellArg(plan.downloadScriptPath) << " --preset " << plan.preset;
	if (plan.checksumRequired) {
		command << " --require-checksum";
	}
	if (!outputDir.empty()) {
		command << " --output " << quoteShellArg(outputDir);
	}
	plan.suggestedCommand = command.str();

	if (!plan.verifiedCatalogEntry) {
		plan.warnings.push_back(
			"Selected preset is present in the catalog but is not yet marked verified-sha256.");
	}
	if (plan.checksumRequired && plan.sha256.empty()) {
		plan.warnings.push_back(
			"Selected preset requires checksum verification, but the catalog entry does not publish a SHA256 checksum.");
	}
	if (plan.downloadScriptPath.empty() ||
		!std::filesystem::exists(std::filesystem::path(plan.downloadScriptPath))) {
		plan.warnings.push_back(
			"Could not confirm that scripts/download-model.sh exists next to the resolved catalog.");
	}
	return plan;
}

ofxGgmlEasyHealthSnapshot ofxGgmlEasy::inspectTextHealth(
	const ofxGgml * runtime) const {
	ofxGgmlEasyHealthSnapshot snapshot;
	snapshot.textConfigured = !m_textConfig.modelPath.empty() || !m_textConfig.serverUrl.empty();
	snapshot.serverExpected = m_textConfig.preferServer || !m_textConfig.serverUrl.empty();

	const std::string metricKey = configuredModelMetricKey(m_textConfig);
	const auto & metrics = ofxGgmlMetrics::getInstance();
	const auto inferenceStats = metrics.getInferenceStats(metricKey);
	snapshot.averageTokensPerSecond = metrics.getAverageTokensPerSecond(metricKey);
	snapshot.retrievalCacheHitRate = metrics.getCacheHitRate("rag.retrieval");
	snapshot.tokenCountCacheHitRate = metrics.getCacheHitRate("token-count");
	if (inferenceStats.successfulCalls > 0) {
		snapshot.averageLatencyMs =
			inferenceStats.totalTimeMs / static_cast<double>(inferenceStats.successfulCalls);
		snapshot.minLatencyMs =
			inferenceStats.minTimeMs == std::numeric_limits<double>::max()
				? 0.0
				: inferenceStats.minTimeMs;
		snapshot.maxLatencyMs = inferenceStats.maxTimeMs;
	}

	if (runtime) {
		snapshot.localRuntimeAttached = true;
		snapshot.localRuntimeReady = runtime->isReady();
		snapshot.memoryUsage = runtime->getMemoryUsage();
	}

	if (snapshot.serverExpected && !m_textConfig.serverUrl.empty()) {
		snapshot.serverProbe = ofxGgmlInference::probeServer(m_textConfig.serverUrl, true);
		snapshot.serverQueue = ofxGgmlInference::getServerQueueStatus(m_textConfig.serverUrl);
		if (!snapshot.serverProbe.reachable) {
			snapshot.warnings.push_back(
				"Configured text server is not currently reachable: " + snapshot.serverProbe.error);
		}
		if (!snapshot.serverQueue.available && !snapshot.serverQueue.error.empty()) {
			snapshot.warnings.push_back(
				"Queue status is unavailable: " + snapshot.serverQueue.error);
		}
	} else if (snapshot.serverExpected) {
		snapshot.warnings.push_back(
			"Text health expects a server backend, but no server URL is configured.");
	}

	if (!snapshot.textConfigured) {
		snapshot.warnings.push_back(
			"Text health is limited because no text model or server backend is configured.");
	}
	if (snapshot.localRuntimeAttached && !snapshot.localRuntimeReady) {
		snapshot.warnings.push_back(
			"Local runtime pointer was provided, but the runtime is not ready.");
	}
	return snapshot;
}

ofxGgmlEasyDiagnosticsReport ofxGgmlEasy::inspectTextDiagnostics(
	const std::string & task,
	const std::string & catalogPath,
	const ofxGgml * runtime) const {
	ofxGgmlEasyDiagnosticsReport report;
	report.setup = inspectTextSetup(task, catalogPath);
	report.health = inspectTextHealth(runtime);

	for (const auto & error : report.setup.errors) {
		addDiagnosticIssue(
			report.issues,
			ofxGgmlEasyDiagnosticSeverity::Blocking,
			"setup",
			error);
	}
	for (const auto & warning : report.setup.warnings) {
		addDiagnosticIssue(
			report.issues,
			ofxGgmlEasyDiagnosticSeverity::Warning,
			"setup",
			warning);
	}
	for (const auto & recommendation : report.setup.recommendations) {
		addDiagnosticIssue(
			report.issues,
			ofxGgmlEasyDiagnosticSeverity::Info,
			"setup",
			recommendation);
	}
	for (const auto & warning : report.health.warnings) {
		addDiagnosticIssue(
			report.issues,
			ofxGgmlEasyDiagnosticSeverity::Degraded,
			"health",
			warning);
	}

	if (report.setup.configuredPreset &&
		report.setup.configuredPreset->hasChecksum() &&
		!report.setup.configuredPreset->checksumVerified()) {
		addDiagnosticIssue(
			report.issues,
			ofxGgmlEasyDiagnosticSeverity::Warning,
			"model-catalog",
			"Configured catalog preset is not marked verified-sha256.");
	}
	if (report.health.serverExpected && !report.health.serverProbe.reachable) {
		addDiagnosticIssue(
			report.issues,
			ofxGgmlEasyDiagnosticSeverity::Degraded,
			"server",
			"Configured text server is expected but not reachable.");
	}

	report.ready = report.setup.ready &&
		report.countIssues(ofxGgmlEasyDiagnosticSeverity::Blocking) == 0;
	report.degraded =
		report.countIssues(ofxGgmlEasyDiagnosticSeverity::Degraded) > 0 ||
		report.countIssues(ofxGgmlEasyDiagnosticSeverity::Warning) > 0;
	return report;
}

ofxGgmlInferenceSettings ofxGgmlEasy::makeTextSettings() const {
	ofxGgmlInferenceSettings settings = m_textConfig.settings;
	if (m_textConfig.preferServer) {
		settings.useServerBackend = true;
	}
	if (!m_textConfig.serverUrl.empty()) {
		settings.serverUrl = m_textConfig.serverUrl;
	}
	if (!m_textConfig.serverModel.empty()) {
		settings.serverModel = m_textConfig.serverModel;
	}
	return settings;
}

ofxGgmlVisionModelProfile ofxGgmlEasy::makeVisionProfile() const {
	ofxGgmlVisionModelProfile profile;
	profile.name = "Configured Easy API vision model";
	profile.modelPath = m_visionConfig.modelPath;
	profile.mmprojPath = m_visionConfig.mmprojPath;
	profile.serverUrl = m_visionConfig.serverUrl;
	profile.mayRequireMmproj = !m_visionConfig.mmprojPath.empty();
	return profile;
}

ofxGgmlWebCrawlerRequest ofxGgmlEasy::makeCrawlerRequest(
	const std::string & startUrl,
	int maxDepth) const {
	ofxGgmlWebCrawlerRequest request;
	request.startUrl = startUrl;
	request.outputDir = m_crawlerConfig.outputDir;
	request.maxDepth = maxDepth >= 0 ? maxDepth : m_crawlerConfig.maxDepth;
	request.renderJavaScript = m_crawlerConfig.renderJavaScript;
	request.keepOutputFiles = m_crawlerConfig.keepOutputFiles;
	request.stayOnStartDomain = m_crawlerConfig.stayOnStartDomain;
	request.maxPages = m_crawlerConfig.maxPages;
	request.allowedDomains = m_crawlerConfig.allowedDomains;
	request.extraArgs = m_crawlerConfig.extraArgs;
	request.executablePath = m_crawlerConfig.executablePath;
	return request;
}

void ofxGgmlEasy::syncCrawlerBackend() {
	if (m_citationSearch) {
		applyCrawlerBackend(m_citationSearch->getWebCrawler(), m_crawlerConfig);
	}
}

void ofxGgmlEasy::syncSpeechBackend() {
	if (m_speechInference) {
		applySpeechBackend(*m_speechInference, m_speechConfig);
	}
}

ofxGgmlInferenceResult ofxGgmlEasy::complete(const std::string & prompt) const {
	if (m_textConfig.modelPath.empty()) {
		return makeTextConfigError(
			"Easy API text model is not configured. Call configureText(...) first.");
	}
	return ensureInference().generate(
		m_textConfig.modelPath,
		prompt,
		makeTextSettings());
}

ofxGgmlChatAssistantResult ofxGgmlEasy::chat(
	const std::string & userText,
	const std::string & responseLanguage,
	const std::string & systemPrompt) const {
	if (m_textConfig.modelPath.empty()) {
		return makeChatConfigError(
			"Easy API text model is not configured. Call configureText(...) first.");
	}
	ofxGgmlChatAssistantRequest request;
	request.userText = userText;
	request.systemPrompt = systemPrompt;
	request.responseLanguage = responseLanguage;
	return ensureChatAssistant().run(
		m_textConfig.modelPath,
		request,
		makeTextSettings());
}

ofxGgmlTextAssistantResult ofxGgmlEasy::summarize(const std::string & text) const {
	if (m_textConfig.modelPath.empty()) {
		return makeTextAssistantConfigError(
			"Summarize text.",
			"Easy API text model is not configured. Call configureText(...) first.");
	}
	ofxGgmlTextAssistantRequest request;
	request.task = ofxGgmlTextTask::Summarize;
	request.inputText = text;
	return ensureTextAssistant().run(
		m_textConfig.modelPath,
		request,
		makeTextSettings());
}

ofxGgmlTextAssistantResult ofxGgmlEasy::rewrite(const std::string & text) const {
	if (m_textConfig.modelPath.empty()) {
		return makeTextAssistantConfigError(
			"Rewrite text.",
			"Easy API text model is not configured. Call configureText(...) first.");
	}
	ofxGgmlTextAssistantRequest request;
	request.task = ofxGgmlTextTask::Rewrite;
	request.inputText = text;
	return ensureTextAssistant().run(
		m_textConfig.modelPath,
		request,
		makeTextSettings());
}

ofxGgmlTextAssistantResult ofxGgmlEasy::translate(
	const std::string & text,
	const std::string & targetLanguage,
	const std::string & sourceLanguage) const {
	if (m_textConfig.modelPath.empty()) {
		return makeTextAssistantConfigError(
			"Translate text.",
			"Easy API text model is not configured. Call configureText(...) first.");
	}
	ofxGgmlTextAssistantRequest request;
	request.task = ofxGgmlTextTask::Translate;
	request.inputText = text;
	request.sourceLanguage = sourceLanguage;
	request.targetLanguage = targetLanguage;
	return ensureTextAssistant().run(
		m_textConfig.modelPath,
		request,
		makeTextSettings());
}

ofxGgmlVisionResult ofxGgmlEasy::describeImage(
	const std::string & imagePath,
	const std::string & prompt) const {
	if (m_visionConfig.modelPath.empty()) {
		return makeVisionConfigError(
			"Easy API vision model is not configured. Call configureVision(...) first.");
	}
	ofxGgmlVisionRequest request;
	request.task = ofxGgmlVisionTask::Describe;
	request.prompt = prompt.empty()
		? ofxGgmlVisionInference::defaultPromptForTask(ofxGgmlVisionTask::Describe)
		: prompt;
	request.systemPrompt = ofxGgmlVisionInference::defaultSystemPromptForTask(
		ofxGgmlVisionTask::Describe);
	request.maxTokens = m_visionConfig.maxTokens;
	request.temperature = m_visionConfig.temperature;
	request.images.push_back({
		imagePath,
		"image",
		ofxGgmlVisionInference::detectMimeType(imagePath)
	});
	return ensureVisionInference().runServerRequest(makeVisionProfile(), request);
}

ofxGgmlVisionResult ofxGgmlEasy::askImage(
	const std::string & imagePath,
	const std::string & question) const {
	if (m_visionConfig.modelPath.empty()) {
		return makeVisionConfigError(
			"Easy API vision model is not configured. Call configureVision(...) first.");
	}
	ofxGgmlVisionRequest request;
	request.task = ofxGgmlVisionTask::Ask;
	request.prompt = question;
	request.systemPrompt = ofxGgmlVisionInference::defaultSystemPromptForTask(
		ofxGgmlVisionTask::Ask);
	request.maxTokens = m_visionConfig.maxTokens;
	request.temperature = m_visionConfig.temperature;
	request.images.push_back({
		imagePath,
		"image",
		ofxGgmlVisionInference::detectMimeType(imagePath)
	});
	return ensureVisionInference().runServerRequest(makeVisionProfile(), request);
}

ofxGgmlSpeechResult ofxGgmlEasy::transcribeAudio(const std::string & audioPath) const {
	if (m_speechConfig.modelPath.empty() &&
		!(m_speechConfig.preferServer && !m_speechConfig.serverUrl.empty())) {
		return makeSpeechConfigError(
			"Easy API speech model is not configured. Call configureSpeech(...) first.");
	}
	ofxGgmlSpeechRequest request;
	request.task = ofxGgmlSpeechTask::Transcribe;
	request.audioPath = audioPath;
	request.modelPath = m_speechConfig.modelPath;
	request.serverUrl = m_speechConfig.serverUrl;
	request.serverModel = m_speechConfig.serverModel;
	request.languageHint = m_speechConfig.languageHint;
	request.returnTimestamps = m_speechConfig.returnTimestamps;
	return ensureSpeechInference().transcribe(request);
}

ofxGgmlSpeechResult ofxGgmlEasy::translateAudio(const std::string & audioPath) const {
	if (m_speechConfig.modelPath.empty() &&
		!(m_speechConfig.preferServer && !m_speechConfig.serverUrl.empty())) {
		return makeSpeechConfigError(
			"Easy API speech model is not configured. Call configureSpeech(...) first.");
	}
	ofxGgmlSpeechRequest request;
	request.task = ofxGgmlSpeechTask::Translate;
	request.audioPath = audioPath;
	request.modelPath = m_speechConfig.modelPath;
	request.serverUrl = m_speechConfig.serverUrl;
	request.serverModel = m_speechConfig.serverModel;
	request.languageHint = m_speechConfig.languageHint;
	request.returnTimestamps = m_speechConfig.returnTimestamps;
	return ensureSpeechInference().transcribe(request);
}

#if OFXGGML_ENABLE_COMPANION_WORKFLOWS
ofxGgmlMusicPromptResult ofxGgmlEasy::generateMusicPrompt(
	const std::string & sourceConcept,
	const std::string & style,
	const std::string & instrumentation,
	int targetDurationSeconds,
	bool instrumentalOnly) const {
	ofxGgmlMusicPromptResult result;
	if (m_textConfig.modelPath.empty()) {
		result.error =
			"Easy API text model is not configured. Call configureText(...) first.";
		return result;
	}

	ofxGgmlMusicPromptRequest request;
	request.sourceConcept = sourceConcept;
	request.style = style;
	request.instrumentation = instrumentation;
	request.targetDurationSeconds = targetDurationSeconds;
	request.instrumentalOnly = instrumentalOnly;
	return ensureMusicGenerator().generateMusicPrompt(
		m_textConfig.modelPath,
		request,
		makeTextSettings());
}

ofxGgmlMusicNotationResult ofxGgmlEasy::generateMusicNotation(
	const std::string & sourceConcept,
	const std::string & title,
	const std::string & style,
	int bars,
	const std::string & key) const {
	ofxGgmlMusicNotationResult result;
	if (m_textConfig.modelPath.empty()) {
		result.error =
			"Easy API text model is not configured. Call configureText(...) first.";
		return result;
	}

	ofxGgmlMusicNotationRequest request;
	request.sourceConcept = sourceConcept;
	request.title = title;
	request.style = style;
	request.bars = bars;
	request.key = key;
	return ensureMusicGenerator().generateAbcNotation(
		m_textConfig.modelPath,
		request,
		makeTextSettings());
}

ofxGgmlImageToMusicResult ofxGgmlEasy::generateImageToMusicPrompt(
	const std::string & imageDescription,
	const std::string & musicalStyle,
	const std::string & instrumentation,
	int targetDurationSeconds,
	bool instrumentalOnly) const {
	ofxGgmlImageToMusicResult result;
	if (m_textConfig.modelPath.empty()) {
		result.error =
			"Easy API text model is not configured. Call configureText(...) first.";
		return result;
	}

	ofxGgmlImageToMusicRequest request;
	request.imageDescription = imageDescription;
	request.musicalStyle = musicalStyle;
	request.instrumentation = instrumentation;
	request.targetDurationSeconds = targetDurationSeconds;
	request.instrumentalOnly = instrumentalOnly;
	return ensureMediaPromptGenerator().generateImageToMusicPrompt(
		m_textConfig.modelPath,
		request,
		makeTextSettings());
}

std::string ofxGgmlEasy::saveMusicNotation(
	const std::string & abcNotation,
	const std::string & outputPath) const {
	return ensureMusicGenerator().saveAbcNotation(abcNotation, outputPath);
}

ofxGgmlAceStepGenerateResult ofxGgmlEasy::generateAceStepMusic(
	const ofxGgmlAceStepRequest & request,
	const std::string & serverUrl) const {
	return ensureAceStepBridge().generate(request, serverUrl);
}

ofxGgmlAceStepUnderstandResult ofxGgmlEasy::understandAceStepAudio(
	const ofxGgmlAceStepUnderstandRequest & request,
	const std::string & serverUrl) const {
	return ensureAceStepBridge().understandAudio(request, serverUrl);
}
#endif

ofxGgmlWebCrawlerResult ofxGgmlEasy::crawlWebsite(
	const std::string & startUrl,
	int maxDepth) const {
	if (startUrl.empty()) {
		ofxGgmlWebCrawlerResult result;
		result.error = "Easy API crawler start URL is empty.";
		return result;
	}
	return ensureCitationSearch().getWebCrawler().crawl(makeCrawlerRequest(startUrl, maxDepth));
}

ofxGgmlCitationSearchResult ofxGgmlEasy::findCitations(
	const std::string & topic,
	const std::vector<std::string> & sourceUrls,
	const std::string & crawlerUrl,
	size_t maxCitations) const {
	ofxGgmlCitationSearchRequest request;
	request.modelPath = m_textConfig.modelPath;
	request.topic = topic;
	request.maxCitations = maxCitations;
	request.sourceUrls = sourceUrls;
	request.inferenceSettings = makeTextSettings();
	if (!crawlerUrl.empty()) {
		request.useCrawler = true;
		request.crawlerRequest = makeCrawlerRequest(crawlerUrl);
	}
	return ensureCitationSearch().search(request);
}

ofxGgmlCitationSearchResult ofxGgmlEasy::findCitationsFromInput(
	const std::string & userInput,
	const std::vector<std::string> & sourceUrls,
	const std::string & crawlerUrl,
	size_t maxCitations,
	const ofxGgmlCitationSearchInputSettings & inputSettings) const {
	ofxGgmlCitationSearchRequest request;
	request.modelPath = m_textConfig.modelPath;
	request.maxCitations = maxCitations;
	request.sourceUrls = sourceUrls;
	request.inferenceSettings = makeTextSettings();
	if (!crawlerUrl.empty()) {
		request.useCrawler = true;
		request.crawlerRequest = makeCrawlerRequest(crawlerUrl);
	}
	return ensureCitationSearch().searchFromInput(userInput, request, inputSettings);
}

#if OFXGGML_ENABLE_COMPANION_WORKFLOWS
ofxGgmlVideoEssayResult ofxGgmlEasy::planVideoEssay(
	const ofxGgmlVideoEssayRequest & request) const {
	ofxGgmlVideoEssayRequest effectiveRequest = request;
	effectiveRequest.inferenceSettings = request.inferenceSettings;
	if (effectiveRequest.modelPath.empty()) {
		effectiveRequest.modelPath = m_textConfig.modelPath;
	}
	if (effectiveRequest.inferenceSettings.maxTokens <= 0) {
		effectiveRequest.inferenceSettings = makeTextSettings();
	}
	if (effectiveRequest.crawlerRequest.executablePath.empty()) {
		effectiveRequest.crawlerRequest.executablePath = m_crawlerConfig.executablePath;
	}
	if (effectiveRequest.crawlerRequest.outputDir.empty()) {
		effectiveRequest.crawlerRequest.outputDir = m_crawlerConfig.outputDir;
	}
	if (effectiveRequest.crawlerRequest.maxDepth <= 0) {
		effectiveRequest.crawlerRequest.maxDepth = std::max(1, m_crawlerConfig.maxDepth);
	}
	if (effectiveRequest.crawlerRequest.allowedDomains.empty()) {
		effectiveRequest.crawlerRequest.allowedDomains = m_crawlerConfig.allowedDomains;
	}
	if (effectiveRequest.sourceSettings.maxSources == 0 &&
		effectiveRequest.sourceSettings.maxCharsPerSource == 0 &&
		effectiveRequest.sourceSettings.maxTotalChars == 0) {
		effectiveRequest.sourceSettings.maxSources = 6;
		effectiveRequest.sourceSettings.maxCharsPerSource = 2200;
		effectiveRequest.sourceSettings.maxTotalChars = 14000;
		effectiveRequest.sourceSettings.requestCitations = true;
	}
	return ensureVideoEssayWorkflow().run(effectiveRequest);
}
#endif

ofxGgmlLongVideoPlanResult ofxGgmlEasy::planLongVideo(
	const ofxGgmlLongVideoPlanRequest & request) const {
	ofxGgmlLongVideoPlanRequest effectiveRequest = request;
	effectiveRequest.inferenceSettings = request.inferenceSettings;
	if (effectiveRequest.modelPath.empty()) {
		effectiveRequest.modelPath = m_textConfig.modelPath;
	}
	if (effectiveRequest.inferenceSettings.maxTokens <= 0) {
		effectiveRequest.inferenceSettings = makeTextSettings();
	}
	return ensureLongVideoPlanner().run(effectiveRequest);
}

std::string ofxGgmlEasy::buildLongVideoManifestJson(
	const ofxGgmlLongVideoPlanRequest & request) const {
	const ofxGgmlLongVideoPlanResult result = planLongVideo(request);
	return result.manifestJson;
}

ofxGgmlCodingAgentResult ofxGgmlEasy::runCodingAgent(
	const ofxGgmlCodingAgentRequest & request,
	const ofxGgmlCodeAssistantContext & context,
	const ofxGgmlCodingAgentSettings & settings) {
	ofxGgmlCodingAgentResult result;
	if (m_textConfig.modelPath.empty()) {
		result.error =
			"Easy API text model is not configured. Call configureText(...) first.";
		result.summary = result.error;
		return result;
	}

	ofxGgmlCodingAgentSettings effectiveSettings = settings;
	const ofxGgmlInferenceSettings baseSettings = makeTextSettings();
	const ofxGgmlInferenceSettings defaultSettings;
	if (effectiveSettings.inferenceSettings.maxTokens <= 0) {
		effectiveSettings.inferenceSettings.maxTokens = baseSettings.maxTokens;
	}
	if (effectiveSettings.inferenceSettings.temperature == defaultSettings.temperature) {
		effectiveSettings.inferenceSettings.temperature = baseSettings.temperature;
	}
	if (effectiveSettings.inferenceSettings.topP == defaultSettings.topP) {
		effectiveSettings.inferenceSettings.topP = baseSettings.topP;
	}
	if (effectiveSettings.inferenceSettings.topK == defaultSettings.topK) {
		effectiveSettings.inferenceSettings.topK = baseSettings.topK;
	}
	if (effectiveSettings.inferenceSettings.minP == defaultSettings.minP) {
		effectiveSettings.inferenceSettings.minP = baseSettings.minP;
	}
	if (effectiveSettings.inferenceSettings.repeatPenalty == defaultSettings.repeatPenalty) {
		effectiveSettings.inferenceSettings.repeatPenalty = baseSettings.repeatPenalty;
	}
	if (effectiveSettings.inferenceSettings.contextSize <= 0) {
		effectiveSettings.inferenceSettings.contextSize = baseSettings.contextSize;
	}
	if (effectiveSettings.inferenceSettings.batchSize <= 0) {
		effectiveSettings.inferenceSettings.batchSize = baseSettings.batchSize;
	}
	if (effectiveSettings.inferenceSettings.threads <= 0) {
		effectiveSettings.inferenceSettings.threads = baseSettings.threads;
	}
	if (effectiveSettings.inferenceSettings.gpuLayers == defaultSettings.gpuLayers) {
		effectiveSettings.inferenceSettings.gpuLayers = baseSettings.gpuLayers;
	}
	if (effectiveSettings.inferenceSettings.seed == defaultSettings.seed) {
		effectiveSettings.inferenceSettings.seed = baseSettings.seed;
	}
	if (effectiveSettings.inferenceSettings.useServerBackend ==
		defaultSettings.useServerBackend) {
		effectiveSettings.inferenceSettings.useServerBackend =
			baseSettings.useServerBackend;
	}
	if (effectiveSettings.inferenceSettings.serverUrl.empty()) {
		effectiveSettings.inferenceSettings.serverUrl = baseSettings.serverUrl;
	}
	if (effectiveSettings.inferenceSettings.serverModel.empty()) {
		effectiveSettings.inferenceSettings.serverModel = baseSettings.serverModel;
	}
	return ensureCodingAgent().run(
		m_textConfig.modelPath,
		request,
		context,
		effectiveSettings);
}

#if OFXGGML_ENABLE_COMPANION_WORKFLOWS
ofxGgmlEasyMontageResult ofxGgmlEasy::planMontageFromSrt(
	const std::string & srtPath,
	const std::string & goal,
	size_t maxClips,
	double minScore,
	bool preserveChronology,
	const std::string & reelName,
	const std::string & edlTitle,
	int fps) const {
	ofxGgmlEasyMontageResult result;
	const auto segmentsResult = ofxGgmlMontagePlanner::loadSegmentsFromSrt(srtPath, reelName);
	if (!segmentsResult.isOk()) {
		result.error = segmentsResult.error().message;
		return result;
	}

	ofxGgmlMontagePlannerRequest request;
	request.goal = goal;
	request.segments = segmentsResult.value();
	request.maxClips = maxClips;
	request.minScore = minScore;
	request.preserveChronology = preserveChronology;
	request.fallbackReelName = reelName;
	result.planning = ofxGgmlMontagePlanner::plan(request);
	if (!result.planning.success) {
		result.error = result.planning.error;
		return result;
	}

	result.success = true;
	result.editorBrief = ofxGgmlMontagePlanner::buildEditorBrief(result.planning.plan);
	result.montageTrack = ofxGgmlMontagePlanner::buildSubtitleTrack(result.planning.plan, edlTitle);
	result.sourceTrack = ofxGgmlMontagePlanner::buildSourceSubtitleTrack(result.planning.plan, edlTitle);
	result.edlText = ofxGgmlMontagePlanner::buildEdl(result.planning.plan, edlTitle, fps);
	result.srtText = ofxGgmlMontagePlanner::buildSrt(result.montageTrack);
	result.vttText = ofxGgmlMontagePlanner::buildVtt(result.montageTrack);
	result.previewBundle = ofxGgmlMontagePreviewBridge::buildBundle(result.planning.plan, edlTitle);
	return result;
}
#endif

ofxGgmlEasyVideoEditResult ofxGgmlEasy::planVideoEdit(
	const std::string & sourcePrompt,
	const std::string & editGoal,
	const std::string & sourceAnalysis,
	double targetDurationSeconds,
	int clipCount,
	bool preserveChronology,
	const ofxGgmlVideoEditWorkflowContext & workflowContext) const {
	ofxGgmlEasyVideoEditResult result;
	if (m_textConfig.modelPath.empty()) {
		result.error = "Easy API text model is not configured. Call configureText(...) first.";
		return result;
	}

	ofxGgmlVideoEditPlannerRequest request;
	request.sourcePrompt = sourcePrompt;
	request.editGoal = editGoal;
	request.sourceAnalysis = sourceAnalysis;
	request.targetDurationSeconds = targetDurationSeconds;
	request.clipCount = clipCount;
	request.preserveChronology = preserveChronology;
	result.planning = ensureVideoPlanner().planEdits(
		m_textConfig.modelPath,
		request,
		makeTextSettings(),
		ensureInference());
	if (!result.planning.success) {
		result.error = result.planning.error;
		return result;
	}

	result.success = true;
	result.editorBrief = ofxGgmlVideoPlanner::buildEditorBrief(result.planning.plan);
	result.workflow = ofxGgmlVideoPlanner::buildEditWorkflow(
		result.planning.plan,
		workflowContext);
	return result;
}

#if OFXGGML_ENABLE_COMPANION_WORKFLOWS
ofxGgmlMilkDropResult ofxGgmlEasy::generateMilkDropPreset(
	const std::string & prompt,
	const std::string & category,
	float randomness) const {
	ofxGgmlMilkDropResult result;
	if (m_textConfig.modelPath.empty()) {
		result.error =
			"Easy API text model is not configured. Call configureText(...) first.";
		return result;
	}

	ofxGgmlMilkDropRequest request;
	request.prompt = prompt;
	request.category = category;
	request.randomness = randomness;
	return ensureMilkDropGenerator().generatePreset(
		m_textConfig.modelPath,
		request,
		makeTextSettings());
}

ofxGgmlMilkDropVariantResult ofxGgmlEasy::generateMilkDropVariants(
	const std::string & prompt,
	const std::string & category,
	float randomness,
	int variantCount) const {
	ofxGgmlMilkDropVariantResult result;
	if (m_textConfig.modelPath.empty()) {
		result.error =
			"Easy API text model is not configured. Call configureText(...) first.";
		return result;
	}

	ofxGgmlMilkDropRequest request;
	request.prompt = prompt;
	request.category = category;
	request.randomness = randomness;
	return ensureMilkDropGenerator().generateVariants(
		m_textConfig.modelPath,
		request,
		variantCount,
		makeTextSettings());
}

ofxGgmlMilkDropResult ofxGgmlEasy::editMilkDropPreset(
	const std::string & existingPresetText,
	const std::string & editInstruction,
	const std::string & category,
	float randomness) const {
	ofxGgmlMilkDropResult result;
	if (m_textConfig.modelPath.empty()) {
		result.error =
			"Easy API text model is not configured. Call configureText(...) first.";
		return result;
	}

	return ensureMilkDropGenerator().editPreset(
		m_textConfig.modelPath,
		existingPresetText,
		editInstruction,
		category,
		randomness,
		makeTextSettings());
}

ofxGgmlMilkDropResult ofxGgmlEasy::repairMilkDropPreset(
	const std::string & presetText,
	const std::string & category,
	float randomness,
	const std::string & repairInstruction) const {
	ofxGgmlMilkDropResult result;
	if (m_textConfig.modelPath.empty()) {
		result.error =
			"Easy API text model is not configured. Call configureText(...) first.";
		return result;
	}

	return ensureMilkDropGenerator().repairPreset(
		m_textConfig.modelPath,
		presetText,
		category,
		randomness,
		repairInstruction,
		makeTextSettings());
}

ofxGgmlMilkDropValidation ofxGgmlEasy::validateMilkDropPreset(
	const std::string & presetText) const {
	return ensureMilkDropGenerator().validatePreset(presetText);
}

std::string ofxGgmlEasy::saveMilkDropPreset(
	const std::string & presetText,
	const std::string & outputPath) const {
	return ensureMilkDropGenerator().savePreset(presetText, outputPath);
}
#endif

// Workflow Presets Implementation

ofxGgmlEasyWorkflowResult ofxGgmlEasy::summarizeAndTranslate(
	const std::string & text,
	const std::string & targetLanguage,
	const std::string & sourceLanguage,
	int maxSummaryWords) const {
	ofxGgmlEasyWorkflowResult result;
	auto startTime = std::chrono::steady_clock::now();

	// Step 1: Summarize
	auto summaryResult = summarize(text);
	if (!summaryResult.inference.success) {
		result.error = "Summarization failed: " + summaryResult.inference.error;
		return result;
	}
	result.intermediateResults.push_back(summaryResult.inference.text);

	// Step 2: Translate the summary
	auto translationResult =
		translate(summaryResult.inference.text, targetLanguage, sourceLanguage);
	if (!translationResult.inference.success) {
		result.error = "Translation failed: " + translationResult.inference.error;
		return result;
	}
	result.finalOutput = translationResult.inference.text;

	auto endTime = std::chrono::steady_clock::now();
	result.totalElapsedMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
	result.success = true;
	return result;
}

ofxGgmlEasyWorkflowResult ofxGgmlEasy::transcribeAndSummarize(
	const std::string & audioPath,
	int maxSummaryWords) const {
	ofxGgmlEasyWorkflowResult result;
	auto startTime = std::chrono::steady_clock::now();

	// Step 1: Transcribe audio
	auto transcriptionResult = transcribeAudio(audioPath);
	if (!transcriptionResult.success) {
		result.error = "Transcription failed: " + transcriptionResult.error;
		return result;
	}
	result.intermediateResults.push_back(transcriptionResult.text);

	// Step 2: Summarize the transcript
	auto summaryResult = summarize(transcriptionResult.text);
	if (!summaryResult.inference.success) {
		result.error = "Summarization failed: " + summaryResult.inference.error;
		return result;
	}
	result.finalOutput = summaryResult.inference.text;

	auto endTime = std::chrono::steady_clock::now();
	result.totalElapsedMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
	result.success = true;
	return result;
}

ofxGgmlEasyWorkflowResult ofxGgmlEasy::describeAndAnalyze(
	const std::string & imagePath,
	const std::string & analysisPrompt,
	const std::string & descriptionPrompt) const {
	ofxGgmlEasyWorkflowResult result;
	auto startTime = std::chrono::steady_clock::now();

	// Step 1: Describe the image
	auto descriptionResult = describeImage(imagePath, descriptionPrompt);
	if (!descriptionResult.success) {
		result.error = "Image description failed: " + descriptionResult.error;
		return result;
	}
	result.intermediateResults.push_back(descriptionResult.text);

	// Step 2: Analyze the description with text model
	std::string fullPrompt = analysisPrompt + "\n\nImage description: " + descriptionResult.text;
	auto analysisResult = complete(fullPrompt);
	if (!analysisResult.success) {
		result.error = "Analysis failed: " + analysisResult.error;
		return result;
	}
	result.finalOutput = analysisResult.text;

	auto endTime = std::chrono::steady_clock::now();
	result.totalElapsedMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
	result.success = true;
	return result;
}

ofxGgmlEasyWorkflowResult ofxGgmlEasy::crawlAndSummarize(
	const std::string & startUrl,
	int maxDepth,
	int maxSummaryWords) const {
	ofxGgmlEasyWorkflowResult result;
	auto startTime = std::chrono::steady_clock::now();

	// Step 1: Crawl the website
	auto crawlResult = crawlWebsite(startUrl, maxDepth);
	if (!crawlResult.success) {
		result.error = "Website crawling failed: " + crawlResult.error;
		return result;
	}

	// Combine crawled documents
	std::string combinedContent;
	for (const auto& doc : crawlResult.documents) {
		combinedContent += doc.title + "\n" + doc.markdown + "\n\n";
	}
	result.intermediateResults.push_back(combinedContent);

	// Step 2: Summarize the crawled content
	auto summaryResult = summarize(combinedContent);
	if (!summaryResult.inference.success) {
		result.error = "Summarization failed: " + summaryResult.inference.error;
		return result;
	}
	result.finalOutput = summaryResult.inference.text;

	auto endTime = std::chrono::steady_clock::now();
	result.totalElapsedMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
	result.success = true;
	return result;
}

ofxGgmlRAGResult ofxGgmlEasy::ragQuery(
	const std::string & query,
	size_t topK,
	size_t chunkSize,
	size_t chunkOverlap,
	const std::string & promptPrefix) const {
	ofxGgmlRAGResult result;
	if (m_textConfig.modelPath.empty()) {
		result.error =
			"Easy API text model is not configured. Call configureText(...) first.";
		return result;
	}

	ofxGgmlRAGRequest request;
	request.query.query = query;
	request.query.topK = topK;
	request.query.chunkSize = chunkSize;
	request.query.chunkOverlap = chunkOverlap;
	request.query.embeddingModelPath = m_textConfig.modelPath;
	request.modelPath = m_textConfig.modelPath;
	request.promptPrefix = promptPrefix;
	request.inferenceSettings = makeTextSettings();
	return ensureRAGPipeline().generate(request);
}

ofxGgmlInference & ofxGgmlEasy::ensureInference() const {
	return ensureOwned(m_inference, [this](ofxGgmlInference & inference) {
		applyInferenceExecutables(inference, m_textConfig);
	});
}

ofxGgmlChatAssistant & ofxGgmlEasy::ensureChatAssistant() const {
	return ensureOwned(m_chatAssistant, [this](ofxGgmlChatAssistant & assistant) {
		assistant.setCompletionExecutable(m_textConfig.completionExecutable);
	});
}

ofxGgmlTextAssistant & ofxGgmlEasy::ensureTextAssistant() const {
	return ensureOwned(m_textAssistant, [this](ofxGgmlTextAssistant & assistant) {
		assistant.setCompletionExecutable(m_textConfig.completionExecutable);
	});
}

ofxGgmlVisionInference & ofxGgmlEasy::ensureVisionInference() const {
	return ensureOwned(m_visionInference, [](ofxGgmlVisionInference &) {});
}

ofxGgmlSpeechInference & ofxGgmlEasy::ensureSpeechInference() const {
	return ensureOwned(m_speechInference, [this](ofxGgmlSpeechInference & inference) {
		applySpeechBackend(inference, m_speechConfig);
	});
}

ofxGgmlCitationSearch & ofxGgmlEasy::ensureCitationSearch() const {
	return ensureOwned(m_citationSearch, [this](ofxGgmlCitationSearch & citationSearch) {
		applyInferenceExecutables(citationSearch.getInference(), m_textConfig);
		applyCrawlerBackend(citationSearch.getWebCrawler(), m_crawlerConfig);
	});
}

ofxGgmlVideoPlanner & ofxGgmlEasy::ensureVideoPlanner() const {
	return ensureOwned(m_videoPlanner, [](ofxGgmlVideoPlanner &) {});
}

ofxGgmlMediaPromptGenerator & ofxGgmlEasy::ensureMediaPromptGenerator() const {
	return ensureOwned(
		m_mediaPromptGenerator,
		[this](ofxGgmlMediaPromptGenerator & generator) {
			generator.setCompletionExecutable(m_textConfig.completionExecutable);
		});
}

#if OFXGGML_ENABLE_COMPANION_WORKFLOWS
ofxGgmlMusicGenerator & ofxGgmlEasy::ensureMusicGenerator() const {
	return ensureOwned(m_musicGenerator, [this](ofxGgmlMusicGenerator & generator) {
		generator.setCompletionExecutable(m_textConfig.completionExecutable);
	});
}

ofxGgmlAceStepBridge & ofxGgmlEasy::ensureAceStepBridge() const {
	return ensureOwned(m_aceStepBridge, [](ofxGgmlAceStepBridge &) {});
}

ofxGgmlMilkDropGenerator & ofxGgmlEasy::ensureMilkDropGenerator() const {
	return ensureOwned(
		m_milkDropGenerator,
		[this](ofxGgmlMilkDropGenerator & generator) {
			generator.setCompletionExecutable(m_textConfig.completionExecutable);
		});
}

ofxGgmlVideoEssayWorkflow & ofxGgmlEasy::ensureVideoEssayWorkflow() const {
	return ensureOwned(
		m_videoEssayWorkflow,
		[this](ofxGgmlVideoEssayWorkflow & workflow) {
			workflow.getTextAssistant().setCompletionExecutable(
				m_textConfig.completionExecutable);
			applyInferenceExecutables(
				workflow.getCitationSearch().getInference(),
				m_textConfig);
		});
}
#endif

ofxGgmlLongVideoPlanner & ofxGgmlEasy::ensureLongVideoPlanner() const {
	return ensureOwned(m_longVideoPlanner, [this](ofxGgmlLongVideoPlanner & planner) {
		planner.getTextAssistant().setCompletionExecutable(
			m_textConfig.completionExecutable);
	});
}

ofxGgmlCodingAgent & ofxGgmlEasy::ensureCodingAgent() const {
	return ensureOwned(m_codingAgent, [this](ofxGgmlCodingAgent & agent) {
		agent.setCompletionExecutable(m_textConfig.completionExecutable);
		agent.setEmbeddingExecutable(m_textConfig.embeddingExecutable);
	});
}

ofxGgmlRAGPipeline & ofxGgmlEasy::ensureRAGPipeline() const {
	return ensureOwned(m_ragPipeline, [this](ofxGgmlRAGPipeline & pipeline) {
		applyInferenceExecutables(pipeline.getInference(), m_textConfig);
	});
}

ofxGgmlConversationManager & ofxGgmlEasy::ensureConversationManager() const {
	return ensureOwned(
		m_conversationManager,
		[this](ofxGgmlConversationManager & manager) {
			manager.getInference().setCompletionExecutable(
				m_textConfig.completionExecutable);
		});
}
