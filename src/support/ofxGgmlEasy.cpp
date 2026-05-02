#include "support/ofxGgmlEasy.h"

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

} // namespace

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
